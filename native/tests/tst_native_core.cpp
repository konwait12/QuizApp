#include "domain/QuestionIdentity.h"
#include "domain/QuestionOrdering.h"
#include "handwriting/SpeedyNoteStrokeAdapter.h"
#include "services/BankInstallService.h"
#include "services/BankDirectorySyncService.h"
#include "services/BankReleaseService.h"
#include "services/BankReleaseStateStore.h"
#include "services/AnnouncementService.h"
#include "services/AiConfigService.h"
#include "services/AiQuestionAnalysisService.h"
#include "services/AppUpdateService.h"
#include "services/BlobStore.h"
#include "services/DefaultBankBundleBootstrapService.h"
#include "services/ExamService.h"
#include "services/LegacyBankImporter.h"
#include "services/LegacyMigrationService.h"
#include "services/LocalBackupService.h"
#include "services/NotebookService.h"
#include "platform/SecureSecretStore.h"
#include "services/PracticeService.h"
#include "services/ReviewService.h"
#include "services/SharedStorageService.h"
#include "services/SharedStorageFileService.h"
#include "services/StudyService.h"
#include "services/XiaoyiDirectoryInstallService.h"
#include "services/WrongBookService.h"
#include "storage/Database.h"
#include "storage/SqliteAnswerStateRepository.h"
#include "storage/SqliteAiRecordRepository.h"
#include "storage/SqliteBankSourceRepository.h"
#include "storage/SqliteExamRepository.h"
#include "storage/SqliteLibraryRepository.h"
#include "storage/SqlitePracticeRepository.h"
#include "storage/SqliteQuestionRepository.h"
#include "storage/SqliteReviewRepository.h"
#include "storage/SqliteStudyRepository.h"
#include "storage/SqliteWrongBookRepository.h"

#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {

quizapp::domain::BankImportPackage sampleBankPackage()
{
    quizapp::domain::BankImportPackage package;
    package.subjects = {
        {QStringLiteral("subject-root"), {}, QStringLiteral("Subject"), {}, 0},
        {QStringLiteral("subject-chapter"), QStringLiteral("subject-root"),
         QStringLiteral("Chapter"), {}, 0},
    };
    package.bank.id = QStringLiteral("bank-one");
    package.bank.subjectId = QStringLiteral("subject-chapter");
    package.bank.title = QStringLiteral("Chapter Bank");
    package.bank.sourceProvider = QStringLiteral("test-provider");
    package.bank.sourceId = QStringLiteral("bank-source-one");
    package.bank.contentHash = QByteArray(32, 'b');
    package.bank.distributionVersion = QStringLiteral("1");

    const QStringList path{QStringLiteral("Subject"), QStringLiteral("Chapter")};
    for (int index = 0; index < 2; ++index) {
        quizapp::domain::Question question;
        question.bankId = package.bank.id;
        question.sourceProvider = QStringLiteral("test-provider");
        question.sourceId = QStringLiteral("question-%1").arg(index + 1);
        question.path = path;
        question.type = index == 0
            ? quizapp::domain::QuestionType::Single
            : quizapp::domain::QuestionType::Multiple;
        question.prompt = QStringLiteral("Question %1").arg(index + 1);
        question.options = {QStringLiteral("Option A"), QStringLiteral("Option B")};
        question.correctAnswer = index == 0 ? QStringLiteral("A") : QStringLiteral("AB");
        question.sourceOrder = index;
        question.builtinExplanation.text = QStringLiteral("Explanation %1").arg(index + 1);
        question.id = quizapp::domain::QuestionIdentity::create(
            question.sourceProvider,
            question.sourceId,
            question.path,
            question.prompt,
            question.options);
        question.contentHash = quizapp::domain::QuestionIdentity::contentHash(
            question.path, question.prompt, question.options);
        question.updatedAt = QDateTime::currentDateTimeUtc();
        package.bank.questions.append(question);
    }
    return package;
}

QString uniqueConnectionName(const QString &prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool createDefaultBankBundle(
    const QString &rootPath,
    qsizetype manifestQuestionCount = 2)
{
    if (!QDir().mkpath(QDir(rootPath).filePath(QStringLiteral("blobs")))) {
        return false;
    }
    const QString databasePath = QDir(rootPath).filePath(QStringLiteral("quizapp.sqlite"));
    {
        quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("bundle-fixture-")));
        QString error;
        if (!database.open(databasePath, &error) || !database.migrate(&error)) {
            return false;
        }
        auto package = sampleBankPackage();
        package.bank.sourceProvider = QStringLiteral("xiaoyivip");
        package.bank.sourceId = QStringLiteral("27-postgraduate-test-bank");
        for (auto &question : package.bank.questions) {
            question.sourceProvider = package.bank.sourceProvider;
        }
        quizapp::storage::SqliteQuestionRepository repository(database.connection());
        if (!repository.replaceBank(package, &error)) {
            return false;
        }
    }

    const QJsonObject manifest{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("displayName"), QStringLiteral("27考研题库包")},
        {QStringLiteral("provider"), QStringLiteral("xiaoyivip")},
        {QStringLiteral("sectionCount"), 1},
        {QStringLiteral("questionCount"), static_cast<qint64>(manifestQuestionCount)},
        {QStringLiteral("blobCount"), 0},
        {QStringLiteral("blobBytes"), 0},
    };
    QFile manifestFile(QDir(rootPath).filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::WriteOnly)
        || manifestFile.write(QJsonDocument(manifest).toJson()) < 0) {
        return false;
    }
    manifestFile.close();
    QFile fileIndex(QDir(rootPath).filePath(QStringLiteral("files.txt")));
    return fileIndex.open(QIODevice::WriteOnly)
        && fileIndex.write("manifest.json\nquizapp.sqlite\n") > 0;
}

QByteArray legacyMigrationPackage(const QString &legacyBankId = QStringLiteral("legacy-bank"))
{
    const QJsonObject localStorage{
        {QStringLiteral("quizapp_ui_config"), QStringLiteral(
            R"JSON({"theme":"light","palette":"forest","primary":"#1f8f62"})JSON")},
        {QStringLiteral("quizapp_ai_config"), QStringLiteral(
            R"JSON({"provider":"deepseek","apiKey":"never-store-me","nested":{"token":"also-secret"}})JSON")},
        {QStringLiteral("quizapp_wrong_book"), QStringLiteral(
            R"JSON({"legacy-bank:0":{"reasonTags":["concept"],"reasonNote":"review"}})JSON")},
        {QStringLiteral("quizapp_answer_state"), QStringLiteral(
            R"JSON({"sequence":{"legacy-bank:0":"A"},"random":{"legacy-bank:1":"BC"}})JSON")},
        {QStringLiteral("quizapp_study_stats"), QStringLiteral(
            R"JSON({"2026-07-15":{"seconds":120},"2026-07-16":{"seconds":60},"__meta":{"startedAt":1}})JSON")},
    };
    const QJsonObject indexedDb{
        {QStringLiteral("quizapp_study_data"), QJsonObject{
            {QStringLiteral("version"), 4},
            {QStringLiteral("stores"), QJsonObject{
                {QStringLiteral("ink_notes"), QJsonArray{
                    QJsonObject{{QStringLiteral("key"), QStringLiteral("q1")},
                                {QStringLiteral("strokes"), QJsonArray{}}},
                    QJsonObject{{QStringLiteral("key"), QStringLiteral("q2")},
                                {QStringLiteral("strokes"), QJsonArray{}}},
                }},
            }},
        }},
    };
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("quizapp-legacy-v1")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("sourceVersion"), QStringLiteral("1.0.18")},
        {QStringLiteral("exportedAt"), QStringLiteral("2026-07-17T04:00:00.000Z")},
        {QStringLiteral("localStorage"), localStorage},
        {QStringLiteral("indexedDb"), indexedDb},
    };
    QByteArray result = QJsonDocument(root).toJson(QJsonDocument::Compact);
    result.replace("legacy-bank", legacyBankId.toUtf8());
    return result;
}

QByteArray legacyContentMatchedSessionPackage()
{
    const QJsonObject legacyBank{
        {QStringLiteral("id"), QStringLiteral("old-renamed-bank")},
        {QStringLiteral("name"), QStringLiteral("旧题库名称")},
        {QStringLiteral("questions"), QJsonArray{
            QJsonObject{{QStringLiteral("q"), QStringLiteral("Question 1")},
                        {QStringLiteral("options"), QJsonArray{
                            QStringLiteral("Option A"), QStringLiteral("Option B")}}},
            QJsonObject{{QStringLiteral("q"), QStringLiteral("Question 2")},
                        {QStringLiteral("options"), QJsonArray{
                            QStringLiteral("Option A"), QStringLiteral("Option B")}}},
        }},
    };
    const QJsonObject savedSession{
        {QStringLiteral("title"), QStringLiteral("旧版保存进度")},
        {QStringLiteral("path"), QJsonArray{QStringLiteral("Subject"), QStringLiteral("Chapter")}},
        {QStringLiteral("shuffled"), false},
        {QStringLiteral("scopeKey"), QStringLiteral("practice:sequence:legacy")},
        {QStringLiteral("bankIds"), QJsonArray{QStringLiteral("old-renamed-bank")}},
        {QStringLiteral("currentIndex"), 1},
        {QStringLiteral("currentQuestionKey"), QStringLiteral("old-renamed-bank:1")},
        {QStringLiteral("questionOrderKeys"), QJsonArray{
            QStringLiteral("old-renamed-bank:0"), QStringLiteral("old-renamed-bank:1")}},
        {QStringLiteral("answersByKey"), QJsonObject{
            {QStringLiteral("old-renamed-bank:0"), QStringLiteral("A")}}},
        {QStringLiteral("draftsByKey"), QJsonObject{
            {QStringLiteral("old-renamed-bank:1"), QStringLiteral("AB")}}},
        {QStringLiteral("mode"), QStringLiteral("page")},
        {QStringLiteral("savedAt"), 1784246400000LL},
    };
    const QJsonObject localStorage{
        {QStringLiteral("quizapp_banks"), QString::fromUtf8(
            QJsonDocument(QJsonArray{legacyBank}).toJson(QJsonDocument::Compact))},
        {QStringLiteral("quizapp_saved_progress"), QString::fromUtf8(
            QJsonDocument(QJsonObject{{QStringLiteral("legacy-scope"), savedSession}})
                .toJson(QJsonDocument::Compact))},
        {QStringLiteral("quizapp_saved_session"), QString::fromUtf8(
            QJsonDocument(savedSession).toJson(QJsonDocument::Compact))},
    };
    return QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("quizapp-legacy-v1")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("sourceVersion"), QStringLiteral("1.0.20")},
        {QStringLiteral("exportedAt"), QStringLiteral("2026-07-18T06:00:00.000Z")},
        {QStringLiteral("localStorage"), localStorage},
        {QStringLiteral("indexedDb"), QJsonObject{}},
    }).toJson(QJsonDocument::Compact);
}

} // namespace

class NativeCoreTests final : public QObject {
    Q_OBJECT

private slots:
    void stableQuestionIdentity();
    void naturalQuestionOrderingUsesChapterAndSectionOrder();
    void randomOrderKeepsQuestionSet();
    void answerCanBeChangedAndCleared();
    void notebookContextPreservesPracticeState();
    void speedyNoteStrokeRoundTrip();
    void speedyNoteStrokeRejectsInvalidPayload();
    void databaseMigrationIsIdempotent();
    void libraryNodeOrderIsScopedAndPersistent();
    void libraryRemovalSeparatesHiddenAndSharedBanks();
    void bankReleaseCatalogSupportsManifestAndFallbackAssets();
    void bankReleaseInstallValidatesAndRollsBackWholeBatch();
    void bankReleaseStateTracksSelectedFingerprintsAndAssetChanges();
    void announcementCatalogSanitizesAndTracksUnreadItems();
    void appUpdateReleaseParsingVersioningAndVerification();
    void aiConfigUsesSecureStorageAndValidatesCompatibleEndpoints();
    void aiQuestionAnalysisBuildsStructuredRequestAndCachesBySourceHash();
    void examSessionScoresPersistsAndKeepsHistorySnapshot();
    void localBackupStreamsVerifiesRollsBackAndRestores();
    void freeNotebookLifecycleRecyclesRestoresAndDeletesFiles();
    void questionRepositoryRoundTripAndSoftDelete();
    void questionRepositoryListsWholePathSubtree();
    void questionRepositoryRollsBackMissingBlob();
    void practiceRepositoryRoundTripAndModeIsolation();
    void questionAnswerStateSynchronizesScopesAndModes();
    void wrongBookMembershipIsExplicitAndIndependentFromPracticeProgress();
    void fsrsReviewRoundTripPersistsCardAndHistory();
    void studyTotalsSplitForegroundEventsAcrossLocalMidnight();
    void xiaoyiImportRepairsTypesAndSeparatesMedia();
    void xiaoyiMissingAnswerWithBuiltinExplanationRemainsUngradedChoice();
    void xiaoyiMediaInstallDeduplicatesAndPersistsBlobs();
    void invalidEmbeddedMediaRejectsWholeInstall();
    void xiaoyiDirectoryInstallReportsProgressAndUpdatesStats();
    void bankImportRejectsUnsafeBooleanQuestion();
    void sharedStorageCreatesNamedDirectories();
    void sharedBankHierarchyComesFromRelativePath();
    void sharedBankSyncTracksUpdatesAndMissingFiles();
    void sharedBankOverridesBundledPathAndRestoresIt();
    void sharedBankMovesPreserveQuestionIdentity();
    void sharedStorageFileOperationsStayInsideManagedRoot();
    void defaultBankBundleInstallsOnFreshDatabase();
    void realDefaultBankBundleBootstrapsWithProgress();
    void defaultBankBundleReplacesOnlyEmptyDatabase();
    void defaultBankBundleProtectsNonEmptyDatabase();
    void defaultBankBundleRollsBackInterruptedCopy();
    void defaultBankBundleRejectsCountMismatch();
    void legacyMigrationPreservesDataAndRedactsSecrets();
    void legacyMigrationIsIdempotent();
    void legacyMigrationRollsBackOnInterruption();
    void legacyMigrationRejectsMalformedPackage();
    void legacyMigrationAppliesUiSettingsWithoutOverwrite();
    void legacyMigrationResolvesDirectQuestionKeys();
    void legacyMigrationMatchesQuestionContentAndMaterializesPracticeSession();
};

void NativeCoreTests::stableQuestionIdentity()
{
    const QStringList path{QStringLiteral("Subject"), QStringLiteral("Chapter")};
    const QStringList options{QStringLiteral("A"), QStringLiteral("B")};
    const QUuid first = quizapp::domain::QuestionIdentity::create(
        QString(), QString(), path, QStringLiteral("Question"), options);
    const QUuid second = quizapp::domain::QuestionIdentity::create(
        QString(), QString(), path, QStringLiteral("  Question  "), options);
    QCOMPARE(first, second);

    const QUuid sourced = quizapp::domain::QuestionIdentity::create(
        QStringLiteral("provider"), QStringLiteral("42"), {}, {}, {});
    QCOMPARE(sourced, quizapp::domain::QuestionIdentity::create(
        QStringLiteral("provider"), QStringLiteral("42"), path, QStringLiteral("changed"), options));
}

void NativeCoreTests::naturalQuestionOrderingUsesChapterAndSectionOrder()
{
    QVERIFY(quizapp::domain::naturalLibraryTitleLess(
        QStringLiteral("第二章 导数"), QStringLiteral("第十章 级数")));
    QVERIFY(quizapp::domain::naturalLibraryTitleLess(
        QStringLiteral("第2讲 极限"), QStringLiteral("第10讲 积分")));
    QVERIFY(quizapp::domain::naturalLibraryTitleLess(
        QStringLiteral("选择题"), QStringLiteral("填空题")));
    QVERIFY(quizapp::domain::naturalLibraryTitleLess(
        QStringLiteral("填空题"), QStringLiteral("解答题")));

    quizapp::domain::Question later;
    later.id = QUuid::createUuid();
    later.path = {QStringLiteral("考研数学"), QStringLiteral("第十章 级数"),
                  QStringLiteral("选择题")};
    quizapp::domain::Question earlier = later;
    earlier.id = QUuid::createUuid();
    earlier.path[1] = QStringLiteral("第二章 导数");
    QVERIFY(quizapp::domain::sourceQuestionLess(earlier, later));
}

