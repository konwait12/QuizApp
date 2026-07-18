#include "domain/QuestionIdentity.h"
#include "services/BankInstallService.h"
#include "services/BankReleaseStateStore.h"
#include "services/BlobStore.h"
#include "services/LocalBackupService.h"
#include "platform/SecureSecretStore.h"
#include "storage/Database.h"
#include "storage/SqliteLibraryRepository.h"
#include "storage/SqliteQuestionRepository.h"
#include "storage/SqlitePracticeRepository.h"
#include "storage/SqliteReviewRepository.h"
#include "storage/SqliteWrongBookRepository.h"
#include "core/DocumentViewport.h"
#include "ui/AppWindow.h"
#include "ui/AnswerTablePage.h"
#include "ui/AnnouncementDialog.h"
#include "ui/AiSettingsPanel.h"
#include "ui/AppUpdateDialog.h"
#include "ui/BankReleaseDialog.h"
#include "ui/BackupSettingsPanel.h"
#include "ui/ChoiceComboBox.h"
#include "ui/ExamPage.h"
#include "ui/HandwritingPage.h"
#include "ui/NotebookLibraryPage.h"
#include "ui/PracticePage.h"
#include "ui/QuestionOverviewDialog.h"
#include "ui/QuestionAiPanel.h"
#include "ui/ReviewPage.h"
#include "ui/StudyHubPage.h"
#include "ui/ThemePreview.h"
#include "ui/ThemePalette.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QRawFont>
#include <QSettings>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTableView>
#include <QTextBrowser>
#include <QTimer>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

#include <array>

class AppShellTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void applicationFontSupportsChineseAndIconsLoad();
    void navigationAdaptsToPhoneAndTablet();
    void libraryStatsReflectInstalledQuestions();
    void emojiIconChoicesFollowLibraryHierarchyDepth();
    void sharedStorageHierarchyEditingFromUi();
    void libraryEditModeReordersCurrentLevel();
    void libraryEditModeDeletesAndRestoresBundledNode();
    void bankReleaseDialogSelectsHierarchyAndInstallsBanks();
    void announcementFeedShowsUnreadAndArchive();
    void appUpdateDialogShowsVersionAndPlatformPackage();
    void mockExamPersistsResumesSubmitsAndOpensHandwriting();
    void backupSettingsExportsPreviewsAndStagesRestore();
    void aiSettingsPersistSecurelyAndAdaptToPhoneAndTablet();
    void questionAiPanelRequiresActionAndShowsCachedAndStaleStates();
    void freeNotebookLibraryCreatesSavesRecyclesRestoresAndDeletes();
    void startPracticeFromInstalledBankAndOpenHandwriting();
    void practiceSaveControlsRespectSettings();
    void savedProgressWidgetRestoresAndRespectsSizing();
    void practiceRendersInstalledQuestionImages();
    void questionOverviewShowsStatusAndJumpsToQuestion();
    void wrongAnswerRequiresExplicitWrongBookAction();
    void explicitReviewFlowSchedulesAndReturnsToStudyHub();
    void answerTableBrowsesAllAnswersAndReturnsFromDetailAndHandwriting();
    void realXiaoyiBundleHierarchyScreenshots();
    void systemBackReturnsToHome();
    void settingsPersistAndApply();
    void endfieldThemeCoversPracticeAndStudy();
    void handwritingPagePersistsQuestionNoteAndReturnsContext();

private:
    void saveScreenshot(QWidget &widget, const QString &name);
};

namespace {

quizapp::domain::BankImportPackage installedSampleBank()
{
    quizapp::domain::BankImportPackage package;
    package.subjects = {
        {QStringLiteral("postgraduate-math"), {}, QStringLiteral("考研数学"), {}, 0},
        {QStringLiteral("public-bank"), QStringLiteral("postgraduate-math"),
         QStringLiteral("公开题库"), {}, 0},
    };
    package.bank.id = QStringLiteral("public-bank-section");
    package.bank.subjectId = QStringLiteral("public-bank");
    package.bank.title = QStringLiteral("第一章练习");
    package.bank.sourceProvider = QStringLiteral("xiaoyivip");
    package.bank.sourceId = QStringLiteral("公开题库/第一章练习.json");
    package.bank.contentHash = QByteArray(32, 'b');

    quizapp::domain::Question question;
    question.bankId = package.bank.id;
    question.sourceProvider = package.bank.sourceProvider;
    question.sourceId = QStringLiteral("xiaoyi:sample");
    question.path = {QStringLiteral("考研数学"), QStringLiteral("公开题库")};
    question.type = quizapp::domain::QuestionType::Single;
    question.prompt = QStringLiteral("示例选择题");
    question.options = {QStringLiteral("选项 A"), QStringLiteral("选项 B")};
    question.correctAnswer = QStringLiteral("A");
    question.sourceOrder = 0;
    question.id = quizapp::domain::QuestionIdentity::create(
        question.sourceProvider, question.sourceId, question.path,
        question.prompt, question.options);
    question.contentHash = quizapp::domain::QuestionIdentity::contentHash(question);
    question.updatedAt = QDateTime::currentDateTimeUtc();
    package.bank.questions.append(question);

    quizapp::domain::Question second = question;
    second.sourceId = QStringLiteral("xiaoyi:sample-2");
    second.prompt = QStringLiteral("第二道示例判断题");
    second.type = quizapp::domain::QuestionType::Boolean;
    second.options = {QStringLiteral("正确"), QStringLiteral("错误")};
    second.correctAnswer = QStringLiteral("B");
    second.sourceOrder = 1;
    second.id = quizapp::domain::QuestionIdentity::create(
        second.sourceProvider, second.sourceId, second.path,
        second.prompt, second.options);
    second.contentHash = quizapp::domain::QuestionIdentity::contentHash(second);
    package.bank.questions.append(second);
    return package;
}

quizapp::domain::BankImportPackage installedBankAtPath(
    const QString &id,
    const QStringList &path)
{
    auto package = installedSampleBank();
    package.bank.id = id;
    package.bank.title = path.constLast() + QStringLiteral("练习");
    package.bank.sourceId = id + QStringLiteral(".json");
    for (qsizetype index = 0; index < package.bank.questions.size(); ++index) {
        auto &question = package.bank.questions[index];
        question.bankId = id;
        question.sourceId = QStringLiteral("%1-question-%2").arg(id).arg(index);
        question.path = path;
        question.id = quizapp::domain::QuestionIdentity::create(
            question.sourceProvider, question.sourceId, question.path,
            question.prompt, question.options);
        question.contentHash = quizapp::domain::QuestionIdentity::contentHash(question);
    }
    return package;
}

QByteArray imageQuestionBankJson()
{
    const QString png = QStringLiteral(
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwC"
        "AAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
    return QStringLiteral(R"JSON({
      "name": "小易图片题库-第一章-图片题",
      "path": ["考研数学", "小易图片题库", "第一章", "图片题"],
      "source": {"provider": "xiaoyivip"},
      "questions": [{
        "id": "xiaoyi:image-ui-1",
        "type": "subjective",
        "q": "第 1 题（题目见图片）",
        "options": [],
        "ans": "",
        "questionImages": ["%1"],
        "explanations": {"builtin": {
          "text": "内置解析",
          "images": ["%1"],
          "source": {"provider": "xiaoyivip", "sourceId": "image-ui-1"}
        }}
      }]
    })JSON").arg(png).toUtf8();
}

QPushButton *firstVisibleButton(QWidget &window, const QString &objectName)
{
    const auto buttons = window.findChildren<QPushButton *>(objectName);
    const auto found = std::find_if(buttons.cbegin(), buttons.cend(), [](QPushButton *button) {
        return button->isVisible();
    });
    return found == buttons.cend() ? nullptr : *found;
}

bool pixmapContainsColor(const QPixmap &pixmap, const QColor &expected, int tolerance = 8)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor actual = QColor::fromRgba(image.pixel(x, y));
            if (actual.alpha() > 0
                && std::abs(actual.red() - expected.red()) <= tolerance
                && std::abs(actual.green() - expected.green()) <= tolerance
                && std::abs(actual.blue() - expected.blue()) <= tolerance) {
                return true;
            }
        }
    }
    return false;
}

QPushButton *drillToLibraryLeaf(quizapp::ui::AppWindow &window)
{
    for (int depth = 0; depth < 12; ++depth) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QPushButton *node = firstVisibleButton(
            window, QStringLiteral("libraryPathNodeButton"));
        if (!node) {
            return firstVisibleButton(
                window, QStringLiteral("libraryScopePracticeButton"));
        }
        QTest::mouseClick(node, Qt::LeftButton);
        QTest::qWait(10);
    }
    return nullptr;
}

QPushButton *firstVisibleModeButton(
    QWidget &window,
    quizapp::domain::PracticeMode mode)
{
    const auto buttons = window.findChildren<QPushButton *>();
    const auto found = std::find_if(buttons.cbegin(), buttons.cend(), [mode](QPushButton *button) {
        return button->isVisible()
            && button->property("practiceMode").isValid()
            && button->property("practiceMode").toInt() == static_cast<int>(mode);
    });
    return found == buttons.cend() ? nullptr : *found;
}

} // namespace

void AppShellTests::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("QuizAppTests"));
    QCoreApplication::setApplicationName(QStringLiteral("NativeShell"));
    QStandardPaths::setTestModeEnabled(true);
    qputenv("QUIZAPP_DISABLE_NETWORK_CHECKS", "1");
    qRegisterMetaType<quizapp::domain::NotebookLaunchContext>();
    QSettings().clear();
}

void AppShellTests::applicationFontSupportsChineseAndIconsLoad()
{
    quizapp::ui::AppWindow window;
    const QRawFont rawFont = QRawFont::fromFont(QApplication::font());
    QVERIFY(rawFont.isValid());
    QVERIFY(rawFont.supportsCharacter(0x9898U));

    auto *home = window.findChild<QToolButton *>(QStringLiteral("bottomNav0"));
    QVERIFY(home);
    const QRawFont labelFont = QRawFont::fromFont(home->font());
    QVERIFY(labelFont.isValid());
    QVERIFY(labelFont.supportsCharacter(0x9898U));
    QVERIFY(!home->icon().isNull());
    QVERIFY(!home->icon().pixmap(24, 24).isNull());
    for (const QString &resource : {
             QStringLiteral(":/quizapp/icons/combo_chevron_dark.svg"),
             QStringLiteral(":/quizapp/icons/combo_chevron_light.svg"),
             QStringLiteral(":/quizapp/icons/combo_chevron_endfield.svg"),
             QStringLiteral(":/quizapp/icons/tree_chevron_right_dark.svg"),
             QStringLiteral(":/quizapp/icons/tree_chevron_right_light.svg"),
             QStringLiteral(":/quizapp/icons/tree_chevron_right_endfield.svg"),
             QStringLiteral(":/quizapp/icons/mail.svg")}) {
        QFile icon(resource);
        QVERIFY2(icon.open(QIODevice::ReadOnly), qPrintable(resource));
        QVERIFY(!icon.readAll().isEmpty());
    }
}

void AppShellTests::navigationAdaptsToPhoneAndTablet()
{
    quizapp::ui::AppWindow window;
    window.resize(390, 844);
    window.show();
    QTest::qWait(40);
    QVERIFY(window.isBottomNavigationVisible());
    QVERIFY(!window.isRailNavigationVisible());
    auto *brand = window.findChild<QLabel *>(QStringLiteral("brandMark"));
    auto *homeSummary = window.findChild<QWidget *>(QStringLiteral("homeSummarySurface"));
    auto *homeActions = window.findChild<QWidget *>(QStringLiteral("homeActionsSurface"));
    QVERIFY(brand);
    QVERIFY(homeSummary);
    QVERIFY(homeActions);
    QVERIFY(brand->isVisible());
    QVERIFY(homeSummary->geometry().bottom() < homeActions->geometry().top());
    saveScreenshot(window, QStringLiteral("app-shell-home-phone.png"));

    auto *library = window.findChild<QToolButton *>(QStringLiteral("bottomNav1"));
    QVERIFY(library);
    QTest::mouseClick(library, Qt::LeftButton);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Library);
    saveScreenshot(window, QStringLiteral("app-shell-library-phone.png"));

    window.resize(1280, 800);
    QTest::qWait(40);
    QVERIFY(window.isRailNavigationVisible());
    QVERIFY(!window.isBottomNavigationVisible());
    auto *study = window.findChild<QToolButton *>(QStringLiteral("railNav2"));
    QVERIFY(study);
    QTest::mouseClick(study, Qt::LeftButton);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Study);
    window.navigateTo(quizapp::ui::AppWindow::Section::Home);
    QTest::qWait(20);
    QVERIFY(!brand->isVisible());
    QVERIFY(homeSummary->geometry().right() < homeActions->geometry().left());
    saveScreenshot(window, QStringLiteral("app-shell-home-tablet.png"));
}

void AppShellTests::libraryStatsReflectInstalledQuestions()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-library-stats"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    QVERIFY2(repository.replaceBank(installedSampleBank(), &error), qPrintable(error));
    const QString notesDirectory = QDir(dataRoot.path()).filePath(
        QStringLiteral("SharedStorage/Notes"));
    QVERIFY(QDir().mkpath(notesDirectory));
    QFile noteFile(QDir(notesDirectory).filePath(QStringLiteral("example.txt")));
    QVERIFY(noteFile.open(QIODevice::WriteOnly));
    noteFile.write("test");
    noteFile.close();

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(1280, 800);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(40);
    auto *sourceColumn = window.findChild<QWidget *>(QStringLiteral("librarySourceColumn"));
    auto *browserColumn = window.findChild<QWidget *>(QStringLiteral("libraryBrowserColumn"));
    QVERIFY(sourceColumn);
    QVERIFY(browserColumn);
    QVERIFY(sourceColumn->geometry().right() < browserColumn->geometry().left());
    const auto supportingLabels = window.findChildren<QLabel *>(QStringLiteral("pageSupportingText"));
    QVERIFY(std::any_of(
        supportingLabels.cbegin(), supportingLabels.cend(), [](const QLabel *label) {
            return label->text().contains(QStringLiteral("1 个分区"))
                && label->text().contains(QStringLiteral("2 道题"));
        }));
    auto *install = window.findChild<QPushButton *>(QStringLiteral("installXiaoyiButton"));
    auto *storageTree = window.findChild<QTreeWidget *>(
        QStringLiteral("sharedStorageFileTree"));
    QVERIFY(install);
    QVERIFY(storageTree);
    QVERIFY(install->isVisible());
    QCOMPARE(storageTree->topLevelItemCount(), 1);
    QCOMPARE(storageTree->topLevelItem(0)->childCount(), 5);
    QCOMPARE(storageTree->topLevelItem(0)->child(0)->text(0), QStringLiteral("QuestionBanks"));
    QVERIFY(!window.findChild<QLabel *>(QStringLiteral("storageLocationText")));
    const auto verifyNoPathTooltip = [&](auto &&self, const QTreeWidgetItem *item) -> void {
        QVERIFY(!item->toolTip(0).contains(dataRoot.path(), Qt::CaseInsensitive));
        for (int index = 0; index < item->childCount(); ++index) {
            self(self, item->child(index));
        }
    };
    verifyNoPathTooltip(verifyNoPathTooltip, storageTree->topLevelItem(0));
    saveScreenshot(window, QStringLiteral("app-shell-library-installed-tablet.png"));
    window.resize(390, 844);
    QTest::qWait(40);
    QVERIFY(sourceColumn->geometry().bottom() < browserColumn->geometry().top());
    const auto pageScrolls = window.findChildren<QScrollArea *>(QStringLiteral("pageScroll"));
    for (QScrollArea *scroll : pageScrolls) {
        if (scroll->isVisible()) {
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        }
    }
    saveScreenshot(window, QStringLiteral("app-shell-library-installed-phone.png"));
}

