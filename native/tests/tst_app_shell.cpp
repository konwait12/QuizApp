#include "domain/QuestionIdentity.h"
#include "services/BankInstallService.h"
#include "services/BlobStore.h"
#include "storage/Database.h"
#include "storage/SqliteQuestionRepository.h"
#include "storage/SqliteReviewRepository.h"
#include "storage/SqliteWrongBookRepository.h"
#include "core/DocumentViewport.h"
#include "ui/AppWindow.h"
#include "ui/AnswerTablePage.h"
#include "ui/PracticePage.h"
#include "ui/QuestionOverviewDialog.h"
#include "ui/ReviewPage.h"
#include "ui/StudyHubPage.h"
#include "ui/ThemePreview.h"
#include "ui/ThemePalette.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRawFont>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

class AppShellTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void applicationFontSupportsChineseAndIconsLoad();
    void navigationAdaptsToPhoneAndTablet();
    void libraryStatsReflectInstalledQuestions();
    void startPracticeFromInstalledBankAndOpenHandwriting();
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
    auto *preview = window.findChild<quizapp::ui::ThemePreview *>(
        QStringLiteral("themePreview"));
    auto *surface = window.findChild<QFrame *>(QStringLiteral("settingsSurface"));
    QVERIFY(theme);
    QVERIFY(palette);
    QCOMPARE(palette->count(), 7);
    QVERIFY(reduceMotion);
    QVERIFY(save);
    QVERIFY(cornerRadius);
    QVERIFY(preview);
    QVERIFY(surface);
    QVERIFY(surface->width() <= 760);

    theme->setCurrentIndex(theme->findData(QStringLiteral("light")));
    palette->setCurrentIndex(palette->findData(QStringLiteral("forest")));
    cornerRadius->setValue(12);
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(QSettings().value(QStringLiteral("ui/cornerRadius")).toInt(), 12);
    QVERIFY(window.styleSheet().contains(QStringLiteral("border-radius: 12px")));
    const QString classicLightStyle = window.styleSheet();

    for (const quizapp::ui::ThemePalette &preset
         : quizapp::ui::ThemePalettes::legacyPresets()) {
        palette->setCurrentIndex(palette->findData(preset.id));
        QCOMPARE(preview->paletteId(), preset.id);
        QTest::mouseClick(save, Qt::LeftButton);
        QVERIFY2(
            window.styleSheet().contains(preset.primary.name()),
            qPrintable(preset.name));
    }

    palette->setCurrentIndex(palette->findData(QStringLiteral("endfield")));
    QCOMPARE(preview->paletteId(), QStringLiteral("endfield"));
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(
        QSettings().value(QStringLiteral("ui/palette")).toString(),
        QStringLiteral("endfield"));
    QVERIFY(window.styleSheet().contains(QStringLiteral("#fdfc00")));
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
    QVERIFY(endfieldRestoredTheme);
    QVERIFY(endfieldRestoredPalette);
    QVERIFY(endfieldRestoredRadius);
    QCOMPARE(
        endfieldRestoredPalette->currentData().toString(), QStringLiteral("endfield"));
    QVERIFY(endfieldRestored.styleSheet().contains(QStringLiteral("#fdfc00")));
    QCOMPARE(endfieldRestoredRadius->value(), 12);

    window.navigateTo(quizapp::ui::AppWindow::Section::Settings);
    theme->setCurrentIndex(theme->findData(QStringLiteral("light")));
    palette->setCurrentIndex(palette->findData(QStringLiteral("forest")));
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(window.styleSheet(), classicLightStyle);

    theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
    reduceMotion->setChecked(true);
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(QSettings().value(QStringLiteral("ui/theme")).toString(), QStringLiteral("dark"));
    QVERIFY(QSettings().value(QStringLiteral("ui/reduceMotion")).toBool());
    QVERIFY(window.reduceMotion());
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