void NativeCoreTests::randomOrderKeepsQuestionSet()
{
    quizapp::services::PracticeService service;
    const QVector<QUuid> ids{QUuid::createUuid(), QUuid::createUuid(), QUuid::createUuid()};
    auto session = service.start(
        QStringLiteral("scope"), quizapp::domain::PracticeMode::Random, ids, 721);
    QCOMPARE(session.questionOrder.size(), ids.size());
    for (const QUuid &id : ids) {
        QVERIFY(session.questionOrder.contains(id));
    }
}

void NativeCoreTests::answerCanBeChangedAndCleared()
{
    quizapp::services::PracticeService service;
    const QUuid questionId = QUuid::createUuid();
    auto session = service.start(
        QStringLiteral("scope"), quizapp::domain::PracticeMode::Sequential, {questionId});
    QVERIFY(service.selectAnswer(session, QStringLiteral("a")));
    QCOMPARE(session.answers.value(questionId), QStringLiteral("A"));
    QVERIFY(service.selectAnswer(session, QStringLiteral("B")));
    QCOMPARE(session.answers.value(questionId), QStringLiteral("B"));
    QVERIFY(service.selectAnswer(session, QStringLiteral("B")));
    QVERIFY(!session.answers.contains(questionId));
}

void NativeCoreTests::notebookContextPreservesPracticeState()
{
    quizapp::services::PracticeService service;
    const QUuid questionId = QUuid::createUuid();
    auto session = service.start(
        QStringLiteral("scope"), quizapp::domain::PracticeMode::Memorize, {questionId});
    session.viewport.insert(QStringLiteral("scrollTop"), 128);
    const auto context = service.notebookContext(session);
    QCOMPARE(context.sessionId, session.id);
    QCOMPARE(context.questionId, questionId);
    QCOMPARE(context.practiceMode, quizapp::domain::PracticeMode::Memorize);
    QCOMPARE(context.practiceViewport.value(QStringLiteral("scrollTop")).toInt(), 128);
}

void NativeCoreTests::speedyNoteStrokeRoundTrip()
{
    quizapp::handwriting::SpeedyNoteStrokeAdapter adapter;
    const QVector<quizapp::handwriting::PenSample> samples{
        {QPointF(10.0, 20.0), -0.5, 100},
        {QPointF(30.0, 40.0), 1.5, 120},
    };
    const auto stroke = adapter.createStroke(samples, QColor(QStringLiteral("#176b4d")), 4.0);
    QCOMPARE(stroke.samples.size(), 2);
    QCOMPARE(stroke.samples.at(0).pressure, 0.0);
    QCOMPARE(stroke.samples.at(1).pressure, 1.0);
    QVERIFY(stroke.boundingBox.contains(QPointF(10.0, 20.0)));
    QVERIFY(adapter.hitTest(stroke, QPointF(20.0, 30.0), 1.0));

    const auto restored = adapter.deserialize(adapter.serialize(stroke));
    QVERIFY(restored.has_value());
    QCOMPARE(restored->id, stroke.id);
    QCOMPARE(restored->samples.size(), stroke.samples.size());
    QCOMPARE(restored->samples.at(1).timestamp, 120);
}

void NativeCoreTests::speedyNoteStrokeRejectsInvalidPayload()
{
    quizapp::handwriting::SpeedyNoteStrokeAdapter adapter;
    QJsonObject invalid;
    invalid.insert(QStringLiteral("color"), QStringLiteral("#000000"));
    invalid.insert(QStringLiteral("thickness"), 3.0);
    QVERIFY(!adapter.deserialize(invalid).has_value());
}

void NativeCoreTests::databaseMigrationIsIdempotent()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("migration-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));

    QSqlQuery version(database.connection());
    QVERIFY(version.exec(QStringLiteral("SELECT MAX(version) FROM schema_migrations")));
    QVERIFY(version.next());
    QCOMPARE(version.value(0).toInt(), 11);

    QSet<QString> columns;
    QSqlQuery tableInfo(database.connection());
    QVERIFY(tableInfo.exec(QStringLiteral("PRAGMA table_info(questions)")));
    while (tableInfo.next()) {
        columns.insert(tableInfo.value(1).toString());
    }
    QVERIFY(columns.contains(QStringLiteral("path_json")));
    QVERIFY(columns.contains(QStringLiteral("active")));

    QSqlQuery hiddenTable(database.connection());
    QVERIFY(hiddenTable.exec(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type='table' "
        "AND name='library_hidden_banks'")));
    QVERIFY(hiddenTable.next());
}

void NativeCoreTests::libraryNodeOrderIsScopedAndPersistent()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("library-order-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteLibraryRepository repository(database.connection());

    const QStringList rootOrder{
        QStringLiteral("毛概"), QStringLiteral("高数"), QStringLiteral("英语")};
    QVERIFY2(repository.setChildOrder({}, rootOrder, &error), qPrintable(error));
    QCOMPARE(repository.childOrder({}, &error), rootOrder);

    const QStringList chapterOrder{
        QStringLiteral("第二章"), QStringLiteral("导论"), QStringLiteral("第一章")};
    QVERIFY2(repository.setChildOrder(
        {QStringLiteral("毛概")}, chapterOrder, &error), qPrintable(error));
    QCOMPARE(
        repository.childOrder({QStringLiteral("毛概")}, &error), chapterOrder);
    QCOMPARE(repository.childOrder({}, &error), rootOrder);

    const QStringList replacedRoot{
        QStringLiteral("英语"), QStringLiteral("毛概")};
    QVERIFY2(repository.setChildOrder({}, replacedRoot, &error), qPrintable(error));
    QCOMPARE(repository.childOrder({}, &error), replacedRoot);
    QCOMPARE(
        repository.childOrder({QStringLiteral("毛概")}, &error), chapterOrder);
}

void NativeCoreTests::libraryRemovalSeparatesHiddenAndSharedBanks()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("library-remove-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());

    auto localBank = sampleBankPackage();
    QVERIFY2(questionRepository.replaceBank(localBank, &error), qPrintable(error));

    auto sharedBank = sampleBankPackage();
    sharedBank.bank.id = QStringLiteral("shared-bank");
    sharedBank.bank.sourceId = QStringLiteral("shared-bank-source");
    for (qsizetype index = 0; index < sharedBank.bank.questions.size(); ++index) {
        auto &question = sharedBank.bank.questions[index];
        question.bankId = sharedBank.bank.id;
        question.sourceId = QStringLiteral("shared-question-%1").arg(index);
        question.path = {QStringLiteral("Subject"), QStringLiteral("Shared")};
        question.id = quizapp::domain::QuestionIdentity::create(
            question.sourceProvider,
            question.sourceId,
            question.path,
            question.prompt,
            question.options);
        question.contentHash = quizapp::domain::QuestionIdentity::contentHash(question);
    }
    QVERIFY2(questionRepository.replaceBank(sharedBank, &error), qPrintable(error));

    quizapp::domain::ManagedBankSource source;
    source.sourceKey = QStringLiteral("shared:Subject/Shared.json");
    source.managedRoot = QStringLiteral("primary-shared-storage");
    source.relativePath = QStringLiteral("Subject/Shared.json");
    source.bankId = sharedBank.bank.id;
    source.sha256 = QByteArrayLiteral("shared-bank-hash");
    source.available = true;
    source.lastError = QStringLiteral("");
    source.lastSyncedAt = QDateTime::currentDateTimeUtc();
    quizapp::storage::SqliteBankSourceRepository sourceRepository(database.connection());
    QVERIFY2(sourceRepository.save(source, &error), qPrintable(error));

    quizapp::storage::SqliteLibraryRepository libraryRepository(database.connection());
    QVERIFY2(libraryRepository.deactivateForRemoval(
        {QStringLiteral("Subject")},
        {localBank.bank.id},
        {source.sourceKey},
        &error), qPrintable(error));
    QCOMPARE(questionRepository.listInstalledBanks(&error).size(), 0);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const auto hidden = libraryRepository.hiddenNodes(&error);
    QCOMPARE(hidden.size(), 1);
    QCOMPARE(hidden.constFirst().path, QStringList{QStringLiteral("Subject")});
    QCOMPARE(hidden.constFirst().bankCount, 1);
    const auto unavailableSource = sourceRepository.findByKey(source.sourceKey, &error);
    QVERIFY(unavailableSource.has_value());
    QVERIFY(!unavailableSource->available);

    QVERIFY2(libraryRepository.restoreHiddenNode(
        {QStringLiteral("Subject")}, &error), qPrintable(error));
    const auto restoredBanks = questionRepository.listInstalledBanks(&error);
    QCOMPARE(restoredBanks.size(), 1);
    QCOMPARE(restoredBanks.constFirst().id, localBank.bank.id);
    QCOMPARE(libraryRepository.hiddenNodes(&error).size(), 0);
    QVERIFY(!sourceRepository.findByKey(source.sourceKey, &error)->available);
}

void NativeCoreTests::bankReleaseCatalogSupportsManifestAndFallbackAssets()
{
    const QByteArray bankJson = R"JSON({
      "name": "毛概-导论",
      "questions": [{
        "id": "release-question-1",
        "type": "single",
        "q": "Release 题库是否可解析？",
        "options": ["可以", "不可以"],
        "ans": "A"
      }]
    })JSON";
    const QByteArray digest = QCryptographicHash::hash(
        bankJson, QCryptographicHash::Sha256);
    const QJsonArray releaseAssets{
        QJsonObject{
            {QStringLiteral("name"), QStringLiteral("quizapp-bank-manifest.json")},
            {QStringLiteral("browser_download_url"), QStringLiteral("https://example.test/manifest")},
            {QStringLiteral("size"), 100}},
        QJsonObject{
            {QStringLiteral("name"), QStringLiteral("quizapp-bank-001.json")},
            {QStringLiteral("browser_download_url"), QStringLiteral("https://example.test/bank-1")},
            {QStringLiteral("size"), bankJson.size()},
            {QStringLiteral("digest"), QStringLiteral("sha256:%1")
                 .arg(QString::fromLatin1(digest.toHex()))}},
    };
    const QJsonObject release{
        {QStringLiteral("tag_name"), QStringLiteral("v2.0.0")},
        {QStringLiteral("html_url"), QStringLiteral("https://example.test/release")},
        {QStringLiteral("assets"), releaseAssets},
    };
    quizapp::services::BankReleaseMetadata metadata;
    QString error;
    QVERIFY2(quizapp::services::BankReleaseService::parseReleaseMetadata(
        QJsonDocument(release).toJson(), &metadata, &error), qPrintable(error));
    QCOMPARE(metadata.manifestAssetIndex, 0);
    const QJsonObject manifestEntry{
        {QStringLiteral("file"), QStringLiteral("quizapp-bank-001.json")},
        {QStringLiteral("name"), QStringLiteral("毛概-导论")},
        {QStringLiteral("path"), QJsonArray{
            QStringLiteral("毛概"), QStringLiteral("导论")}},
        {QStringLiteral("questionCount"), 1},
    };
    const QJsonObject manifest{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("banks"), QJsonArray{manifestEntry}},
    };
    quizapp::services::BankReleaseCatalog catalog;
    QVERIFY2(quizapp::services::BankReleaseService::buildCatalog(
        metadata, QJsonDocument(manifest).toJson(), &catalog, &error), qPrintable(error));
    QCOMPARE(catalog.tagName, QStringLiteral("v2.0.0"));
    QCOMPARE(catalog.entries.size(), 1);
    QCOMPARE(catalog.entries.constFirst().path,
             QStringList({QStringLiteral("毛概"), QStringLiteral("导论")}));
    QCOMPARE(catalog.entries.constFirst().expectedSha256, digest);
    QVERIFY2(quizapp::services::BankReleaseService::verifyPayload(
        catalog.entries.constFirst(), bankJson, &error), qPrintable(error));

    quizapp::services::BankReleaseMetadata fallbackMetadata = metadata;
    fallbackMetadata.manifestAssetIndex = -1;
    QVERIFY2(quizapp::services::BankReleaseService::buildCatalog(
        fallbackMetadata, {}, &catalog, &error), qPrintable(error));
    QCOMPARE(catalog.entries.size(), 1);
    QCOMPARE(catalog.entries.constFirst().assetName,
             QStringLiteral("quizapp-bank-001.json"));
}

void NativeCoreTests::bankReleaseInstallValidatesAndRollsBackWholeBatch()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::services::SharedStorageService storageService;
    const auto layout = storageService.prepare(
        QDir(root.path()).filePath(QStringLiteral("QuizApp")));
    QVERIFY2(layout.ready(), qPrintable(layout.error));
    const QByteArray firstPayload = R"JSON({
      "name": "毛概-导论",
      "questions": [{"id":"r1","type":"single","q":"第一题", "options":["A","B"],"ans":"A"}]
    })JSON";
    const QByteArray secondPayload = R"JSON({
      "name": "英语-第一章",
      "questions": [{"id":"r2","type":"single","q":"第二题", "options":["A","B"],"ans":"B"}]
    })JSON";
    quizapp::services::BankReleaseEntry first;
    first.id = QStringLiteral("first");
    first.name = QStringLiteral("毛概-导论");
    first.path = {QStringLiteral("毛概"), QStringLiteral("导论")};
    first.questionCount = 1;
    first.expectedByteSize = firstPayload.size();
    first.expectedSha256 = QCryptographicHash::hash(
        firstPayload, QCryptographicHash::Sha256);
    quizapp::services::BankReleaseEntry second;
    second.id = QStringLiteral("second");
    second.name = QStringLiteral("英语-第一章");
    second.path = {QStringLiteral("阻塞"), QStringLiteral("第一章")};
    second.questionCount = 1;
    second.expectedByteSize = secondPayload.size();
    second.expectedSha256 = QCryptographicHash::hash(
        secondPayload, QCryptographicHash::Sha256);

    const QString firstTarget = quizapp::services::BankReleaseService::destinationPath(
        layout, first);
    QVERIFY(QDir().mkpath(QFileInfo(firstTarget).absolutePath()));
    QFile existing(firstTarget);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("original"), 8);
    existing.close();
    QFile blocker(QDir(layout.questionBanks).filePath(QStringLiteral("阻塞")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    QCOMPARE(blocker.write("block"), 5);
    blocker.close();

    quizapp::services::BankReleaseService releaseService;
    const QVector<quizapp::services::BankReleaseSelection> selections{
        {first, quizapp::services::BankReleaseConflictPolicy::Overwrite},
        {second, quizapp::services::BankReleaseConflictPolicy::Overwrite},
    };
    const QHash<QString, QByteArray> payloads{
        {first.id, firstPayload}, {second.id, secondPayload}};
    const auto failed = releaseService.install(layout, QStringLiteral("v2.0.0"), selections, payloads);
    QVERIFY(!failed.succeeded());
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("original"));
    existing.close();

    QVERIFY(QFile::remove(blocker.fileName()));
    const auto installed = releaseService.install(
        layout, QStringLiteral("v2.0.0"), selections, payloads);
    QVERIFY2(installed.succeeded(), qPrintable(installed.error));
    QCOMPARE(installed.installedEntries, 2);
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), firstPayload);
    existing.close();
    QVERIFY(QFileInfo::exists(
        QDir(layout.questionBanks).filePath(QStringLiteral("阻塞/第一章.json"))));

    const auto copied = releaseService.install(
        layout,
        QStringLiteral("v2.0.0"),
        {{first, quizapp::services::BankReleaseConflictPolicy::KeepBoth}},
        {{first.id, firstPayload}});
    QVERIFY2(copied.succeeded(), qPrintable(copied.error));
    QCOMPARE(copied.installedEntries, 1);
    QVERIFY(copied.destinationPaths.constFirst().contains(QStringLiteral("v2.0.0")));
}