void AppShellTests::emojiIconChoicesFollowLibraryHierarchyDepth()
{
    QSettings settings;
    const QVariant previousSubject = settings.value(QStringLiteral("ui/subjectIconStyle"));
    const QVariant previousChapter = settings.value(QStringLiteral("ui/chapterIconStyle"));
    const QVariant previousLevels = settings.value(QStringLiteral("ui/levelIconStyles"));
    settings.setValue(QStringLiteral("ui/subjectIconStyle"), QStringLiteral("target"));
    settings.setValue(QStringLiteral("ui/chapterIconStyle"), QStringLiteral("memo"));
    settings.remove(QStringLiteral("ui/levelIconStyles"));
    settings.sync();

    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-library-emoji-icons"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    QVERIFY2(repository.replaceBank(installedSampleBank(), &error), qPrintable(error));

    auto deepPackage = installedSampleBank();
    deepPackage.bank.id = QStringLiteral("deep-library-section");
    deepPackage.bank.title = QStringLiteral("第四层练习");
    deepPackage.bank.sourceId = QStringLiteral("公开题库/专题/单元/第四层练习.json");
    for (qsizetype index = 0; index < deepPackage.bank.questions.size(); ++index) {
        auto &question = deepPackage.bank.questions[index];
        question.bankId = deepPackage.bank.id;
        question.sourceId = QStringLiteral("xiaoyi:deep-%1").arg(index);
        question.path = {
            QStringLiteral("考研数学"),
            QStringLiteral("公开题库"),
            QStringLiteral("专题"),
            QStringLiteral("单元"),
        };
        question.id = quizapp::domain::QuestionIdentity::create(
            question.sourceProvider, question.sourceId, question.path,
            question.prompt, question.options);
        question.contentHash = quizapp::domain::QuestionIdentity::contentHash(question);
    }
    QVERIFY2(repository.replaceBank(deepPackage, &error), qPrintable(error));

    const QString emptyFifthLevel = QDir(dataRoot.path()).filePath(
        QStringLiteral("SharedStorage/QuestionBanks/考研数学/公开题库/专题/单元/空目录"));
    QVERIFY(QDir().mkpath(emptyFifthLevel));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(30);
    const auto visibleNodeIcon = [&window]() -> QLabel * {
        const auto labels = window.findChildren<QLabel *>(
            QStringLiteral("libraryPathNodeIcon"));
        const auto found = std::find_if(
            labels.cbegin(), labels.cend(), [](QLabel *label) {
                return label->isVisible();
            });
        return found == labels.cend() ? nullptr : *found;
    };
    QTRY_VERIFY_WITH_TIMEOUT(visibleNodeIcon() != nullptr, 500);
    QLabel *icon = visibleNodeIcon();
    QVERIFY(icon);
    QCOMPARE(icon->text(), QStringLiteral("🎯"));
    saveScreenshot(window, QStringLiteral("app-shell-library-subject-emoji-phone.png"));

    QPushButton *subject = firstVisibleButton(
        window, QStringLiteral("libraryPathNodeButton"));
    QVERIFY(subject);
    QTest::mouseClick(subject, Qt::LeftButton);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY_WITH_TIMEOUT(visibleNodeIcon() != nullptr, 500);
    icon = visibleNodeIcon();
    QVERIFY(icon);
    QCOMPARE(icon->text(), QStringLiteral("📝"));
    saveScreenshot(window, QStringLiteral("app-shell-library-chapter-emoji-phone.png"));

    window.navigateTo(quizapp::ui::AppWindow::Section::Settings);
    const auto iconButtons = window.findChildren<QToolButton *>();
    std::array<int, 5> levelChoices{};
    for (QToolButton *button : iconButtons) {
        if (button->objectName().startsWith(QStringLiteral("subjectIconChoice-"))) {
            ++levelChoices[0];
        }
        if (button->objectName().startsWith(QStringLiteral("chapterIconChoice-"))) {
            ++levelChoices[1];
        }
        for (int level = 3; level <= 5; ++level) {
            if (button->objectName().startsWith(
                    QStringLiteral("level%1IconChoice-").arg(level))) {
                ++levelChoices[static_cast<std::size_t>(level - 1)];
            }
        }
    }
    for (int count : levelChoices) {
        QCOMPARE(count, 8);
    }
    QVERIFY(window.findChild<QToolButton *>(
        QStringLiteral("subjectIconChoice-target"))->isChecked());
    QVERIFY(window.findChild<QToolButton *>(
        QStringLiteral("chapterIconChoice-memo"))->isChecked());
    const auto settingsPageScrolls = window.findChildren<QScrollArea *>(
        QStringLiteral("pageScroll"));
    const auto visibleSettingsScroll = std::find_if(
        settingsPageScrolls.cbegin(), settingsPageScrolls.cend(),
        [](QScrollArea *scroll) { return scroll->isVisible(); });
    QVERIFY(visibleSettingsScroll != settingsPageScrolls.cend());
    QCOMPARE((*visibleSettingsScroll)->horizontalScrollBar()->maximum(), 0);
    saveScreenshot(window, QStringLiteral("app-shell-settings-level-icons-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(30);
    QCOMPARE((*visibleSettingsScroll)->horizontalScrollBar()->maximum(), 0);
    saveScreenshot(window, QStringLiteral("app-shell-settings-level-icons-tablet.png"));
    window.resize(390, 844);
    QTest::qWait(30);

    auto *thirdLevelStar = window.findChild<QToolButton *>(
        QStringLiteral("level3IconChoice-star"));
    QVERIFY(thirdLevelStar);
    QTest::mouseClick(thirdLevelStar, Qt::LeftButton);
    auto *saveButton = window.findChild<QPushButton *>(
        QStringLiteral("saveSettingsButton"));
    QVERIFY(saveButton);
    QTest::mouseClick(saveButton, Qt::LeftButton);
    const QStringList savedLevelIcons = settings.value(
        QStringLiteral("ui/levelIconStyles")).toStringList();
    QCOMPARE(savedLevelIcons.size(), 5);
    QCOMPARE(savedLevelIcons.at(0), QStringLiteral("target"));
    QCOMPARE(savedLevelIcons.at(1), QStringLiteral("memo"));
    QCOMPARE(savedLevelIcons.at(2), QStringLiteral("star"));

    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(20);
    QPushButton *chapter = firstVisibleButton(
        window, QStringLiteral("libraryPathNodeButton"));
    QVERIFY(chapter);
    QTest::mouseClick(chapter, Qt::LeftButton);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY_WITH_TIMEOUT(visibleNodeIcon() != nullptr, 500);
    icon = visibleNodeIcon();
    QVERIFY(icon);
    QCOMPARE(icon->text(), QStringLiteral("⭐"));

    if (previousSubject.isValid()) {
        settings.setValue(QStringLiteral("ui/subjectIconStyle"), previousSubject);
    } else {
        settings.remove(QStringLiteral("ui/subjectIconStyle"));
    }
    if (previousChapter.isValid()) {
        settings.setValue(QStringLiteral("ui/chapterIconStyle"), previousChapter);
    } else {
        settings.remove(QStringLiteral("ui/chapterIconStyle"));
    }
    if (previousLevels.isValid()) {
        settings.setValue(QStringLiteral("ui/levelIconStyles"), previousLevels);
    } else {
        settings.remove(QStringLiteral("ui/levelIconStyles"));
    }
    settings.sync();
}

void AppShellTests::sharedStorageHierarchyEditingFromUi()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString sharedRoot = QDir(dataRoot.path()).filePath(
        QStringLiteral("SharedStorage"));
    quizapp::services::SharedStorageService storageService;
    const auto layout = storageService.prepare(sharedRoot);
    QVERIFY2(layout.ready(), qPrintable(layout.error));
    const QString sourceDirectory = QDir(layout.questionBanks).filePath(
        QStringLiteral("毛概/第一章"));
    const QString targetDirectory = QDir(layout.questionBanks).filePath(
        QStringLiteral("目标层级"));
    QVERIFY(QDir().mkpath(sourceDirectory));
    QVERIFY(QDir().mkpath(targetDirectory));
    const QString originalPath = QDir(sourceDirectory).filePath(
        QStringLiteral("单选题.json"));
    QFile source(originalPath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray json = R"JSON({
      "name": "界面编辑测试",
      "source": {"provider": "local-ui-test"},
      "questions": [{
        "id": "ui-edit-1",
        "type": "single",
        "q": "界面是否可以编辑题库层级？",
        "options": ["可以", "不可以"],
        "ans": "A"
      }]
    })JSON";
    QCOMPARE(source.write(json), static_cast<qint64>(json.size()));
    source.close();

    const QString databasePath = QDir(dataRoot.path()).filePath(
        QStringLiteral("quizapp.sqlite"));
    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(40);

    auto *tree = window.findChild<QTreeWidget *>(
        QStringLiteral("sharedStorageFileTree"));
    auto *renameButton = window.findChild<QPushButton *>(
        QStringLiteral("sharedStorageRenameButton"));
    auto *moveButton = window.findChild<QPushButton *>(
        QStringLiteral("sharedStorageMoveButton"));
    auto *status = window.findChild<QLabel *>(
        QStringLiteral("libraryImportStatus"));
    QVERIFY(tree);
    QVERIFY(renameButton);
    QVERIFY(moveButton);
    QVERIFY(status);
    const auto findItem = [](QTreeWidgetItem *rootItem, const QString &text) {
        const auto find = [&](auto &&self, QTreeWidgetItem *item) -> QTreeWidgetItem * {
            if (item->text(0) == text) {
                return item;
            }
            for (int index = 0; index < item->childCount(); ++index) {
                if (QTreeWidgetItem *matched = self(self, item->child(index))) {
                    return matched;
                }
            }
            return nullptr;
        };
        return find(find, rootItem);
    };
    QTreeWidgetItem *sourceItem = findItem(
        tree->topLevelItem(0), QStringLiteral("单选题.json"));
    QVERIFY(sourceItem);
    tree->setCurrentItem(sourceItem);
    QVERIFY(renameButton->isEnabled());
    QVERIFY(moveButton->isEnabled());

    QTimer renameDialogTimer;
    renameDialogTimer.setInterval(10);
    connect(&renameDialogTimer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<QInputDialog *>(widget);
            if (!dialog || !dialog->isVisible()) {
                continue;
            }
            dialog->setTextValue(QStringLiteral("重点单选"));
            dialog->accept();
            renameDialogTimer.stop();
            return;
        }
    });
    renameDialogTimer.start();
    QTest::mouseClick(renameButton, Qt::LeftButton);
    renameDialogTimer.stop();
    const QString renamedPath = QDir(sourceDirectory).filePath(
        QStringLiteral("重点单选.json"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(renamedPath), 1000);

    QTreeWidgetItem *renamedItem = findItem(
        tree->topLevelItem(0), QStringLiteral("重点单选.json"));
    QVERIFY(renamedItem);
    tree->setCurrentItem(renamedItem);
    QTimer moveDialogTimer;
    moveDialogTimer.setInterval(10);
    connect(&moveDialogTimer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<QInputDialog *>(widget);
            if (!dialog || !dialog->isVisible()) {
                continue;
            }
            if (auto *combo = dialog->findChild<QComboBox *>()) {
                combo->setCurrentText(QStringLiteral("目标层级"));
            }
            dialog->accept();
            moveDialogTimer.stop();
            return;
        }
    });
    moveDialogTimer.start();
    QTest::mouseClick(moveButton, Qt::LeftButton);
    moveDialogTimer.stop();
    const QString movedPath = QDir(targetDirectory).filePath(
        QStringLiteral("重点单选.json"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(movedPath), 1000);
    QVERIFY(!QFileInfo::exists(renamedPath));
    QTreeWidgetItem *movedItem = findItem(
        tree->topLevelItem(0), QStringLiteral("重点单选.json"));
    QVERIFY(movedItem);
    tree->setCurrentItem(movedItem);
    tree->scrollToItem(movedItem);
    QVERIFY(renameButton->isEnabled());
    QVERIFY(moveButton->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(
        status->text().contains(QStringLiteral("扫描完成")), 3000);
    QVERIFY(tree->currentItem());
    QCOMPARE(tree->currentItem()->text(0), QStringLiteral("重点单选.json"));
    QVERIFY(renameButton->isEnabled());
    QVERIFY(moveButton->isEnabled());

    saveScreenshot(window, QStringLiteral("app-shell-library-editing-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(30);
    QVERIFY(renameButton->isEnabled());
    QVERIFY(moveButton->isEnabled());
    saveScreenshot(window, QStringLiteral("app-shell-library-editing-tablet.png"));
}

void AppShellTests::libraryEditModeReordersCurrentLevel()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(
        QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-library-order"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    QVERIFY2(questionRepository.replaceBank(installedBankAtPath(
        QStringLiteral("order-math"),
        {QStringLiteral("高数"), QStringLiteral("第一章")}), &error), qPrintable(error));
    QVERIFY2(questionRepository.replaceBank(installedBankAtPath(
        QStringLiteral("order-politics"),
        {QStringLiteral("毛概"), QStringLiteral("第一章")}), &error), qPrintable(error));
    QVERIFY2(questionRepository.replaceBank(installedBankAtPath(
        QStringLiteral("order-english"),
        {QStringLiteral("英语"), QStringLiteral("第一章")}), &error), qPrintable(error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(40);
    auto *editButton = window.findChild<QToolButton *>(
        QStringLiteral("libraryEditOrderButton"));
    auto *surface = window.findChild<QWidget *>(QStringLiteral("libraryBanksSurface"));
    QVERIFY(editButton);
    QVERIFY(surface);

    const auto visibleOrder = [&window]() {
        QVector<std::pair<int, QString>> positionedTitles;
        for (QWidget *row : window.findChildren<QWidget *>()) {
            if (row->objectName() != QStringLiteral("libraryPathNodeRow")) {
                continue;
            }
            const QStringList path = row->property("libraryPath").toStringList();
            if (path.size() != 1 || !row->isVisible()) {
                continue;
            }
            auto *button = row->findChild<QPushButton *>(
                QStringLiteral("libraryPathNodeButton"));
            if (button && button->isVisible()) {
                positionedTitles.append({row->geometry().top(), path.constLast()});
            }
        }
        std::sort(
            positionedTitles.begin(), positionedTitles.end(),
            [](const auto &left, const auto &right) { return left.first < right.first; });
        QStringList result;
        for (const auto &[position, title] : positionedTitles) {
            Q_UNUSED(position)
            result.append(title);
        }
        return result;
    };
    QTRY_COMPARE_WITH_TIMEOUT(visibleOrder().size(), 3, 1000);
    const QStringList initialOrder = visibleOrder();

    QTest::mouseClick(editButton, Qt::LeftButton);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(20);
    QVERIFY(editButton->isChecked());
    const auto modeButtons = window.findChildren<QPushButton *>(
        QStringLiteral("libraryScopeModeButton"));
    QVERIFY(std::none_of(modeButtons.cbegin(), modeButtons.cend(),
                         [](QPushButton *button) { return button->isVisible(); }));

    QMimeData mime;
    mime.setData(
        QStringLiteral("application/x-quizapp-library-path"),
        QJsonDocument(QJsonArray::fromStringList({initialOrder.constFirst()}))
            .toJson(QJsonDocument::Compact));
    const QPoint dropPoint(surface->width() / 2, surface->height() + 20);
    QDragEnterEvent dragEnter(
        dropPoint, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &dragEnter);
    QVERIFY(dragEnter.isAccepted());
    QDropEvent drop(
        QPointF(dropPoint), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &drop);
    QVERIFY(drop.isAccepted());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(20);

    QStringList expectedOrder = initialOrder.mid(1);
    expectedOrder.append(initialOrder.constFirst());
    QCOMPARE(visibleOrder(), expectedOrder);
    quizapp::storage::SqliteLibraryRepository libraryRepository(database.connection());
    QCOMPARE(libraryRepository.childOrder({}, &error), expectedOrder);

    const auto pageScrolls = window.findChildren<QScrollArea *>(
        QStringLiteral("pageScroll"));
    const auto visiblePageScroll = std::find_if(
        pageScrolls.cbegin(), pageScrolls.cend(),
        [](QScrollArea *scroll) { return scroll->isVisible(); });
    QVERIFY(visiblePageScroll != pageScrolls.cend());
    (*visiblePageScroll)->ensureWidgetVisible(surface, 0, 80);
    QTest::qWait(20);
    QCOMPARE((*visiblePageScroll)->horizontalScrollBar()->maximum(), 0);
    saveScreenshot(window, QStringLiteral("app-shell-library-order-edit-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(30);
    (*visiblePageScroll)->verticalScrollBar()->setValue(0);
    QCOMPARE(visibleOrder(), expectedOrder);
    saveScreenshot(window, QStringLiteral("app-shell-library-order-edit-tablet.png"));

    quizapp::ui::AppWindow restored(databasePath, dataRoot.path());
    restored.resize(390, 844);
    restored.show();
    restored.navigateTo(quizapp::ui::AppWindow::Section::Library);
    const auto restoredVisibleOrder = [&restored]() {
        QStringList result;
        QVector<std::pair<int, QString>> restoredTitles;
        for (QWidget *row : restored.findChildren<QWidget *>()) {
            if (row->objectName() != QStringLiteral("libraryPathNodeRow")) {
                continue;
            }
            const QStringList path = row->property("libraryPath").toStringList();
            if (path.size() == 1 && row->isVisible()) {
                restoredTitles.append({row->geometry().top(), path.constLast()});
            }
        }
        std::sort(
            restoredTitles.begin(), restoredTitles.end(),
            [](const auto &left, const auto &right) { return left.first < right.first; });
        for (const auto &[position, title] : restoredTitles) {
            Q_UNUSED(position)
            result.append(title);
        }
        return result;
    };
    QTRY_COMPARE_WITH_TIMEOUT(restoredVisibleOrder(), expectedOrder, 1000);
}

void AppShellTests::libraryEditModeDeletesAndRestoresBundledNode()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(
        QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-library-delete"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    QVERIFY2(questionRepository.replaceBank(installedSampleBank(), &error), qPrintable(error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(40);
    auto *editButton = window.findChild<QToolButton *>(
        QStringLiteral("libraryEditOrderButton"));
    QVERIFY(editButton);
    QTest::mouseClick(editButton, Qt::LeftButton);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(20);
    auto *deleteButton = window.findChild<QToolButton *>(
        QStringLiteral("libraryPathDeleteButton"));
    QVERIFY(deleteButton);
    QVERIFY(deleteButton->isVisible());
    QVERIFY(!window.findChild<QToolButton *>(QStringLiteral("libraryPathChevron")));

    QTimer::singleShot(50, [] {
        auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (message) {
            if (auto *yes = message->button(QMessageBox::Yes)) {
                QTest::mouseClick(yes, Qt::LeftButton);
            }
        }
    });
    QTest::mouseClick(deleteButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(questionRepository.listInstalledBanks(&error).size(), 0, 1000);
    const quizapp::storage::SqliteLibraryRepository libraryRepository(database.connection());
    QTRY_COMPARE_WITH_TIMEOUT(libraryRepository.hiddenNodes(&error).size(), 1, 1000);

    auto *tree = window.findChild<QTreeWidget *>(QStringLiteral("sharedStorageFileTree"));
    auto *restore = window.findChild<QPushButton *>(
        QStringLiteral("sharedStorageRestoreButton"));
    QVERIFY(tree);
    QVERIFY(restore);
    QTreeWidgetItem *hiddenItem = nullptr;
    for (QTreeWidgetItemIterator iterator(tree); *iterator; ++iterator) {
        if ((*iterator)->text(1) == QStringLiteral("内置题库")) {
            hiddenItem = *iterator;
            break;
        }
    }
    QVERIFY(hiddenItem);
    tree->setCurrentItem(hiddenItem);
    QTRY_VERIFY(restore->isVisible());
    QVERIFY(restore->isEnabled());
    saveScreenshot(window, QStringLiteral("app-shell-library-hidden-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(30);
    saveScreenshot(window, QStringLiteral("app-shell-library-hidden-tablet.png"));

    QTest::mouseClick(restore, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(questionRepository.listInstalledBanks(&error).size(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(libraryRepository.hiddenNodes(&error).size(), 0, 1000);
    QTRY_VERIFY(window.findChild<QToolButton *>(
        QStringLiteral("libraryPathDeleteButton")) != nullptr);
}

void AppShellTests::bankReleaseDialogSelectsHierarchyAndInstallsBanks()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    quizapp::services::SharedStorageService storageService;
    const auto layout = storageService.prepare(
        QDir(root.path()).filePath(QStringLiteral("QuizApp")));
    QVERIFY2(layout.ready(), qPrintable(layout.error));

    const auto makeEntry = [](const QString &id,
                              const QStringList &path,
                              const QString &questionId) {
        quizapp::services::BankReleaseEntry entry;
        entry.id = id;
        entry.name = path.join(QStringLiteral("-"));
        entry.path = path;
        entry.questionCount = 1;
        const QJsonObject payload{
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("questions"), QJsonArray{QJsonObject{
                {QStringLiteral("id"), questionId},
                {QStringLiteral("type"), QStringLiteral("single")},
                {QStringLiteral("q"), QStringLiteral("Release UI 测试题")},
                {QStringLiteral("options"), QJsonArray{
                    QStringLiteral("A"), QStringLiteral("B")}},
                {QStringLiteral("ans"), QStringLiteral("A")},
            }}},
        };
        entry.embeddedPayload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        return entry;
    };
    quizapp::services::BankReleaseCatalog catalog;
    catalog.tagName = QStringLiteral("v2.0.0-test");
    catalog.releasePageUrl = QStringLiteral("https://example.test/release");
    catalog.entries = {
        makeEntry(QStringLiteral("politics-intro"),
                  {QStringLiteral("毛概"), QStringLiteral("导论")},
                  QStringLiteral("ui-release-1")),
        makeEntry(QStringLiteral("politics-chapter"),
                  {QStringLiteral("毛概"), QStringLiteral("第一章")},
                  QStringLiteral("ui-release-2")),
        makeEntry(QStringLiteral("english-chapter"),
                  {QStringLiteral("英语"), QStringLiteral("第一章")},
                  QStringLiteral("ui-release-3")),
    };
    const QString conflictPath = quizapp::services::BankReleaseService::destinationPath(
        layout, catalog.entries.constFirst());
    QVERIFY(QDir().mkpath(QFileInfo(conflictPath).absolutePath()));
    QFile conflict(conflictPath);
    QVERIFY(conflict.open(QIODevice::WriteOnly));
    QVERIFY(conflict.write("old") > 0);
    conflict.close();

    quizapp::ui::AppWindow host;
    host.resize(390, 844);
    host.show();
    quizapp::ui::BankReleaseDialog dialog(layout, &host);
    dialog.resize(390, 760);
    dialog.setCatalogForTesting(catalog);
    dialog.show();
    QTest::qWait(30);
    auto *tree = dialog.findChild<QTreeWidget *>(QStringLiteral("bankReleaseTree"));
    auto *download = dialog.findChild<QPushButton *>(
        QStringLiteral("bankReleaseDownloadButton"));
    auto *selectAll = dialog.findChild<QPushButton *>(
        QStringLiteral("bankReleaseSelectAllButton"));
    auto *selection = dialog.findChild<QLabel *>(
        QStringLiteral("bankReleaseSelectionSummary"));
    QVERIFY(tree);
    QVERIFY(download);
    QVERIFY(selectAll);
    QVERIFY(selection);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QTreeWidgetItem *politics = tree->topLevelItem(0);
    QCOMPARE(politics->text(0), QStringLiteral("毛概"));
    QCOMPARE(politics->childCount(), 2);
    auto *conflictChoice = qobject_cast<QComboBox *>(
        tree->itemWidget(politics->child(0), 1));
    QVERIFY(conflictChoice);
    QVERIFY(dynamic_cast<quizapp::ui::ChoiceComboBox *>(conflictChoice));
    QCOMPARE(conflictChoice->view()->objectName(), QStringLiteral("choiceComboPopup"));
    QVERIFY(conflictChoice->minimumHeight() >= 42);
    QCOMPARE(conflictChoice->count(), 2);
    QVERIFY(host.styleSheet().contains(
        QStringLiteral("QTreeView::branch:has-children:closed")));
    QVERIFY(host.styleSheet().contains(
        QStringLiteral(":/quizapp/icons/tree_chevron_right_dark.svg")));

    politics->child(1)->setCheckState(0, Qt::Unchecked);
    QCOMPARE(politics->checkState(0), Qt::PartiallyChecked);
    QCOMPARE(selection->text(), QStringLiteral("已选择 2/3 个题库"));
    saveScreenshot(dialog, QStringLiteral("bank-release-dialog-phone.png"));
    politics->setExpanded(false);
    QTest::qWait(20);
    saveScreenshot(dialog, QStringLiteral("bank-release-dialog-collapsed-phone.png"));
    politics->setExpanded(true);
    conflictChoice->showPopup();
    QTRY_VERIFY(conflictChoice->view()->isVisible());
    saveScreenshot(*conflictChoice->view()->window(),
                   QStringLiteral("bank-release-combobox-popup-phone.png"));
    conflictChoice->hidePopup();
    dialog.resize(760, 680);
    QTest::qWait(30);
    saveScreenshot(dialog, QStringLiteral("bank-release-dialog-tablet.png"));
    politics->setExpanded(false);
    QTest::qWait(20);
    saveScreenshot(dialog, QStringLiteral("bank-release-dialog-collapsed-tablet.png"));
    politics->setExpanded(true);
    conflictChoice->showPopup();
    QTRY_VERIFY(conflictChoice->view()->isVisible());
    saveScreenshot(*conflictChoice->view()->window(),
                   QStringLiteral("bank-release-combobox-popup-tablet.png"));
    conflictChoice->hidePopup();

    QTest::mouseClick(selectAll, Qt::LeftButton);
    QCOMPARE(selection->text(), QStringLiteral("已选择 3/3 个题库"));
    QSignalSpy installedSpy(&dialog, &quizapp::ui::BankReleaseDialog::banksInstalled);
    QTest::mouseClick(download, Qt::LeftButton);
    QTRY_COMPARE(installedSpy.count(), 1);
    QCOMPARE(installedSpy.constFirst().constFirst().toInt(), 3);
    for (const auto &entry : catalog.entries) {
        QVERIFY(QFileInfo::exists(
            quizapp::services::BankReleaseService::destinationPath(layout, entry)));
    }
    const auto releaseState = quizapp::services::BankReleaseStateStore::load(QSettings());
    QVERIFY(quizapp::services::BankReleaseStateStore::outdatedEntryIds(
        catalog, releaseState).isEmpty());

    quizapp::ui::BankReleaseDialog silentAutomatic(layout, &host);
    QSignalSpy silentSpy(
        &silentAutomatic, &quizapp::ui::BankReleaseDialog::checkCompleted);
    silentAutomatic.setCatalogForTesting(catalog, {}, true);
    QCOMPARE(silentSpy.count(), 1);
    QCOMPARE(silentSpy.constFirst().at(0).toInt(), 0);
    QVERIFY(!silentAutomatic.isVisible());

    auto changedCatalog = catalog;
    changedCatalog.entries[0].assetApiId = 909;
    quizapp::ui::BankReleaseDialog changedAutomatic(layout, &host);
    changedAutomatic.resize(390, 760);
    QSignalSpy changedSpy(
        &changedAutomatic, &quizapp::ui::BankReleaseDialog::checkCompleted);
    changedAutomatic.setCatalogForTesting(changedCatalog, {}, true);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.constFirst().at(0).toInt(), 1);
    QTRY_VERIFY(changedAutomatic.isVisible());
    saveScreenshot(
        changedAutomatic, QStringLiteral("bank-release-automatic-update-phone.png"));
    changedAutomatic.close();
}

void AppShellTests::announcementFeedShowsUnreadAndArchive()
{
    QSettings settings;
    settings.remove(QStringLiteral("updates/announcements"));
    const QByteArray payload = R"JSON({
      "announcements": [
        {
          "id": "native-preview-2",
          "title": "原生预览第二次更新",
          "date": "2026-07-18",
          "latest": true,
          "body": "<ul><li>修复题库选择器。</li><li>新增题库自动更新状态。</li></ul>"
        },
        {
          "id": "native-preview-1",
          "title": "原生预览首次公告",
          "date": "2026-07-17",
          "body": "<p>公告正文可以在独立区域滚动查看。</p>"
        }
      ]
    })JSON";
    quizapp::services::AnnouncementCatalog catalog;
    QString error;
    QVERIFY2(quizapp::services::AnnouncementService::parseCatalog(
                 payload, &catalog, &error), qPrintable(error));
    QVERIFY2(quizapp::services::AnnouncementStateStore::saveCatalog(
                 settings, catalog, QDateTime::currentDateTimeUtc(), &error),
             qPrintable(error));

    quizapp::ui::AppWindow window;
    window.resize(390, 844);
    window.show();
    QTest::qWait(30);
    auto *mail = window.findChild<QToolButton *>(QStringLiteral("announcementButton"));
    auto *dot = window.findChild<QLabel *>(QStringLiteral("announcementUnreadDot"));
    QVERIFY(mail);
    QVERIFY(dot);
    QVERIFY(mail->isVisible());
    QVERIFY(dot->isVisible());
    saveScreenshot(window, QStringLiteral("app-shell-home-announcement-unread-phone.png"));

    quizapp::ui::AnnouncementDialog archive(&window);
    archive.resize(390, 720);
    archive.showCached(catalog);
    archive.show();
    QTest::qWait(30);
    auto *tree = archive.findChild<QTreeWidget *>(QStringLiteral("announcementTree"));
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QVERIFY(tree->topLevelItem(0)->isExpanded());
    saveScreenshot(archive, QStringLiteral("announcement-archive-phone.png"));
    archive.resize(760, 650);
    QTest::qWait(30);
    saveScreenshot(archive, QStringLiteral("announcement-archive-tablet.png"));
    auto *close = archive.findChild<QPushButton *>(QStringLiteral("announcementCloseButton"));
    QVERIFY(close);
    QTest::mouseClick(close, Qt::LeftButton);
    QVERIFY(quizapp::services::AnnouncementStateStore::unreadIds(
        quizapp::services::AnnouncementStateStore::load(settings)).isEmpty());

    auto newer = catalog;
    quizapp::services::AnnouncementItem latest;
    latest.id = QStringLiteral("native-preview-3");
    latest.title = QStringLiteral("原生预览第三次更新");
    latest.date = QStringLiteral("2026-07-19");
    latest.bodyHtml = QStringLiteral("<p>这是一篇新的远程公告。</p>");
    latest.latest = true;
    newer.items.prepend(latest);
    newer.fingerprint = quizapp::services::AnnouncementService::fingerprint(newer.items);
    quizapp::ui::AnnouncementDialog automatic(&window);
    automatic.resize(390, 720);
    QSignalSpy checkSpy(&automatic, &quizapp::ui::AnnouncementDialog::checkCompleted);
    automatic.setCatalogForTesting(newer, true);
    QCOMPARE(checkSpy.count(), 1);
    QCOMPARE(checkSpy.constFirst().at(0).toInt(), 1);
    QTRY_VERIFY(automatic.isVisible());
    auto *automaticTree = automatic.findChild<QTreeWidget *>(
        QStringLiteral("announcementTree"));
    QVERIFY(automaticTree);
    QCOMPARE(automaticTree->topLevelItemCount(), 1);
    auto *suppress = automatic.findChild<QCheckBox *>(
        QStringLiteral("announcementSuppressChoice"));
    QVERIFY(suppress);
    QVERIFY(suppress->isVisible());
    suppress->setChecked(true);
    saveScreenshot(automatic, QStringLiteral("announcement-automatic-phone.png"));
    automatic.accept();

    const auto readState = quizapp::services::AnnouncementStateStore::load(settings);
    QVERIFY(quizapp::services::AnnouncementStateStore::unreadIds(readState).isEmpty());
    QCOMPARE(readState.suppressedCatalogFingerprint, newer.fingerprint);
    quizapp::ui::AnnouncementDialog silent(&window);
    QSignalSpy silentSpy(&silent, &quizapp::ui::AnnouncementDialog::checkCompleted);
    silent.setCatalogForTesting(newer, true);
    QCOMPARE(silentSpy.count(), 1);
    QCOMPARE(silentSpy.constFirst().at(0).toInt(), 0);
    QVERIFY(!silent.isVisible());
}

void AppShellTests::appUpdateDialogShowsVersionAndPlatformPackage()
{
    quizapp::services::AppReleaseInfo release;
    release.tagName = QStringLiteral("v2.0.0-alpha.4");
    release.targetCommit = QStringLiteral("next-build");
    release.releasePageUrl = QStringLiteral(
        "https://github.com/konwait12/QuizApp/releases/tag/v2.0.0-alpha.4");
    release.body = QStringLiteral("修复更新检查、下载进度和安装流程。");
    quizapp::services::AppUpdateAsset installer;
    installer.id = 7;
    installer.name = QStringLiteral("QuizApp-Setup.exe");
    installer.downloadUrl = QStringLiteral("https://example.test/QuizApp-Setup.exe");
    installer.size = 1024;
    release.assets.append(installer);

    quizapp::ui::AppWindow host;
    host.resize(390, 844);
    host.show();
    quizapp::ui::AppUpdateDialog dialog(
        QStringLiteral("2.0.0-alpha.3"), QStringLiteral("current-build"), &host);
    dialog.resize(390, 720);
    dialog.setReleaseForTesting(release);
    dialog.show();
    QTest::qWait(30);
    auto *versions = dialog.findChild<QLabel *>(QStringLiteral("appUpdateVersions"));
    auto *download = dialog.findChild<QPushButton *>(
        QStringLiteral("appUpdateDownloadButton"));
    auto *releaseButton = dialog.findChild<QPushButton *>(
        QStringLiteral("appUpdateReleaseButton"));
    auto *dismiss = dialog.findChild<QCheckBox *>(
        QStringLiteral("appUpdateDismissChoice"));
    QVERIFY(versions);
    QVERIFY(download);
    QVERIFY(releaseButton);
    QVERIFY(dismiss);
    QVERIFY(versions->text().contains(QStringLiteral("alpha.4")));
    QVERIFY(download->isEnabled());
    QVERIFY(releaseButton->isEnabled());
    QVERIFY(dismiss->isVisible());
    saveScreenshot(dialog, QStringLiteral("app-update-dialog-phone.png"));

    dialog.resize(680, 560);
    QTest::qWait(20);
    saveScreenshot(dialog, QStringLiteral("app-update-dialog-tablet.png"));

    release.tagName = QStringLiteral("2.0.0-alpha.3");
    release.targetCommit = QStringLiteral("current-build");
    dialog.setReleaseForTesting(release);
    QVERIFY(!download->isEnabled());
}

void AppShellTests::mockExamPersistsResumesSubmitsAndOpensHandwriting()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-mock-exam"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    QVERIFY2(repository.replaceBank(installedSampleBank(), &error), qPrintable(error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Study);
    auto *openExam = window.findChild<QPushButton *>(QStringLiteral("studyOpenExamButton"));
    QVERIFY(openExam);
    QTest::mouseClick(openExam, Qt::LeftButton);
    auto *examPage = window.findChild<quizapp::ui::ExamPage *>(QStringLiteral("examPageRoot"));
    QVERIFY(examPage);
    QTRY_VERIFY(examPage->isVisible());
    auto *scope = window.findChild<QComboBox *>(QStringLiteral("examScopeChoice"));
    auto *start = window.findChild<QPushButton *>(QStringLiteral("examStartButton"));
    QVERIFY(scope);
    QVERIFY(start);
    QVERIFY(dynamic_cast<quizapp::ui::ChoiceComboBox *>(scope));
    auto *setupStatus = window.findChild<QLabel *>(QStringLiteral("examSetupStatus"));
    QVERIFY(setupStatus);
    QVERIFY2(scope->count() >= 2, qPrintable(setupStatus->text()));
    saveScreenshot(window, QStringLiteral("app-shell-exam-setup-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(30);
    saveScreenshot(window, QStringLiteral("app-shell-exam-setup-tablet.png"));
    window.resize(390, 844);
    QTest::mouseClick(start, Qt::LeftButton);
    auto *prompt = window.findChild<QLabel *>(QStringLiteral("examQuestionPrompt"));
    QTRY_VERIFY(prompt && prompt->isVisible());
    QVERIFY(!prompt->text().isEmpty());
    QTest::qWait(30);
    saveScreenshot(window, QStringLiteral("app-shell-exam-question-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(30);
    saveScreenshot(window, QStringLiteral("app-shell-exam-question-tablet.png"));
    window.resize(390, 844);
    QTest::qWait(30);

    auto *handwriting = window.findChild<QPushButton *>(
        QStringLiteral("examHandwritingButton"));
    QVERIFY(handwriting);
    QTest::mouseClick(handwriting, Qt::LeftButton);
    QTRY_VERIFY(window.isHandwritingVisible());
    auto *handwritingBack = window.findChild<QToolButton *>(
        QStringLiteral("handwritingBackButton"));
    QVERIFY(handwritingBack);
    QTest::mouseClick(handwritingBack, Qt::LeftButton);
    QTRY_VERIFY(!window.isHandwritingVisible());
    QTRY_VERIFY(examPage->isVisible());

    const auto options = window.findChildren<QPushButton *>(
        QStringLiteral("examOptionButton"));
    QVERIFY(!options.isEmpty());
    QTest::mouseClick(options.first(), Qt::LeftButton);
    QVERIFY(options.first()->isChecked());

    const auto confirmYes = [] {
        QTimer::singleShot(40, [] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (message) {
                if (auto *yes = message->button(QMessageBox::Yes)) {
                    QTest::mouseClick(yes, Qt::LeftButton);
                }
            }
        });
    };
    auto *examBack = window.findChild<QPushButton *>(QStringLiteral("examBackButton"));
    QVERIFY(examBack);
    confirmYes();
    QTest::mouseClick(examBack, Qt::LeftButton);
    QTRY_VERIFY(openExam->isVisible());

    QTest::mouseClick(openExam, Qt::LeftButton);
    auto *resume = window.findChild<QPushButton *>(QStringLiteral("examResumeButton"));
    QTRY_VERIFY(resume && resume->isVisible());
    QTest::mouseClick(resume, Qt::LeftButton);
    QTRY_VERIFY(prompt->isVisible());
    auto *submit = window.findChild<QPushButton *>(QStringLiteral("examSubmitButton"));
    QVERIFY(submit);
    confirmYes();
    QTest::mouseClick(submit, Qt::LeftButton);
    auto *score = window.findChild<QLabel *>(QStringLiteral("examResultScore"));
    QTRY_VERIFY(score && score->isVisible());
    QVERIFY(score->text().contains(QStringLiteral("分")));
    saveScreenshot(window, QStringLiteral("app-shell-exam-result-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(30);
    saveScreenshot(window, QStringLiteral("app-shell-exam-result-tablet.png"));
    auto *resultBack = window.findChild<QPushButton *>(
        QStringLiteral("examResultBackButton"));
    QVERIFY(resultBack);
    QTest::mouseClick(resultBack, Qt::LeftButton);
    auto *history = window.findChild<QListWidget *>(QStringLiteral("examHistoryList"));
    QTRY_VERIFY(history && history->count() == 1);
}

void AppShellTests::backupSettingsExportsPreviewsAndStagesRestore()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-local-backup"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    QVERIFY2(repository.replaceBank(installedSampleBank(), &error), qPrintable(error));
    const QString notePath = QDir(dataRoot.path()).filePath(
        QStringLiteral("notes/questions/ui-backup.snb/document.json"));
    QVERIFY(QDir().mkpath(QFileInfo(notePath).absolutePath()));
    QFile note(notePath);
    QVERIFY(note.open(QIODevice::WriteOnly));
    QVERIFY(note.write(QByteArrayLiteral("{\"title\":\"UI backup\"}")) > 0);
    note.close();

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Settings);
    auto *panel = window.findChild<quizapp::ui::BackupSettingsPanel *>(
        QStringLiteral("backupSettingsSurface"));
    auto *exportButton = window.findChild<QPushButton *>(
        QStringLiteral("backupExportButton"));
    auto *restoreButton = window.findChild<QPushButton *>(
        QStringLiteral("backupRestoreButton"));
    auto *progress = window.findChild<QProgressBar *>(QStringLiteral("backupProgress"));
    QVERIFY(panel);
    QVERIFY(exportButton);
    QVERIFY(restoreButton);
    QVERIFY(progress);
    const auto scrolls = window.findChildren<QScrollArea *>(QStringLiteral("pageScroll"));
    const auto visibleScroll = std::find_if(
        scrolls.cbegin(), scrolls.cend(), [](QScrollArea *scroll) {
            return scroll->isVisible();
        });
    QVERIFY(visibleScroll != scrolls.cend());
    (*visibleScroll)->ensureWidgetVisible(panel, 0, 20);
    QTest::qWait(40);
    saveScreenshot(window, QStringLiteral("app-shell-settings-backup-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(40);
    (*visibleScroll)->ensureWidgetVisible(panel, 0, 20);
    saveScreenshot(window, QStringLiteral("app-shell-settings-backup-tablet.png"));

    const QString archivePath = QDir(dataRoot.path()).filePath(
        QStringLiteral("outside-complete.quizbackup"));
    QSignalSpy created(panel, &quizapp::ui::BackupSettingsPanel::backupCreated);
    panel->exportTo(archivePath);
    QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 5000);
    QVERIFY(QFileInfo(archivePath).isFile());
    QVERIFY(!progress->isVisible());

    QSignalSpy inspected(panel, &quizapp::ui::BackupSettingsPanel::backupInspected);
    panel->inspectForRestore(archivePath);
    QTRY_COMPARE_WITH_TIMEOUT(inspected.count(), 1, 5000);
    QCOMPARE(inspected.takeFirst().at(0).toBool(), true);
    auto *preview = window.findChild<QDialog *>(QStringLiteral("backupPreviewDialog"));
    QTRY_VERIFY(preview && preview->isVisible());
    auto *confirm = preview->findChild<QPushButton *>(
        QStringLiteral("backupConfirmRestoreButton"));
    QVERIFY(confirm);
    preview->resize(390, 650);
    QTest::qWait(30);
    saveScreenshot(*preview, QStringLiteral("backup-preview-phone.png"));
    preview->resize(680, 520);
    QTest::qWait(30);
    saveScreenshot(*preview, QStringLiteral("backup-preview-tablet.png"));

    QSignalSpy staged(panel, &quizapp::ui::BackupSettingsPanel::restoreStaged);
    QTimer::singleShot(120, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                message->accept();
            }
        }
    });
    QTest::mouseClick(confirm, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(staged.count(), 1, 5000);
    quizapp::services::LocalBackupService service;
    QVERIFY(service.hasPendingRestore(dataRoot.path()));
}

void AppShellTests::aiSettingsPersistSecurelyAndAdaptToPhoneAndTablet()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-ai-settings"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));

    QSettings settings;
    settings.remove(QStringLiteral("ai"));
    quizapp::platform::SecureSecretStore::removeSecret(
        QStringLiteral("ai/apiKey"), settings, nullptr);
    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Settings);

    auto *panel = window.findChild<quizapp::ui::AiSettingsPanel *>(
        QStringLiteral("aiSettingsSurface"));
    auto *provider = window.findChild<QComboBox *>(QStringLiteral("aiProviderChoice"));
    auto *baseUrl = window.findChild<QLineEdit *>(QStringLiteral("aiBaseUrlInput"));
    auto *apiKey = window.findChild<QLineEdit *>(QStringLiteral("aiApiKeyInput"));
    auto *model = window.findChild<QComboBox *>(QStringLiteral("aiModelChoice"));
    auto *testConnection = window.findChild<QPushButton *>(
        QStringLiteral("aiTestConnectionButton"));
    auto *save = window.findChild<QPushButton *>(QStringLiteral("saveSettingsButton"));
    QVERIFY(panel);
    QVERIFY(provider);
    QVERIFY(baseUrl);
    QVERIFY(apiKey);
    QVERIFY(model);
    QVERIFY(testConnection);
    QVERIFY(save);

    provider->setCurrentIndex(provider->findData(QStringLiteral("custom")));
    baseUrl->setText(QStringLiteral("https://example.com/v1"));
    apiKey->setText(QStringLiteral("ui-secure-secret"));
    model->setCurrentText(QStringLiteral("compatible-model"));
    QTest::mouseClick(save, Qt::LeftButton);
    QTRY_COMPARE(settings.value(QStringLiteral("ai/provider")).toString(),
                 QStringLiteral("custom"));
    QVERIFY(!settings.contains(QStringLiteral("ai/apiKey")));
    QCOMPARE(quizapp::platform::SecureSecretStore::readSecret(
        QStringLiteral("ai/apiKey"), settings, &error), QStringLiteral("ui-secure-secret"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const auto scrolls = window.findChildren<QScrollArea *>(QStringLiteral("pageScroll"));
    const auto visibleScroll = std::find_if(
        scrolls.cbegin(), scrolls.cend(), [](QScrollArea *scroll) {
            return scroll->isVisible();
        });
    QVERIFY(visibleScroll != scrolls.cend());
    (*visibleScroll)->ensureWidgetVisible(panel, 0, 20);
    (*visibleScroll)->verticalScrollBar()->setValue(std::max(
        0, (*visibleScroll)->verticalScrollBar()->value() - 30));
    QTest::qWait(40);
    saveScreenshot(window, QStringLiteral("app-shell-settings-ai-phone.png"));
    (*visibleScroll)->ensureWidgetVisible(testConnection, 0, 24);
    QTest::qWait(30);
    saveScreenshot(window, QStringLiteral("app-shell-settings-ai-phone-bottom.png"));
    window.resize(1280, 800);
    QTest::qWait(40);
    (*visibleScroll)->ensureWidgetVisible(panel, 0, 20);
    saveScreenshot(window, QStringLiteral("app-shell-settings-ai-tablet.png"));

    quizapp::platform::SecureSecretStore::removeSecret(
        QStringLiteral("ai/apiKey"), settings, nullptr);
    settings.remove(QStringLiteral("ai"));
}

void AppShellTests::questionAiPanelRequiresActionAndShowsCachedAndStaleStates()
{
    quizapp::ui::QuestionAiPanel panel;
    panel.resize(390, 560);
    auto question = installedSampleBank().bank.questions.first();
    panel.setQuestion(question);
    panel.show();
    QTest::qWait(20);

    auto *analyze = panel.findChild<QPushButton *>(QStringLiteral("questionAiAnalyzeButton"));
    auto *content = panel.findChild<QTextBrowser *>(QStringLiteral("questionAiContent"));
    auto *status = panel.findChild<QLabel *>(QStringLiteral("questionAiStatus"));
    QVERIFY(analyze);
    QVERIFY(content);
    QVERIFY(status);
    QCOMPARE(analyze->text(), QStringLiteral("开始分析"));
    QVERIFY(!content->isVisible());
    QVERIFY(status->text().contains(QStringLiteral("点击")));

    bool requested = false;
    bool replaceCached = true;
    connect(&panel, &quizapp::ui::QuestionAiPanel::analyzeRequested,
            &panel, [&](const quizapp::domain::Question &requestedQuestion, bool replace) {
                requested = requestedQuestion.id == question.id;
                replaceCached = replace;
            });
    QTest::mouseClick(analyze, Qt::LeftButton);
    QVERIFY(requested);
    QVERIFY(!replaceCached);

    quizapp::domain::AiRecord record;
    record.id = QUuid::createUuid();
    record.recordType = QStringLiteral("question_analysis");
    record.sourceId = question.id.toString(QUuid::WithoutBraces);
    record.model = QStringLiteral("test-model");
    record.content = QStringLiteral("## 解析\n\n正确答案依据。 ");
    record.sourceHash = question.contentHash;
    record.createdAt = QDateTime::currentDateTimeUtc();
    panel.setRecord(record);
    QVERIFY(content->isVisible());
    QCOMPARE(analyze->text(), QStringLiteral("重新分析"));
    QVERIFY(status->text().contains(QStringLiteral("test-model")));
    saveScreenshot(panel, QStringLiteral("question-ai-panel-phone.png"));

    question.contentHash = QByteArray(32, 'x');
    panel.setQuestion(question, record);
    QVERIFY(status->text().contains(QStringLiteral("旧解析")));
    panel.resize(760, 480);
    QTest::qWait(20);
    saveScreenshot(panel, QStringLiteral("question-ai-panel-tablet.png"));
}

void AppShellTests::freeNotebookLibraryCreatesSavesRecyclesRestoresAndDeletes()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-free-notebooks"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    auto *homeAction = window.findChild<QPushButton *>(QStringLiteral("homeNotebookAction"));
    QVERIFY(homeAction);
    saveScreenshot(window, QStringLiteral("app-shell-home-notebooks-phone.png"));
    QTest::mouseClick(homeAction, Qt::LeftButton);

    auto *page = window.findChild<quizapp::ui::NotebookLibraryPage *>(
        QStringLiteral("notebookLibraryPage"));
    auto *title = window.findChild<QLineEdit *>(QStringLiteral("notebookTitleInput"));
    auto *create = window.findChild<QPushButton *>(QStringLiteral("notebookCreateButton"));
    auto *list = window.findChild<QListWidget *>(QStringLiteral("notebookList"));
    auto *recycle = window.findChild<QPushButton *>(QStringLiteral("notebookRecycleButton"));
    auto *permanent = window.findChild<QPushButton *>(
        QStringLiteral("notebookPermanentDeleteButton"));
    auto *recycleView = window.findChild<QToolButton *>(
        QStringLiteral("notebookRecycleViewButton"));
    QVERIFY(page);
    QVERIFY(title);
    QVERIFY(create);
    QVERIFY(list);
    QVERIFY(recycle);
    QVERIFY(permanent);
    QVERIFY(recycleView);
    QTRY_VERIFY(page->isVisible());
    QCOMPARE(list->count(), 0);
    saveScreenshot(window, QStringLiteral("notebook-library-empty-phone.png"));
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY(!page->isVisible());
    window.navigateTo(quizapp::ui::AppWindow::Section::Home);
    QTest::mouseClick(homeAction, Qt::LeftButton);
    QTRY_VERIFY(page->isVisible());

    title->setText(QStringLiteral("高等数学草稿"));
    QTest::mouseClick(create, Qt::LeftButton);
    QTRY_VERIFY(window.isHandwritingVisible());
    auto *handwriting = window.findChild<quizapp::ui::HandwritingPage *>();
    QVERIFY(handwriting);
    const QString bundlePath = handwriting->currentBundlePath();
    QVERIFY(bundlePath.contains(QStringLiteral("notes/free")));
    QVERIFY(!bundlePath.contains(QStringLiteral("notes/questions")));
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY(page->isVisible());
    QTRY_COMPARE(list->count(), 1);
    QVERIFY(QFileInfo(QDir(bundlePath).filePath(QStringLiteral("document.json"))).isFile());
    saveScreenshot(window, QStringLiteral("notebook-library-list-phone.png"));

    window.resize(1280, 800);
    QTest::qWait(40);
    saveScreenshot(window, QStringLiteral("notebook-library-list-tablet.png"));
    window.resize(390, 844);
    QTest::qWait(30);

    const auto acceptQuestion = [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                if (QAbstractButton *yes = message->button(QMessageBox::Yes)) {
                    yes->click();
                }
            }
        }
    };
    list->setCurrentRow(0);
    QVERIFY(recycle->isVisible());
    QVERIFY(recycle->isEnabled());
    QTimer::singleShot(80, acceptQuestion);
    QTest::mouseClick(recycle, Qt::LeftButton);
    QTRY_COMPARE(list->count(), 0);
    recycleView->setChecked(true);
    QTRY_COMPARE(list->count(), 1);
    list->setCurrentRow(0);
    QTest::mouseClick(recycle, Qt::LeftButton);
    QTRY_COMPARE(list->count(), 0);

    recycleView->setChecked(false);
    QTRY_COMPARE(list->count(), 1);
    list->setCurrentRow(0);
    QTimer::singleShot(80, acceptQuestion);
    QTest::mouseClick(recycle, Qt::LeftButton);
    QTRY_COMPARE(list->count(), 0);
    recycleView->setChecked(true);
    QTRY_COMPARE(list->count(), 1);
    list->setCurrentRow(0);
    QTimer::singleShot(80, acceptQuestion);
    QTest::mouseClick(permanent, Qt::LeftButton);
    QTRY_COMPARE(list->count(), 0);
    QVERIFY(!QFileInfo::exists(bundlePath));
    saveScreenshot(window, QStringLiteral("notebook-library-recycle-phone.png"));
}

void AppShellTests::startPracticeFromInstalledBankAndOpenHandwriting()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-practice-flow"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    QVERIFY2(repository.replaceBank(installedSampleBank(), &error), qPrintable(error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(80);

    auto *bankButton = drillToLibraryLeaf(window);
    QVERIFY(bankButton);
    QTest::mouseClick(bankButton, Qt::LeftButton);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Study);

    auto *practicePage = window.findChild<quizapp::ui::PracticePage *>(
        QStringLiteral("practicePage"));
    QVERIFY(practicePage);
    QVERIFY(practicePage->hasActiveSession());
    auto *headerBack = window.findChild<QPushButton *>(QStringLiteral("practiceBackButton"));
    auto *headerActions = window.findChild<QWidget *>(
        QStringLiteral("practiceHeaderActions"));
    QVERIFY(headerBack);
    QVERIFY(headerActions);
    QVERIFY(headerActions->geometry().top() > headerBack->geometry().top());
    const auto firstQuestionId = practicePage->currentQuestionId();
    QVERIFY(firstQuestionId.has_value());

    auto *prompt = window.findChild<QLabel *>(QStringLiteral("practicePrompt"));
    QVERIFY(prompt);
    QVERIFY(prompt->text().contains(QStringLiteral("示例选择题")));

    const auto optionButtons = window.findChildren<QPushButton *>(
        QStringLiteral("practiceOptionButton"));
    QVERIFY(optionButtons.size() >= 2);
    QTest::mouseClick(optionButtons.at(0), Qt::LeftButton);
    QVERIFY(practicePage->session().answers.contains(*firstQuestionId));

    auto *aiButton = window.findChild<QPushButton *>(QStringLiteral("practiceAiButton"));
    auto *aiSurface = window.findChild<QFrame *>(QStringLiteral("questionAiSurface"));
    auto *practiceScroll = window.findChild<QScrollArea *>(QStringLiteral("practiceScroll"));
    QVERIFY(aiButton);
    QVERIFY(aiSurface);
    QVERIFY(practiceScroll);
    QTest::mouseClick(aiButton, Qt::LeftButton);
    QVERIFY(aiSurface->isVisible());
    practiceScroll->ensureWidgetVisible(aiSurface, 0, 12);
    QTest::qWait(20);
    saveScreenshot(window, QStringLiteral("app-shell-question-ai-phone.png"));
    window.resize(1180, 760);
    QTest::qWait(30);
    practiceScroll->ensureWidgetVisible(aiSurface, 0, 12);
    saveScreenshot(window, QStringLiteral("app-shell-question-ai-tablet.png"));
    window.resize(390, 844);
    QTest::mouseClick(aiButton, Qt::LeftButton);

    auto *handwriting = window.findChild<QPushButton *>(
        QStringLiteral("practiceHandwritingButton"));
    QVERIFY(handwriting);
    QTest::mouseClick(handwriting, Qt::LeftButton);
    QTest::qWait(80);
    QVERIFY(window.isHandwritingVisible());

    auto *back = window.findChild<QToolButton *>(
        QStringLiteral("handwritingBackButton"));
    QVERIFY(back);
    QTest::mouseClick(back, Qt::LeftButton);
    QTest::qWait(80);
    QVERIFY(!window.isHandwritingVisible());
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Study);
    QCOMPARE(practicePage->currentQuestionId().value(), *firstQuestionId);
    QVERIFY(practicePage->session().answers.contains(*firstQuestionId));

    auto *practiceBack = window.findChild<QPushButton *>(
        QStringLiteral("practiceBackButton"));
    QVERIFY(practiceBack);
    QTest::mouseClick(practiceBack, Qt::LeftButton);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Library);
    bankButton = firstVisibleButton(window, QStringLiteral("libraryScopePracticeButton"));
    QVERIFY(bankButton);
    QTest::mouseClick(bankButton, Qt::LeftButton);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Study);
    QCOMPARE(practicePage->currentQuestionId().value(), *firstQuestionId);
    QCOMPARE(practicePage->session().answers.value(*firstQuestionId), QStringLiteral("A"));
    saveScreenshot(window, QStringLiteral("app-shell-practice-phone.png"));
    QTest::keyClick(&window, Qt::Key_Back);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Library);
    auto *pathTitle = window.findChild<QLabel *>(QStringLiteral("libraryPathTitle"));
    QVERIFY(pathTitle);
    QCOMPARE(pathTitle->text(), QStringLiteral("公开题库"));
}

void AppShellTests::practiceSaveControlsRespectSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("practice/autoSaveOnExit"), false);
    settings.setValue(QStringLiteral("practice/progressScopePolicy"),
                      QStringLiteral("separate"));
    settings.sync();

    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-practice-save-controls"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    const auto package = installedSampleBank();
    QVERIFY2(repository.replaceBank(package, &error), qPrintable(error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(40);
    auto *start = drillToLibraryLeaf(window);
    QVERIFY(start);
    QTest::mouseClick(start, Qt::LeftButton);
    auto *practicePage = window.findChild<quizapp::ui::PracticePage *>(
        QStringLiteral("practicePage"));
    QVERIFY(practicePage);
    const QUuid firstQuestion = practicePage->currentQuestionId().value();
    auto options = window.findChildren<QPushButton *>(QStringLiteral("practiceOptionButton"));
    QVERIFY(!options.isEmpty());
    QTest::mouseClick(options.constFirst(), Qt::LeftButton);
    QTest::qWait(550);
    QTest::mouseClick(
        window.findChild<QPushButton *>(QStringLiteral("practiceBackButton")),
        Qt::LeftButton);

    QSqlQuery count(database.connection());
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM practice_sessions")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 0);
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM question_answer_state")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 0);

    start = firstVisibleButton(window, QStringLiteral("libraryScopePracticeButton"));
    QVERIFY(start);
    QTest::mouseClick(start, Qt::LeftButton);
    QVERIFY(!practicePage->session().answers.contains(firstQuestion));
    options = window.findChildren<QPushButton *>(QStringLiteral("practiceOptionButton"));
    QTest::mouseClick(options.constFirst(), Qt::LeftButton);
    auto *save = window.findChild<QToolButton *>(QStringLiteral("practiceSaveButton"));
    QVERIFY(save);
    QTest::mouseClick(save, Qt::LeftButton);
    QTest::mouseClick(
        window.findChild<QPushButton *>(QStringLiteral("practiceBackButton")),
        Qt::LeftButton);

    start = firstVisibleButton(window, QStringLiteral("libraryScopePracticeButton"));
    QVERIFY(start);
    QTest::mouseClick(start, Qt::LeftButton);
    QCOMPARE(practicePage->session().answers.value(firstQuestion), QStringLiteral("A"));

    auto *reset = window.findChild<QToolButton *>(QStringLiteral("practiceResetButton"));
    QVERIFY(reset);
    bool resetConfirmed = false;
    QTimer resetConfirmationTimer;
    resetConfirmationTimer.setInterval(10);
    connect(&resetConfirmationTimer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<QMessageBox *>(widget);
            if (!dialog || !dialog->isVisible()) {
                continue;
            }
            QAbstractButton *confirm = dialog->button(QMessageBox::Yes);
            if (!confirm) {
                continue;
            }
            resetConfirmed = true;
            confirm->click();
            resetConfirmationTimer.stop();
            return;
        }
    });
    resetConfirmationTimer.start();
    QTest::mouseClick(reset, Qt::LeftButton);
    resetConfirmationTimer.stop();
    QVERIFY(resetConfirmed);
    auto *resetStatus = window.findChild<QLabel *>(QStringLiteral("practiceSaveStatus"));
    QVERIFY(resetStatus);
    QTRY_VERIFY2(
        !practicePage->session().answers.contains(firstQuestion),
        qPrintable(resetStatus->text()));
    QCOMPARE(practicePage->session().currentIndex, 0);
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM question_answer_state")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 0);

    settings.setValue(QStringLiteral("practice/autoSaveOnExit"), true);
    settings.sync();
}

