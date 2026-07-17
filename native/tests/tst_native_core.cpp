#include "domain/QuestionIdentity.h"
#include "domain/QuestionOrdering.h"
#include "handwriting/SpeedyNoteStrokeAdapter.h"
#include "services/BankInstallService.h"
#include "services/BankDirectorySyncService.h"
#include "services/BlobStore.h"
#include "services/LegacyBankImporter.h"
#include "services/PracticeService.h"
#include "services/ReviewService.h"
#include "services/SharedStorageService.h"
#include "services/SharedStorageFileService.h"
#include "services/StudyService.h"
#include "services/XiaoyiDirectoryInstallService.h"
#include "services/WrongBookService.h"
#include "storage/Database.h"
#include "storage/SqliteLibraryRepository.h"
#include "storage/SqlitePracticeRepository.h"
#include "storage/SqliteQuestionRepository.h"
#include "storage/SqliteReviewRepository.h"
#include "storage/SqliteStudyRepository.h"
#include "storage/SqliteWrongBookRepository.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
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
    void questionRepositoryRoundTripAndSoftDelete();
    void questionRepositoryListsWholePathSubtree();
    void questionRepositoryRollsBackMissingBlob();
    void practiceRepositoryRoundTripAndModeIsolation();
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
    void sharedStorageFileOperationsStayInsideManagedRoot();
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
    QCOMPARE(version.value(0).toInt(), 4);

    QSet<QString> columns;
    QSqlQuery tableInfo(database.connection());
    QVERIFY(tableInfo.exec(QStringLiteral("PRAGMA table_info(questions)")));
    while (tableInfo.next()) {
        columns.insert(tableInfo.value(1).toString());
    }
    QVERIFY(columns.contains(QStringLiteral("path_json")));
    QVERIFY(columns.contains(QStringLiteral("active")));
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

    package.bank.questions.removeLast();
    package.bank.contentHash = QByteArray(32, 'd');
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));
    QVERIFY(practiceRepository.load(sequential.id, &error).has_value());
    QVERIFY2(practiceRepository.remove(sequential.id, &error), qPrintable(error));
    QVERIFY(!practiceRepository.load(sequential.id, &error).has_value());
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

    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray updatedJson = QByteArrayLiteral("{\"questions\":[1]}");
    QCOMPARE(source.write(updatedJson), static_cast<qint64>(updatedJson.size()));
    source.close();
    const auto overwritten = fileService.importJsonFiles(
        layout,
        folder.destinationPath,
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

QTEST_MAIN(NativeCoreTests)
#include "tst_native_core.moc"