void NativeCoreTests::bankReleaseStateTracksSelectedFingerprintsAndAssetChanges()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QSettings settings(
        QDir(root.path()).filePath(QStringLiteral("bank-release.ini")),
        QSettings::IniFormat);
    quizapp::services::BankReleaseEntry first;
    first.id = QStringLiteral("first-ui-id");
    first.name = QStringLiteral("毛概-导论");
    first.path = {QStringLiteral("毛概"), QStringLiteral("导论")};
    first.assetName = QStringLiteral("quizapp-bank-001.json");
    first.downloadUrl = QStringLiteral("https://example.test/bank-1");
    first.questionCount = 100;
    first.expectedByteSize = 1024;
    first.expectedSha256 = QByteArray(32, 'a');
    first.assetApiId = 101;
    first.assetUpdatedAt = QStringLiteral("2026-07-18T00:00:00Z");
    auto second = first;
    second.id = QStringLiteral("second-ui-id");
    second.name = QStringLiteral("英语-第一章");
    second.path = {QStringLiteral("英语"), QStringLiteral("第一章")};
    second.assetName = QStringLiteral("quizapp-bank-002.json");
    second.assetApiId = 102;

    quizapp::services::BankReleaseCatalog catalog;
    catalog.tagName = QStringLiteral("v1.0.18");
    catalog.entries = {first, second};
    auto state = quizapp::services::BankReleaseStateStore::load(settings);
    QCOMPARE(
        quizapp::services::BankReleaseStateStore::outdatedEntryIds(catalog, state).size(),
        2);

    QString error;
    const QDateTime checkedAt = QDateTime::fromString(
        QStringLiteral("2026-07-18T01:00:00.000Z"), Qt::ISODateWithMs);
    QVERIFY2(quizapp::services::BankReleaseStateStore::recordCheck(
                 settings, catalog, checkedAt, &error), qPrintable(error));
    state = quizapp::services::BankReleaseStateStore::load(settings);
    QCOMPARE(state.lastCheckedTag, QStringLiteral("v1.0.18"));
    QCOMPARE(
        quizapp::services::BankReleaseStateStore::outdatedEntryIds(catalog, state).size(),
        2);

    QVERIFY2(quizapp::services::BankReleaseStateStore::recordInstall(
                 settings,
                 catalog,
                 {{first, quizapp::services::BankReleaseConflictPolicy::Overwrite}},
                 checkedAt.addSecs(60),
                 &error), qPrintable(error));
    state = quizapp::services::BankReleaseStateStore::load(settings);
    QCOMPARE(
        quizapp::services::BankReleaseStateStore::outdatedEntryIds(catalog, state),
        QStringList{QStringLiteral("second-ui-id")});

    quizapp::services::BankReleaseCatalog reordered = catalog;
    std::reverse(reordered.entries.begin(), reordered.entries.end());
    QCOMPARE(
        quizapp::services::BankReleaseStateStore::catalogFingerprint(catalog),
        quizapp::services::BankReleaseStateStore::catalogFingerprint(reordered));

    catalog.entries[0].assetApiId = 201;
    catalog.entries[0].assetUpdatedAt = QStringLiteral("2026-07-18T02:00:00Z");
    const QStringList changed =
        quizapp::services::BankReleaseStateStore::outdatedEntryIds(catalog, state);
    QVERIFY(changed.contains(QStringLiteral("first-ui-id")));
    QVERIFY(changed.contains(QStringLiteral("second-ui-id")));
}

void NativeCoreTests::announcementCatalogSanitizesAndTracksUnreadItems()
{
    const QByteArray payload = R"JSON({
      "announcements": [
        {
          "id": "release-v2",
          "title": "第二版",
          "date": "2026-07-18",
          "body": "<p onclick=\"bad()\">修复内容</p><script>alert(1)</script>"
        },
        {
          "id": "release-v1",
          "title": "第一版",
          "date": "2026-07-17",
          "text": "首次公告"
        },
        {
          "id": "release-v1",
          "title": "重复公告",
          "body": "<p>不应保留</p>"
        }
      ]
    })JSON";
    quizapp::services::AnnouncementCatalog catalog;
    QString error;
    QVERIFY2(quizapp::services::AnnouncementService::parseCatalog(
                 payload, &catalog, &error), qPrintable(error));
    QCOMPARE(catalog.items.size(), 2);
    QVERIFY(catalog.items.constFirst().latest);
    QVERIFY(!catalog.items.constFirst().bodyHtml.contains(
        QStringLiteral("onclick"), Qt::CaseInsensitive));
    QVERIFY(!catalog.items.constFirst().bodyHtml.contains(
        QStringLiteral("script"), Qt::CaseInsensitive));

    const QJsonObject release{
        {QStringLiteral("assets"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("quizapp-announcements.json")},
                 {QStringLiteral("browser_download_url"),
                  QStringLiteral("https://example.test/announcements")},
             }}},
    };
    QString assetUrl;
    QVERIFY2(quizapp::services::AnnouncementService::announcementAssetUrl(
                 QJsonDocument(release).toJson(), &assetUrl, &error),
             qPrintable(error));
    QCOMPARE(assetUrl, QStringLiteral("https://example.test/announcements"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    QSettings settings(
        QDir(root.path()).filePath(QStringLiteral("announcements.ini")),
        QSettings::IniFormat);
    const QDateTime checkedAt = QDateTime::fromString(
        QStringLiteral("2026-07-18T03:00:00.000Z"), Qt::ISODateWithMs);
    QVERIFY2(quizapp::services::AnnouncementStateStore::saveCatalog(
                 settings, catalog, checkedAt, &error), qPrintable(error));
    auto state = quizapp::services::AnnouncementStateStore::load(settings);
    QCOMPARE(quizapp::services::AnnouncementStateStore::unreadIds(state).size(), 2);
    QVERIFY2(quizapp::services::AnnouncementStateStore::markAllRead(
                 settings, catalog, true, &error), qPrintable(error));
    state = quizapp::services::AnnouncementStateStore::load(settings);
    QVERIFY(quizapp::services::AnnouncementStateStore::unreadIds(state).isEmpty());
    QCOMPARE(state.suppressedCatalogFingerprint, catalog.fingerprint);

    auto newer = catalog;
    quizapp::services::AnnouncementItem third;
    third.id = QStringLiteral("release-v3");
    third.title = QStringLiteral("第三版");
    third.date = QStringLiteral("2026-07-19");
    third.bodyHtml = QStringLiteral("<p>新的修复</p>");
    third.latest = true;
    newer.items.prepend(third);
    newer.fingerprint = quizapp::services::AnnouncementService::fingerprint(newer.items);
    QVERIFY2(quizapp::services::AnnouncementStateStore::saveCatalog(
                 settings, newer, checkedAt.addDays(1), &error), qPrintable(error));
    state = quizapp::services::AnnouncementStateStore::load(settings);
    QCOMPARE(
        quizapp::services::AnnouncementStateStore::unreadIds(state),
        QStringList{QStringLiteral("release-v3")});
}

void NativeCoreTests::appUpdateReleaseParsingVersioningAndVerification()
{
    const QByteArray releaseJson = R"JSON({
      "tag_name": "v2.0.0-alpha.4",
      "target_commitish": "abcdef1234567890",
      "html_url": "https://github.com/konwait12/QuizApp/releases/tag/v2.0.0-alpha.4",
      "name": "Native alpha 4",
      "body": "Fixes and migration updates",
      "assets": [
        {"id": 1, "name": "QuizApp-debug-x86.apk", "size": 8,
         "browser_download_url": "https://example.test/x86.apk"},
        {"id": 2, "name": "QuizApp-arm64-v8a.apk", "size": 4,
         "digest": "sha256:88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589",
         "browser_download_url": "https://example.test/arm64.apk"},
        {"id": 3, "name": "QuizApp-Setup.exe", "size": 12,
         "browser_download_url": "https://example.test/setup.exe"}
      ]
    })JSON";
    quizapp::services::AppReleaseInfo release;
    QString error;
    QVERIFY2(quizapp::services::AppUpdateService::parseLatestRelease(
        releaseJson, &release, &error), qPrintable(error));
    QCOMPARE(release.tagName, QStringLiteral("v2.0.0-alpha.4"));
    QCOMPARE(release.assets.size(), 3);
    QCOMPARE(
        quizapp::services::AppUpdateService::compareVersionTags(
            QStringLiteral("v2.0.0-alpha.4"), QStringLiteral("2.0.0-alpha.3")),
        1);
    QCOMPARE(
        quizapp::services::AppUpdateService::compareVersionTags(
            QStringLiteral("2.0.0"), QStringLiteral("2.0.0-rc.9")),
        1);
    QCOMPARE(
        quizapp::services::AppUpdateService::compareVersionTags(
            QStringLiteral("1.9.9"), QStringLiteral("2.0.0-alpha.1")),
        -1);

    const auto androidDecision = quizapp::services::AppUpdateService::evaluate(
        release,
        QStringLiteral("2.0.0-alpha.3"),
        QStringLiteral("old-build"),
        quizapp::services::AppUpdatePlatform::Android);
    QVERIFY(androidDecision.updateAvailable);
    QVERIFY(androidDecision.asset.has_value());
    QCOMPARE(androidDecision.asset->id, 2);
    const auto windowsAsset = quizapp::services::AppUpdateService::selectAsset(
        release, quizapp::services::AppUpdatePlatform::Windows);
    QVERIFY(windowsAsset.has_value());
    QCOMPARE(windowsAsset->id, 3);
    QVERIFY2(quizapp::services::AppUpdateService::verifyDownloadedPackage(
        *androidDecision.asset,
        4,
        QCryptographicHash::hash(QByteArray("abcd"), QCryptographicHash::Sha256),
        &error), qPrintable(error));
    QVERIFY(!quizapp::services::AppUpdateService::verifyDownloadedPackage(
        *androidDecision.asset,
        3,
        QCryptographicHash::hash(QByteArray("abc"), QCryptographicHash::Sha256),
        &error));

    auto sameVersion = release;
    sameVersion.tagName = QStringLiteral("2.0.0-alpha.3");
    sameVersion.targetCommit = QStringLiteral("new-build");
    const auto rebuilt = quizapp::services::AppUpdateService::evaluate(
        sameVersion,
        QStringLiteral("2.0.0-alpha.3"),
        QStringLiteral("old-build"),
        quizapp::services::AppUpdatePlatform::Android);
    QVERIFY(rebuilt.updateAvailable);
    const auto sameBuild = quizapp::services::AppUpdateService::evaluate(
        sameVersion,
        QStringLiteral("2.0.0-alpha.3"),
        QStringLiteral("new-build"),
        quizapp::services::AppUpdatePlatform::Android);
    QVERIFY(!sameBuild.updateAvailable);
}

void NativeCoreTests::examSessionScoresPersistsAndKeepsHistorySnapshot()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("exam-")));
    QVERIFY(database.open(QStringLiteral(":memory:")));
    QString error;
    QVERIFY2(database.migrate(&error), qPrintable(error));
    QSqlQuery removeMigrationRecord(database.connection());
    QVERIFY(removeMigrationRecord.exec(QStringLiteral(
        "DELETE FROM schema_migrations WHERE version=10")));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    QSqlQuery repairedMigration(database.connection());
    QVERIFY(repairedMigration.exec(QStringLiteral(
        "SELECT 1 FROM schema_migrations WHERE version=10")));
    QVERIFY(repairedMigration.next());
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    const auto package = sampleBankPackage();
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));
    const QVector<quizapp::domain::Question> questions =
        questionRepository.listByBankId(package.bank.id, &error);
    QCOMPARE(questions.size(), 2);

    quizapp::services::ExamService service;
    auto session = service.start(
        QStringLiteral("all"),
        QStringLiteral("综合模拟考试"),
        questions,
        2,
        30 * 60,
        721);
    QCOMPARE(session.questionOrder.size(), 2);
    QCOMPARE(session.remainingSeconds, 1800);
    QHash<QUuid, quizapp::domain::Question> byId;
    for (const auto &question : questions) {
        byId.insert(question.id, question);
    }
    const auto first = byId.constFind(session.questionOrder.first());
    QVERIFY(first != byId.cend());
    for (const QChar option : first->correctAnswer) {
        QVERIFY(service.selectAnswer(session, *first, option));
    }
    QCOMPARE(
        quizapp::services::ExamService::normalizeAnswer(
            session.answers.value(first->id)),
        quizapp::services::ExamService::normalizeAnswer(first->correctAnswer));
    QVERIFY(service.advanceTimer(session, 12));
    QCOMPARE(session.remainingSeconds, 1788);
    QVERIFY(service.setPaused(session, true));
    QVERIFY(!service.advanceTimer(session, 5));
    QCOMPARE(session.remainingSeconds, 1788);
    QVERIFY(service.setPaused(session, false));
    QVERIFY(service.move(session, 1));

    quizapp::storage::SqliteExamRepository repository(database.connection());
    QVERIFY2(repository.save(session, &error), qPrintable(error));
    const auto active = repository.latestActive(&error);
    QVERIFY2(active.has_value(), qPrintable(error));
    QCOMPARE(active->currentIndex, 1);
    QCOMPARE(active->remainingSeconds, 1788);
    QCOMPARE(active->answers.size(), 1);

    QVERIFY(service.submit(session, byId));
    QCOMPARE(session.status, quizapp::domain::ExamStatus::Submitted);
    QCOMPARE(session.correctCount, 1);
    QCOMPARE(session.unansweredCount, 1);
    QCOMPARE(session.wrongCount, 0);
    QCOMPARE(session.score, 50);
    QVERIFY2(repository.save(session, &error), qPrintable(error));
    QVERIFY(!repository.latestActive(&error).has_value());

    QSqlQuery removeBank(database.connection());
    removeBank.prepare(QStringLiteral("DELETE FROM banks WHERE id=?"));
    removeBank.addBindValue(package.bank.id);
    QVERIFY2(removeBank.exec(), qPrintable(removeBank.lastError().text()));
    const QVector<quizapp::domain::ExamSession> history = repository.history(30, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.first().score, 50);
    QCOMPARE(history.first().resultItems.size(), 2);
    QVERIFY(!history.first().resultItems.first().questionSnapshot.prompt.isEmpty());
}