void AppShellTests::savedProgressWidgetRestoresAndRespectsSizing()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("saved-progress-widget"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    const auto package = installedSampleBank();
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));

    quizapp::domain::PracticeSession session;
    session.id = QUuid::createUuid();
    session.scopeId = package.bank.id;
    session.mode = quizapp::domain::PracticeMode::Sequential;
    for (const auto &question : package.bank.questions) {
        session.questionOrder.append(question.id);
    }
    session.currentIndex = 1;
    session.answers.insert(session.questionOrder.constFirst(), QStringLiteral("A"));
    quizapp::storage::SqlitePracticeRepository practiceRepository(database.connection());
    QVERIFY2(practiceRepository.save(session, &error), qPrintable(error));
    database.connection().close();

    QSettings settings;
    settings.setValue(QStringLiteral("home/showSavedProgressEntry"), true);
    settings.setValue(QStringLiteral("home/showSavedProgressHint"), true);
    settings.setValue(QStringLiteral("home/savedProgressWidthPhone"), 360);
    settings.setValue(QStringLiteral("home/savedProgressWidthTablet"), 520);
    settings.sync();

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    QTest::qWait(30);
    auto *card = window.findChild<QFrame *>(QStringLiteral("homeSavedProgressCard"));
    auto *title = window.findChild<QLabel *>(QStringLiteral("homeSavedProgressTitle"));
    auto *summary = window.findChild<QLabel *>(QStringLiteral("homeSavedProgressSummary"));
    auto *continueButton = window.findChild<QPushButton *>(
        QStringLiteral("homeSavedProgressButton"));
    QVERIFY(card);
    QVERIFY(title);
    QVERIFY(summary);
    QVERIFY(continueButton);
    QTRY_VERIFY(card->isVisible());
    QVERIFY(title->text().contains(package.bank.title));
    QVERIFY(summary->text().contains(QStringLiteral("第 2 / 2 题")));
    QCOMPARE(card->width(), 342);
    saveScreenshot(window, QStringLiteral("app-shell-saved-progress-phone.png"));

    window.resize(1280, 800);
    QTest::qWait(20);
    QCOMPARE(card->width(), 520);
    saveScreenshot(window, QStringLiteral("app-shell-saved-progress-tablet.png"));
    QTest::mouseClick(continueButton, Qt::LeftButton);
    QTRY_COMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Study);
    auto *practicePage = window.findChild<quizapp::ui::PracticePage *>(
        QStringLiteral("practicePage"));
    QVERIFY(practicePage);
    QCOMPARE(practicePage->session().mode, quizapp::domain::PracticeMode::Sequential);
    QCOMPARE(practicePage->session().currentIndex, 1);

    window.navigateTo(quizapp::ui::AppWindow::Section::Settings);
    auto *showSaved = window.findChild<QCheckBox *>(
        QStringLiteral("showSavedProgressChoice"));
    auto *phoneWidth = window.findChild<QSlider *>(
        QStringLiteral("savedProgressPhoneWidthChoice"));
    auto *tabletWidth = window.findChild<QSlider *>(
        QStringLiteral("savedProgressTabletWidthChoice"));
    auto *save = window.findChild<QPushButton *>(QStringLiteral("saveSettingsButton"));
    QVERIFY(showSaved);
    QVERIFY(phoneWidth);
    QVERIFY(tabletWidth);
    QVERIFY(save);
    QCOMPARE(phoneWidth->minimum(), 300);
    QCOMPARE(phoneWidth->maximum(), 430);
    QCOMPARE(tabletWidth->minimum(), 440);
    QCOMPARE(tabletWidth->maximum(), 720);
    auto *practiceSettings = window.findChild<QFrame *>(
        QStringLiteral("practiceSettingsSurface"));
    QVERIFY(practiceSettings);
    const auto settingsScrolls = window.findChildren<QScrollArea *>(
        QStringLiteral("pageScroll"));
    const auto visibleSettingsScroll = std::find_if(
        settingsScrolls.cbegin(), settingsScrolls.cend(),
        [](QScrollArea *scroll) { return scroll->isVisible(); });
    QVERIFY(visibleSettingsScroll != settingsScrolls.cend());
    (*visibleSettingsScroll)->ensureWidgetVisible(practiceSettings, 0, 0);
    QTest::qWait(20);
    saveScreenshot(window, QStringLiteral("app-shell-saved-progress-settings-tablet.png"));
    phoneWidth->setValue(300);
    tabletWidth->setValue(440);
    showSaved->setChecked(false);
    QTest::mouseClick(save, Qt::LeftButton);
    window.navigateTo(quizapp::ui::AppWindow::Section::Home);
    QVERIFY(!card->isVisible());

    window.navigateTo(quizapp::ui::AppWindow::Section::Settings);
    showSaved->setChecked(true);
    QTest::mouseClick(save, Qt::LeftButton);
    window.resize(390, 844);
    window.navigateTo(quizapp::ui::AppWindow::Section::Home);
    QTRY_VERIFY(card->isVisible());
    QCOMPARE(card->width(), 300);
    QCOMPARE(settings.value(QStringLiteral("home/savedProgressWidthTablet")).toInt(), 440);

    settings.remove(QStringLiteral("home/showSavedProgressEntry"));
    settings.remove(QStringLiteral("home/showSavedProgressHint"));
    settings.remove(QStringLiteral("home/savedProgressWidthPhone"));
    settings.remove(QStringLiteral("home/savedProgressWidthTablet"));
    settings.sync();
}