void NativeCoreTests::aiConfigUsesSecureStorageAndValidatesCompatibleEndpoints()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QSettings settings(
        QDir(root.path()).filePath(QStringLiteral("ai-settings.ini")),
        QSettings::IniFormat);
    QString error;
    quizapp::services::AiConfiguration emptyConfiguration;
    QVERIFY2(quizapp::services::AiConfigService::save(
        emptyConfiguration, settings, &error), qPrintable(error));
    settings.setValue(QStringLiteral("ai/apiKey"), QStringLiteral("legacy-secret"));

    auto configuration = quizapp::services::AiConfigService::load(settings, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(configuration.provider, QStringLiteral("deepseek"));
    QCOMPARE(configuration.baseUrl, QStringLiteral("https://api.deepseek.com"));
    QCOMPARE(configuration.model, QStringLiteral("deepseek-chat"));
    QCOMPARE(configuration.apiKey, QStringLiteral("legacy-secret"));
    QVERIFY(!settings.contains(QStringLiteral("ai/apiKey")));
    const QByteArray protectedValue = settings.value(
        quizapp::platform::SecureSecretStore::settingsKey(
            QStringLiteral("ai/apiKey"))).toByteArray();
    QVERIFY(!protectedValue.isEmpty());
    QVERIFY(!protectedValue.contains(QByteArrayLiteral("legacy-secret")));

    configuration.provider = QStringLiteral("custom");
    configuration.baseUrl = QStringLiteral("https://example.com/v1/");
    configuration.apiKey = QStringLiteral("new-secret");
    configuration.model = QStringLiteral("model-b");
    configuration.modelOptions = {
        QStringLiteral("model-b"), QStringLiteral("model-a"), QStringLiteral("model-b")};
    configuration.maxTokens = 99999;
    configuration.historyMessages = -4;
    configuration.temperature = 4.0;
    QVERIFY2(quizapp::services::AiConfigService::save(
        configuration, settings, &error), qPrintable(error));
    const auto restored = quizapp::services::AiConfigService::load(settings, &error);
    QCOMPARE(restored.baseUrl, QStringLiteral("https://example.com/v1"));
    QCOMPARE(restored.apiKey, QStringLiteral("new-secret"));
    QCOMPARE(restored.modelOptions,
             QStringList({QStringLiteral("model-a"), QStringLiteral("model-b")}));
    QCOMPARE(restored.maxTokens, 16384);
    QCOMPARE(restored.historyMessages, 0);
    QCOMPARE(restored.temperature, 2.0);
    QCOMPARE(quizapp::services::AiConfigService::modelsEndpoint(restored).toString(),
             QStringLiteral("https://example.com/v1/models"));
    QCOMPARE(quizapp::services::AiConfigService::chatEndpoint(restored).toString(),
             QStringLiteral("https://example.com/v1/chat/completions"));

    auto invalid = restored;
    invalid.baseUrl = QStringLiteral("http://example.com/v1");
    QVERIFY(!quizapp::services::AiConfigService::validate(invalid, true, &error));
    QVERIFY(error.contains(QStringLiteral("HTTPS")));
    invalid.baseUrl = QStringLiteral("http://127.0.0.1:8080/v1");
    QVERIFY2(quizapp::services::AiConfigService::validate(invalid, true, &error),
             qPrintable(error));

    const QByteArray models = R"JSON({"data":[
        {"id":"deepseek-chat"},{"name":"deepseek-reasoner"},
        {"id":"deepseek-chat"},{}
    ]})JSON";
    QCOMPARE(quizapp::services::AiConfigService::parseModelList(models, &error),
             QStringList({QStringLiteral("deepseek-chat"),
                          QStringLiteral("deepseek-reasoner")}));
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void NativeCoreTests::aiQuestionAnalysisBuildsStructuredRequestAndCachesBySourceHash()
{
    auto question = sampleBankPackage().bank.questions.first();
    question.questionImageBlobIds = {QStringLiteral("image-one")};
    quizapp::services::AiConfiguration configuration;
    configuration.model = QStringLiteral("test-model");
    configuration.maxTokens = 2048;
    configuration.temperature = 0.2;
    configuration.customSystemPrompt = QStringLiteral("补充概念背景");
    const QJsonObject body = quizapp::services::AiQuestionAnalysisService::requestBody(
        question, configuration,
        {QStringLiteral("data:image/png;base64,AA==")});
    QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("test-model"));
    QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 2048);
    const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
    QCOMPARE(messages.size(), 2);
    const QString system = messages.first().toObject()
                               .value(QStringLiteral("content")).toString();
    QVERIFY(system.contains(QStringLiteral("不得擅自改写")));
    QVERIFY(system.contains(QStringLiteral("补充概念背景")));
    const QJsonArray userParts = messages.at(1).toObject()
                                     .value(QStringLiteral("content")).toArray();
    QCOMPARE(userParts.size(), 2);
    const QString prompt = userParts.first().toObject()
                               .value(QStringLiteral("text")).toString();
    QVERIFY(prompt.contains(question.prompt));
    QVERIFY(prompt.contains(QStringLiteral("A. Option A")));
    QVERIFY(prompt.contains(QStringLiteral("结构化正确答案：A")));
    QVERIFY(prompt.contains(question.builtinExplanation.text));

    QString error;
    QCOMPARE(quizapp::services::AiQuestionAnalysisService::parseResponse(
        R"JSON({"choices":[{"message":{"content":"  parsed answer  "}}]})JSON",
        &error), QStringLiteral("parsed answer"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(quizapp::services::AiQuestionAnalysisService::parseResponse(
        QByteArrayLiteral("{}"), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("ai-record-")));
    QVERIFY2(database.open(QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite")), &error),
             qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteAiRecordRepository repository(database.connection());
    quizapp::domain::AiRecord record;
    record.id = QUuid::createUuid();
    record.recordType = QStringLiteral("question_analysis");
    record.sourceId = question.id.toString(QUuid::WithoutBraces);
    record.model = QStringLiteral("test-model");
    record.content = QStringLiteral("cached analysis");
    record.sourceHash = question.contentHash;
    record.createdAt = QDateTime::currentDateTimeUtc();
    QVERIFY2(repository.upsert(record, &error), qPrintable(error));
    const auto restored = repository.find(record.recordType, record.sourceId, &error);
    QVERIFY2(restored.has_value(), qPrintable(error));
    QCOMPARE(restored->content, record.content);
    QVERIFY(!quizapp::services::AiQuestionAnalysisService::isStale(*restored, question));
    question.contentHash = QByteArray(32, 'z');
    QVERIFY(quizapp::services::AiQuestionAnalysisService::isStale(*restored, question));
}

void NativeCoreTests::localBackupStreamsVerifiesRollsBackAndRestores()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto writeFile = [](const QString &path, const QByteArray &data) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            return false;
        }
        QFile file(path);
        return file.open(QIODevice::WriteOnly)
            && file.write(data) == data.size();
    };

    const QString sourceData = QDir(root.path()).filePath(QStringLiteral("source-data"));
    const QString sourceShared = QDir(root.path()).filePath(QStringLiteral("source-shared"));
    const QString sourceDatabasePath = QDir(sourceData).filePath(QStringLiteral("quizapp.sqlite"));
    QVERIFY(QDir().mkpath(sourceData));
    QVERIFY(QDir().mkpath(sourceShared));
    quizapp::storage::Database sourceDatabase(QStringLiteral("backup-source"));
    QString error;
    QVERIFY2(sourceDatabase.open(sourceDatabasePath, &error), qPrintable(error));
    QVERIFY2(sourceDatabase.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository sourceQuestions(sourceDatabase.connection());
    QVERIFY2(sourceQuestions.replaceBank(sampleBankPackage(), &error), qPrintable(error));
    QVERIFY(writeFile(
        QDir(sourceData).filePath(QStringLiteral("notes/questions/example.snb/document.json")),
        QByteArrayLiteral("{\"title\":\"source note\"}")));
    QByteArray largeBlob(2 * 1024 * 1024 + 137, Qt::Uninitialized);
    for (qsizetype index = 0; index < largeBlob.size(); ++index) {
        largeBlob[index] = static_cast<char>((index * 31) % 251);
    }
    QVERIFY(writeFile(
        QDir(sourceData).filePath(QStringLiteral("blobs/example.bin")),
        largeBlob));
    QVERIFY(writeFile(
        QDir(sourceShared).filePath(QStringLiteral("QuestionBanks/Subject/Chapter.json")),
        QByteArrayLiteral("{\"name\":\"shared bank\"}")));
    QVERIFY(writeFile(
        QDir(sourceShared).filePath(QStringLiteral("RecycleBin/item.json")),
        QByteArrayLiteral("recycled")));
    QVERIFY(writeFile(
        QDir(sourceData).filePath(QStringLiteral(".backup-import-pending/ignored.quizbackup")),
        QByteArrayLiteral("temporary")));
    QVERIFY(writeFile(
        QDir(sourceData).filePath(QStringLiteral(".notebook-delete-test/ignored.json")),
        QByteArrayLiteral("temporary")));

    QSettings sourceSettings(
        QDir(root.path()).filePath(QStringLiteral("source-settings.ini")),
        QSettings::IniFormat);
    sourceSettings.setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    sourceSettings.setValue(QStringLiteral("ui/cornerRadius"), 12);
    QVERIFY2(quizapp::platform::SecureSecretStore::writeSecret(
        QStringLiteral("ai/apiKey"), QStringLiteral("source-secret"), sourceSettings, &error),
        qPrintable(error));
    sourceSettings.sync();

    quizapp::services::LocalBackupService service;
    const QString archivePath = QDir(root.path()).filePath(QStringLiteral("complete.quizbackup"));
    QVector<QString> progressStages;
    QVERIFY2(service.create(
        archivePath,
        sourceDatabase.connection(),
        sourceDatabasePath,
        sourceData,
        sourceShared,
        sourceSettings,
        false,
        QStringLiteral("2.0.0-alpha.3"),
        QStringLiteral("test-build"),
        [&progressStages](const QString &stage, qint64, qint64) {
            progressStages.append(stage);
        },
        &error), qPrintable(error));
    QVERIFY(progressStages.contains(QStringLiteral("archive")));
    QVERIFY(progressStages.contains(QStringLiteral("complete")));
    const auto inspection = service.inspect(archivePath, true);
    QVERIFY2(inspection.valid, qPrintable(inspection.error));
    QCOMPARE(inspection.schemaVersion, 2);
    QCOMPARE(inspection.counts.value(QStringLiteral("banks")).toInt(), 1);
    QCOMPARE(inspection.counts.value(QStringLiteral("questions")).toInt(), 2);
    QVERIFY(!inspection.includesSecrets);
    QVERIFY(inspection.entries.size() >= 6);
    QVERIFY(std::none_of(
        inspection.entries.cbegin(), inspection.entries.cend(),
        [](const quizapp::services::BackupEntryInfo &entry) {
            return entry.path.contains(QStringLiteral("backup-import-pending"))
                || entry.path.contains(QStringLiteral("notebook-delete"));
        }));

    const QString secretsArchivePath = QDir(root.path()).filePath(
        QStringLiteral("complete-with-secrets.quizbackup"));
    QVERIFY2(service.create(
        secretsArchivePath,
        sourceDatabase.connection(),
        sourceDatabasePath,
        sourceData,
        sourceShared,
        sourceSettings,
        true,
        QStringLiteral("2.0.0-alpha.3"),
        QStringLiteral("test-build"),
        {},
        &error), qPrintable(error));
    const auto secretsInspection = service.inspect(secretsArchivePath, true);
    QVERIFY2(secretsInspection.valid, qPrintable(secretsInspection.error));
    QVERIFY(secretsInspection.includesSecrets);

    const QString corruptPath = QDir(root.path()).filePath(QStringLiteral("corrupt.quizbackup"));
    QVERIFY(QFile::copy(archivePath, corruptPath));
    QFile corrupt(corruptPath);
    QVERIFY(corrupt.open(QIODevice::ReadWrite));
    QVERIFY(corrupt.seek(corrupt.size() - 1));
    const QByteArray tail = corrupt.read(1);
    QVERIFY(tail.size() == 1);
    QVERIFY(corrupt.seek(corrupt.size() - 1));
    const char changed = static_cast<char>(tail.at(0) ^ 0x5a);
    QVERIFY(corrupt.write(&changed, 1) == 1);
    corrupt.close();
    const auto corruptInspection = service.inspect(corruptPath, true);
    QVERIFY(!corruptInspection.valid);
    QVERIFY(corruptInspection.error.contains(QStringLiteral("校验失败")));

    const QString targetData = QDir(root.path()).filePath(QStringLiteral("target-data"));
    const QString targetDatabasePath = QDir(targetData).filePath(QStringLiteral("quizapp.sqlite"));
    const QString targetShared = QDir(root.path()).filePath(QStringLiteral("target-shared"));
    QVERIFY(QDir().mkpath(targetData));
    {
        quizapp::storage::Database targetDatabase(QStringLiteral("backup-target-before"));
        QVERIFY2(targetDatabase.open(targetDatabasePath, &error), qPrintable(error));
        QVERIFY2(targetDatabase.migrate(&error), qPrintable(error));
    }
    QVERIFY(writeFile(
        QDir(targetData).filePath(QStringLiteral("notes/questions/old.txt")),
        QByteArrayLiteral("old-note")));
    QSettings targetSettings(
        QDir(root.path()).filePath(QStringLiteral("target-settings.ini")),
        QSettings::IniFormat);
    targetSettings.setValue(QStringLiteral("ui/theme"), QStringLiteral("light"));
    QVERIFY2(quizapp::platform::SecureSecretStore::writeSecret(
        QStringLiteral("ai/apiKey"), QStringLiteral("target-secret"), targetSettings, &error),
        qPrintable(error));
    targetSettings.sync();

    QVERIFY2(service.stageRestore(archivePath, targetData, {}, &error), qPrintable(error));
    QVERIFY(service.hasPendingRestore(targetData));
    const QString blockedSharedRoot = QDir(root.path()).filePath(QStringLiteral("blocked-shared"));
    QVERIFY(writeFile(blockedSharedRoot, QByteArrayLiteral("not a directory")));
    QVERIFY(!service.applyPendingRestore(
        targetData, targetDatabasePath, blockedSharedRoot, targetSettings, &error));
    QVERIFY(error.contains(QStringLiteral("已回滚")));
    QVERIFY(service.hasPendingRestore(targetData));
    QCOMPARE(targetSettings.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("light"));
    QCOMPARE(quizapp::platform::SecureSecretStore::readSecret(
        QStringLiteral("ai/apiKey"), targetSettings, &error), QStringLiteral("target-secret"));
    {
        quizapp::storage::Database rolledBackDatabase(QStringLiteral("backup-target-rollback"));
        QVERIFY2(rolledBackDatabase.open(targetDatabasePath, &error), qPrintable(error));
        QVERIFY2(rolledBackDatabase.migrate(&error), qPrintable(error));
        quizapp::storage::SqliteQuestionRepository repository(rolledBackDatabase.connection());
        QCOMPARE(repository.listInstalledBanks(&error).size(), 0);
    }
    QVERIFY(QFileInfo::exists(
        QDir(targetData).filePath(QStringLiteral("notes/questions/old.txt"))));

    QVERIFY(QFile::remove(blockedSharedRoot));
    QVERIFY(QDir().mkpath(targetShared));
    QVERIFY2(service.applyPendingRestore(
        targetData, targetDatabasePath, targetShared, targetSettings, &error),
        qPrintable(error));
    QVERIFY(!service.hasPendingRestore(targetData));
    QCOMPARE(targetSettings.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("dark"));
    QCOMPARE(targetSettings.value(QStringLiteral("ui/cornerRadius")).toInt(), 12);
    QCOMPARE(quizapp::platform::SecureSecretStore::readSecret(
        QStringLiteral("ai/apiKey"), targetSettings, &error), QStringLiteral("target-secret"));
    QVERIFY(QFileInfo(QDir(targetData).filePath(
        QStringLiteral("notes/questions/example.snb/document.json"))).isFile());
    QFile restoredBlob(QDir(targetData).filePath(QStringLiteral("blobs/example.bin")));
    QVERIFY(restoredBlob.open(QIODevice::ReadOnly));
    QCOMPARE(restoredBlob.readAll(), largeBlob);
    restoredBlob.close();
    QVERIFY(!QFileInfo::exists(QDir(targetData).filePath(
        QStringLiteral("notes/questions/old.txt"))));
    QVERIFY(QFileInfo(QDir(targetShared).filePath(
        QStringLiteral("QuestionBanks/Subject/Chapter.json"))).isFile());
    {
        quizapp::storage::Database restoredDatabase(QStringLiteral("backup-target-restored"));
        QVERIFY2(restoredDatabase.open(targetDatabasePath, &error), qPrintable(error));
        QVERIFY2(restoredDatabase.migrate(&error), qPrintable(error));
        quizapp::storage::SqliteQuestionRepository repository(restoredDatabase.connection());
        QCOMPARE(repository.listInstalledBanks(&error).size(), 1);
        QCOMPARE(repository.countByPath(
            {QStringLiteral("Subject"), QStringLiteral("Chapter")}, &error), 2);
    }

    QVERIFY2(service.stageRestore(secretsArchivePath, targetData, {}, &error), qPrintable(error));
    QVERIFY2(service.applyPendingRestore(
        targetData, targetDatabasePath, targetShared, targetSettings, &error), qPrintable(error));
    QCOMPARE(quizapp::platform::SecureSecretStore::readSecret(
        QStringLiteral("ai/apiKey"), targetSettings, &error), QStringLiteral("source-secret"));
}

void NativeCoreTests::freeNotebookLifecycleRecyclesRestoresAndDeletesFiles()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString databasePath = QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("free-notebook-lifecycle"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::services::NotebookService service(database.connection(), root.path());

    const auto created = service.createFree(QStringLiteral("  复习 草稿  "), &error);
    QVERIFY2(created.has_value(), qPrintable(error));
    QCOMPARE(created->title, QStringLiteral("复习 草稿"));
    QCOMPARE(service.listFree(false, &error).size(), 1);
    QCOMPARE(service.listFree(true, &error).size(), 0);
    const QString bundlePath = service.absoluteBundlePath(*created);
    QVERIFY(bundlePath.startsWith(QDir::cleanPath(root.path()), Qt::CaseInsensitive));
    QVERIFY(QDir().mkpath(bundlePath));
    QFile manifest(QDir(bundlePath).filePath(QStringLiteral("document.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    const QByteArray document = QByteArrayLiteral("{\"name\":\"复习 草稿\"}");
    QCOMPARE(manifest.write(document), document.size());
    manifest.close();
    QVERIFY2(service.markSaved(created->id, &error), qPrintable(error));

    QVERIFY2(service.rename(created->id, QStringLiteral("期末复习"), &error), qPrintable(error));
    auto records = service.listFree(false, &error);
    QCOMPARE(records.first().title, QStringLiteral("期末复习"));
    QVERIFY(!records.first().contentHash.isEmpty());
    QVERIFY2(service.recycle(created->id, &error), qPrintable(error));
    QCOMPARE(service.listFree(false, &error).size(), 0);
    QCOMPARE(service.listFree(true, &error).size(), 1);
    QVERIFY(QFileInfo::exists(bundlePath));
    QVERIFY2(service.restore(created->id, &error), qPrintable(error));
    QCOMPARE(service.listFree(false, &error).size(), 1);

    QVERIFY2(service.recycle(created->id, &error), qPrintable(error));
    QVERIFY2(service.permanentlyDelete(created->id, &error), qPrintable(error));
    QCOMPARE(service.listFree(true, &error).size(), 0);
    QVERIFY(!QFileInfo::exists(bundlePath));
}

void NativeCoreTests::questionRepositoryRoundTripAndSoftDelete()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("questions-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());

    auto package = sampleBankPackage();
    QVERIFY2(repository.replaceBank(package, &error), qPrintable(error));
    const QStringList path{QStringLiteral("Subject"), QStringLiteral("Chapter")};
    QCOMPARE(repository.countByPath(path, &error), 2);
    const auto questions = repository.listByPath(path, &error);
    QCOMPARE(questions.size(), 2);
    QCOMPARE(questions.at(0).options.size(), 2);
    QCOMPARE(questions.at(1).builtinExplanation.text, QStringLiteral("Explanation 2"));

    const QUuid removedId = package.bank.questions.at(1).id;
    package.bank.questions.removeLast();
    package.bank.questions[0].prompt = QStringLiteral("Updated question");
    package.bank.questions[0].contentHash = quizapp::domain::QuestionIdentity::contentHash(
        package.bank.questions[0].path,
        package.bank.questions[0].prompt,
        package.bank.questions[0].options);
    package.bank.contentHash = QByteArray(32, 'c');
    package.bank.distributionVersion = QStringLiteral("2");
    QVERIFY2(repository.replaceBank(package, &error), qPrintable(error));
    QCOMPARE(repository.countByPath(path, &error), 1);
    QCOMPARE(repository.findById(package.bank.questions.at(0).id, &error)->prompt,
             QStringLiteral("Updated question"));
    QVERIFY(!repository.findById(removedId, &error).has_value());

    QSqlQuery inactive(database.connection());
    inactive.prepare(QStringLiteral("SELECT active FROM questions WHERE id=?"));
    inactive.addBindValue(removedId.toString(QUuid::WithoutBraces));
    QVERIFY(inactive.exec());
    QVERIFY(inactive.next());
    QCOMPARE(inactive.value(0).toInt(), 0);
}

void NativeCoreTests::questionRepositoryListsWholePathSubtree()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("question-tree-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());

    auto first = sampleBankPackage();
    QVERIFY2(repository.replaceBank(first, &error), qPrintable(error));

    auto second = sampleBankPackage();
    second.subjects[1].id = QStringLiteral("subject-chapter-two");
    second.subjects[1].title = QStringLiteral("Chapter 2");
    second.bank.id = QStringLiteral("bank-two");
    second.bank.subjectId = second.subjects[1].id;
    second.bank.title = QStringLiteral("Chapter 2 Bank");
    second.bank.sourceId = QStringLiteral("bank-source-two");
    second.bank.contentHash = QByteArray(32, 'e');
    for (qsizetype index = 0; index < second.bank.questions.size(); ++index) {
        auto &question = second.bank.questions[index];
        question.bankId = second.bank.id;
        question.sourceId = QStringLiteral("chapter-two-question-%1").arg(index + 1);
        question.path = {QStringLiteral("Subject"), QStringLiteral("Chapter 2")};
        question.id = quizapp::domain::QuestionIdentity::create(
            question.sourceProvider, question.sourceId, question.path,
            question.prompt, question.options);
        question.contentHash = quizapp::domain::QuestionIdentity::contentHash(question);
    }
    QVERIFY2(repository.replaceBank(second, &error), qPrintable(error));

    QCOMPARE(repository.listByPathPrefix({QStringLiteral("Subject")}, &error).size(), 4);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(repository.listByPathPrefix(
                 {QStringLiteral("Subject"), QStringLiteral("Chapter")}, &error).size(), 2);
    QCOMPARE(repository.listByPathPrefix({}, &error).size(), 4);
}

void NativeCoreTests::questionRepositoryRollsBackMissingBlob()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("rollback-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());

    auto package = sampleBankPackage();
    QVERIFY2(repository.replaceBank(package, &error), qPrintable(error));
    const QUuid firstId = package.bank.questions.constFirst().id;
    const QString originalPrompt = package.bank.questions.constFirst().prompt;
    package.bank.questions[0].prompt = QStringLiteral("Must roll back");
    package.bank.questions[0].questionImageBlobIds.append(QStringLiteral("missing-blob"));
    package.bank.questions[0].contentHash = quizapp::domain::QuestionIdentity::contentHash(
        package.bank.questions[0].path,
        package.bank.questions[0].prompt,
        package.bank.questions[0].options);
    QVERIFY(!repository.replaceBank(package, &error));
    QVERIFY(!error.isEmpty());
    const auto restored = repository.findById(firstId, &error);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->prompt, originalPrompt);
}

void NativeCoreTests::practiceRepositoryRoundTripAndModeIsolation()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("practice-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    quizapp::storage::SqlitePracticeRepository practiceRepository(database.connection());
    auto package = sampleBankPackage();
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));

    quizapp::domain::PracticeSession sequential;
    sequential.id = QUuid::createUuid();
    sequential.scopeId = QStringLiteral("scope-one");
    sequential.mode = quizapp::domain::PracticeMode::Sequential;
    for (const auto &question : package.bank.questions) {
        sequential.questionOrder.append(question.id);
    }
    sequential.currentIndex = 1;
    sequential.answers.insert(sequential.questionOrder.at(0), QStringLiteral("A"));
    sequential.drafts.insert(sequential.questionOrder.at(1), QStringLiteral("AB"));
    sequential.revealedAnswers.insert(sequential.questionOrder.at(1));
    sequential.viewport.insert(QStringLiteral("scrollTop"), 240);
    QVERIFY2(practiceRepository.save(sequential, &error), qPrintable(error));

    const auto loaded = practiceRepository.load(sequential.id, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->currentIndex, 1);
    QCOMPARE(loaded->answers.value(sequential.questionOrder.at(0)), QStringLiteral("A"));
    QCOMPARE(loaded->drafts.value(sequential.questionOrder.at(1)), QStringLiteral("AB"));
    QVERIFY(loaded->revealedAnswers.contains(sequential.questionOrder.at(1)));
    QCOMPARE(loaded->viewport.value(QStringLiteral("scrollTop")).toInt(), 240);
    QVERIFY(!loaded->dirty);

    auto random = sequential;
    random.id = QUuid::createUuid();
    random.mode = quizapp::domain::PracticeMode::Random;
    std::reverse(random.questionOrder.begin(), random.questionOrder.end());
    QVERIFY2(practiceRepository.save(random, &error), qPrintable(error));
    QCOMPARE(practiceRepository.latest(
                 sequential.scopeId, quizapp::domain::PracticeMode::Sequential, &error)->id,
             sequential.id);
    QCOMPARE(practiceRepository.latest(
                 random.scopeId, quizapp::domain::PracticeMode::Random, &error)->id,
             random.id);
    QCOMPARE(practiceRepository.latestAcrossModes(
                 sequential.scopeId,
                 {quizapp::domain::PracticeMode::Sequential,
                  quizapp::domain::PracticeMode::Random},
                 &error)->id,
             random.id);
    QCOMPARE(practiceRepository.latestIncompleteAcrossScopes(
                 {quizapp::domain::PracticeMode::Sequential,
                  quizapp::domain::PracticeMode::Random},
                 &error)->id,
             random.id);
    random.complete = true;
    QVERIFY2(practiceRepository.save(random, &error), qPrintable(error));
    QCOMPARE(practiceRepository.latestIncompleteAcrossScopes(
                 {quizapp::domain::PracticeMode::Sequential,
                  quizapp::domain::PracticeMode::Random},
                 &error)->id,
             sequential.id);
    QVERIFY2(practiceRepository.removeScopeMode(
                 random.scopeId, quizapp::domain::PracticeMode::Random, &error),
             qPrintable(error));
    QVERIFY(!practiceRepository.latest(
        random.scopeId, quizapp::domain::PracticeMode::Random, &error).has_value());

    package.bank.questions.removeLast();
    package.bank.contentHash = QByteArray(32, 'd');
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));
    QVERIFY(practiceRepository.load(sequential.id, &error).has_value());
    QVERIFY2(practiceRepository.remove(sequential.id, &error), qPrintable(error));
    QVERIFY(!practiceRepository.load(sequential.id, &error).has_value());
}