void AppShellTests::practiceRendersInstalledQuestionImages()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-practice-images"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    quizapp::services::BlobStore blobStore(dataRoot.path());
    quizapp::services::BankInstallService installer;
    const auto install = installer.installJson(
        imageQuestionBankJson(),
        QStringLiteral("小易图片题库/第一章/图片题.json"),
        blobStore,
        repository);
    QVERIFY2(install.installed, qPrintable(install.error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(80);

    auto *bankButton = drillToLibraryLeaf(window);
    QVERIFY(bankButton);
    QTest::mouseClick(bankButton, Qt::LeftButton);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Study);
    QTRY_VERIFY(!window.findChildren<QLabel *>(QStringLiteral("practiceImage")).isEmpty());

    auto *reveal = window.findChild<QPushButton *>(QStringLiteral("practiceRevealButton"));
    QVERIFY(reveal);
    QTest::mouseClick(reveal, Qt::LeftButton);
    QTRY_VERIFY(window.findChildren<QLabel *>(QStringLiteral("practiceImage")).size() >= 2);
    saveScreenshot(window, QStringLiteral("app-shell-practice-image-phone.png"));
}

void AppShellTests::questionOverviewShowsStatusAndJumpsToQuestion()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-question-overview"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    const auto package = installedSampleBank();
    QVERIFY2(repository.replaceBank(package, &error), qPrintable(error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(40);
    auto *start = drillToLibraryLeaf(window);
    QVERIFY(start);
    QTest::mouseClick(start, Qt::LeftButton);

    auto *practicePage = window.findChild<quizapp::ui::PracticePage *>(
        QStringLiteral("practicePage"));
    QVERIFY(practicePage);
    const QUuid firstQuestionId = package.bank.questions.at(0).id;
    const QUuid secondQuestionId = package.bank.questions.at(1).id;
    QCOMPARE(practicePage->currentQuestionId().value(), firstQuestionId);
    auto *firstOption = firstVisibleButton(window, QStringLiteral("practiceOptionButton"));
    QVERIFY(firstOption);
    QTest::mouseClick(firstOption, Qt::LeftButton);

    auto *overview = window.findChild<QToolButton *>(
        QStringLiteral("practiceOverviewButton"));
    QVERIFY(overview);
    QTest::mouseClick(overview, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<quizapp::ui::QuestionOverviewDialog *>() != nullptr);
    auto *dialog = window.findChild<quizapp::ui::QuestionOverviewDialog *>();
    QVERIFY(dialog);
    auto *overviewClose = dialog->findChild<QToolButton *>(
        QStringLiteral("questionOverviewCloseButton"));
    QVERIFY(overviewClose);
    QVERIFY(!overviewClose->icon().isNull());
    QCOMPARE(
        dialog->findChildren<QLabel *>(QStringLiteral("questionOverviewStat")).size(), 4);
    const auto numberButtons = dialog->findChildren<QPushButton *>(
        QStringLiteral("questionOverviewNumberButton"));
    QCOMPARE(numberButtons.size(), 2);
    auto firstStatus = std::find_if(
        numberButtons.cbegin(), numberButtons.cend(), [](QPushButton *button) {
            return button->text() == QStringLiteral("1");
        });
    auto secondButton = std::find_if(
        numberButtons.cbegin(), numberButtons.cend(), [](QPushButton *button) {
            return button->text() == QStringLiteral("2");
        });
    QVERIFY(firstStatus != numberButtons.cend());
    QVERIFY(secondButton != numberButtons.cend());
    QCOMPARE((*firstStatus)->property("answerStatus").toString(), QStringLiteral("correct"));
    QCOMPARE((*secondButton)->property("answerStatus").toString(), QStringLiteral("unanswered"));
    saveScreenshot(*dialog, QStringLiteral("question-overview-phone.png"));
    dialog->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    window.resize(1280, 800);
    QTest::qWait(30);
    QTest::mouseClick(overview, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<quizapp::ui::QuestionOverviewDialog *>() != nullptr);
    dialog = window.findChild<quizapp::ui::QuestionOverviewDialog *>();
    QVERIFY(dialog);
    saveScreenshot(*dialog, QStringLiteral("question-overview-tablet.png"));
    const auto tabletButtons = dialog->findChildren<QPushButton *>(
        QStringLiteral("questionOverviewNumberButton"));
    secondButton = std::find_if(
        tabletButtons.cbegin(), tabletButtons.cend(), [](QPushButton *button) {
            return button->text() == QStringLiteral("2");
        });
    QVERIFY(secondButton != tabletButtons.cend());
    QTest::mouseClick(*secondButton, Qt::LeftButton);
    QCOMPARE(practicePage->currentQuestionId().value(), secondQuestionId);
}

void AppShellTests::wrongAnswerRequiresExplicitWrongBookAction()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-wrong-book"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    quizapp::storage::SqliteWrongBookRepository wrongBookRepository(database.connection());
    const auto package = installedSampleBank();
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));
    const QUuid questionId = package.bank.questions.constFirst().id;

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(40);
    auto *start = drillToLibraryLeaf(window);
    QVERIFY(start);
    QTest::mouseClick(start, Qt::LeftButton);
    auto *practicePage = window.findChild<quizapp::ui::PracticePage *>(
        QStringLiteral("practicePage"));
    QVERIFY(practicePage);

    auto *wrongBookAction = window.findChild<QPushButton *>(
        QStringLiteral("practiceWrongBookButton"));
    QVERIFY(wrongBookAction);
    QVERIFY(!wrongBookAction->isVisible());
    const auto options = window.findChildren<QPushButton *>(
        QStringLiteral("practiceOptionButton"));
    auto wrongOption = std::find_if(options.cbegin(), options.cend(), [](QPushButton *button) {
        return button->text().startsWith(QStringLiteral("B."));
    });
    QVERIFY(wrongOption != options.cend());
    QPushButton *wrongOptionButton = *wrongOption;
    QTest::mouseClick(*wrongOption, Qt::LeftButton);
    QTRY_VERIFY(wrongBookAction->isVisible());
    const auto optionsAfterSelection = window.findChildren<QPushButton *>(
        QStringLiteral("practiceOptionButton"));
    QVERIFY(optionsAfterSelection.contains(wrongOptionButton));
    QCOMPARE(wrongOptionButton->property("answerState").toString(), QStringLiteral("wrong"));
    QCOMPARE(wrongBookAction->text(), QStringLiteral("加入错题集"));

    bool contained = false;
    QVERIFY2(wrongBookRepository.contains(questionId, &contained, &error), qPrintable(error));
    QVERIFY(!contained);
    QTest::qWait(30);
    saveScreenshot(window, QStringLiteral("practice-wrong-book-prompt-phone.png"));
    QTest::mouseClick(wrongBookAction, Qt::LeftButton);
    QTRY_COMPARE(wrongBookAction->text(), QStringLiteral("移出错题集"));
    QVERIFY2(wrongBookRepository.contains(questionId, &contained, &error), qPrintable(error));
    QVERIFY(contained);

    auto *back = window.findChild<QPushButton *>(QStringLiteral("practiceBackButton"));
    QVERIFY(back);
    QTest::mouseClick(back, Qt::LeftButton);
    auto *wrongBookMode = firstVisibleButton(window, QStringLiteral("libraryWrongBookButton"));
    QVERIFY(wrongBookMode);
    QVERIFY(wrongBookMode->isEnabled());
    QTest::mouseClick(wrongBookMode, Qt::LeftButton);
    QCOMPARE(practicePage->session().mode, quizapp::domain::PracticeMode::WrongBook);
    QCOMPARE(practicePage->currentQuestionId().value(), questionId);
    QTRY_COMPARE(wrongBookAction->text(), QStringLiteral("移出错题集"));
    QTest::mouseClick(wrongBookAction, Qt::LeftButton);
    QVERIFY2(wrongBookRepository.contains(questionId, &contained, &error), qPrintable(error));
    QVERIFY(!contained);
}

void AppShellTests::explicitReviewFlowSchedulesAndReturnsToStudyHub()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-review-flow"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository questionRepository(database.connection());
    quizapp::storage::SqliteReviewRepository reviewRepository(database.connection());
    const auto package = installedSampleBank();
    QVERIFY2(questionRepository.replaceBank(package, &error), qPrintable(error));
    const QUuid questionId = package.bank.questions.constFirst().id;

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(40);
    auto *start = drillToLibraryLeaf(window);
    QVERIFY(start);
    QTest::mouseClick(start, Qt::LeftButton);

    auto *reviewAction = window.findChild<QPushButton *>(
        QStringLiteral("practiceReviewButton"));
    QVERIFY(reviewAction);
    QVERIFY(reviewAction->isVisible());
    QCOMPARE(reviewAction->text(), QStringLiteral("加入复习"));
    QTest::mouseClick(reviewAction, Qt::LeftButton);
    QTRY_COMPARE(reviewAction->text(), QStringLiteral("移出复习"));
    QVERIFY(reviewRepository.find(questionId, &error).has_value());

    auto *studyNav = window.findChild<QToolButton *>(QStringLiteral("bottomNav2"));
    QVERIFY(studyNav);
    QTest::mouseClick(studyNav, Qt::LeftButton);
    auto *studyHub = window.findChild<quizapp::ui::StudyHubPage *>(
        QStringLiteral("studyHubPage"));
    QVERIFY(studyHub);
    QTRY_VERIFY(studyHub->isVisible());
    auto *startReview = window.findChild<QPushButton *>(
        QStringLiteral("studyStartReviewButton"));
    QVERIFY(startReview);
    QVERIFY(startReview->isEnabled());
    QTest::mouseClick(startReview, Qt::LeftButton);

    auto *reviewPage = window.findChild<quizapp::ui::ReviewPage *>(
        QStringLiteral("reviewPage"));
    QVERIFY(reviewPage);
    QTRY_VERIFY(reviewPage->isVisible());
    QCOMPARE(reviewPage->currentQuestionId().value(), questionId);
    auto *good = window.findChild<QPushButton *>(QStringLiteral("reviewRating3"));
    auto *reveal = window.findChild<QPushButton *>(QStringLiteral("reviewRevealButton"));
    QVERIFY(good);
    QVERIFY(reveal);
    QVERIFY(!good->isEnabled());
    QTest::mouseClick(reveal, Qt::LeftButton);
    QTRY_VERIFY(good->isEnabled());
    auto *reviewBack = window.findChild<QPushButton *>(QStringLiteral("reviewBackButton"));
    auto *reviewProgress = window.findChild<QLabel *>(QStringLiteral("reviewProgress"));
    QVERIFY(reviewBack);
    QVERIFY(reviewProgress);
    QVERIFY(reviewProgress->geometry().top() > reviewBack->geometry().top());
    saveScreenshot(window, QStringLiteral("review-flow-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(50);
    QCOMPARE(reviewProgress->geometry().top(), reviewBack->geometry().top());
    saveScreenshot(window, QStringLiteral("review-flow-tablet.png"));
    QTest::mouseClick(good, Qt::LeftButton);
    QTRY_VERIFY(studyHub->isVisible());
    const auto updated = reviewRepository.find(questionId, &error);
    QVERIFY2(updated.has_value(), qPrintable(error));
    QCOMPARE(updated->reviewCount, 1);
    QVERIFY(updated->dueAt > QDateTime::currentDateTimeUtc());
    QTest::qWait(560);
    saveScreenshot(window, QStringLiteral("study-hub-tablet.png"));
    window.resize(390, 844);
    QTest::qWait(80);
    saveScreenshot(window, QStringLiteral("study-hub-phone.png"));

    QTest::keyClick(&window, Qt::Key_Back);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Home);
}