void NativeCoreTests::questionAnswerStateSynchronizesScopesAndModes()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("answer-state-")));
    QString error;
    QVERIFY2(database.open(QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite")), &error),
             qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    const auto package = sampleBankPackage();
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));

    const QUuid first = package.bank.questions.at(0).id;
    const QUuid second = package.bank.questions.at(1).id;
    quizapp::storage::SqliteAnswerStateRepository repository(database.connection());
    quizapp::domain::PracticeSession sequential;
    sequential.id = QUuid::createUuid();
    sequential.scopeId = QStringLiteral("path:first");
    sequential.mode = quizapp::domain::PracticeMode::Sequential;
    sequential.questionOrder = {first, second};
    sequential.answers.insert(first, QStringLiteral("A"));
    sequential.answers.insert(second, QStringLiteral("AB"));
    QVERIFY2(repository.saveSessionAnswers(sequential, &error), qPrintable(error));

    quizapp::domain::PracticeSession random = sequential;
    random.id = QUuid::createUuid();
    random.scopeId = QStringLiteral("path:all");
    random.mode = quizapp::domain::PracticeMode::Random;
    random.answers.clear();
    random.answers.insert(first, QStringLiteral("B"));
    QVERIFY2(repository.saveSessionAnswers(random, &error), qPrintable(error));

    const auto sequentialState = repository.load(
        {first, second}, quizapp::domain::PracticeMode::Sequential, &error);
    QCOMPARE(sequentialState.value(first), QStringLiteral("A"));
    QCOMPARE(sequentialState.value(second), QStringLiteral("AB"));
    const auto randomState = repository.load(
        {first, second}, quizapp::domain::PracticeMode::Random, &error);
    QCOMPARE(randomState.value(first), QStringLiteral("B"));
    QVERIFY(randomState.contains(second));
    QVERIFY(randomState.value(second).isEmpty());

    sequential.answers.remove(first);
    QVERIFY2(repository.saveSessionAnswers(sequential, &error), qPrintable(error));
    quizapp::domain::PracticeSession stale = sequential;
    stale.answers.insert(first, QStringLiteral("A"));
    QVERIFY2(repository.applyToSession(&stale, &error), qPrintable(error));
    QVERIFY(!stale.answers.contains(first));
    QCOMPARE(stale.answers.value(second), QStringLiteral("AB"));

    QVector<QUuid> largeQuery;
    for (int index = 0; index < 1001; ++index) largeQuery.append(QUuid::createUuid());
    largeQuery.append(first);
    const auto chunked = repository.load(
        largeQuery, quizapp::domain::PracticeMode::Random, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(chunked.value(first), QStringLiteral("B"));

    QVERIFY2(repository.clear(
                 {first, second}, quizapp::domain::PracticeMode::Random, &error),
             qPrintable(error));
    QVERIFY(repository.load(
        {first, second}, quizapp::domain::PracticeMode::Random, &error).isEmpty());
    QCOMPARE(repository.load(
                 {first, second}, quizapp::domain::PracticeMode::Sequential, &error)
                 .value(second),
             QStringLiteral("AB"));
}

void NativeCoreTests::wrongBookMembershipIsExplicitAndIndependentFromPracticeProgress()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("wrong-book-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    quizapp::storage::SqlitePracticeRepository practiceRepository(database.connection());
    quizapp::storage::SqliteWrongBookRepository wrongBookRepository(database.connection());
    quizapp::services::WrongBookService service(wrongBookRepository);
    const auto package = sampleBankPackage();
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));
    const QUuid questionId = package.bank.questions.constFirst().id;

    bool contained = true;
    QVERIFY2(wrongBookRepository.contains(questionId, &contained, &error), qPrintable(error));
    QVERIFY(!contained);
    QVERIFY2(service.setMembership(questionId, true, &error), qPrintable(error));
    QVERIFY2(wrongBookRepository.contains(questionId, &contained, &error), qPrintable(error));
    QVERIFY(contained);
    QCOMPARE(service.questionIds({QStringLiteral("Subject")}, &error), QSet<QUuid>{questionId});

    quizapp::domain::PracticeSession session;
    session.id = QUuid::createUuid();
    session.scopeId = QStringLiteral("scope");
    session.questionOrder = {questionId};
    QVERIFY2(practiceRepository.save(session, &error), qPrintable(error));
    QVERIFY2(practiceRepository.remove(session.id, &error), qPrintable(error));
    QVERIFY2(wrongBookRepository.contains(questionId, &contained, &error), qPrintable(error));
    QVERIFY(contained);

    QVERIFY2(service.setMembership(questionId, false, &error), qPrintable(error));
    QVERIFY2(wrongBookRepository.contains(questionId, &contained, &error), qPrintable(error));
    QVERIFY(!contained);
}