void AppShellTests::answerTableBrowsesAllAnswersAndReturnsFromDetailAndHandwriting()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-answer-table"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    const auto package = installedSampleBank();
    QVERIFY2(repository.replaceBank(package, &error), qPrintable(error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(40);
    QVERIFY(drillToLibraryLeaf(window));
    auto *answerMode = firstVisibleModeButton(
        window, quizapp::domain::PracticeMode::AnswerLookup);
    QVERIFY(answerMode);
    QTest::mouseClick(answerMode, Qt::LeftButton);
    QVERIFY(window.isAnswerTableVisible());

    auto *answerTable = window.findChild<quizapp::ui::AnswerTablePage *>(
        QStringLiteral("answerTablePage"));
    auto *table = window.findChild<QTableView *>(QStringLiteral("answerTableView"));
    auto *answerTableBack = window.findChild<QPushButton *>(
        QStringLiteral("answerTableBackButton"));
    auto *answerTableDetail = window.findChild<QPushButton *>(
        QStringLiteral("answerTableDetailButton"));
    auto *answerTableHandwriting = window.findChild<QPushButton *>(
        QStringLiteral("answerTableHandwritingButton"));
    QVERIFY(answerTable);
    QVERIFY(table);
    QVERIFY(answerTableBack);
    QVERIFY(answerTableDetail);
    QVERIFY(answerTableHandwriting);
    QVERIFY(answerTableDetail->geometry().top() > answerTableBack->geometry().top());
    QCOMPARE(table->model()->rowCount(), 2);
    QCOMPARE(table->model()->data(table->model()->index(0, 3)).toString(), QStringLiteral("A"));
    QCOMPARE(table->model()->data(table->model()->index(1, 3)).toString(), QStringLiteral("B"));
    saveScreenshot(window, QStringLiteral("answer-table-phone.png"));

    table->selectRow(1);
    table->setCurrentIndex(table->model()->index(1, 0));
    QTRY_COMPARE(answerTable->currentQuestionIndex(), 1);
    auto *practicePage = window.findChild<quizapp::ui::PracticePage *>(
        QStringLiteral("practicePage"));
    QVERIFY(practicePage);
    QTRY_COMPARE(practicePage->session().currentIndex, 1);

    auto *detail = window.findChild<QPushButton *>(
        QStringLiteral("answerTableDetailButton"));
    QVERIFY(detail);
    QTest::mouseClick(detail, Qt::LeftButton);
    QVERIFY(!window.isAnswerTableVisible());
    QCOMPARE(practicePage->currentQuestionId().value(), package.bank.questions.at(1).id);
    auto *answerSurface = window.findChild<QFrame *>(QStringLiteral("practiceAnswerSurface"));
    QVERIFY(answerSurface);
    QVERIFY(answerSurface->isVisible());

    auto *practiceBack = window.findChild<QPushButton *>(
        QStringLiteral("practiceBackButton"));
    QVERIFY(practiceBack);
    QTest::mouseClick(practiceBack, Qt::LeftButton);
    QVERIFY(window.isAnswerTableVisible());
    QCOMPARE(answerTable->currentQuestionIndex(), 1);

    auto *handwriting = window.findChild<QPushButton *>(
        QStringLiteral("answerTableHandwritingButton"));
    QVERIFY(handwriting);
    QTest::mouseClick(handwriting, Qt::LeftButton);
    QTRY_VERIFY(window.isHandwritingVisible());
    auto *handwritingBack = window.findChild<QToolButton *>(
        QStringLiteral("handwritingBackButton"));
    QVERIFY(handwritingBack);
    QTest::mouseClick(handwritingBack, Qt::LeftButton);
    QTRY_VERIFY(!window.isHandwritingVisible());
    QVERIFY(window.isAnswerTableVisible());
    QCOMPARE(answerTable->currentQuestionIndex(), 1);

    window.resize(1280, 800);
    QTest::qWait(40);
    QCOMPARE(answerTableDetail->geometry().top(), answerTableBack->geometry().top());
    saveScreenshot(window, QStringLiteral("answer-table-tablet.png"));
    QTest::keyClick(&window, Qt::Key_Back);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Library);
    answerMode = firstVisibleModeButton(
        window, quizapp::domain::PracticeMode::AnswerLookup);
    QVERIFY(answerMode);
    QTest::mouseClick(answerMode, Qt::LeftButton);
    QVERIFY(window.isAnswerTableVisible());
    QCOMPARE(answerTable->currentQuestionIndex(), 1);
}

void AppShellTests::realXiaoyiBundleHierarchyScreenshots()
{
    const QString bundleRoot = qEnvironmentVariable("QUIZAPP_REAL_BUNDLE_DIR");
    if (bundleRoot.isEmpty()) {
        QSKIP("QUIZAPP_REAL_BUNDLE_DIR is not set");
    }
    const QString sourceDatabase = QDir(bundleRoot).filePath(QStringLiteral("quizapp.sqlite"));
    QVERIFY2(QFileInfo::exists(sourceDatabase), qPrintable(sourceDatabase));
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    QVERIFY2(QFile::copy(sourceDatabase, databasePath), qPrintable(sourceDatabase));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(80);
    auto *pathTitle = window.findChild<QLabel *>(QStringLiteral("libraryPathTitle"));
    auto *pathSummary = window.findChild<QLabel *>(QStringLiteral("libraryPathSummary"));
    QVERIFY(pathTitle);
    QVERIFY(pathSummary);
    QVERIFY(!window.findChild<QLabel *>(QStringLiteral("storageLocationText")));
    QCOMPARE(pathTitle->text(), QStringLiteral("全部题库"));
    QVERIFY(pathSummary->text().contains(QStringLiteral("870 道题")));
    saveScreenshot(window, QStringLiteral("xiaoyi-library-root-phone.png"));

    auto *rootAnswerMode = firstVisibleModeButton(
        window, quizapp::domain::PracticeMode::AnswerLookup);
    QVERIFY(rootAnswerMode);
    QElapsedTimer answerTableTimer;
    answerTableTimer.start();
    QTest::mouseClick(rootAnswerMode, Qt::LeftButton);
    QTRY_VERIFY(window.isAnswerTableVisible());
    auto *realAnswerTable = window.findChild<QTableView *>(QStringLiteral("answerTableView"));
    QVERIFY(realAnswerTable);
    QCOMPARE(realAnswerTable->model()->rowCount(), 870);
    QVERIFY2(answerTableTimer.elapsed() < 1500,
             qPrintable(QStringLiteral("870题答案表打开耗时 %1ms").arg(answerTableTimer.elapsed())));
    saveScreenshot(window, QStringLiteral("xiaoyi-answer-table-870-phone.png"));
    auto *answerTableBack = window.findChild<QPushButton *>(
        QStringLiteral("answerTableBackButton"));
    QVERIFY(answerTableBack);
    QTest::mouseClick(answerTableBack, Qt::LeftButton);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Library);

    auto *node = firstVisibleButton(window, QStringLiteral("libraryPathNodeButton"));
    QVERIFY(node);
    QVERIFY(node->property("libraryTitle").toString().contains(QStringLiteral("考研数学")));
    QTest::mouseClick(node, Qt::LeftButton);
    QTest::qWait(30);
    QCOMPARE(pathTitle->text(), QStringLiteral("考研数学"));
    const auto bankNodes = window.findChildren<QPushButton *>(
        QStringLiteral("libraryPathNodeButton"));
    QVERIFY(bankNodes.size() >= 2);
    saveScreenshot(window, QStringLiteral("xiaoyi-library-banks-phone.png"));

    node = firstVisibleButton(window, QStringLiteral("libraryPathNodeButton"));
    QVERIFY(node);
    QTest::mouseClick(node, Qt::LeftButton);
    QTest::qWait(30);
    const auto chapterNodes = window.findChildren<QPushButton *>(
        QStringLiteral("libraryPathNodeButton"));
    int visibleChapterCount = 0;
    for (QPushButton *chapter : chapterNodes) {
        if (chapter->isVisible()) {
            ++visibleChapterCount;
        }
    }
    QVERIFY(visibleChapterCount >= 8);
    node = firstVisibleButton(window, QStringLiteral("libraryPathNodeButton"));
    QVERIFY(node);
    QVERIFY(node->property("libraryTitle").toString().contains(QStringLiteral("第1讲")));
    const auto pageScrolls = window.findChildren<QScrollArea *>(QStringLiteral("pageScroll"));
    const auto visibleScroll = std::find_if(
        pageScrolls.cbegin(), pageScrolls.cend(), [](QScrollArea *scroll) {
            return scroll->isVisible();
    });
    QVERIFY(visibleScroll != pageScrolls.cend());
    QCOMPARE((*visibleScroll)->verticalScrollBar()->value(), 0);
    QCOMPARE((*visibleScroll)->horizontalScrollBar()->maximum(), 0);
    saveScreenshot(window, QStringLiteral("xiaoyi-library-chapters-phone.png"));

    window.resize(1280, 800);
    QTest::qWait(60);
    saveScreenshot(window, QStringLiteral("xiaoyi-library-chapters-tablet.png"));
}

void AppShellTests::systemBackReturnsToHome()
{
    quizapp::ui::AppWindow window;
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Settings);
    QTest::keyClick(&window, Qt::Key_Back);
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Home);
}