void NativeCoreTests::fsrsReviewRoundTripPersistsCardAndHistory()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("review-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    quizapp::storage::SqliteReviewRepository reviewRepository(database.connection());
    quizapp::services::ReviewService service(reviewRepository);
    const auto package = sampleBankPackage();
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));
    const QUuid questionId = package.bank.questions.constFirst().id;
    const QDateTime reviewedAt(
        QDate(2026, 7, 16), QTime(12, 0), QTimeZone::UTC);

    QVERIFY2(service.add(questionId, reviewedAt, &error), qPrintable(error));
    bool contained = false;
    QVERIFY2(service.contains(questionId, &contained, &error), qPrintable(error));
    QVERIFY(contained);
    QCOMPARE(service.due(reviewedAt, 10, &error).size(), 1);
    const auto previews = service.preview(questionId, reviewedAt, &error);
    QVERIFY2(previews.has_value(), qPrintable(error));
    QVERIFY(previews->at(0).scheduledDays >= 1);
    QVERIFY(previews->at(3).scheduledDays >= previews->at(2).scheduledDays);

    const auto updated = service.rate(
        questionId, quizapp::domain::ReviewRating::Good, reviewedAt, &error);
    QVERIFY2(updated.has_value(), qPrintable(error));
    QVERIFY(updated->hasMemoryState);
    QCOMPARE(updated->reviewCount, 1);
    QCOMPARE(updated->lapseCount, 0);
    QVERIFY(updated->dueAt > reviewedAt);
    QCOMPARE(service.due(reviewedAt, 10, &error).size(), 0);

    const auto history = reviewRepository.history(questionId, 10, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.constFirst().rating, quizapp::domain::ReviewRating::Good);
    QCOMPARE(history.constFirst().schedulerVersion, QStringLiteral("fsrs-rs/6.6.0"));
    const auto stats = service.stats(reviewedAt, &error);
    QCOMPARE(stats.total, 1);
    QCOMPARE(stats.learned, 1);
    QCOMPARE(stats.due, 0);

    QVERIFY2(service.remove(questionId, &error), qPrintable(error));
    QVERIFY2(service.contains(questionId, &contained, &error), qPrintable(error));
    QVERIFY(!contained);
    QVERIFY(reviewRepository.history(questionId, 10, &error).isEmpty());

    const QUuid legacyQuestionId = package.bank.questions.at(1).id;
    QSqlQuery legacy(database.connection());
    legacy.prepare(QStringLiteral(
        "INSERT INTO review_cards(question_id, fsrs_state_json, due_at, updated_at) "
        "VALUES(?, ?, ?, ?)"));
    legacy.addBindValue(legacyQuestionId.toString(QUuid::WithoutBraces));
    legacy.addBindValue(QStringLiteral("{\"stability\":7.5,\"difficulty\":4.25}"));
    legacy.addBindValue(reviewedAt.toString(Qt::ISODateWithMs));
    legacy.addBindValue(reviewedAt.toString(Qt::ISODateWithMs));
    QVERIFY2(legacy.exec(), qPrintable(legacy.lastError().text()));
    const auto migratedLegacy = reviewRepository.find(legacyQuestionId, &error);
    QVERIFY2(migratedLegacy.has_value(), qPrintable(error));
    QVERIFY(migratedLegacy->hasMemoryState);
    QCOMPARE(migratedLegacy->memory.stability, 7.5);
    QCOMPARE(migratedLegacy->memory.difficulty, 4.25);
}

void NativeCoreTests::studyTotalsSplitForegroundEventsAcrossLocalMidnight()
{
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("study-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteStudyRepository repository(database.connection());
    quizapp::services::StudyService service(repository);
    const QTimeZone zone(QByteArrayLiteral("Asia/Shanghai"));
    QVERIFY(zone.isValid());
    const QDateTime startedAt(
        QDate(2026, 7, 16), QTime(23, 59, 30), zone);
    QVERIFY2(service.record(
                 quizapp::domain::StudyActivity::Review,
                 QStringLiteral("review"), startedAt, 90, &error),
             qPrintable(error));
    const auto totals = service.dailyTotals(
        QDate(2026, 7, 16), QDate(2026, 7, 17), zone, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(totals.size(), 2);
    QCOMPARE(totals.at(0).durationSeconds, 30);
    QCOMPARE(totals.at(1).durationSeconds, 60);
}

void NativeCoreTests::xiaoyiImportRepairsTypesAndSeparatesMedia()
{
    const QByteArray json = R"JSON({
      "schemaVersion": 2,
      "name": "公开考研题库-第一章-练习",
      "path": ["考研数学", "公开考研题库", "第一章", "练习"],
      "source": {"provider": "xiaoyivip"},
      "questions": [
        {
          "id": "xiaoyi:1",
          "type": "single",
          "q": "\u200B第 1 题（题目见图片）",
          "options": ["A", "B", "C", "D"],
          "ans": "AC",
          "questionImages": ["data:image/png;base64,AAAA"],
          "explanations": {"builtin": {
            "text": "来源解析",
            "images": ["data:image/png;base64,BBBB"],
            "source": {"provider": "xiaoyivip", "sourceId": "1"}
          }}
        },
        {
          "id": "xiaoyi:2",
          "type": "subjective",
          "q": "第 2 题（题目见图片）",
          "options": [],
          "ans": "",
          "questionImages": ["data:image/png;base64,CCCC"],
          "explanations": {"builtin": {"text": "解答过程"}}
        },
        {
          "id": "xiaoyi:3",
          "type": "single",
          "q": "判断测试",
          "options": ["错", "对"],
          "ans": "A"
        }
      ]
    })JSON";
    quizapp::services::LegacyBankImporter importer;
    const auto result = importer.importJson(
        json, QStringLiteral("公开考研题库/第一章/练习.json"));
    QVERIFY(result.succeeded());
    QCOMPARE(result.acceptedQuestionCount, 3);
    QCOMPARE(result.repairedQuestionCount, 2);
    QCOMPARE(result.pendingMedia.size(), 3);
    QCOMPARE(result.package->bank.sourceProvider, QStringLiteral("xiaoyivip"));
    QCOMPARE(result.package->bank.questions.at(0).type,
             quizapp::domain::QuestionType::Multiple);
    QCOMPARE(result.package->bank.questions.at(0).correctAnswer, QStringLiteral("AC"));
    QVERIFY(!result.package->bank.questions.at(0).prompt.contains(QChar(0x200B)));
    QCOMPARE(result.package->bank.questions.at(1).type,
             quizapp::domain::QuestionType::Subjective);
    QCOMPARE(result.package->bank.questions.at(2).type,
             quizapp::domain::QuestionType::Boolean);
    QCOMPARE(result.package->bank.questions.at(2).options,
             QStringList({QStringLiteral("对"), QStringLiteral("错")}));
    QCOMPARE(result.package->bank.questions.at(2).correctAnswer, QStringLiteral("B"));
}

void NativeCoreTests::xiaoyiMissingAnswerWithBuiltinExplanationRemainsUngradedChoice()
{
    const QByteArray json = R"JSON({
      "name": "公开题库-缺少答案",
      "path": ["考研数学", "公开题库", "选择题"],
      "source": {"provider": "xiaoyivip"},
      "questions": [{
        "id": "xiaoyi:no-answer",
        "type": "single",
        "q": "第 1 题（题目见图片）",
        "options": ["A", "B", "C", "D"],
        "ans": "",
        "explanations": {"builtin": {
          "images": ["data:image/png;base64,AAAA"],
          "source": {"provider": "xiaoyivip", "sourceId": "no-answer"}
        }}
      }]
    })JSON";
    quizapp::services::LegacyBankImporter importer;
    const auto result = importer.importJson(json, QStringLiteral("公开题库/选择题.json"));
    QVERIFY(result.succeeded());
    QCOMPARE(result.package->bank.questions.constFirst().type,
             quizapp::domain::QuestionType::Single);
    QVERIFY(result.package->bank.questions.constFirst().correctAnswer.isEmpty());
    QVERIFY(std::any_of(
        result.diagnostics.cbegin(), result.diagnostics.cend(),
        [](const quizapp::domain::ImportDiagnostic &diagnostic) {
            return diagnostic.code == QStringLiteral("question.answer_unavailable")
                && diagnostic.severity == quizapp::domain::ImportDiagnosticSeverity::Warning;
        }));
}

void NativeCoreTests::xiaoyiMediaInstallDeduplicatesAndPersistsBlobs()
{
    const QString png = QStringLiteral(
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwC"
        "AAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
    const QByteArray json = QStringLiteral(R"JSON({
      "name": "公开题库-图片题",
      "path": ["考研数学", "公开题库", "图片题"],
      "source": {"provider": "xiaoyivip"},
      "questions": [{
        "id": "xiaoyi:media-1",
        "type": "subjective",
        "q": "第 1 题（题目见图片）",
        "options": [],
        "ans": "",
        "questionImages": ["%1"],
        "explanations": {"builtin": {
          "text": "内置解析",
          "images": ["%1"],
          "source": {"provider": "xiaoyivip", "sourceId": "media-1"}
        }}
      }]
    })JSON").arg(png).toUtf8();

    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("media-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    quizapp::services::BlobStore blobStore(dataRoot.path());
    quizapp::services::BankInstallService installer;

    const auto result = installer.installJson(
        json, QStringLiteral("公开题库/图片题.json"), blobStore, repository);
    QVERIFY2(result.installed, qPrintable(result.error));
    QVERIFY(result.import.package.has_value());
    QCOMPARE(result.import.package->blobs.size(), 1);

    const auto &asset = result.import.package->blobs.constFirst();
    QCOMPARE(asset.id.size(), 64);
    QCOMPARE(asset.mediaType, QStringLiteral("image/png"));
    QVERIFY(QFileInfo::exists(blobStore.absolutePath(asset)));

    const QUuid questionId = result.import.package->bank.questions.constFirst().id;
    const auto stored = repository.findById(questionId, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));
    QCOMPARE(stored->questionImageBlobIds, QStringList({asset.id}));
    QCOMPARE(stored->builtinExplanation.imageBlobIds, QStringList({asset.id}));
    QCOMPARE(stored->blobRelativePaths.value(asset.id), asset.relativePath);
    QVERIFY(QFileInfo::exists(QDir(dataRoot.path()).filePath(
        stored->blobRelativePaths.value(asset.id))));

    QSqlQuery blobs(database.connection());
    QVERIFY(blobs.exec(QStringLiteral("SELECT COUNT(*) FROM blobs")));
    QVERIFY(blobs.next());
    QCOMPARE(blobs.value(0).toInt(), 1);
    QVERIFY(blobs.exec(QStringLiteral("SELECT COUNT(*) FROM question_blobs")));
    QVERIFY(blobs.next());
    QCOMPARE(blobs.value(0).toInt(), 2);
}

void NativeCoreTests::invalidEmbeddedMediaRejectsWholeInstall()
{
    const QByteArray json = R"JSON({
      "name": "损坏媒体题库",
      "path": ["考研数学", "损坏媒体题库"],
      "source": {"provider": "xiaoyivip"},
      "questions": [{
        "id": "xiaoyi:bad-media",
        "type": "subjective",
        "q": "损坏图片",
        "options": [],
        "ans": "",
        "questionImages": ["data:image/png;base64,AAAA"]
      }]
    })JSON";

    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("bad-media-")));
    QString error;
    QVERIFY2(database.open(QStringLiteral(":memory:"), &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    quizapp::services::BlobStore blobStore(dataRoot.path());
    quizapp::services::BankInstallService installer;

    const auto result = installer.installJson(
        json, QStringLiteral("损坏媒体题库.json"), blobStore, repository);
    QVERIFY(!result.installed);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(repository.countByPath(
                 {QStringLiteral("考研数学"), QStringLiteral("损坏媒体题库")}, &error), 0);

    int fileCount = 0;
    QDirIterator files(dataRoot.path(), QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        ++fileCount;
    }
    QCOMPARE(fileCount, 0);
}

void NativeCoreTests::xiaoyiDirectoryInstallReportsProgressAndUpdatesStats()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString inputPath = QDir(root.path()).filePath(QStringLiteral("input/第一章"));
    const QString dataPath = QDir(root.path()).filePath(QStringLiteral("data"));
    QVERIFY(QDir().mkpath(inputPath));
    QVERIFY(QDir().mkpath(dataPath));
    QFile section(QDir(inputPath).filePath(QStringLiteral("练习.json")));
    QVERIFY(section.open(QIODevice::WriteOnly));
    const QByteArray sectionJson = R"JSON({
      "name": "公开题库-第一章-练习",
      "path": ["考研数学", "公开题库", "第一章", "练习"],
      "source": {"provider": "xiaoyivip"},
      "questions": [{
        "id": "xiaoyi:directory-1",
        "type": "subjective",
        "q": "目录安装测试",
        "options": [],
        "ans": ""
      }]
    })JSON";
    QCOMPARE(section.write(sectionJson), static_cast<qint64>(sectionJson.size()));
    section.close();

    const QString databasePath = QDir(dataPath).filePath(QStringLiteral("quizapp.sqlite"));
    int progressCalls = 0;
    quizapp::services::XiaoyiDirectoryInstallService service;
    const auto result = service.install(
        QDir(root.path()).filePath(QStringLiteral("input")),
        databasePath,
        dataPath,
        [&progressCalls](int, int, const QString &) {
            ++progressCalls;
            return true;
        });
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(!result.canceled);
    QCOMPARE(result.discoveredSections, 1);
    QCOMPARE(result.installedSections, 1);
    QCOMPARE(result.installedQuestions, 1);
    QVERIFY(progressCalls >= 2);

    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("directory-stats-")));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    const quizapp::storage::SqliteLibraryRepository repository(database.connection());
    const auto stats = repository.stats(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(stats.bankCount, 1);
    QCOMPARE(stats.questionCount, 1);
}

void NativeCoreTests::bankImportRejectsUnsafeBooleanQuestion()
{
    const QByteArray json = R"JSON({
      "name": "坏题库",
      "subject": "测试",
      "chapter": "判断题",
      "questions": [{
        "id": "bad-1",
        "type": "判断",
        "q": "错误声明",
        "options": ["甲", "乙", "丙", "丁"],
        "ans": "A"
      }]
    })JSON";
    quizapp::services::LegacyBankImporter importer;
    const auto result = importer.importJson(json, QStringLiteral("测试/坏题库.json"));
    QVERIFY(!result.succeeded());
    QCOMPARE(result.rejectedQuestionCount, 1);
    QVERIFY(std::any_of(
        result.diagnostics.cbegin(), result.diagnostics.cend(),
        [](const quizapp::domain::ImportDiagnostic &diagnostic) {
            return diagnostic.code == QStringLiteral("question.boolean_options_invalid");
        }));
}

void NativeCoreTests::sharedStorageCreatesNamedDirectories()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString sharedRoot = QDir(root.path()).filePath(QStringLiteral("QuizApp"));
    quizapp::services::SharedStorageService service;
    const auto layout = service.prepare(sharedRoot);
    QVERIFY2(layout.ready(), qPrintable(layout.error));
    QVERIFY(QFileInfo::exists(layout.questionBanks));
    QVERIFY(QFileInfo::exists(layout.backups));
    QVERIFY(QFileInfo::exists(layout.exports));
    QVERIFY(QFileInfo::exists(layout.notes));
    QVERIFY(QFileInfo::exists(layout.recycleBin));
    QCOMPARE(QFileInfo(layout.questionBanks).fileName(), QStringLiteral("QuestionBanks"));
}

void NativeCoreTests::sharedBankHierarchyComesFromRelativePath()
{
    QCOMPARE(
        quizapp::services::BankDirectorySyncService::hierarchyForRelativePath(
            QStringLiteral("毛概/第一章/单选题.json")),
        QStringList({QStringLiteral("毛概"), QStringLiteral("第一章"), QStringLiteral("单选题")}));
    QCOMPARE(
        quizapp::services::BankDirectorySyncService::hierarchyForRelativePath(
            QStringLiteral("独立题库.json")),
        QStringList({QStringLiteral("独立题库")}));
    QVERIFY(quizapp::services::BankDirectorySyncService::hierarchyForRelativePath(
        QStringLiteral("../越界.json")).isEmpty());
}

void NativeCoreTests::sharedBankSyncTracksUpdatesAndMissingFiles()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dataRoot = QDir(root.path()).filePath(QStringLiteral("private"));
    QVERIFY(QDir().mkpath(dataRoot));
    quizapp::services::SharedStorageService storageService;
    const auto layout = storageService.prepare(
        QDir(root.path()).filePath(QStringLiteral("QuizApp")));
    QVERIFY2(layout.ready(), qPrintable(layout.error));
    const QString chapterDirectory =
        QDir(layout.questionBanks).filePath(QStringLiteral("毛概/第一章"));
    QVERIFY(QDir().mkpath(chapterDirectory));
    const QString bankPath = QDir(chapterDirectory).filePath(QStringLiteral("单选题.json"));
    const QByteArray firstJson = R"JSON({
      "name": "JSON 内标题不控制目录",
      "path": ["错误科目", "错误章节"],
      "source": {"provider": "local-test"},
      "questions": [{
        "id": "shared-question-1",
        "type": "single",
        "q": "第一道共享题库题目",
        "options": ["甲", "乙"],
        "ans": "A"
      }]
    })JSON";
    QFile bank(bankPath);
    QVERIFY(bank.open(QIODevice::WriteOnly));
    QCOMPARE(bank.write(firstJson), static_cast<qint64>(firstJson.size()));
    bank.close();

    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::services::BankDirectorySyncService syncService;
    const auto installed = syncService.synchronize(
        layout.questionBanks, databasePath, dataRoot);
    QVERIFY2(installed.error.isEmpty(), qPrintable(installed.error));
    QCOMPARE(installed.installedFiles, 1);
    QCOMPARE(installed.installedQuestions, 1);

    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("shared-sync-")));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    const QStringList expectedPath{
        QStringLiteral("毛概"), QStringLiteral("第一章"), QStringLiteral("单选题")};
    QCOMPARE(repository.listByPath(expectedPath, &error).size(), 1);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const auto unchanged = syncService.synchronize(
        layout.questionBanks, databasePath, dataRoot);
    QVERIFY2(unchanged.error.isEmpty(), qPrintable(unchanged.error));
    QCOMPARE(unchanged.unchangedFiles, 1);

    QVERIFY(QFile::remove(bankPath));
    const auto missing = syncService.synchronize(
        layout.questionBanks, databasePath, dataRoot);
    QVERIFY2(missing.error.isEmpty(), qPrintable(missing.error));
    QCOMPARE(missing.missingFiles, 1);
    QCOMPARE(repository.listByPath(expectedPath, &error).size(), 0);

    QVERIFY(bank.open(QIODevice::WriteOnly));
    QCOMPARE(bank.write(firstJson), static_cast<qint64>(firstJson.size()));
    bank.close();
    const auto restored = syncService.synchronize(
        layout.questionBanks, databasePath, dataRoot);
    QVERIFY2(restored.error.isEmpty(), qPrintable(restored.error));
    QCOMPARE(restored.restoredFiles, 1);
    QCOMPARE(repository.listByPath(expectedPath, &error).size(), 1);
}

void NativeCoreTests::sharedBankOverridesBundledPathAndRestoresIt()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dataRoot = QDir(root.path()).filePath(QStringLiteral("private"));
    QVERIFY(QDir().mkpath(dataRoot));
    quizapp::services::SharedStorageService storageService;
    const auto layout = storageService.prepare(
        QDir(root.path()).filePath(QStringLiteral("QuizApp")));
    QVERIFY2(layout.ready(), qPrintable(layout.error));
    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("shared-override-")));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());

    auto bundled = sampleBankPackage();
    bundled.bank.id = QStringLiteral("bundled-politics-intro");
    bundled.bank.sourceId = QStringLiteral("bundled-politics-intro-source");
    bundled.bank.questions.resize(1);
    bundled.bank.questions[0].bankId = bundled.bank.id;
    bundled.bank.questions[0].sourceId = QStringLiteral("bundled-question");
    bundled.bank.questions[0].path = {
        QStringLiteral("毛概"), QStringLiteral("导论")};
    bundled.bank.questions[0].prompt = QStringLiteral("内置题目");
    bundled.bank.questions[0].id = quizapp::domain::QuestionIdentity::create(
        bundled.bank.questions[0].sourceProvider,
        bundled.bank.questions[0].sourceId,
        bundled.bank.questions[0].path,
        bundled.bank.questions[0].prompt,
        bundled.bank.questions[0].options);
    bundled.bank.questions[0].contentHash =
        quizapp::domain::QuestionIdentity::contentHash(bundled.bank.questions[0]);
    QVERIFY2(repository.replaceBank(bundled, &error), qPrintable(error));

    const QString bankDirectory = QDir(layout.questionBanks).filePath(QStringLiteral("毛概"));
    QVERIFY(QDir().mkpath(bankDirectory));
    const QString sharedPath = QDir(bankDirectory).filePath(QStringLiteral("导论.json"));
    QFile shared(sharedPath);
    QVERIFY(shared.open(QIODevice::WriteOnly));
    const QByteArray sharedPayload = R"JSON({
      "name": "毛概-导论",
      "questions": [{
        "id": "shared-question",
        "type": "single",
        "q": "Release 共享题目",
        "options": ["A", "B"],
        "ans": "A"
      }]
    })JSON";
    QCOMPARE(shared.write(sharedPayload), static_cast<qint64>(sharedPayload.size()));
    shared.close();

    quizapp::services::BankDirectorySyncService syncService;
    const auto installed = syncService.synchronize(
        layout.questionBanks, databasePath, dataRoot);
    QVERIFY2(installed.error.isEmpty(), qPrintable(installed.error));
    QCOMPARE(repository.listInstalledBanks(&error).size(), 1);
    const auto sharedQuestions = repository.listByPath(
        {QStringLiteral("毛概"), QStringLiteral("导论")}, &error);
    QCOMPARE(sharedQuestions.size(), 1);
    QCOMPARE(sharedQuestions.constFirst().prompt, QStringLiteral("Release 共享题目"));
    QSqlQuery overrideCount(database.connection());
    QVERIFY(overrideCount.exec(QStringLiteral(
        "SELECT COUNT(*) FROM managed_bank_overrides")));
    QVERIFY(overrideCount.next());
    QCOMPARE(overrideCount.value(0).toInt(), 1);
    overrideCount.finish();

    QVERIFY(QFile::remove(sharedPath));
    const auto missing = syncService.synchronize(
        layout.questionBanks, databasePath, dataRoot);
    QVERIFY2(missing.error.isEmpty(), qPrintable(missing.error));
    QCOMPARE(missing.missingFiles, 1);
    const auto restoredQuestions = repository.listByPath(
        {QStringLiteral("毛概"), QStringLiteral("导论")}, &error);
    QCOMPARE(restoredQuestions.size(), 1);
    QCOMPARE(restoredQuestions.constFirst().prompt, QStringLiteral("内置题目"));
    QVERIFY(overrideCount.exec(QStringLiteral(
        "SELECT COUNT(*) FROM managed_bank_overrides")));
    QVERIFY(overrideCount.next());
    QCOMPARE(overrideCount.value(0).toInt(), 0);
}

void NativeCoreTests::sharedBankMovesPreserveQuestionIdentity()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dataRoot = QDir(root.path()).filePath(QStringLiteral("private"));
    QVERIFY(QDir().mkpath(dataRoot));
    quizapp::services::SharedStorageService storageService;
    const auto layout = storageService.prepare(
        QDir(root.path()).filePath(QStringLiteral("QuizApp")));
    QVERIFY2(layout.ready(), qPrintable(layout.error));

    const QString chapterDirectory = QDir(layout.questionBanks).filePath(
        QStringLiteral("毛概/第一章"));
    QVERIFY(QDir().mkpath(chapterDirectory));
    const QString originalFile = QDir(chapterDirectory).filePath(
        QStringLiteral("单选题.json"));
    QFile source(originalFile);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray json = R"JSON({
      "name": "路径移动测试",
      "source": {"provider": "local-test"},
      "questions": [{
        "id": "stable-move-question-1",
        "type": "single",
        "q": "移动题库后题目身份是否保留？",
        "options": ["保留", "不保留"],
        "ans": "A"
      }]
    })JSON";
    QCOMPARE(source.write(json), static_cast<qint64>(json.size()));
    source.close();

    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::services::BankDirectorySyncService syncService;
    const auto installed = syncService.synchronize(
        layout.questionBanks, databasePath, dataRoot);
    QVERIFY2(installed.error.isEmpty(), qPrintable(installed.error));
    QCOMPARE(installed.installedFiles, 1);

    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("move-sync-")));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    const QStringList originalPath{
        QStringLiteral("毛概"), QStringLiteral("第一章"), QStringLiteral("单选题")};
    const auto originalQuestions = repository.listByPath(originalPath, &error);
    QCOMPARE(originalQuestions.size(), 1);
    const QUuid stableQuestionId = originalQuestions.constFirst().id;

    quizapp::services::SharedStorageFileService fileService;
    const auto renamed = fileService.renameQuestionBankEntry(
        layout, chapterDirectory, QStringLiteral("导论"));
    QVERIFY2(renamed.completed, qPrintable(renamed.error));
    const auto renameSync = syncService.synchronize(
        layout.questionBanks, databasePath, dataRoot);
    QVERIFY2(renameSync.error.isEmpty(), qPrintable(renameSync.error));
    QCOMPARE(renameSync.relocatedFiles, 1);
    QCOMPARE(renameSync.missingFiles, 0);
    const QStringList renamedPath{
        QStringLiteral("毛概"), QStringLiteral("导论"), QStringLiteral("单选题")};
    const auto renamedQuestions = repository.listByPath(renamedPath, &error);
    QCOMPARE(renamedQuestions.size(), 1);
    QCOMPARE(renamedQuestions.constFirst().id, stableQuestionId);
    QCOMPARE(repository.listByPath(originalPath, &error).size(), 0);

    const auto destinationFolder = fileService.createQuestionBankFolder(
        layout, layout.questionBanks, QStringLiteral("思政"));
    QVERIFY2(destinationFolder.completed, qPrintable(destinationFolder.error));
    const auto moved = fileService.moveQuestionBankEntry(
        layout,
        renamed.destinationPath,
        destinationFolder.destinationPath,
        quizapp::services::StorageConflictPolicy::Skip);
    QVERIFY2(moved.completed, qPrintable(moved.error));
    const auto moveSync = syncService.synchronize(
        layout.questionBanks, databasePath, dataRoot);
    QVERIFY2(moveSync.error.isEmpty(), qPrintable(moveSync.error));
    QCOMPARE(moveSync.relocatedFiles, 1);
    QCOMPARE(moveSync.missingFiles, 0);
    const QStringList movedPath{
        QStringLiteral("思政"), QStringLiteral("导论"), QStringLiteral("单选题")};
    const auto movedQuestions = repository.listByPath(movedPath, &error);
    QCOMPARE(movedQuestions.size(), 1);
    QCOMPARE(movedQuestions.constFirst().id, stableQuestionId);
    QCOMPARE(repository.listByPath(renamedPath, &error).size(), 0);
}

void NativeCoreTests::sharedStorageFileOperationsStayInsideManagedRoot()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::services::SharedStorageService storageService;
    const auto layout = storageService.prepare(
        QDir(root.path()).filePath(QStringLiteral("QuizApp")));
    QVERIFY2(layout.ready(), qPrintable(layout.error));
    quizapp::services::SharedStorageFileService fileService;

    const auto folder = fileService.createQuestionBankFolder(
        layout, layout.questionBanks, QStringLiteral("毛概"));
    QVERIFY2(folder.completed, qPrintable(folder.error));
    QVERIFY(QFileInfo(folder.destinationPath).isDir());
    QVERIFY(!fileService.createQuestionBankFolder(
        layout, layout.questionBanks, QStringLiteral("../越界")).completed);

    const QString sourcePath = QDir(root.path()).filePath(QStringLiteral("第一章.json"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray json = QByteArrayLiteral("{\"questions\":[]}");
    QCOMPARE(source.write(json), static_cast<qint64>(json.size()));
    source.close();

    const auto imported = fileService.importJsonFiles(
        layout,
        folder.destinationPath,
        {sourcePath},
        quizapp::services::StorageConflictPolicy::KeepBoth);
    QVERIFY2(imported.completed, qPrintable(imported.error));
    QCOMPARE(imported.affectedEntries, 1);
    QVERIFY(QFileInfo::exists(imported.destinationPath));

    const auto renamed = fileService.renameQuestionBankEntry(
        layout, imported.destinationPath, QStringLiteral("第二章"));
    QVERIFY2(renamed.completed, qPrintable(renamed.error));
    QCOMPARE(QFileInfo(renamed.destinationPath).fileName(), QStringLiteral("第二章.json"));
    QVERIFY(!QFileInfo::exists(imported.destinationPath));
    QVERIFY(!fileService.renameQuestionBankEntry(
        layout, sourcePath, QStringLiteral("越界")).completed);

    const auto targetFolder = fileService.createQuestionBankFolder(
        layout, layout.questionBanks, QStringLiteral("思政"));
    QVERIFY2(targetFolder.completed, qPrintable(targetFolder.error));
    const auto moved = fileService.moveQuestionBankEntry(
        layout,
        renamed.destinationPath,
        targetFolder.destinationPath,
        quizapp::services::StorageConflictPolicy::Skip);
    QVERIFY2(moved.completed, qPrintable(moved.error));
    QVERIFY(QFileInfo::exists(moved.destinationPath));
    QVERIFY(!QFileInfo::exists(renamed.destinationPath));
    QVERIFY(!fileService.moveQuestionBankEntry(
        layout,
        targetFolder.destinationPath,
        targetFolder.destinationPath,
        quizapp::services::StorageConflictPolicy::Skip).completed);

    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray updatedJson = QByteArrayLiteral("{\"questions\":[1]}");
    QCOMPARE(source.write(updatedJson), static_cast<qint64>(updatedJson.size()));
    source.close();
    const auto overwritten = fileService.importJsonFiles(
        layout,
        targetFolder.destinationPath,
        {sourcePath},
        quizapp::services::StorageConflictPolicy::Overwrite);
    QVERIFY2(overwritten.completed, qPrintable(overwritten.error));
    QFile overwrittenFile(overwritten.destinationPath);
    QVERIFY(overwrittenFile.open(QIODevice::ReadOnly));
    QCOMPARE(overwrittenFile.readAll(), updatedJson);
    overwrittenFile.close();

    const auto recycled = fileService.moveToRecycleBin(layout, overwritten.destinationPath);
    QVERIFY2(recycled.completed, qPrintable(recycled.error));
    QVERIFY(!QFileInfo::exists(overwritten.destinationPath));
    QVERIFY(QFileInfo::exists(recycled.destinationPath));
    QVERIFY(quizapp::services::SharedStorageFileService::isPathInside(
        recycled.destinationPath, layout.recycleBin));

    const auto restored = fileService.restoreFromRecycleBin(
        layout,
        recycled.destinationPath,
        quizapp::services::StorageConflictPolicy::KeepBoth);
    QVERIFY2(restored.completed, qPrintable(restored.error));
    QVERIFY(QFileInfo::exists(restored.destinationPath));

    const auto recycledAgain = fileService.moveToRecycleBin(layout, restored.destinationPath);
    QVERIFY2(recycledAgain.completed, qPrintable(recycledAgain.error));
    const auto deleted = fileService.permanentlyDelete(layout, recycledAgain.destinationPath);
    QVERIFY2(deleted.completed, qPrintable(deleted.error));
    QVERIFY(!QFileInfo::exists(recycledAgain.destinationPath));
    QVERIFY(!fileService.permanentlyDelete(layout, sourcePath).completed);
}

void NativeCoreTests::defaultBankBundleInstallsOnFreshDatabase()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString bundleRoot = QDir(root.path()).filePath(QStringLiteral("bundle"));
    const QString dataRoot = QDir(root.path()).filePath(QStringLiteral("data"));
    QVERIFY(createDefaultBankBundle(bundleRoot));

    quizapp::services::DefaultBankBundleBootstrapService service;
    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    QList<int> progressValues;
    int progressTotal = 0;
    const auto result = service.install(
        bundleRoot,
        dataRoot,
        databasePath,
        {},
        [&](const QString &, int completed, int total) {
            progressValues.append(completed);
            progressTotal = total;
        });
    QCOMPARE(
        result.status,
        quizapp::services::DefaultBankBundleBootstrapStatus::Installed);
    QCOMPARE(result.displayName, QStringLiteral("27考研题库包"));
    QCOMPARE(result.questionCount, 2);
    QVERIFY(QFileInfo::exists(databasePath));
    QVERIFY(!progressValues.isEmpty());
    QVERIFY(std::is_sorted(progressValues.cbegin(), progressValues.cend()));
    QCOMPARE(progressValues.constLast(), progressTotal);

    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("bundle-result-")));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteLibraryRepository repository(database.connection());
    const auto stats = repository.stats(&error);
    QCOMPARE(stats.bankCount, 1);
    QCOMPARE(stats.questionCount, 2);
}