void AppShellTests::settingsPersistAndApply()
{
    quizapp::ui::AppWindow window;
    window.resize(1180, 760);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Settings);

    auto *theme = window.findChild<QComboBox *>(QStringLiteral("themeChoice"));
    auto *palette = window.findChild<QComboBox *>(QStringLiteral("paletteChoice"));
    auto *reduceMotion = window.findChild<QCheckBox *>(
        QStringLiteral("reduceMotionChoice"));
    auto *save = window.findChild<QPushButton *>(QStringLiteral("saveSettingsButton"));
    auto *cornerRadius = window.findChild<QSlider *>(QStringLiteral("cornerRadiusChoice"));
    auto *primaryColor = window.findChild<QPushButton *>(
        QStringLiteral("primaryColorButton"));
    auto *primaryColorValue = window.findChild<QLabel *>(
        QStringLiteral("primaryColorValue"));
    auto *resetPrimaryColor = window.findChild<QPushButton *>(
        QStringLiteral("resetPrimaryColorButton"));
    auto *autoSave = window.findChild<QCheckBox *>(QStringLiteral("autoSaveOnExitChoice"));
    auto *mergeProgress = window.findChild<QCheckBox *>(
        QStringLiteral("mergePracticeProgressChoice"));
    auto *reshuffle = window.findChild<QCheckBox *>(
        QStringLiteral("randomReshuffleChoice"));
    auto *rememberReview = window.findChild<QCheckBox *>(
        QStringLiteral("rememberReviewPositionChoice"));
    auto *rememberAnswer = window.findChild<QCheckBox *>(
        QStringLiteral("rememberAnswerLookupPositionChoice"));
    auto *preview = window.findChild<quizapp::ui::ThemePreview *>(
        QStringLiteral("themePreview"));
    auto *surface = window.findChild<QFrame *>(QStringLiteral("settingsSurface"));
    auto *bankUpdateSurface = window.findChild<QFrame *>(
        QStringLiteral("bankUpdateSettingsSurface"));
    auto *checkBankUpdates = window.findChild<QPushButton *>(
        QStringLiteral("checkBankUpdatesButton"));
    auto *autoBankUpdates = window.findChild<QCheckBox *>(
        QStringLiteral("autoBankUpdateCheckChoice"));
    auto *autoAnnouncements = window.findChild<QCheckBox *>(
        QStringLiteral("autoAnnouncementCheckChoice"));
    auto *checkAnnouncements = window.findChild<QPushButton *>(
        QStringLiteral("checkAnnouncementsButton"));
    auto *autoAppUpdates = window.findChild<QCheckBox *>(
        QStringLiteral("autoAppUpdateCheckChoice"));
    auto *checkAppUpdates = window.findChild<QPushButton *>(
        QStringLiteral("checkAppUpdatesButton"));
    QVERIFY(theme);
    QVERIFY(palette);
    QVERIFY(dynamic_cast<quizapp::ui::ChoiceComboBox *>(theme));
    QVERIFY(dynamic_cast<quizapp::ui::ChoiceComboBox *>(palette));
    QCOMPARE(theme->view()->objectName(), QStringLiteral("choiceComboPopup"));
    QCOMPARE(palette->count(), 7);
    QVERIFY(reduceMotion);
    QVERIFY(save);
    QVERIFY(cornerRadius);
    QVERIFY(primaryColor);
    QVERIFY(primaryColorValue);
    QVERIFY(resetPrimaryColor);
    QVERIFY(autoSave);
    QVERIFY(mergeProgress);
    QVERIFY(reshuffle);
    QVERIFY(rememberReview);
    QVERIFY(rememberAnswer);
    QVERIFY(preview);
    QVERIFY(surface);
    QVERIFY(bankUpdateSurface);
    QVERIFY(checkBankUpdates);
    QVERIFY(autoBankUpdates);
    QVERIFY(autoAnnouncements);
    QVERIFY(checkAnnouncements);
    QVERIFY(autoAppUpdates);
    QVERIFY(checkAppUpdates);
    QVERIFY(checkBankUpdates->isEnabled());
    QVERIFY(checkAppUpdates->isEnabled());
    QVERIFY(surface->width() <= 760);

    theme->setCurrentIndex(theme->findData(QStringLiteral("light")));
    palette->setCurrentIndex(palette->findData(QStringLiteral("forest")));
    QVERIFY(window.styleSheet().contains(QStringLiteral("QComboBox::drop-down")));
    QVERIFY(window.styleSheet().contains(
        QStringLiteral(":/quizapp/icons/combo_chevron_dark.svg")));
    window.resize(390, 844);
    QTest::qWait(40);
    saveScreenshot(window, QStringLiteral("app-shell-settings-combobox-phone.png"));
    theme->showPopup();
    QTRY_VERIFY(theme->view()->isVisible());
    saveScreenshot(*theme->view()->window(),
                   QStringLiteral("app-shell-settings-combobox-popup-phone.png"));
    theme->hidePopup();
    window.resize(1280, 800);
    QTest::qWait(40);
    saveScreenshot(window, QStringLiteral("app-shell-settings-combobox-tablet.png"));
    theme->showPopup();
    QTRY_VERIFY(theme->view()->isVisible());
    saveScreenshot(*theme->view()->window(),
                   QStringLiteral("app-shell-settings-combobox-popup-tablet.png"));
    theme->hidePopup();
    window.resize(1180, 760);
    QTest::qWait(30);
    QTimer::singleShot(0, &window, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<QColorDialog *>(widget);
            if (!dialog || !dialog->isVisible()) {
                continue;
            }
            dialog->setCurrentColor(QColor(QStringLiteral("#0057b8")));
            dialog->accept();
            return;
        }
    });
    QTest::mouseClick(primaryColor, Qt::LeftButton);
    QTRY_VERIFY(primaryColorValue->text().contains(QStringLiteral("#0057B8")));
    cornerRadius->setValue(12);
    autoSave->setChecked(false);
    mergeProgress->setChecked(true);
    reshuffle->setChecked(false);
    rememberReview->setChecked(false);
    rememberAnswer->setChecked(false);
    autoBankUpdates->setChecked(false);
    autoAnnouncements->setChecked(false);
    autoAppUpdates->setChecked(false);
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(QSettings().value(QStringLiteral("ui/primary")).toString(),
             QStringLiteral("#0057b8"));
    QVERIFY(window.styleSheet().contains(QStringLiteral("#0057b8")));
    saveScreenshot(window, QStringLiteral("app-shell-settings-custom-primary.png"));
    QTest::mouseClick(resetPrimaryColor, Qt::LeftButton);
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(QSettings().value(QStringLiteral("ui/primary")).toString(),
             QStringLiteral("#1f8f62"));
    QCOMPARE(QSettings().value(QStringLiteral("ui/cornerRadius")).toInt(), 12);
    QVERIFY(window.styleSheet().contains(QStringLiteral("border-radius: 12px")));
    QVERIFY(!QSettings().value(QStringLiteral("practice/autoSaveOnExit")).toBool());
    QCOMPARE(
        QSettings().value(QStringLiteral("practice/progressScopePolicy")).toString(),
        QStringLiteral("merged"));
    QVERIFY(!QSettings().value(
        QStringLiteral("practice/randomReshuffleOnReset")).toBool());
    QVERIFY(!QSettings().value(QStringLiteral("view/rememberReviewPosition")).toBool());
    QVERIFY(!QSettings().value(
        QStringLiteral("view/rememberAnswerLookupPosition")).toBool());
    QVERIFY(!QSettings().value(QStringLiteral("updates/autoBankCheck")).toBool());
    QVERIFY(!QSettings().value(
        QStringLiteral("updates/autoAnnouncementCheck")).toBool());
    QVERIFY(!QSettings().value(QStringLiteral("updates/autoAppCheck")).toBool());
    window.resize(390, 844);
    QTest::qWait(40);
    const auto settingsScrolls = window.findChildren<QScrollArea *>(
        QStringLiteral("pageScroll"));
    const auto visibleSettingsScroll = std::find_if(
        settingsScrolls.cbegin(), settingsScrolls.cend(), [](QScrollArea *scroll) {
            return scroll->isVisible();
        });
    QVERIFY(visibleSettingsScroll != settingsScrolls.cend());
    (*visibleSettingsScroll)->verticalScrollBar()->setValue(
        (*visibleSettingsScroll)->verticalScrollBar()->maximum());
    saveScreenshot(window, QStringLiteral("app-shell-settings-practice-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(40);
    saveScreenshot(window, QStringLiteral("app-shell-settings-practice-tablet.png"));
    window.resize(1180, 760);
    (*visibleSettingsScroll)->verticalScrollBar()->setValue(0);
    const QString classicLightStyle = window.styleSheet();

    for (const quizapp::ui::ThemePalette &preset
         : quizapp::ui::ThemePalettes::legacyPresets()) {
        palette->setCurrentIndex(palette->findData(preset.id));
        QCOMPARE(preview->paletteId(), preset.id);
        QTest::mouseClick(save, Qt::LeftButton);
        QVERIFY2(
            window.styleSheet().contains(preset.primary.name()),
            qPrintable(preset.name));
        QVERIFY(window.styleSheet().contains(
            QStringLiteral(":/quizapp/icons/combo_chevron_dark.svg")));
        QVERIFY(window.styleSheet().contains(
            QStringLiteral(":/quizapp/icons/tree_chevron_right_dark.svg")));
    }

    palette->setCurrentIndex(palette->findData(QStringLiteral("endfield")));
    QCOMPARE(preview->paletteId(), QStringLiteral("endfield"));
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(
        QSettings().value(QStringLiteral("ui/palette")).toString(),
        QStringLiteral("endfield"));
    QVERIFY(window.styleSheet().contains(QStringLiteral("#fdfc00")));
    QVERIFY(window.styleSheet().contains(
        QStringLiteral(":/quizapp/icons/combo_chevron_endfield.svg")));
    QVERIFY(window.styleSheet().contains(
        QStringLiteral(":/quizapp/icons/tree_chevron_right_endfield.svg")));
    QVERIFY(!window.styleSheet().contains(QStringLiteral("#1d9367")));
    window.resize(390, 844);
    saveScreenshot(window, QStringLiteral("app-shell-settings-endfield-phone.png"));
    window.navigateTo(quizapp::ui::AppWindow::Section::Home);
    saveScreenshot(window, QStringLiteral("app-shell-home-endfield-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(40);
    saveScreenshot(window, QStringLiteral("app-shell-home-endfield-tablet.png"));

    quizapp::ui::AppWindow endfieldRestored;
    auto *endfieldRestoredTheme = endfieldRestored.findChild<QComboBox *>(
        QStringLiteral("themeChoice"));
    auto *endfieldRestoredPalette = endfieldRestored.findChild<QComboBox *>(
        QStringLiteral("paletteChoice"));
    auto *endfieldRestoredRadius = endfieldRestored.findChild<QSlider *>(
        QStringLiteral("cornerRadiusChoice"));
    auto *endfieldRestoredAutoSave = endfieldRestored.findChild<QCheckBox *>(
        QStringLiteral("autoSaveOnExitChoice"));
    QVERIFY(endfieldRestoredTheme);
    QVERIFY(endfieldRestoredPalette);
    QVERIFY(endfieldRestoredRadius);
    QVERIFY(endfieldRestoredAutoSave);
    QCOMPARE(
        endfieldRestoredPalette->currentData().toString(), QStringLiteral("endfield"));
    QVERIFY(endfieldRestored.styleSheet().contains(QStringLiteral("#fdfc00")));
    QCOMPARE(endfieldRestoredRadius->value(), 12);
    QVERIFY(!endfieldRestoredAutoSave->isChecked());

    window.navigateTo(quizapp::ui::AppWindow::Section::Settings);
    theme->setCurrentIndex(theme->findData(QStringLiteral("light")));
    palette->setCurrentIndex(palette->findData(QStringLiteral("forest")));
    autoSave->setChecked(true);
    mergeProgress->setChecked(false);
    reshuffle->setChecked(true);
    rememberReview->setChecked(true);
    rememberAnswer->setChecked(true);
    autoBankUpdates->setChecked(true);
    autoAnnouncements->setChecked(true);
    autoAppUpdates->setChecked(true);
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(window.styleSheet(), classicLightStyle);

    theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
    reduceMotion->setChecked(true);
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(QSettings().value(QStringLiteral("ui/theme")).toString(), QStringLiteral("dark"));
    QVERIFY(QSettings().value(QStringLiteral("ui/reduceMotion")).toBool());
    QVERIFY(window.reduceMotion());
    QVERIFY(window.styleSheet().contains(
        QStringLiteral(":/quizapp/icons/combo_chevron_light.svg")));
    QVERIFY(window.styleSheet().contains(
        QStringLiteral(":/quizapp/icons/tree_chevron_right_light.svg")));
    saveScreenshot(window, QStringLiteral("app-shell-settings-dark.png"));

    quizapp::ui::AppWindow restored;
    auto *restoredTheme = restored.findChild<QComboBox *>(QStringLiteral("themeChoice"));
    auto *restoredRadius = restored.findChild<QSlider *>(QStringLiteral("cornerRadiusChoice"));
    QVERIFY(restoredTheme);
    QVERIFY(restoredRadius);
    QCOMPARE(restoredTheme->currentData().toString(), QStringLiteral("dark"));
    QVERIFY(restored.reduceMotion());
    QCOMPARE(restoredRadius->value(), 12);
}

void AppShellTests::endfieldThemeCoversPracticeAndStudy()
{
    QSettings settings;
    const QVariant previousTheme = settings.value(QStringLiteral("ui/theme"));
    const QVariant previousPalette = settings.value(QStringLiteral("ui/palette"));
    settings.setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    settings.setValue(QStringLiteral("ui/palette"), QStringLiteral("endfield"));

    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    const QString databasePath = QDir(dataRoot.path()).filePath(QStringLiteral("quizapp.sqlite"));
    quizapp::storage::Database database(QStringLiteral("ui-endfield-theme"));
    QString error;
    QVERIFY2(database.open(databasePath, &error), qPrintable(error));
    QVERIFY2(database.migrate(&error), qPrintable(error));
    quizapp::storage::SqliteQuestionRepository repository(database.connection());
    QVERIFY2(repository.replaceBank(installedSampleBank(), &error), qPrintable(error));

    quizapp::ui::AppWindow window(databasePath, dataRoot.path());
    window.resize(390, 844);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Library);
    QTest::qWait(50);
    auto *libraryNav = window.findChild<QToolButton *>(QStringLiteral("bottomNav1"));
    QVERIFY(libraryNav);
    const QPixmap selectedLibraryIcon = libraryNav->icon().pixmap(
        QSize(24, 24), QIcon::Normal, QIcon::On);
    QVERIFY(pixmapContainsColor(selectedLibraryIcon, QColor(QStringLiteral("#fdfc00"))));
    QVERIFY(!pixmapContainsColor(selectedLibraryIcon, QColor(QStringLiteral("#1d9367"))));
    saveScreenshot(window, QStringLiteral("app-shell-library-endfield-phone.png"));
    auto *start = drillToLibraryLeaf(window);
    QVERIFY(start);
    QTest::mouseClick(start, Qt::LeftButton);
    QTRY_COMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Study);
    saveScreenshot(window, QStringLiteral("app-shell-practice-endfield-phone.png"));
    window.resize(1280, 800);
    QTest::qWait(50);
    auto *practiceBack = window.findChild<QPushButton *>(QStringLiteral("practiceBackButton"));
    auto *practiceHeaderActions = window.findChild<QWidget *>(
        QStringLiteral("practiceHeaderActions"));
    QVERIFY(practiceBack);
    QVERIFY(practiceHeaderActions);
    QCOMPARE(practiceHeaderActions->geometry().top(), practiceBack->geometry().top());
    saveScreenshot(window, QStringLiteral("app-shell-practice-endfield-tablet.png"));

    auto *study = window.findChild<QToolButton *>(QStringLiteral("railNav2"));
    QVERIFY(study);
    QTest::mouseClick(study, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<quizapp::ui::StudyHubPage *>(
        QStringLiteral("studyHubPage"))->isVisible());
    saveScreenshot(window, QStringLiteral("study-hub-endfield-tablet.png"));
    window.resize(390, 844);
    QTest::qWait(50);
    saveScreenshot(window, QStringLiteral("study-hub-endfield-phone.png"));

    if (previousTheme.isValid()) {
        settings.setValue(QStringLiteral("ui/theme"), previousTheme);
    } else {
        settings.remove(QStringLiteral("ui/theme"));
    }
    if (previousPalette.isValid()) {
        settings.setValue(QStringLiteral("ui/palette"), previousPalette);
    } else {
        settings.remove(QStringLiteral("ui/palette"));
    }
}

void AppShellTests::handwritingPagePersistsQuestionNoteAndReturnsContext()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    quizapp::ui::AppWindow window(QString(), dataRoot.path());
    window.resize(1280, 800);
    window.show();
    window.navigateTo(quizapp::ui::AppWindow::Section::Study);

    quizapp::domain::NotebookLaunchContext context;
    context.sessionId = QUuid::createUuid();
    context.questionId = QUuid::createUuid();
    context.questionIndex = 7;
    context.practiceMode = quizapp::domain::PracticeMode::Random;
    context.practiceViewport.insert(QStringLiteral("scrollY"), 320);

    QSignalSpy returnedSpy(
        &window, &quizapp::ui::AppWindow::handwritingReturned);
    window.openHandwriting(context);
    QTest::qWait(80);
    QVERIFY(window.isHandwritingVisible());

    auto *pagePanel = window.findChild<QWidget *>(QStringLiteral("handwritingPagePanel"));
    auto *mobilePageBar = window.findChild<QWidget *>(QStringLiteral("handwritingMobilePageBar"));
    auto *pageLabel = window.findChild<QLabel *>(QStringLiteral("handwritingPageLabel"));
    auto *addPage = window.findChild<QPushButton *>(QStringLiteral("handwritingAddPageButton"));
    QVERIFY(pagePanel);
    QVERIFY(mobilePageBar);
    QVERIFY(pageLabel);
    QVERIFY(addPage);
    QVERIFY(pagePanel->isVisible());
    QVERIFY(!mobilePageBar->isVisible());
    QCOMPARE(pageLabel->text(), QStringLiteral("第 1 / 1 页"));
    QTest::mouseClick(addPage, Qt::LeftButton);
    QTRY_COMPARE(pageLabel->text(), QStringLiteral("第 2 / 2 页"));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("handwritingPageButton1")));

    auto *viewport = window.findChild<DocumentViewport *>(
        QStringLiteral("questionDocumentViewport"));
    QVERIFY(viewport);
    viewport->setZoomLevel(1.25);
    viewport->setPanOffset(QPointF(42.0, 84.0));
    const qreal savedZoom = viewport->zoomLevel();
    const QPointF savedPan = viewport->panOffset();

    auto *back = window.findChild<QToolButton *>(
        QStringLiteral("handwritingBackButton"));
    QVERIFY(back);
    QTest::mouseClick(back, Qt::LeftButton);
    QTRY_COMPARE(returnedSpy.count(), 1);
    QVERIFY(!window.isHandwritingVisible());
    QCOMPARE(window.currentSection(), quizapp::ui::AppWindow::Section::Study);

    const quizapp::domain::NotebookLaunchContext restored =
        window.lastRestoredPracticeContext();
    QCOMPARE(restored.sessionId, context.sessionId);
    QCOMPARE(restored.questionId, context.questionId);
    QCOMPARE(restored.questionIndex, context.questionIndex);
    QCOMPARE(restored.practiceMode, context.practiceMode);
    QCOMPARE(restored.practiceViewport, context.practiceViewport);

    const QString bundlePath = window.currentHandwritingNotePath();
    QVERIFY(QFileInfo::exists(QDir(bundlePath).filePath(QStringLiteral("document.json"))));
    const QString key = context.questionId.toString(QUuid::WithoutBraces);
    const QString statePath = QDir(dataRoot.path()).filePath(
        QStringLiteral("notes/questions/%1.viewport.json").arg(key));
    QFile stateFile(statePath);
    QVERIFY(stateFile.open(QIODevice::ReadOnly));
    const QJsonObject state = QJsonDocument::fromJson(stateFile.readAll()).object();
    QCOMPARE(state.value(QStringLiteral("questionId")).toString(), key);
    QCOMPARE(state.value(QStringLiteral("page")).toInt(), viewport->currentPageIndex());
    QVERIFY(qAbs(state.value(QStringLiteral("zoom")).toDouble() - savedZoom) < 0.001);

    window.openHandwriting(context);
    QTest::qWait(80);
    QVERIFY(window.isHandwritingVisible());
    QVERIFY(qAbs(viewport->zoomLevel() - savedZoom) < 0.001);
    QCOMPARE(viewport->panOffset(), savedPan);
    viewport->zoomToFit();
    QTest::qWait(30);
    saveScreenshot(window, QStringLiteral("app-shell-handwriting-tablet.png"));

    window.resize(390, 844);
    QTest::qWait(80);
    QVERIFY(!pagePanel->isVisible());
    QVERIFY(mobilePageBar->isVisible());
    saveScreenshot(window, QStringLiteral("app-shell-handwriting-phone.png"));
}

void AppShellTests::saveScreenshot(QWidget &widget, const QString &name)
{
    const QString outputRoot = qEnvironmentVariable("QUIZAPP_TEST_OUTPUT_DIR");
    if (outputRoot.isEmpty()) {
        return;
    }
    QVERIFY(QDir().mkpath(outputRoot));
    QVERIFY(widget.grab().save(QDir(outputRoot).filePath(name)));
}

QTEST_MAIN(AppShellTests)
#include "tst_app_shell.moc"