void NativeCoreTests::realDefaultBankBundleBootstrapsWithProgress()
{
    const QString bundleRoot = qEnvironmentVariable("QUIZAPP_REAL_BUNDLE_DIR");
    if (bundleRoot.isEmpty()) {
        QSKIP("QUIZAPP_REAL_BUNDLE_DIR is not set");
    }
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dataRoot = QDir(root.path()).filePath(QStringLiteral("data"));
    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    int finalCompleted = 0;
    int finalTotal = 0;
    QElapsedTimer timer;
    timer.start();

    quizapp::services::DefaultBankBundleBootstrapService service;
    const auto result = service.install(
        bundleRoot,
        dataRoot,
        databasePath,
        {},
        [&](const QString &, int completed, int total) {
            QVERIFY(completed >= finalCompleted);
            finalCompleted = completed;
            finalTotal = total;
        });

    QCOMPARE(result.status, quizapp::services::DefaultBankBundleBootstrapStatus::Installed);
    QCOMPARE(result.displayName, QStringLiteral("27考研题库包"));
    QCOMPARE(result.questionCount, 870);
    QCOMPARE(result.sectionCount, 72);
    QCOMPARE(result.blobCount, 1936);
    QCOMPARE(finalCompleted, finalTotal);
    QVERIFY(QFileInfo::exists(databasePath));
    qInfo().noquote() << QStringLiteral("Real bundle bootstrap elapsed: %1 ms")
                             .arg(timer.elapsed());
}

void NativeCoreTests::defaultBankBundleReplacesOnlyEmptyDatabase()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString bundleRoot = QDir(root.path()).filePath(QStringLiteral("bundle"));
    const QString dataRoot = QDir(root.path()).filePath(QStringLiteral("data"));
    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    QVERIFY(createDefaultBankBundle(bundleRoot));
    QVERIFY(QDir().mkpath(dataRoot));
    {
        quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("empty-alpha-")));
        QString error;
        QVERIFY2(database.open(databasePath, &error), qPrintable(error));
        QVERIFY2(database.migrate(&error), qPrintable(error));
    }

    quizapp::services::DefaultBankBundleBootstrapService service;
    const auto result = service.install(bundleRoot, dataRoot, databasePath);
    QCOMPARE(
        result.status,
        quizapp::services::DefaultBankBundleBootstrapStatus::Installed);
    const QDir backupRoot(QDir(dataRoot).filePath(QStringLiteral("bootstrap-backups")));
    QCOMPARE(backupRoot.entryList({QStringLiteral("*.sqlite")}, QDir::Files).size(), 1);
}

void NativeCoreTests::defaultBankBundleProtectsNonEmptyDatabase()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString bundleRoot = QDir(root.path()).filePath(QStringLiteral("bundle"));
    const QString dataRoot = QDir(root.path()).filePath(QStringLiteral("data"));
    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    QVERIFY(createDefaultBankBundle(bundleRoot));
    QVERIFY(QDir().mkpath(dataRoot));
    {
        quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("nonempty-alpha-")));
        QString error;
        QVERIFY2(database.open(databasePath, &error), qPrintable(error));
        QVERIFY2(database.migrate(&error), qPrintable(error));
        quizapp::storage::SqliteQuestionRepository repository(database.connection());
        QVERIFY2(repository.replaceBank(sampleBankPackage(), &error), qPrintable(error));
    }

    quizapp::services::DefaultBankBundleBootstrapService service;
    const auto result = service.install(bundleRoot, dataRoot, databasePath);
    QCOMPARE(
        result.status,
        quizapp::services::DefaultBankBundleBootstrapStatus::SkippedNonEmpty);

    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("protected-result-")));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    const auto banks = repository.listInstalledBanks(&error);
    QCOMPARE(banks.size(), 1);
    QSqlQuery provider(database.connection());
    QVERIFY(provider.exec(QStringLiteral("SELECT source_provider FROM banks")));
    QVERIFY(provider.next());
    QCOMPARE(provider.value(0).toString(), QStringLiteral("test-provider"));
}

void NativeCoreTests::defaultBankBundleRollsBackInterruptedCopy()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString bundleRoot = QDir(root.path()).filePath(QStringLiteral("bundle"));
    const QString dataRoot = QDir(root.path()).filePath(QStringLiteral("data"));
    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    QVERIFY(createDefaultBankBundle(bundleRoot));

    quizapp::services::DefaultBankBundleBootstrapService service;
    const auto result = service.install(
        bundleRoot,
        dataRoot,
        databasePath,
        [](const QString &relativePath) {
            return relativePath != QStringLiteral("quizapp.sqlite");
        });
    QCOMPARE(result.status, quizapp::services::DefaultBankBundleBootstrapStatus::Failed);
    QVERIFY(!QFileInfo::exists(databasePath));
}

void NativeCoreTests::defaultBankBundleRejectsCountMismatch()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString bundleRoot = QDir(root.path()).filePath(QStringLiteral("bundle"));
    const QString dataRoot = QDir(root.path()).filePath(QStringLiteral("data"));
    const QString databasePath = QDir(dataRoot).filePath(QStringLiteral("quizapp.sqlite"));
    QVERIFY(createDefaultBankBundle(bundleRoot, 3));

    quizapp::services::DefaultBankBundleBootstrapService service;
    const auto result = service.install(bundleRoot, dataRoot, databasePath);
    QCOMPARE(result.status, quizapp::services::DefaultBankBundleBootstrapStatus::Failed);
    QVERIFY(result.error.contains(QStringLiteral("计数")));
    QVERIFY(!QFileInfo::exists(databasePath));
}

void NativeCoreTests::legacyMigrationPreservesDataAndRedactsSecrets()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("legacy-import-")));
    QString error;
    QVERIFY2(database.open(QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite")), &error),
             qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));

    quizapp::services::LegacyMigrationService service;
    const auto result = service.importPackage(legacyMigrationPackage(), database.connection());
    QCOMPARE(result.status, quizapp::services::LegacyMigrationStatus::Imported);
    QCOMPARE(result.sourceVersion, QStringLiteral("1.0.18"));
    QCOMPARE(result.summary.localStorageRecords, 5);
    QCOMPARE(result.summary.indexedDbRecords, 2);
    QCOMPARE(result.summary.studyDays, 2);
    QCOMPARE(result.summary.pendingQuestionRecords, 3);
    QCOMPARE(result.summary.resolvedQuestionRecords, 0);
    QCOMPARE(result.summary.redactedSecrets, 2);

    QSqlQuery migration(database.connection());
    QVERIFY(migration.exec(QStringLiteral(
        "SELECT status, sanitized_package_json FROM legacy_migrations")));
    QVERIFY(migration.next());
    QCOMPARE(migration.value(0).toString(), QStringLiteral("applied"));
    const QString sanitized = migration.value(1).toString();
    QVERIFY(!sanitized.contains(QStringLiteral("never-store-me")));
    QVERIFY(!sanitized.contains(QStringLiteral("also-secret")));
    QVERIFY(sanitized.contains(QStringLiteral("deepseek")));

    QSqlQuery recordCount(database.connection());
    QVERIFY(recordCount.exec(QStringLiteral("SELECT COUNT(*) FROM legacy_records")));
    QVERIFY(recordCount.next());
    QCOMPARE(recordCount.value(0).toInt(), 10);

    QSqlQuery settings(database.connection());
    settings.prepare(QStringLiteral("SELECT value_json FROM settings WHERE key=?"));
    settings.addBindValue(QStringLiteral("legacy/v1/uiConfig"));
    QVERIFY(settings.exec());
    QVERIFY(settings.next());
    QVERIFY(settings.value(0).toString().contains(QStringLiteral("forest")));

    QSqlQuery study(database.connection());
    QVERIFY(study.exec(QStringLiteral(
        "SELECT COUNT(*), SUM(duration_seconds) FROM study_events")));
    QVERIFY(study.next());
    QCOMPARE(study.value(0).toInt(), 2);
    QCOMPARE(study.value(1).toInt(), 180);

    QSqlQuery wrongBook(database.connection());
    QVERIFY(wrongBook.exec(QStringLiteral("SELECT COUNT(*) FROM wrong_book")));
    QVERIFY(wrongBook.next());
    QCOMPARE(wrongBook.value(0).toInt(), 0);
}

void NativeCoreTests::legacyMigrationIsIdempotent()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("legacy-repeat-")));
    QString error;
    QVERIFY2(database.open(QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite")), &error),
             qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::services::LegacyMigrationService service;
    QCOMPARE(service.importPackage(legacyMigrationPackage(), database.connection()).status,
             quizapp::services::LegacyMigrationStatus::Imported);
    QCOMPARE(service.importPackage(legacyMigrationPackage(), database.connection()).status,
             quizapp::services::LegacyMigrationStatus::AlreadyImported);
    QSqlQuery counts(database.connection());
    QVERIFY(counts.exec(QStringLiteral(
        "SELECT (SELECT COUNT(*) FROM legacy_migrations), "
        "(SELECT COUNT(*) FROM study_events)")));
    QVERIFY(counts.next());
    QCOMPARE(counts.value(0).toInt(), 1);
    QCOMPARE(counts.value(1).toInt(), 2);
}

void NativeCoreTests::legacyMigrationRollsBackOnInterruption()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("legacy-rollback-")));
    QString error;
    QVERIFY2(database.open(QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite")), &error),
             qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::services::LegacyMigrationService service;
    const auto result = service.importPackage(
        legacyMigrationPackage(),
        database.connection(),
        [](const QString &stage) { return stage != QStringLiteral("before-commit"); });
    QCOMPARE(result.status, quizapp::services::LegacyMigrationStatus::Failed);
    QSqlQuery counts(database.connection());
    QVERIFY(counts.exec(QStringLiteral(
        "SELECT (SELECT COUNT(*) FROM legacy_migrations), "
        "(SELECT COUNT(*) FROM legacy_records), "
        "(SELECT COUNT(*) FROM study_events)")));
    QVERIFY(counts.next());
    QCOMPARE(counts.value(0).toInt(), 0);
    QCOMPARE(counts.value(1).toInt(), 0);
    QCOMPARE(counts.value(2).toInt(), 0);
}

void NativeCoreTests::legacyMigrationRejectsMalformedPackage()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("legacy-invalid-")));
    QString error;
    QVERIFY2(database.open(QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite")), &error),
             qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::services::LegacyMigrationService service;
    const auto result = service.importPackage(
        QByteArrayLiteral("{\"format\":\"other\"}"), database.connection());
    QCOMPARE(result.status, quizapp::services::LegacyMigrationStatus::InvalidPackage);
    QSqlQuery count(database.connection());
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM legacy_migrations")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 0);
}

void NativeCoreTests::legacyMigrationAppliesUiSettingsWithoutOverwrite()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("legacy-settings-")));
    QString error;
    QVERIFY2(database.open(QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite")), &error),
             qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::services::LegacyMigrationService service;
    QCOMPARE(service.importPackage(legacyMigrationPackage(), database.connection()).status,
             quizapp::services::LegacyMigrationStatus::Imported);

    QSettings settings(
        QDir(root.path()).filePath(QStringLiteral("native-settings.ini")),
        QSettings::IniFormat);
    settings.setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    int applied = 0;
    QVERIFY2(service.applyUiSettings(
                 database.connection(), settings, &applied, &error),
             qPrintable(error));
    QCOMPARE(applied, 2);
    QCOMPARE(settings.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("dark"));
    QCOMPARE(settings.value(QStringLiteral("ui/palette")).toString(), QStringLiteral("forest"));
    QCOMPARE(settings.value(QStringLiteral("ui/primary")).toString(), QStringLiteral("#1f8f62"));

    applied = -1;
    QVERIFY2(service.applyUiSettings(
                 database.connection(), settings, &applied, &error),
             qPrintable(error));
    QCOMPARE(applied, 0);
}

void NativeCoreTests::legacyMigrationResolvesDirectQuestionKeys()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("legacy-resolve-")));
    QString error;
    QVERIFY2(database.open(QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite")), &error),
             qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    QVERIFY2(questionRepository.replaceBank(sampleBankPackage(), &error), qPrintable(error));

    quizapp::services::LegacyMigrationService service;
    const auto result = service.importPackage(
        legacyMigrationPackage(QStringLiteral("bank-one")), database.connection());
    QCOMPARE(result.status, quizapp::services::LegacyMigrationStatus::Imported);
    QCOMPARE(result.summary.resolvedQuestionRecords, 3);
    QCOMPARE(result.summary.pendingQuestionRecords, 0);

    QSqlQuery resolved(database.connection());
    QVERIFY(resolved.exec(QStringLiteral(
        "SELECT COUNT(*) FROM legacy_records WHERE category LIKE 'question:%' "
        "AND resolution_status='resolved' AND resolved_id IS NOT NULL")));
    QVERIFY(resolved.next());
    QCOMPARE(resolved.value(0).toInt(), 3);

    QSqlQuery wrong(database.connection());
    QVERIFY(wrong.exec(QStringLiteral(
        "SELECT reason_tags_json, note FROM wrong_book")));
    QVERIFY(wrong.next());
    QVERIFY(wrong.value(0).toString().contains(QStringLiteral("concept")));
    QCOMPARE(wrong.value(1).toString(), QStringLiteral("review"));

    QSqlQuery answers(database.connection());
    QVERIFY(answers.exec(QStringLiteral(
        "SELECT mode, answer FROM question_answer_state ORDER BY mode")));
    QVERIFY(answers.next());
    QCOMPARE(answers.value(0).toInt(), 0);
    QCOMPARE(answers.value(1).toString(), QStringLiteral("A"));
    QVERIFY(answers.next());
    QCOMPARE(answers.value(0).toInt(), 1);
    QCOMPARE(answers.value(1).toString(), QStringLiteral("BC"));
}

void NativeCoreTests::legacyMigrationMatchesQuestionContentAndMaterializesPracticeSession()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::storage::Database database(uniqueConnectionName(QStringLiteral("legacy-content-")));
    QString error;
    QVERIFY2(database.open(QDir(root.path()).filePath(QStringLiteral("quizapp.sqlite")), &error),
             qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    const auto package = sampleBankPackage();
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));

    quizapp::services::LegacyMigrationService service;
    const auto result = service.importPackage(
        legacyContentMatchedSessionPackage(), database.connection());
    QCOMPARE(result.status, quizapp::services::LegacyMigrationStatus::Imported);
    QCOMPARE(result.summary.practiceSessions, 1);

    quizapp::storage::SqlitePracticeRepository practiceRepository(database.connection());
    const auto session = practiceRepository.latest(
        package.bank.id, quizapp::domain::PracticeMode::Sequential, &error);
    QVERIFY2(session.has_value(), qPrintable(error));
    QCOMPARE(session->questionOrder.size(), 2);
    QCOMPARE(session->questionOrder.at(0), package.bank.questions.at(0).id);
    QCOMPARE(session->questionOrder.at(1), package.bank.questions.at(1).id);
    QCOMPARE(session->currentIndex, 1);
    QCOMPARE(session->answers.value(package.bank.questions.at(0).id), QStringLiteral("A"));
    QCOMPARE(session->drafts.value(package.bank.questions.at(1).id), QStringLiteral("AB"));
    QCOMPARE(session->viewport.value(QStringLiteral("displayMode")).toString(),
             QStringLiteral("page"));

    QCOMPARE(service.importPackage(
        legacyContentMatchedSessionPackage(), database.connection()).status,
        quizapp::services::LegacyMigrationStatus::AlreadyImported);
    QSqlQuery count(database.connection());
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM practice_sessions")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

QTEST_MAIN(NativeCoreTests)
#include "tst_native_core.moc"
