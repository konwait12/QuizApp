#include "ui/AppFont.h"
#include "ui/AnswerTablePage.h"
#include "ui/AppWindow.h"
#include "ui/EndfieldTheme.h"
#include "ui/HandwritingPage.h"
#include "ui/MaterialIconProvider.h"
#include "ui/PracticePage.h"
#include "ui/ReviewPage.h"
#include "ui/StudyHubPage.h"
#include "ui/ThemePreview.h"

#include "domain/QuestionOrdering.h"

#include "storage/Database.h"
#include "storage/SqliteLibraryRepository.h"
#include "storage/SqlitePracticeRepository.h"
#include "storage/SqliteQuestionRepository.h"
#include "storage/SqliteReviewRepository.h"
#include "storage/SqliteStudyRepository.h"
#include "storage/SqliteWrongBookRepository.h"
#include "services/ReviewService.h"
#include "services/StudyService.h"
#include "services/WrongBookService.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFrame>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QPromise>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyleHints>
#include <QToolButton>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace quizapp::ui {
namespace {

constexpr int kTabletNavigationBreakpoint = 840;

QString sectionTitle(AppWindow::Section section)
{
    switch (section) {
    case AppWindow::Section::Home:
        return QStringLiteral("学习首页");
    case AppWindow::Section::Library:
        return QStringLiteral("题库");
    case AppWindow::Section::Study:
        return QStringLiteral("学习");
    case AppWindow::Section::Settings:
        return QStringLiteral("设置");
    }
    return {};
}

int sectionIndex(AppWindow::Section section)
{
    return static_cast<int>(section);
}

QLabel *heading(const QString &text, const QString &objectName)
{
    auto *label = new QLabel(text);
    label->setObjectName(objectName);
    label->setWordWrap(true);
    return label;
}

QFrame *summaryCard(
    const QString &value,
    const QString &label,
    QWidget *parent,
    QLabel **valueOutput)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("summaryCard"));
    card->setMinimumSize(132, 84);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(4);
    auto *valueLabel = new QLabel(value, card);
    valueLabel->setObjectName(QStringLiteral("summaryValue"));
    auto *caption = new QLabel(label, card);
    caption->setObjectName(QStringLiteral("secondaryText"));
    layout->addWidget(valueLabel);
    layout->addWidget(caption);
    if (valueOutput) {
        *valueOutput = valueLabel;
    }
    return card;
}

QWidget *centeredPage(QWidget *content)
{
    auto *container = new QWidget;
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    content->setMaximumWidth(1120);
    layout->addWidget(content, 1);
    layout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(container);
    return scroll;
}

void assignMaterialIcon(QAbstractButton *button, const QString &name, bool onPrimary = false)
{
    button->setProperty("quizappIcon", name);
    button->setProperty("quizappIconOnPrimary", onPrimary);
}

bool pathStartsWith(const QStringList &path, const QStringList &prefix)
{
    if (path.size() < prefix.size()) {
        return false;
    }
    for (qsizetype index = 0; index < prefix.size(); ++index) {
        if (path.at(index) != prefix.at(index)) {
            return false;
        }
    }
    return true;
}

QString scopeIdForPath(const QStringList &path)
{
    const QByteArray serialized = QJsonDocument(QJsonArray::fromStringList(path))
                                      .toJson(QJsonDocument::Compact);
    return QStringLiteral("path:%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(serialized, QCryptographicHash::Sha256).toHex()));
}

void clearLayoutItems(QLayout *layout)
{
    while (layout && layout->count() > 0) {
        delete layout->takeAt(0);
    }
}

domain::StudyActivity studyActivityForMode(domain::PracticeMode mode)
{
    switch (mode) {
    case domain::PracticeMode::Sequential:
        return domain::StudyActivity::Sequential;
    case domain::PracticeMode::Random:
        return domain::StudyActivity::Random;
    case domain::PracticeMode::Memorize:
        return domain::StudyActivity::Memorize;
    case domain::PracticeMode::AnswerLookup:
        return domain::StudyActivity::AnswerTable;
    case domain::PracticeMode::WrongBook:
        return domain::StudyActivity::WrongBook;
    case domain::PracticeMode::Review:
        return domain::StudyActivity::Review;
    }
    return domain::StudyActivity::Sequential;
}

} // namespace

AppWindow::AppWindow(QWidget *parent)
    : AppWindow(QString(), QString(), parent)
{
}

AppWindow::AppWindow(QString databasePath, QString dataRoot, QWidget *parent)
    : QMainWindow(parent)
    , databasePath_(std::move(databasePath))
    , dataRoot_(std::move(dataRoot))
{
    configureApplicationFont();
    setObjectName(QStringLiteral("appWindow"));
    setWindowTitle(QStringLiteral("题库"));
    setMinimumSize(360, 600);
    resize(1180, 760);

    rootStack_ = new QStackedWidget(this);
    shellRoot_ = new QWidget(rootStack_);
    auto *root = new QHBoxLayout(shellRoot_);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    rail_ = new QFrame(shellRoot_);
    rail_->setObjectName(QStringLiteral("navigationRail"));
    rail_->setFixedWidth(92);
    root->addWidget(rail_);

    auto *mainArea = new QWidget(shellRoot_);
    auto *mainLayout = new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *topBar = new QFrame(mainArea);
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setFixedHeight(64);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(20, 0, 20, 0);
    topLayout->setSpacing(12);
    brandMark_ = new QLabel(QStringLiteral("题"), topBar);
    brandMark_->setObjectName(QStringLiteral("brandMark"));
    brandMark_->setAlignment(Qt::AlignCenter);
    brandMark_->setFixedSize(36, 36);
    pageTitle_ = new QLabel(sectionTitle(currentSection_), topBar);
    pageTitle_->setObjectName(QStringLiteral("topBarTitle"));
    pageTitle_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topLayout->addWidget(brandMark_);
    topLayout->addWidget(pageTitle_);
    mainLayout->addWidget(topBar);

    pages_ = new QStackedWidget(mainArea);
    pages_->setObjectName(QStringLiteral("pageStack"));
    pages_->addWidget(createHomePage());
    pages_->addWidget(createLibraryPage());
    pages_->addWidget(createStudyPage());
    pages_->addWidget(createSettingsPage());
    mainLayout->addWidget(pages_, 1);

    bottomBar_ = new QFrame(mainArea);
    bottomBar_->setObjectName(QStringLiteral("bottomNavigation"));
    bottomBar_->setFixedHeight(72);
    mainLayout->addWidget(bottomBar_);
    root->addWidget(mainArea, 1);

    handwritingPage_ = new HandwritingPage(dataRoot_, rootStack_);
    connect(handwritingPage_, &HandwritingPage::returnToPractice,
            this, &AppWindow::handleHandwritingReturn);
    rootStack_->addWidget(shellRoot_);
    rootStack_->addWidget(handwritingPage_);
    setCentralWidget(rootStack_);

    buildNavigation();
    QSettings settings;
    const QString theme = settings.value(QStringLiteral("ui/theme"), QStringLiteral("system"))
                              .toString();
    applyTheme(theme);
    applyResponsiveLayout();
    navigateTo(Section::Home);
    refreshLibraryStats();
    refreshInstalledBankList();
    refreshReviewData();
    initializeStudyTracking();
}

AppWindow::~AppWindow()
{
    flushStudyActivity(true);
    if (libraryImportWatcher_ && libraryImportWatcher_->isRunning()) {
        libraryImportWatcher_->cancel();
        libraryImportWatcher_->waitForFinished();
    }
}

AppWindow::Section AppWindow::currentSection() const
{
    return currentSection_;
}

void AppWindow::navigateTo(Section section)
{
    if (section != Section::Study && !isHandwritingVisible()) {
        clearStudyActivity();
    }
    if (rootStack_ && shellRoot_) {
        rootStack_->setCurrentWidget(shellRoot_);
    }
    currentSection_ = section;
    pages_->setCurrentIndex(sectionIndex(section));
    pageTitle_->setText(sectionTitle(section));
    if (auto *button = railButtons_->button(sectionIndex(section))) {
        button->setChecked(true);
    }
    if (auto *button = bottomButtons_->button(sectionIndex(section))) {
        button->setChecked(true);
    }
}

bool AppWindow::isRailNavigationVisible() const
{
    return rail_->isVisible();
}

bool AppWindow::isBottomNavigationVisible() const
{
    return bottomBar_->isVisible();
}

bool AppWindow::reduceMotion() const
{
    return reduceMotionChoice_ && reduceMotionChoice_->isChecked();
}

bool AppWindow::isHandwritingVisible() const
{
    return rootStack_ && handwritingPage_ && rootStack_->currentWidget() == handwritingPage_;
}

bool AppWindow::isAnswerTableVisible() const
{
    return studyStack_ && answerTablePage_
        && studyStack_->currentWidget() == answerTablePage_;
}

QString AppWindow::currentHandwritingNotePath() const
{
    return handwritingPage_ ? handwritingPage_->currentBundlePath() : QString();
}

domain::NotebookLaunchContext AppWindow::lastRestoredPracticeContext() const
{
    return lastRestoredPracticeContext_;
}

void AppWindow::openHandwriting(const domain::NotebookLaunchContext &context)
{
    if (!handwritingPage_ || !rootStack_) {
        return;
    }
    if (hasStudyActivity_ && currentStudyActivity_ != domain::StudyActivity::Handwriting) {
        activityBeforeHandwriting_ = currentStudyActivity_;
        scopeBeforeHandwriting_ = currentStudyScope_;
    } else {
        activityBeforeHandwriting_.reset();
        scopeBeforeHandwriting_.clear();
    }
    setStudyActivity(
        domain::StudyActivity::Handwriting,
        context.questionId.toString(QUuid::WithoutBraces));
    handwritingPage_->openNotebook(context);
    rootStack_->setCurrentWidget(handwritingPage_);
    handwritingPage_->setFocus();
}

void AppWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    applyResponsiveLayout();
}

void AppWindow::keyPressEvent(QKeyEvent *event)
{
    if (isHandwritingVisible()) {
        if (event->key() == Qt::Key_Back || event->key() == Qt::Key_Escape) {
            handwritingPage_->saveAndReturn();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Back || event->key() == Qt::Key_Escape) {
        if (currentSection_ == Section::Study && studyStack_
            && reviewPage_ && studyStack_->currentWidget() == reviewPage_) {
            showStudyHub();
            event->accept();
            return;
        }
        if (currentSection_ == Section::Study && studyStack_
            && studyHubPage_ && studyStack_->currentWidget() == studyHubPage_) {
            navigateTo(Section::Home);
            event->accept();
            return;
        }
        if (currentSection_ == Section::Study
            && studyStack_ && practicePage_ && practicePage_->hasActiveSession()
            && (studyStack_->currentWidget() == practicePage_
                || studyStack_->currentWidget() == answerTablePage_)) {
            if (practiceSaveTimer_ && practiceSaveTimer_->isActive()) {
                practiceSaveTimer_->stop();
            }
            saveActivePracticeSession();
            if (studyStack_ && studyStack_->currentWidget() == practicePage_
                && practicePage_->session().mode == domain::PracticeMode::AnswerLookup
                && answerTablePage_ && answerTablePage_->hasContent()) {
                answerTablePage_->setCurrentQuestionIndex(
                    practicePage_->session().currentIndex);
                studyStack_->setCurrentWidget(answerTablePage_);
                event->accept();
                return;
            }
            navigateTo(Section::Library);
            event->accept();
            return;
        }
        if (currentSection_ == Section::Library && !libraryPath_.isEmpty()) {
            libraryPath_.removeLast();
            refreshInstalledBankList();
            event->accept();
            return;
        }
        if (currentSection_ != Section::Home) {
            navigateTo(Section::Home);
        } else if (event->key() == Qt::Key_Back) {
            close();
        } else {
            QMainWindow::keyPressEvent(event);
            return;
        }
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

QWidget *AppWindow::createHomePage()
{
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("homePage"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 24, 24, 32);
    layout->setSpacing(18);
    layout->addWidget(heading(QStringLiteral("今天从哪里开始？"), QStringLiteral("pageHeading")));
    layout->addWidget(heading(
        QStringLiteral("学习数据仅保存在当前设备"), QStringLiteral("pageSupportingText")));

    homeResponsiveLayout_ = new QGridLayout;
    homeResponsiveLayout_->setContentsMargins(0, 0, 0, 0);
    homeResponsiveLayout_->setHorizontalSpacing(14);
    homeResponsiveLayout_->setVerticalSpacing(14);

    homeSummarySection_ = new QFrame(content);
    homeSummarySection_->setObjectName(QStringLiteral("homeSummarySurface"));
    auto *summarySectionLayout = new QVBoxLayout(homeSummarySection_);
    summarySectionLayout->setContentsMargins(16, 16, 16, 16);
    summarySectionLayout->setSpacing(12);
    summarySectionLayout->addWidget(
        heading(QStringLiteral("学习概览"), QStringLiteral("sectionHeading")));

    summaryGrid_ = new QGridLayout;
    summaryGrid_->setContentsMargins(0, 0, 0, 0);
    summaryGrid_->setHorizontalSpacing(10);
    summaryGrid_->setVerticalSpacing(10);
    summaryValues_.resize(4);
    summaryCards_ = {
        summaryCard(QStringLiteral("0"), QStringLiteral("题库"), homeSummarySection_, &summaryValues_[0]),
        summaryCard(QStringLiteral("0"), QStringLiteral("题目"), homeSummarySection_, &summaryValues_[1]),
        summaryCard(QStringLiteral("0"), QStringLiteral("待复习"), homeSummarySection_, &summaryValues_[2]),
        summaryCard(QStringLiteral("0 分钟"), QStringLiteral("今日学习"), homeSummarySection_, &summaryValues_[3]),
    };
    summarySectionLayout->addLayout(summaryGrid_);

    homeActionsSection_ = new QFrame(content);
    homeActionsSection_->setObjectName(QStringLiteral("homeActionsSurface"));
    auto *actionsSectionLayout = new QVBoxLayout(homeActionsSection_);
    actionsSectionLayout->setContentsMargins(16, 16, 16, 16);
    actionsSectionLayout->setSpacing(12);
    actionsSectionLayout->addWidget(
        heading(QStringLiteral("开始学习"), QStringLiteral("sectionHeading")));
    homeActionsLayout_ = new QGridLayout;
    homeActionsLayout_->setContentsMargins(0, 0, 0, 0);
    homeActionsLayout_->setHorizontalSpacing(10);
    homeActionsLayout_->setVerticalSpacing(10);

    auto *library = new QPushButton(QStringLiteral("浏览题库"), homeActionsSection_);
    assignMaterialIcon(library, QStringLiteral("library_books"));
    library->setObjectName(QStringLiteral("homeLibraryAction"));
    library->setMinimumHeight(48);
    connect(library, &QPushButton::clicked, this, [this] {
        navigateTo(Section::Library);
    });
    auto *study = new QPushButton(QStringLiteral("学习记录"), homeActionsSection_);
    assignMaterialIcon(study, QStringLiteral("school"));
    study->setObjectName(QStringLiteral("homeStudyAction"));
    study->setMinimumHeight(48);
    connect(study, &QPushButton::clicked, this, [this] {
        showStudyHub();
    });
    homeActionButtons_ = {library, study};
    actionsSectionLayout->addLayout(homeActionsLayout_);
    actionsSectionLayout->addStretch();

    layout->addLayout(homeResponsiveLayout_);
    layout->addStretch();
    return centeredPage(content);
}

QWidget *AppWindow::createLibraryPage()
{
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("libraryPage"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 24, 24, 32);
    layout->setSpacing(16);
    layout->addWidget(heading(QStringLiteral("题库"), QStringLiteral("pageHeading")));

    libraryResponsiveLayout_ = new QGridLayout;
    libraryResponsiveLayout_->setContentsMargins(0, 0, 0, 0);
    libraryResponsiveLayout_->setHorizontalSpacing(16);
    libraryResponsiveLayout_->setVerticalSpacing(16);

    librarySourceColumn_ = new QWidget(content);
    librarySourceColumn_->setObjectName(QStringLiteral("librarySourceColumn"));
    auto *sourceColumnLayout = new QVBoxLayout(librarySourceColumn_);
    sourceColumnLayout->setContentsMargins(0, 0, 0, 0);
    sourceColumnLayout->setSpacing(12);

    auto *surface = new QFrame(librarySourceColumn_);
    surface->setObjectName(QStringLiteral("librarySurface"));
    auto *surfaceLayout = new QVBoxLayout(surface);
    surfaceLayout->setContentsMargins(18, 18, 18, 18);
    surfaceLayout->setSpacing(12);
    libraryEmptyTitle_ = heading(
        QStringLiteral("暂无已安装题库"), QStringLiteral("emptyStateTitle"));
    librarySummaryText_ = heading(QString(), QStringLiteral("pageSupportingText"));
    surfaceLayout->addWidget(libraryEmptyTitle_);
    surfaceLayout->addWidget(librarySummaryText_);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(10);
    libraryImportButton_ = new QPushButton(QStringLiteral("安装小易公开题库"), surface);
    libraryImportButton_->setObjectName(QStringLiteral("installXiaoyiButton"));
    libraryImportButton_->setMinimumHeight(44);
    assignMaterialIcon(libraryImportButton_, QStringLiteral("download"));
    connect(libraryImportButton_, &QPushButton::clicked,
            this, &AppWindow::startXiaoyiDirectoryInstall);
    libraryCancelButton_ = new QPushButton(QStringLiteral("取消"), surface);
    libraryCancelButton_->setObjectName(QStringLiteral("cancelXiaoyiInstallButton"));
    libraryCancelButton_->setMinimumHeight(44);
    libraryCancelButton_->hide();
    actions->addWidget(libraryImportButton_);
    actions->addWidget(libraryCancelButton_);
    actions->addStretch();
    surfaceLayout->addLayout(actions);

    libraryImportProgress_ = new QProgressBar(surface);
    libraryImportProgress_->setObjectName(QStringLiteral("xiaoyiImportProgress"));
    libraryImportProgress_->setTextVisible(true);
    libraryImportProgress_->hide();
    surfaceLayout->addWidget(libraryImportProgress_);
    libraryImportStatus_ = heading(QString(), QStringLiteral("pageSupportingText"));
    surfaceLayout->addWidget(libraryImportStatus_);
    sourceColumnLayout->addWidget(surface);
    sourceColumnLayout->addStretch();

    libraryBrowserColumn_ = new QWidget(content);
    libraryBrowserColumn_->setObjectName(QStringLiteral("libraryBrowserColumn"));
    auto *browserColumnLayout = new QVBoxLayout(libraryBrowserColumn_);
    browserColumnLayout->setContentsMargins(0, 0, 0, 0);
    browserColumnLayout->setSpacing(12);

    libraryBanksTitle_ = heading(QStringLiteral("题库目录"), QStringLiteral("sectionHeading"));
    browserColumnLayout->addWidget(libraryBanksTitle_);

    auto *browserHeader = new QFrame(libraryBrowserColumn_);
    browserHeader->setObjectName(QStringLiteral("libraryBrowserHeader"));
    auto *browserHeaderLayout = new QVBoxLayout(browserHeader);
    browserHeaderLayout->setContentsMargins(14, 12, 14, 12);
    browserHeaderLayout->setSpacing(10);
    auto *pathRow = new QHBoxLayout;
    pathRow->setSpacing(10);
    libraryPathBackButton_ = new QPushButton(QStringLiteral("上一级"), browserHeader);
    libraryPathBackButton_->setObjectName(QStringLiteral("libraryPathBackButton"));
    libraryPathBackButton_->setMinimumHeight(40);
    assignMaterialIcon(libraryPathBackButton_, QStringLiteral("arrow_back"));
    connect(libraryPathBackButton_, &QPushButton::clicked, this, [this] {
        if (!libraryPath_.isEmpty()) {
            libraryPath_.removeLast();
            refreshInstalledBankList();
        }
    });
    pathRow->addWidget(libraryPathBackButton_);
    auto *pathText = new QVBoxLayout;
    pathText->setSpacing(2);
    libraryPathTitle_ = new QLabel(browserHeader);
    libraryPathTitle_->setObjectName(QStringLiteral("libraryPathTitle"));
    libraryPathTitle_->setWordWrap(true);
    libraryPathSummary_ = new QLabel(browserHeader);
    libraryPathSummary_->setObjectName(QStringLiteral("libraryPathSummary"));
    libraryPathSummary_->setWordWrap(true);
    pathText->addWidget(libraryPathTitle_);
    pathText->addWidget(libraryPathSummary_);
    pathRow->addLayout(pathText, 1);
    browserHeaderLayout->addLayout(pathRow);

    libraryScopeModesLayout_ = new QGridLayout;
    libraryScopeModesLayout_->setContentsMargins(0, 0, 0, 0);
    libraryScopeModesLayout_->setHorizontalSpacing(8);
    libraryScopeModesLayout_->setVerticalSpacing(8);
    const std::array<std::pair<QString, domain::PracticeMode>, 5> scopeModes{{
        {QStringLiteral("顺序"), domain::PracticeMode::Sequential},
        {QStringLiteral("随机"), domain::PracticeMode::Random},
        {QStringLiteral("背题"), domain::PracticeMode::Memorize},
        {QStringLiteral("答案表"), domain::PracticeMode::AnswerLookup},
        {QStringLiteral("错题集"), domain::PracticeMode::WrongBook},
    }};
    for (const auto &modeEntry : scopeModes) {
        const QString label = modeEntry.first;
        const domain::PracticeMode mode = modeEntry.second;
        auto *button = new QPushButton(label, browserHeader);
        button->setObjectName(
            mode == domain::PracticeMode::Sequential
                ? QStringLiteral("libraryScopePracticeButton")
                : (mode == domain::PracticeMode::WrongBook
                    ? QStringLiteral("libraryWrongBookButton")
                    : QStringLiteral("libraryScopeModeButton")));
        button->setMinimumHeight(40);
        button->setMinimumWidth(88);
        button->setMaximumWidth(132);
        button->setProperty("practiceMode", static_cast<int>(mode));
        connect(button, &QPushButton::clicked, this, [this, mode] {
            startPracticeForPath(libraryPath_, mode);
        });
        libraryScopeModeButtons_.append(button);
    }
    browserHeaderLayout->addLayout(libraryScopeModesLayout_);
    browserColumnLayout->addWidget(browserHeader);

    auto *banksSurface = new QFrame(libraryBrowserColumn_);
    banksSurface->setObjectName(QStringLiteral("libraryBanksSurface"));
    libraryBanksLayout_ = new QVBoxLayout(banksSurface);
    libraryBanksLayout_->setContentsMargins(0, 0, 0, 0);
    libraryBanksLayout_->setSpacing(8);
    browserColumnLayout->addWidget(banksSurface);
    browserColumnLayout->addStretch();

    libraryResponsiveLayout_->addWidget(librarySourceColumn_, 0, 0);
    libraryResponsiveLayout_->addWidget(libraryBrowserColumn_, 1, 0);
    layout->addLayout(libraryResponsiveLayout_, 1);

    libraryImportWatcher_ = new QFutureWatcher<services::DirectoryInstallResult>(this);
    connect(libraryImportWatcher_, &QFutureWatcherBase::progressRangeChanged,
            libraryImportProgress_, &QProgressBar::setRange);
    connect(libraryImportWatcher_, &QFutureWatcherBase::progressValueChanged,
            libraryImportProgress_, &QProgressBar::setValue);
    connect(libraryImportWatcher_, &QFutureWatcherBase::progressTextChanged,
            this, [this](const QString &text) {
                if (!text.isEmpty()) {
                    libraryImportStatus_->setText(text);
                }
            });
    connect(libraryImportWatcher_, &QFutureWatcherBase::finished,
            this, &AppWindow::finishXiaoyiDirectoryInstall);
    connect(libraryCancelButton_, &QPushButton::clicked,
            libraryImportWatcher_, &QFutureWatcherBase::cancel);

    const bool installationAvailable = !databasePath_.isEmpty() && !dataRoot_.isEmpty();
    libraryImportButton_->setVisible(installationAvailable);
    return centeredPage(content);
}

QWidget *AppWindow::createStudyPage()
{
    studyStack_ = new QStackedWidget;
    studyStack_->setObjectName(QStringLiteral("studyStack"));
    studyHubPage_ = new StudyHubPage(studyStack_);
    practicePage_ = new PracticePage(dataRoot_, studyStack_);
    answerTablePage_ = new AnswerTablePage(studyStack_);
    reviewPage_ = new ReviewPage(dataRoot_, studyStack_);
    practiceSaveTimer_ = new QTimer(this);
    practiceSaveTimer_->setSingleShot(true);
    practiceSaveTimer_->setInterval(450);
    connect(practiceSaveTimer_, &QTimer::timeout, this, [this] {
        saveActivePracticeSession();
    });
    connect(practicePage_, &PracticePage::backRequested, this, [this] {
        if (practiceSaveTimer_ && practiceSaveTimer_->isActive()) {
            practiceSaveTimer_->stop();
        }
        QString error;
        if (!saveActivePracticeSession(&error) && libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("保存练习失败：%1").arg(error));
        }
        if (practicePage_->session().mode == domain::PracticeMode::AnswerLookup
            && answerTablePage_->hasContent()) {
            answerTablePage_->setCurrentQuestionIndex(
                practicePage_->session().currentIndex);
            studyStack_->setCurrentWidget(answerTablePage_);
            return;
        }
        navigateTo(Section::Library);
    });
    connect(practicePage_, &PracticePage::handwritingRequested,
            this, &AppWindow::openHandwriting);
    connect(practicePage_, &PracticePage::sessionChanged, this, [this] {
        schedulePracticeSessionSave();
    });
    connect(practicePage_, &PracticePage::wrongBookToggleRequested,
            this, &AppWindow::setWrongBookMembership);
    connect(practicePage_, &PracticePage::reviewToggleRequested,
            this, &AppWindow::setReviewMembership);
    connect(answerTablePage_, &AnswerTablePage::backRequested, this, [this] {
        if (practiceSaveTimer_ && practiceSaveTimer_->isActive()) {
            practiceSaveTimer_->stop();
        }
        QString error;
        if (!saveActivePracticeSession(&error) && libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("保存答案表进度失败：%1").arg(error));
        }
        navigateTo(Section::Library);
    });
    connect(answerTablePage_, &AnswerTablePage::currentQuestionChanged,
            this, [this](qsizetype questionIndex) {
                practicePage_->selectQuestion(questionIndex, false);
            });
    connect(answerTablePage_, &AnswerTablePage::detailRequested,
            this, [this](qsizetype questionIndex) {
                if (practicePage_->selectQuestion(questionIndex, true)) {
                    studyStack_->setCurrentWidget(practicePage_);
                }
            });
    connect(answerTablePage_, &AnswerTablePage::handwritingRequested,
            this, [this](qsizetype questionIndex) {
                if (practicePage_->selectQuestion(questionIndex, false)) {
                    openHandwriting(practicePage_->notebookContext());
                }
            });
    connect(studyHubPage_, &StudyHubPage::refreshRequested,
            this, &AppWindow::refreshReviewData);
    connect(studyHubPage_, &StudyHubPage::startReviewRequested,
            this, &AppWindow::startDueReview);
    connect(studyHubPage_, &StudyHubPage::studyRangeChanged,
            this, &AppWindow::refreshStudyHistory);
    connect(reviewPage_, &ReviewPage::backRequested, this, &AppWindow::showStudyHub);
    connect(reviewPage_, &ReviewPage::ratingRequested,
            this, &AppWindow::rateReviewCard);
    connect(reviewPage_, &ReviewPage::removeRequested,
            this, &AppWindow::removeReviewCard);
    connect(reviewPage_, &ReviewPage::handwritingRequested,
            this, &AppWindow::openHandwriting);
    studyStack_->addWidget(studyHubPage_);
    studyStack_->addWidget(practicePage_);
    studyStack_->addWidget(answerTablePage_);
    studyStack_->addWidget(reviewPage_);
    studyStack_->setCurrentWidget(studyHubPage_);
    return studyStack_;
}

QWidget *AppWindow::createSettingsPage()
{
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("settingsPage"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 24, 24, 32);
    layout->setSpacing(16);
    layout->addWidget(heading(QStringLiteral("设置"), QStringLiteral("pageHeading")));

    auto *surface = new QFrame(content);
    surface->setObjectName(QStringLiteral("settingsSurface"));
    surface->setMaximumWidth(760);
    auto *surfaceLayout = new QGridLayout(surface);
    surfaceLayout->setContentsMargins(20, 20, 20, 20);
    surfaceLayout->setHorizontalSpacing(16);
    surfaceLayout->setVerticalSpacing(12);

    auto *sectionTitle = new QLabel(QStringLiteral("外观与动效"), surface);
    sectionTitle->setObjectName(QStringLiteral("settingsSectionHeading"));

    auto *themeLabel = new QLabel(QStringLiteral("外观模式"), surface);
    themeLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    themeChoice_ = new QComboBox(surface);
    themeChoice_->setObjectName(QStringLiteral("themeChoice"));
    themeChoice_->addItem(QStringLiteral("跟随系统"), QStringLiteral("system"));
    themeChoice_->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
    themeChoice_->addItem(QStringLiteral("深色"), QStringLiteral("dark"));
    themeChoice_->addItem(
        QStringLiteral("终末地风格（高级）"), QStringLiteral("endfield"));
    themeChoice_->setMinimumHeight(42);

    QSettings settings;
    const QString theme = settings.value(QStringLiteral("ui/theme"), QStringLiteral("system"))
                              .toString();
    const int themeIndex = themeChoice_->findData(theme);
    themeChoice_->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);

    auto *previewLabel = new QLabel(QStringLiteral("主题预览"), surface);
    previewLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    themePreview_ = new ThemePreview(surface);
    themePreview_->setThemeId(themeChoice_->currentData().toString());
    connect(themeChoice_, &QComboBox::currentIndexChanged, this, [this] {
        themePreview_->setThemeId(themeChoice_->currentData().toString());
    });

    reduceMotionChoice_ = new QCheckBox(QStringLiteral("减少界面动画"), surface);
    reduceMotionChoice_->setObjectName(QStringLiteral("reduceMotionChoice"));
    reduceMotionChoice_->setChecked(
        settings.value(QStringLiteral("ui/reduceMotion"), false).toBool());

    auto *save = new QPushButton(QStringLiteral("保存设置"), surface);
    assignMaterialIcon(save, QStringLiteral("save"), true);
    save->setObjectName(QStringLiteral("saveSettingsButton"));
    save->setMinimumHeight(44);
    connect(save, &QPushButton::clicked, this, &AppWindow::saveSettings);
    settingsStatus_ = new QLabel(surface);
    settingsStatus_->setObjectName(QStringLiteral("settingsStatus"));
    settingsStatus_->setAccessibleName(QStringLiteral("设置保存状态"));

    auto *saveRow = new QHBoxLayout;
    saveRow->setContentsMargins(0, 4, 0, 0);
    saveRow->setSpacing(12);
    saveRow->addWidget(settingsStatus_, 1);
    saveRow->addWidget(save);

    surfaceLayout->addWidget(sectionTitle, 0, 0, 1, 2);
    surfaceLayout->addWidget(themeLabel, 1, 0);
    surfaceLayout->addWidget(themeChoice_, 1, 1);
    surfaceLayout->addWidget(previewLabel, 2, 0, 1, 2);
    surfaceLayout->addWidget(themePreview_, 3, 0, 1, 2);
    surfaceLayout->addWidget(reduceMotionChoice_, 4, 0, 1, 2);
    surfaceLayout->addLayout(saveRow, 5, 0, 1, 2);
    surfaceLayout->setColumnStretch(1, 1);
    layout->addWidget(surface, 0, Qt::AlignLeft);
    layout->addStretch();
    return centeredPage(content);
}

QToolButton *AppWindow::createNavigationButton(
    Section section,
    const QString &text,
    const QString &iconName)
{
    auto *button = new QToolButton;
    button->setText(text);
    assignMaterialIcon(button, iconName);
    button->setIconSize(QSize(22, 22));
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setAccessibleName(text);
    button->setProperty("section", sectionIndex(section));
    button->setMinimumSize(64, 56);
    connect(button, &QToolButton::clicked, this, [this, section] {
        if (section == Section::Study) {
            showStudyHub();
        } else {
            navigateTo(section);
        }
    });
    return button;
}

void AppWindow::buildNavigation()
{
    railButtons_ = new QButtonGroup(this);
    railButtons_->setExclusive(true);
    bottomButtons_ = new QButtonGroup(this);
    bottomButtons_->setExclusive(true);

    auto *railLayout = new QVBoxLayout(rail_);
    railLayout->setContentsMargins(10, 14, 10, 14);
    railLayout->setSpacing(8);
    auto *railBrand = new QLabel(QStringLiteral("题"), rail_);
    railBrand->setObjectName(QStringLiteral("railBrand"));
    railBrand->setAlignment(Qt::AlignCenter);
    railBrand->setFixedHeight(42);
    railLayout->addWidget(railBrand);
    railLayout->addSpacing(10);

    auto *bottomLayout = new QHBoxLayout(bottomBar_);
    bottomLayout->setContentsMargins(8, 6, 8, 6);
    bottomLayout->setSpacing(4);

    const std::array<std::tuple<Section, QString, QString>, 4> items{{
        {Section::Home, QStringLiteral("首页"), QStringLiteral("home")},
        {Section::Library, QStringLiteral("题库"), QStringLiteral("library_books")},
        {Section::Study, QStringLiteral("学习"), QStringLiteral("school")},
        {Section::Settings, QStringLiteral("设置"), QStringLiteral("settings")},
    }};
    for (const auto &[section, text, icon] : items) {
        auto *railButton = createNavigationButton(section, text, icon);
        railButton->setObjectName(QStringLiteral("railNav%1").arg(sectionIndex(section)));
        railButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        railButtons_->addButton(railButton, sectionIndex(section));
        railLayout->addWidget(railButton);

        auto *bottomButton = createNavigationButton(section, text, icon);
        bottomButton->setObjectName(QStringLiteral("bottomNav%1").arg(sectionIndex(section)));
        bottomButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        bottomButtons_->addButton(bottomButton, sectionIndex(section));
        bottomLayout->addWidget(bottomButton, 1);
    }
    railLayout->addStretch();
}

void AppWindow::applyResponsiveLayout()
{
    const bool tablet = width() >= kTabletNavigationBreakpoint;
    rail_->setVisible(tablet);
    bottomBar_->setVisible(!tablet);
    if (brandMark_) {
        brandMark_->setVisible(!tablet);
    }

    if (homeResponsiveLayout_ && homeSummarySection_ && homeActionsSection_) {
        clearLayoutItems(homeResponsiveLayout_);
        if (tablet) {
            homeResponsiveLayout_->addWidget(homeSummarySection_, 0, 0);
            homeResponsiveLayout_->addWidget(homeActionsSection_, 0, 1);
            homeResponsiveLayout_->setColumnStretch(0, 2);
            homeResponsiveLayout_->setColumnStretch(1, 1);
        } else {
            homeResponsiveLayout_->addWidget(homeSummarySection_, 0, 0);
            homeResponsiveLayout_->addWidget(homeActionsSection_, 1, 0);
            homeResponsiveLayout_->setColumnStretch(0, 1);
            homeResponsiveLayout_->setColumnStretch(1, 0);
        }
    }

    if (homeActionsLayout_) {
        clearLayoutItems(homeActionsLayout_);
        for (qsizetype index = 0; index < homeActionButtons_.size(); ++index) {
            if (tablet) {
                homeActionsLayout_->addWidget(
                    homeActionButtons_.at(index), static_cast<int>(index), 0);
            } else {
                homeActionsLayout_->addWidget(
                    homeActionButtons_.at(index), 0, static_cast<int>(index));
            }
        }
        homeActionsLayout_->setColumnStretch(0, 1);
        homeActionsLayout_->setColumnStretch(1, tablet ? 0 : 1);
    }

    if (summaryGrid_) {
        clearLayoutItems(summaryGrid_);
        const int columns = 2;
        for (qsizetype index = 0; index < summaryCards_.size(); ++index) {
            summaryGrid_->addWidget(
                summaryCards_.at(index),
                static_cast<int>(index) / columns,
                static_cast<int>(index) % columns);
        }
        for (int column = 0; column < columns; ++column) {
            summaryGrid_->setColumnStretch(column, 1);
        }
    }
    if (libraryScopeModesLayout_) {
        clearLayoutItems(libraryScopeModesLayout_);
        libraryScopeModesLayout_->setAlignment(tablet ? Qt::AlignLeft : Qt::AlignHCenter);
        const int columns = tablet ? 5 : 2;
        for (qsizetype index = 0; index < libraryScopeModeButtons_.size(); ++index) {
            QPushButton *button = libraryScopeModeButtons_.at(index);
            const auto mode = static_cast<domain::PracticeMode>(
                button->property("practiceMode").toInt());
            if (!tablet && mode == domain::PracticeMode::WrongBook) {
                libraryScopeModesLayout_->addWidget(
                    button, static_cast<int>(index) / columns, 0, 1, 2,
                    Qt::AlignCenter);
            } else {
                libraryScopeModesLayout_->addWidget(
                    button,
                    static_cast<int>(index) / columns,
                    static_cast<int>(index) % columns,
                    Qt::AlignCenter);
            }
        }
        for (int column = 0; column < 5; ++column) {
            libraryScopeModesLayout_->setColumnStretch(column, 0);
        }
    }
    if (libraryResponsiveLayout_ && librarySourceColumn_ && libraryBrowserColumn_) {
        clearLayoutItems(libraryResponsiveLayout_);
        librarySourceColumn_->setMinimumWidth(tablet ? 320 : 0);
        librarySourceColumn_->setMaximumWidth(tablet ? 360 : QWIDGETSIZE_MAX);
        if (tablet) {
            libraryResponsiveLayout_->addWidget(librarySourceColumn_, 0, 0, Qt::AlignTop);
            libraryResponsiveLayout_->addWidget(libraryBrowserColumn_, 0, 1);
            libraryResponsiveLayout_->setColumnStretch(0, 0);
            libraryResponsiveLayout_->setColumnStretch(1, 1);
            libraryResponsiveLayout_->setRowStretch(0, 1);
        } else {
            libraryResponsiveLayout_->addWidget(librarySourceColumn_, 0, 0);
            libraryResponsiveLayout_->addWidget(libraryBrowserColumn_, 1, 0);
            libraryResponsiveLayout_->setColumnStretch(0, 1);
            libraryResponsiveLayout_->setColumnStretch(1, 0);
            libraryResponsiveLayout_->setRowStretch(0, 0);
            libraryResponsiveLayout_->setRowStretch(1, 1);
        }
    }
}

QString AppWindow::resolvedTheme(const QString &theme) const
{
    if (theme == QStringLiteral("light") || theme == QStringLiteral("dark")) {
        return theme;
    }
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
        ? QStringLiteral("dark") : QStringLiteral("light");
}

void AppWindow::applyTheme(const QString &theme)
{
    if (theme == QStringLiteral("endfield")) {
        setStyleSheet(EndfieldTheme::styleSheet());
        refreshIconsForTheme(theme);
        return;
    }

    const bool dark = resolvedTheme(theme) == QStringLiteral("dark");
    const QString background = dark ? QStringLiteral("#151a17") : QStringLiteral("#f4f7f2");
    const QString surface = dark ? QStringLiteral("#202722") : QStringLiteral("#ffffff");
    const QString surfaceMuted = dark ? QStringLiteral("#29322c") : QStringLiteral("#eef3ec");
    const QString text = dark ? QStringLiteral("#f0f4f1") : QStringLiteral("#172019");
    const QString muted = dark ? QStringLiteral("#aeb9b1") : QStringLiteral("#657168");
    const QString line = dark ? QStringLiteral("#3b473f") : QStringLiteral("#d9e1d8");
    const QString primary = dark ? QStringLiteral("#55c997") : QStringLiteral("#1d9367");
    const QString primarySoft = dark ? QStringLiteral("#173c2d") : QStringLiteral("#dff2e9");
    const QString wrong = dark ? QStringLiteral("#ff8a8a") : QStringLiteral("#c93c3c");
    const QString wrongSoft = dark ? QStringLiteral("#482525") : QStringLiteral("#fde8e8");

    setStyleSheet(QStringLiteral(R"CSS(
        #appWindow { background: %1; color: %4; }
        QWidget { color: %4; font-size: 14px; }
        #topBar, #navigationRail, #bottomNavigation {
            background: %2; border: 0 solid %6;
        }
        #topBar { border-bottom-width: 1px; }
        #navigationRail { border-right-width: 1px; }
        #bottomNavigation { border-top-width: 1px; }
        #brandMark, #railBrand {
            background: %7; color: white; border-radius: 6px;
            font-size: 18px; font-weight: 800;
        }
        #topBarTitle { font-size: 18px; font-weight: 750; }
        #pageHeading { font-size: 24px; font-weight: 800; }
        #sectionHeading, #emptyStateTitle { font-size: 17px; font-weight: 750; }
        #settingsSectionHeading { font-size: 17px; font-weight: 750; }
        #settingsFieldLabel { color: %5; font-weight: 650; }
        #pageSupportingText, #secondaryText, #settingsStatus { color: %5; }
        #summaryCard, #homeSummarySurface, #homeActionsSurface,
        #settingsSurface, #librarySurface,
        #libraryBrowserHeader, #libraryPathNodeCard,
        #practiceOptionsSurface, #practiceAnswerSurface,
        #studySummaryCard, #reviewOptionsSurface, #reviewAnswerSurface {
            background: %2; border: 1px solid %6; border-radius: 7px;
        }
        #studySummaryValue { font-size: 22px; font-weight: 800; }
        #studyDueList {
            background: %2; border: 1px solid %6; border-radius: 7px;
            padding: 4px; outline: 0;
        }
        #studyDueList::item { padding: 8px 10px; border-bottom: 1px solid %6; }
        #studyTrendChart {
            qproperty-lineColor: %7; qproperty-gridColor: %6; qproperty-textColor: %5;
        }
        #studyStartReviewButton, #reviewRevealButton {
            background: %7; border-color: %7; color: white;
        }
        #studyStartReviewButton:disabled {
            background: %3; border-color: %6; color: %5;
        }
        #libraryPathTitle, #questionOverviewTitle { font-size: 17px; font-weight: 750; }
        #libraryPathSummary, #libraryLeafHint,
        #questionOverviewSection { color: %5; }
        #questionOverviewSummaryBar { background: transparent; border: 0; }
        #questionOverviewStat {
            background: %2; border: 1px solid %6; border-radius: 5px;
            padding: 6px 8px; color: %5; font-weight: 650;
        }
        #questionOverviewStat[answerStatus="correct"] {
            background: %8; border-color: %7; color: %7;
        }
        #questionOverviewStat[answerStatus="wrong"] {
            background: %10; border-color: %9; color: %9;
        }
        #libraryPathNodeButton {
            text-align: left; min-height: 62px; padding: 0;
            background: transparent; border: 0;
        }
        #libraryPathNodeButton:hover { color: %7; }
        #libraryPathNodeTitle { font-weight: 750; }
        #libraryPathNodeCount { color: %5; }
        #libraryBanksSurface { background: transparent; border: 0; }
        #practiceImage {
            background: %3; border: 1px solid %6; border-radius: 6px; padding: 6px;
        }
        #practicePage, #studyHubPage, #reviewPage { background: %1; }
        #answerTablePage { background: %1; }
        #answerTableHeader { background: %1; }
        #answerTableTitle { font-size: 16px; font-weight: 750; }
        #answerTableSummary { color: %5; }
        #answerTableView {
            background: %2; alternate-background-color: %2;
            border: 1px solid %6; border-radius: 7px;
            selection-background-color: %8; selection-color: %7;
        }
        #answerTableView::item { padding: 7px; border-bottom: 1px solid %6; }
        #answerTableView::item:selected { color: %7; font-weight: 700; }
        QHeaderView::section {
            background: %3; color: %5; border: 0; border-bottom: 1px solid %6;
            padding: 8px; font-weight: 700;
        }
        #practiceTopBar, #practiceActions {
            background: %2; border: 0 solid %6;
        }
        #practiceTopBar { border-bottom-width: 1px; }
        #practiceActions { border-top-width: 1px; }
        #practiceBankTitle { font-size: 16px; font-weight: 750; }
        #practiceProgressLabel, #practiceQuestionType { color: %5; font-weight: 650; }
        #practicePrompt { font-size: 18px; line-height: 1.35; font-weight: 650; }
        #practiceAnswerLabel { color: %7; font-weight: 750; }
        #practiceExplanationLabel { color: %4; }
        #practiceOptionButton {
            text-align: left; background: %2; border: 1px solid %6;
            border-radius: 7px; padding: 10px 12px;
        }
        #practiceOptionButton:checked {
            background: %8; border-color: %7; color: %7; font-weight: 750;
        }
        #practiceOptionButton[answerState="wrong"] {
            background: %10; border-color: %9; color: %9; font-weight: 750;
        }
        #practiceOptionButton[answerState="correct"] {
            background: %8; border: 2px solid %7; color: %7; font-weight: 750;
        }
        #practiceOptionButton[answerState="correctReveal"] {
            border: 2px solid %7; color: %7; font-weight: 750;
        }
        #practiceReviewButton {
            background: %8; border-color: %7; color: %7;
        }
        #reviewTopBar, #reviewActions {
            background: %2; border: 0 solid %6;
        }
        #reviewTopBar { border-bottom-width: 1px; }
        #reviewActions { border-top-width: 1px; }
        #reviewActions QPushButton { padding: 6px 3px; }
        #reviewTitle { font-size: 16px; font-weight: 750; }
        #reviewProgress, #reviewPath { color: %5; font-weight: 650; }
        #reviewPrompt { font-size: 18px; font-weight: 650; }
        #reviewOption {
            background: %2; border: 1px solid %6; border-radius: 7px;
            padding: 10px 12px;
        }
        #reviewAnswer { color: %7; font-weight: 750; }
        #reviewRating1 { background: %10; border-color: %9; color: %9; }
        #reviewRating3, #reviewRating4 {
            background: %8; border-color: %7; color: %7;
        }
        #reviewRating1:disabled, #reviewRating2:disabled,
        #reviewRating3:disabled, #reviewRating4:disabled {
            background: %3; border-color: %6; color: %5;
        }
        #questionOverviewDialog { background: %1; }
        #questionOverviewNumberButton {
            min-width: 44px; max-width: 44px; min-height: 44px; max-height: 44px;
            padding: 0; background: %2; border-color: %6;
        }
        #questionOverviewNumberButton[answerStatus="answered"] {
            background: %3; border-color: %5;
        }
        #questionOverviewNumberButton[answerStatus="correct"] {
            background: %8; border-color: %7; color: %7;
        }
        #questionOverviewNumberButton[answerStatus="wrong"] {
            background: %10; border-color: %9; color: %9;
        }
        #questionOverviewNumberButton[currentQuestion="true"] {
            border: 2px solid %7; font-weight: 800;
        }
        #libraryWrongBookButton, #practiceWrongBookButton {
            background: %10; border-color: %9; color: %9;
        }
        #libraryWrongBookButton:disabled {
            background: %3; border-color: %6; color: %5;
        }
        #handwritingPage { background: %1; }
        #handwritingToolbar, #handwritingStatus, #handwritingPagePanel,
        #handwritingMobilePageBar {
            background: %2; border: 0 solid %6;
        }
        #handwritingToolbar { border-bottom-width: 1px; }
        #handwritingStatus { border-top-width: 1px; color: %5; }
        #handwritingTitle { font-size: 17px; font-weight: 750; }
        #handwritingPagePanel { border-right-width: 1px; }
        #handwritingMobilePageBar { border-bottom-width: 1px; }
        #handwritingPagePanelTitle { font-size: 15px; font-weight: 750; }
        #handwritingPageLabel, #handwritingMobilePageLabel,
        #handwritingZoomLabel { color: %5; font-weight: 650; }
        #handwritingPageButton0, #handwritingPageList QPushButton {
            text-align: left; min-height: 46px;
        }
        #handwritingPageList QPushButton:checked {
            background: %8; border: 2px solid %7; color: %7; font-weight: 750;
        }
        #handwritingTopRow, #handwritingToolScroller, #handwritingToolStrip,
        #handwritingPageScroller, #handwritingPageList, #handwritingWorkspace {
            background: transparent; border: 0;
        }
        #questionDocumentViewport {
            background: %3; border: 0;
        }
        #summaryValue { font-size: 22px; font-weight: 800; }
        QToolButton {
            border: 0; border-radius: 6px; padding: 5px; color: %5;
        }
        QToolButton:checked { background: %8; color: %7; font-weight: 700; }
        QToolButton:hover { background: %3; }
        QPushButton {
            background: %2; border: 1px solid %6; border-radius: 6px;
            padding: 8px 14px; font-weight: 650;
        }
        QPushButton:hover { border-color: %7; }
        #saveSettingsButton { background: %7; color: white; border-color: %7; }
        QComboBox {
            background: %2; border: 1px solid %6; border-radius: 6px;
            padding: 6px 10px;
        }
        QComboBox QAbstractItemView { background: %2; selection-background-color: %8; }
        QLineEdit {
            background: %2; border: 1px solid %6; border-radius: 6px;
            padding: 7px 10px;
        }
        QLineEdit:focus { border-color: %7; }
        QProgressBar {
            background: %3; border: 0; border-radius: 4px; height: 8px;
            text-align: center; color: transparent;
        }
        QProgressBar::chunk { background: %7; border-radius: 4px; }
        QScrollBar:vertical {
            background: transparent; width: 10px; margin: 0;
        }
        QScrollBar::handle:vertical {
            background: %6; min-height: 32px; border-radius: 5px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0; background: transparent;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollArea, QScrollArea > QWidget > QWidget { background: %1; }
    )CSS")
        .arg(background, surface, surfaceMuted, text, muted, line, primary, primarySoft,
             wrong, wrongSoft));
    refreshIcons(dark);
}

void AppWindow::refreshIconsForTheme(const QString &theme)
{
    if (theme == QStringLiteral("endfield")) {
        refreshIcons(
            EndfieldTheme::iconNormal(),
            EndfieldTheme::iconEmphasis(),
            EndfieldTheme::iconOnPrimary());
        return;
    }
    refreshIcons(resolvedTheme(theme) == QStringLiteral("dark"));
}

void AppWindow::refreshIcons(bool dark)
{
    const QColor normal = dark ? QColor(QStringLiteral("#aeb9b1"))
                               : QColor(QStringLiteral("#657168"));
    const QColor emphasis = dark ? QColor(QStringLiteral("#55c997"))
                                 : QColor(QStringLiteral("#1d9367"));
    const QColor onPrimary(QStringLiteral("#ffffff"));
    refreshIcons(normal, emphasis, onPrimary);
}

void AppWindow::refreshIcons(
    const QColor &normal,
    const QColor &emphasis,
    const QColor &onPrimary)
{
    const auto buttons = findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        const QString iconName = button->property("quizappIcon").toString();
        if (iconName.isEmpty()) {
            continue;
        }
        const bool inverse = button->property("quizappIconOnPrimary").toBool();
        button->setIcon(MaterialIconProvider::icon(
            iconName,
            inverse ? onPrimary : normal,
            inverse ? onPrimary : emphasis));
    }
}

void AppWindow::refreshLibraryStats()
{
    if (databasePath_.isEmpty()) {
        return;
    }
    storage::Database database(
        QStringLiteral("library-stats-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("无法读取本地题库：%1").arg(error));
        }
        return;
    }
    const storage::SqliteLibraryRepository repository(database.connection());
    const domain::LibraryStats stats = repository.stats(&error);
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("无法统计本地题库：%1").arg(error));
        }
        return;
    }
    if (summaryValues_.size() >= 2) {
        summaryValues_[0]->setText(QString::number(stats.bankCount));
        summaryValues_[1]->setText(QString::number(stats.questionCount));
    }
    if (!libraryEmptyTitle_ || !librarySummaryText_) {
        return;
    }
    if (stats.bankCount == 0) {
        libraryEmptyTitle_->setText(QStringLiteral("暂无已安装题库"));
        librarySummaryText_->setText(QStringLiteral("可从已下载目录安装小易公开考研题库"));
        libraryImportButton_->setText(QStringLiteral("安装小易公开题库"));
    } else {
        libraryEmptyTitle_->setText(QStringLiteral("小易公开考研题库"));
        librarySummaryText_->setText(
            QStringLiteral("%1 个分区 · %2 道题 · %3 个媒体资源")
                .arg(stats.bankCount)
                .arg(stats.questionCount)
                .arg(stats.blobCount));
        libraryImportButton_->setText(QStringLiteral("添加或更新小易题库"));
    }
}

void AppWindow::startXiaoyiDirectoryInstall()
{
    if (!libraryImportWatcher_ || libraryImportWatcher_->isRunning()) {
        return;
    }
    const QString inputDirectory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择小易公开题库导出目录"));
    if (inputDirectory.isEmpty()) {
        return;
    }

    libraryImportButton_->setEnabled(false);
    libraryCancelButton_->show();
    libraryImportProgress_->setRange(0, 0);
    libraryImportProgress_->show();
    libraryImportStatus_->setText(QStringLiteral("正在扫描题库目录"));
    const QString databasePath = databasePath_;
    const QString dataRoot = dataRoot_;
    auto future = QtConcurrent::run(
        [inputDirectory, databasePath, dataRoot](
            QPromise<services::DirectoryInstallResult> &promise) {
            services::XiaoyiDirectoryInstallService service;
            const services::DirectoryInstallResult result = service.install(
                inputDirectory,
                databasePath,
                dataRoot,
                [&promise](int current, int total, const QString &sourceKey) {
                    promise.setProgressRange(0, total);
                    promise.setProgressValueAndText(
                        current,
                        sourceKey.isEmpty()
                            ? QStringLiteral("正在完成题库安装")
                            : QStringLiteral("%1/%2  %3").arg(current).arg(total).arg(sourceKey));
                    return !promise.isCanceled();
                });
            promise.addResult(result);
        });
    libraryImportWatcher_->setFuture(future);
}

void AppWindow::finishXiaoyiDirectoryInstall()
{
    libraryImportButton_->setEnabled(true);
    libraryCancelButton_->hide();
    libraryImportProgress_->hide();
    if (libraryImportWatcher_->future().resultCount() == 0) {
        libraryImportStatus_->setText(QStringLiteral("安装已取消，已完成的分区仍可继续使用"));
        refreshLibraryStats();
        refreshInstalledBankList();
        return;
    }
    const services::DirectoryInstallResult result = libraryImportWatcher_->result();
    if (result.canceled) {
        libraryImportStatus_->setText(
            QStringLiteral("安装已取消，已完成 %1/%2 个分区")
                .arg(result.installedSections)
                .arg(result.discoveredSections));
    } else if (!result.error.isEmpty()) {
        libraryImportStatus_->setText(QStringLiteral("安装失败：%1").arg(result.error));
    } else {
        libraryImportStatus_->setText(
            QStringLiteral("安装完成：%1 个分区，%2 道题")
                .arg(result.installedSections)
                .arg(result.installedQuestions));
    }
    refreshLibraryStats();
    refreshInstalledBankList();
}

void AppWindow::saveSettings()
{
    const QString theme = themeChoice_->currentData().toString();
    QSettings settings;
    settings.setValue(QStringLiteral("ui/theme"), theme);
    settings.setValue(QStringLiteral("ui/reduceMotion"), reduceMotionChoice_->isChecked());
    settings.sync();
    applyTheme(theme);
    settingsStatus_->setText(settings.status() == QSettings::NoError
        ? QStringLiteral("设置已保存") : QStringLiteral("设置保存失败"));
}

void AppWindow::handleHandwritingReturn(const domain::NotebookLaunchContext &context)
{
    lastRestoredPracticeContext_ = context;
    if (practicePage_ && context.practiceMode != domain::PracticeMode::Review) {
        practicePage_->restoreNotebookContext(context);
    }
    if (practiceSaveTimer_ && practiceSaveTimer_->isActive()) {
        practiceSaveTimer_->stop();
    }
    if (context.practiceMode != domain::PracticeMode::Review) {
        saveActivePracticeSession();
    }
    if (rootStack_ && shellRoot_) {
        rootStack_->setCurrentWidget(shellRoot_);
    }
    if (activityBeforeHandwriting_.has_value()) {
        const domain::StudyActivity activity = *activityBeforeHandwriting_;
        const QString scope = scopeBeforeHandwriting_;
        activityBeforeHandwriting_.reset();
        scopeBeforeHandwriting_.clear();
        setStudyActivity(activity, scope);
    } else {
        clearStudyActivity();
    }
    emit handwritingReturned(context);
}

void AppWindow::refreshInstalledBankList()
{
    if (!libraryBanksLayout_) {
        return;
    }
    while (libraryBanksLayout_->count() > 0) {
        QLayoutItem *item = libraryBanksLayout_->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    if (databasePath_.isEmpty()) {
        libraryBanksTitle_->hide();
        return;
    }

    storage::Database database(
        QStringLiteral("library-banks-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        libraryBanksTitle_->hide();
        return;
    }
    const storage::SqliteQuestionRepository repository(database.connection());
    const QVector<domain::InstalledBankSummary> banks = repository.listInstalledBanks(&error);
    libraryBanksTitle_->setVisible(!banks.isEmpty());

    struct ChildNode {
        QString title;
        QStringList path;
        qsizetype questionCount = 0;
    };
    QMap<QString, ChildNode> childNodes;
    qsizetype scopeQuestionCount = 0;
    for (const domain::InstalledBankSummary &bank : banks) {
        if (!pathStartsWith(bank.path, libraryPath_)) {
            continue;
        }
        scopeQuestionCount += bank.questionCount;
        if (bank.path.size() <= libraryPath_.size()) {
            continue;
        }
        const QString title = bank.path.at(libraryPath_.size());
        ChildNode &node = childNodes[title];
        node.title = title;
        node.path = bank.path.mid(0, libraryPath_.size() + 1);
        node.questionCount += bank.questionCount;
    }

    if (libraryPathBackButton_) {
        libraryPathBackButton_->setVisible(!libraryPath_.isEmpty());
    }
    if (libraryPathTitle_) {
        libraryPathTitle_->setText(libraryPath_.isEmpty()
            ? QStringLiteral("全部题库") : libraryPath_.constLast());
    }
    if (libraryPathSummary_) {
        libraryPathSummary_->setText(QStringLiteral("%1 个下级 · %2 道题")
            .arg(childNodes.size()).arg(scopeQuestionCount));
    }
    storage::SqliteWrongBookRepository wrongBookRepository(database.connection());
    services::WrongBookService wrongBookService(wrongBookRepository);
    QString wrongBookError;
    const qsizetype wrongQuestionCount =
        wrongBookService.questionIds(libraryPath_, &wrongBookError).size();
    for (QPushButton *button : std::as_const(libraryScopeModeButtons_)) {
        const auto mode = static_cast<domain::PracticeMode>(
            button->property("practiceMode").toInt());
        button->setEnabled(mode == domain::PracticeMode::WrongBook
            ? wrongBookError.isEmpty() && wrongQuestionCount > 0
            : scopeQuestionCount > 0);
    }

    QVector<ChildNode> sortedNodes = childNodes.values();
    std::sort(sortedNodes.begin(), sortedNodes.end(), [](const ChildNode &left, const ChildNode &right) {
        return domain::naturalLibraryTitleLess(left.title, right.title);
    });
    for (const ChildNode &node : sortedNodes) {
        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);
        rowLayout->addStretch(1);
        auto *card = new QFrame(row);
        card->setObjectName(QStringLiteral("libraryPathNodeCard"));
        card->setMaximumWidth(760);
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 8, 8);
        cardLayout->setSpacing(8);
        auto *button = new QPushButton(card);
        button->setObjectName(QStringLiteral("libraryPathNodeButton"));
        button->setMinimumHeight(62);
        button->setProperty("libraryTitle", node.title);
        button->setProperty("libraryPath", node.path);
        button->setToolTip(node.path.join(QStringLiteral(" / ")));
        button->setAccessibleName(QStringLiteral("进入%1，共%2道题")
            .arg(node.title).arg(node.questionCount));
        auto *buttonLayout = new QVBoxLayout(button);
        buttonLayout->setContentsMargins(4, 4, 4, 4);
        buttonLayout->setSpacing(2);
        auto *nodeTitle = new QLabel(node.title, button);
        nodeTitle->setObjectName(QStringLiteral("libraryPathNodeTitle"));
        nodeTitle->setWordWrap(true);
        nodeTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *nodeCount = new QLabel(
            QStringLiteral("%1 道题").arg(node.questionCount), button);
        nodeCount->setObjectName(QStringLiteral("libraryPathNodeCount"));
        nodeCount->setAttribute(Qt::WA_TransparentForMouseEvents);
        buttonLayout->addWidget(nodeTitle);
        buttonLayout->addWidget(nodeCount);
        const auto openPath = [this, path = node.path] {
            libraryPath_ = path;
            refreshInstalledBankList();
        };
        connect(button, &QPushButton::clicked, this, openPath);
        cardLayout->addWidget(button, 1);
        auto *chevron = new QToolButton(card);
        chevron->setObjectName(QStringLiteral("libraryPathChevron"));
        chevron->setProperty("quizappIcon", QStringLiteral("chevron_right"));
        chevron->setToolTip(QStringLiteral("进入%1").arg(node.title));
        chevron->setAccessibleName(QStringLiteral("进入%1").arg(node.title));
        chevron->setFixedSize(42, 42);
        connect(chevron, &QToolButton::clicked, this, openPath);
        cardLayout->addWidget(chevron);
        rowLayout->addWidget(card, 10);
        rowLayout->addStretch(1);
        libraryBanksLayout_->addWidget(row);
    }
    if (childNodes.isEmpty() && scopeQuestionCount > 0) {
        auto *leafHint = new QLabel(QStringLiteral("已到题目分区，可从上方选择练习模式。"), this);
        leafHint->setObjectName(QStringLiteral("libraryLeafHint"));
        leafHint->setWordWrap(true);
        libraryBanksLayout_->addWidget(leafHint);
    }
    QSettings settings;
    refreshIconsForTheme(
        settings.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString());
}

void AppWindow::startPracticeForPath(const QStringList &path, domain::PracticeMode mode)
{
    if (databasePath_.isEmpty() || !practicePage_) {
        return;
    }
    storage::Database database(
        QStringLiteral("practice-path-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("无法打开题库：%1").arg(error));
        }
        return;
    }
    const storage::SqliteQuestionRepository repository(database.connection());
    QVector<domain::Question> questions = repository.listByPathPrefix(path, &error);
    if (!error.isEmpty() || questions.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(error.isEmpty()
                ? QStringLiteral("当前目录没有可练习题目")
                : QStringLiteral("读取题库失败：%1").arg(error));
        }
        return;
    }

    storage::SqliteWrongBookRepository wrongBookRepository(database.connection());
    services::WrongBookService wrongBookService(wrongBookRepository);
    const QSet<QUuid> wrongQuestionIds = wrongBookService.questionIds(path, &error);
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("读取错题集失败：%1").arg(error));
        }
        return;
    }
    storage::SqliteReviewRepository reviewRepository(database.connection());
    services::ReviewService reviewService(reviewRepository);
    const QSet<QUuid> reviewQuestionIds = reviewService.questionIds(path, &error);
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("读取复习卡失败：%1").arg(error));
        }
        return;
    }
    if (mode == domain::PracticeMode::WrongBook) {
        questions.erase(std::remove_if(
            questions.begin(), questions.end(),
            [&wrongQuestionIds](const domain::Question &question) {
                return !wrongQuestionIds.contains(question.id);
            }), questions.end());
        if (questions.isEmpty()) {
            if (libraryImportStatus_) {
                libraryImportStatus_->setText(QStringLiteral("当前目录的错题集为空"));
            }
            refreshInstalledBankList();
            return;
        }
    }

    domain::InstalledBankSummary scope;
    scope.id = scopeIdForPath(path);
    scope.title = path.isEmpty() ? QStringLiteral("全部题库") : path.constLast();
    scope.path = path;
    scope.questionCount = questions.size();
    storage::SqlitePracticeRepository practiceRepository(database.connection());
    std::optional<domain::PracticeSession> restored =
        practiceRepository.latest(scope.id, mode, &error);
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("读取练习进度失败：%1").arg(error));
        }
        error.clear();
    }
    practicePage_->setDataRoot(dataRoot_);
    practicePage_->start(scope, questions, mode, restored);
    practicePage_->setWrongBookQuestionIds(wrongQuestionIds);
    practicePage_->setReviewQuestionIds(reviewQuestionIds);
    if (mode == domain::PracticeMode::AnswerLookup) {
        answerTablePage_->start(scope, questions, practicePage_->session());
        studyStack_->setCurrentWidget(answerTablePage_);
    } else {
        studyStack_->setCurrentWidget(practicePage_);
    }
    setStudyActivity(studyActivityForMode(mode), scope.id);
    if (!saveActivePracticeSession(&error) && libraryImportStatus_) {
        libraryImportStatus_->setText(QStringLiteral("保存练习失败：%1").arg(error));
    }
    navigateTo(Section::Study);
}

void AppWindow::setWrongBookMembership(const QUuid &questionId, bool included)
{
    if (databasePath_.isEmpty() || questionId.isNull() || !practicePage_) {
        return;
    }
    storage::Database database(
        QStringLiteral("wrong-book-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        QMessageBox::warning(
            this, QStringLiteral("错题集"),
            QStringLiteral("无法打开本地数据：%1").arg(error));
        return;
    }
    storage::SqliteWrongBookRepository repository(database.connection());
    services::WrongBookService service(repository);
    if (!service.setMembership(questionId, included, &error)) {
        QMessageBox::warning(
            this, QStringLiteral("错题集"),
            QStringLiteral("更新错题集失败：%1").arg(error));
        return;
    }
    practicePage_->setWrongBookMembership(questionId, included);
    refreshInstalledBankList();
}

void AppWindow::setReviewMembership(const QUuid &questionId, bool included)
{
    if (databasePath_.isEmpty() || questionId.isNull() || !practicePage_) {
        return;
    }
    storage::Database database(
        QStringLiteral("review-membership-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        QMessageBox::warning(
            this, QStringLiteral("复习"),
            QStringLiteral("无法打开本地数据：%1").arg(error));
        return;
    }
    storage::SqliteReviewRepository repository(database.connection());
    services::ReviewService service(repository);
    const bool succeeded = included
        ? service.add(questionId, QDateTime::currentDateTimeUtc(), &error)
        : service.remove(questionId, &error);
    if (!succeeded) {
        QMessageBox::warning(
            this, QStringLiteral("复习"),
            QStringLiteral("更新复习卡失败：%1").arg(error));
        return;
    }
    practicePage_->setReviewMembership(questionId, included);
    refreshReviewData();
}

void AppWindow::refreshReviewData()
{
    dueReviewCards_.clear();
    dueReviewQuestions_.clear();
    if (databasePath_.isEmpty() || !studyHubPage_) {
        return;
    }
    storage::Database database(
        QStringLiteral("review-summary-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        return;
    }
    storage::SqliteReviewRepository reviewRepository(database.connection());
    services::ReviewService reviewService(reviewRepository);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const domain::ReviewStats stats = reviewService.stats(now, &error);
    if (!error.isEmpty()) {
        return;
    }
    dueReviewCards_ = reviewService.due(now, 200, &error);
    if (!error.isEmpty()) {
        dueReviewCards_.clear();
        return;
    }
    storage::SqliteQuestionRepository questionRepository(database.connection());
    QVector<domain::Question> dueQuestions;
    for (const domain::ReviewCard &card : std::as_const(dueReviewCards_)) {
        const auto question = questionRepository.findById(card.questionId, &error);
        if (!error.isEmpty()) {
            dueReviewCards_.clear();
            dueReviewQuestions_.clear();
            return;
        }
        if (question.has_value()) {
            dueReviewQuestions_.insert(question->id, *question);
            dueQuestions.append(*question);
        }
    }
    dueReviewCards_.erase(std::remove_if(
        dueReviewCards_.begin(), dueReviewCards_.end(), [this](const domain::ReviewCard &card) {
            return !dueReviewQuestions_.contains(card.questionId);
        }), dueReviewCards_.end());
    studyHubPage_->setReviewData(stats, dueQuestions);
    if (summaryValues_.size() >= 3) {
        summaryValues_[2]->setText(QString::number(stats.due));
    }
}

void AppWindow::showStudyHub()
{
    clearStudyActivity();
    refreshReviewData();
    refreshStudyHistory();
    if (studyStack_ && studyHubPage_) {
        studyStack_->setCurrentWidget(studyHubPage_);
    }
    navigateTo(Section::Study);
}

void AppWindow::startDueReview()
{
    refreshReviewData();
    reviewSessionId_ = QUuid::createUuid();
    setStudyActivity(domain::StudyActivity::Review, QStringLiteral("due-review"));
    showNextDueReview();
}

void AppWindow::showNextDueReview()
{
    if (!reviewPage_ || !studyStack_) {
        return;
    }
    while (!dueReviewCards_.isEmpty()
           && !dueReviewQuestions_.contains(dueReviewCards_.constFirst().questionId)) {
        dueReviewCards_.removeFirst();
    }
    if (dueReviewCards_.isEmpty()) {
        showStudyHub();
        return;
    }
    storage::Database database(
        QStringLiteral("review-preview-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        QMessageBox::warning(this, QStringLiteral("复习"), error);
        return;
    }
    storage::SqliteReviewRepository repository(database.connection());
    services::ReviewService service(repository);
    const domain::ReviewCard card = dueReviewCards_.constFirst();
    const auto previews = service.preview(
        card.questionId, QDateTime::currentDateTimeUtc(), &error);
    if (!previews.has_value()) {
        QMessageBox::warning(
            this, QStringLiteral("复习"),
            QStringLiteral("无法计算复习间隔：%1").arg(error));
        return;
    }
    reviewPage_->showCard(
        dueReviewQuestions_.value(card.questionId),
        card,
        *previews,
        dueReviewCards_.size(),
        reviewSessionId_);
    studyStack_->setCurrentWidget(reviewPage_);
    navigateTo(Section::Study);
}

void AppWindow::rateReviewCard(const QUuid &questionId, domain::ReviewRating rating)
{
    storage::Database database(
        QStringLiteral("review-rate-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        QMessageBox::warning(this, QStringLiteral("复习"), error);
        return;
    }
    storage::SqliteReviewRepository repository(database.connection());
    services::ReviewService service(repository);
    if (!service.rate(
            questionId, rating, QDateTime::currentDateTimeUtc(), &error).has_value()) {
        QMessageBox::warning(
            this, QStringLiteral("复习"),
            QStringLiteral("保存复习结果失败：%1").arg(error));
        return;
    }
    refreshReviewData();
    showNextDueReview();
}

void AppWindow::removeReviewCard(const QUuid &questionId)
{
    const auto answer = QMessageBox::question(
        this, QStringLiteral("移出复习"),
        QStringLiteral("确认将当前题目移出复习？复习历史也会一并删除。"));
    if (answer != QMessageBox::Yes) {
        return;
    }
    storage::Database database(
        QStringLiteral("review-remove-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        QMessageBox::warning(this, QStringLiteral("复习"), error);
        return;
    }
    storage::SqliteReviewRepository repository(database.connection());
    services::ReviewService service(repository);
    if (!service.remove(questionId, &error)) {
        QMessageBox::warning(this, QStringLiteral("复习"), error);
        return;
    }
    practicePage_->setReviewMembership(questionId, false);
    refreshReviewData();
    showNextDueReview();
}

void AppWindow::startPracticeForBank(const QString &bankId, domain::PracticeMode mode)
{
    if (databasePath_.isEmpty() || !practicePage_) {
        return;
    }
    storage::Database database(
        QStringLiteral("practice-bank-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("无法打开题库：%1").arg(error));
        }
        return;
    }
    const storage::SqliteQuestionRepository repository(database.connection());
    const QVector<domain::InstalledBankSummary> banks = repository.listInstalledBanks(&error);
    auto found = std::find_if(banks.cbegin(), banks.cend(), [&bankId](const auto &bank) {
        return bank.id == bankId;
    });
    if (found == banks.cend()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("题库不存在或已被移除"));
        }
        return;
    }
    const QVector<domain::Question> questions = repository.listByBankId(bankId, &error);
    if (!error.isEmpty() || questions.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(error.isEmpty()
                ? QStringLiteral("题库没有可练习题目")
                : QStringLiteral("读取题库失败：%1").arg(error));
        }
        return;
    }
    storage::SqlitePracticeRepository practiceRepository(database.connection());
    std::optional<domain::PracticeSession> restored =
        practiceRepository.latest(bankId, mode, &error);
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("读取练习进度失败：%1").arg(error));
        }
        error.clear();
    }
    practicePage_->setDataRoot(dataRoot_);
    practicePage_->start(*found, questions, mode, restored);
    storage::SqliteReviewRepository reviewRepository(database.connection());
    services::ReviewService reviewService(reviewRepository);
    practicePage_->setReviewQuestionIds(reviewService.questionIds(found->path, &error));
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("读取复习卡失败：%1").arg(error));
        }
        return;
    }
    if (mode == domain::PracticeMode::AnswerLookup) {
        answerTablePage_->start(*found, questions, practicePage_->session());
        studyStack_->setCurrentWidget(answerTablePage_);
    } else {
        studyStack_->setCurrentWidget(practicePage_);
    }
    setStudyActivity(studyActivityForMode(mode), found->id);
    if (!saveActivePracticeSession(&error) && libraryImportStatus_) {
        libraryImportStatus_->setText(QStringLiteral("保存练习失败：%1").arg(error));
    }
    navigateTo(Section::Study);
}

void AppWindow::initializeStudyTracking()
{
    studyTotalDate_ = QDate::currentDate();
    reloadTodayStudyTotal();
    studyUiTimer_ = new QTimer(this);
    studyUiTimer_->setInterval(1000);
    connect(studyUiTimer_, &QTimer::timeout, this, &AppWindow::refreshTodayStudyTime);
    studyUiTimer_->start();
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive) {
                    resumeStudyActivity();
                } else {
                    pauseStudyActivity();
                }
            });
    refreshTodayStudyTime();
    refreshStudyHistory();
}

void AppWindow::setStudyActivity(domain::StudyActivity activity, const QString &scopeId)
{
    if (hasStudyActivity_ && currentStudyActivity_ == activity
        && currentStudyScope_ == scopeId) {
        resumeStudyActivity();
        return;
    }
    flushStudyActivity(true);
    hasStudyActivity_ = true;
    currentStudyActivity_ = activity;
    currentStudyScope_ = scopeId;
    resumeStudyActivity();
}

void AppWindow::clearStudyActivity()
{
    flushStudyActivity(true);
}

void AppWindow::pauseStudyActivity()
{
    flushStudyActivity(false);
}

void AppWindow::resumeStudyActivity()
{
    if (!hasStudyActivity_ || studyElapsedRunning_
        || QGuiApplication::applicationState() != Qt::ApplicationActive) {
        return;
    }
    currentStudyStartedAt_ = QDateTime::currentDateTimeUtc();
    studyElapsed_.start();
    studyElapsedRunning_ = true;
}

void AppWindow::flushStudyActivity(bool clearActivity)
{
    if (studyElapsedRunning_) {
        const qint64 totalMilliseconds =
            studyRemainderMilliseconds_ + studyElapsed_.elapsed();
        const int durationSeconds = static_cast<int>(totalMilliseconds / 1000);
        studyRemainderMilliseconds_ = totalMilliseconds % 1000;
        studyElapsedRunning_ = false;
        if (durationSeconds > 0 && !databasePath_.isEmpty()) {
            storage::Database database(
                QStringLiteral("study-record-%1")
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
            QString error;
            if (database.open(databasePath_, &error) && database.migrate(&error)) {
                storage::SqliteStudyRepository repository(database.connection());
                services::StudyService service(repository);
                service.record(
                    currentStudyActivity_, currentStudyScope_,
                    currentStudyStartedAt_, durationSeconds, &error);
            }
        }
    }
    if (clearActivity) {
        hasStudyActivity_ = false;
        currentStudyScope_.clear();
    }
    reloadTodayStudyTotal();
    refreshTodayStudyTime();
}

void AppWindow::reloadTodayStudyTotal()
{
    studyTotalDate_ = QDate::currentDate();
    persistedTodayStudySeconds_ = 0;
    if (databasePath_.isEmpty()) {
        return;
    }
    storage::Database database(
        QStringLiteral("study-today-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        return;
    }
    storage::SqliteStudyRepository repository(database.connection());
    services::StudyService service(repository);
    persistedTodayStudySeconds_ = service.totalForDate(
        studyTotalDate_, QTimeZone::systemTimeZone(), &error);
    if (!error.isEmpty()) {
        persistedTodayStudySeconds_ = 0;
    }
}

void AppWindow::refreshTodayStudyTime()
{
    const QDate today = QDate::currentDate();
    if (studyTotalDate_ != today) {
        flushStudyActivity(false);
        resumeStudyActivity();
    }
    int totalSeconds = persistedTodayStudySeconds_;
    if (studyElapsedRunning_
        && currentStudyStartedAt_.toLocalTime().date() == today) {
        totalSeconds += static_cast<int>(studyElapsed_.elapsed() / 1000);
    }
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const QString text = hours > 0
        ? QStringLiteral("%1时%2分").arg(hours).arg(minutes)
        : QStringLiteral("%1 分钟").arg(minutes);
    if (summaryValues_.size() >= 4) {
        summaryValues_[3]->setText(text);
    }
    if (studyHubPage_) {
        studyHubPage_->setTodayStudySeconds(totalSeconds);
    }
}

void AppWindow::refreshStudyHistory(int days)
{
    if (databasePath_.isEmpty() || !studyHubPage_) {
        return;
    }
    if (days > 0) {
        studyHistoryDays_ = qBound(7, days, 90);
    }
    const int boundedDays = studyHistoryDays_;
    storage::Database database(
        QStringLiteral("study-history-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        return;
    }
    storage::SqliteStudyRepository repository(database.connection());
    services::StudyService service(repository);
    const QDate today = QDate::currentDate();
    auto totals = service.dailyTotals(
        today.addDays(1 - boundedDays), today,
        QTimeZone::systemTimeZone(), &error);
    if (!error.isEmpty()) {
        return;
    }
    if (!totals.isEmpty() && studyElapsedRunning_
        && currentStudyStartedAt_.toLocalTime().date() == today) {
        totals.last().durationSeconds += static_cast<int>(studyElapsed_.elapsed() / 1000);
    }
    studyHubPage_->setStudyTrend(totals, !reduceMotion());
}

void AppWindow::schedulePracticeSessionSave()
{
    if (!practiceSaveTimer_) {
        saveActivePracticeSession();
        return;
    }
    practiceSaveTimer_->start();
}

bool AppWindow::saveActivePracticeSession(QString *error)
{
    if (error) {
        error->clear();
    }
    if (databasePath_.isEmpty() || !practicePage_ || !practicePage_->hasActiveSession()) {
        return true;
    }
    storage::Database database(
        QStringLiteral("practice-save-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!database.open(databasePath_, error) || !database.migrate(error)) {
        return false;
    }
    storage::SqlitePracticeRepository repository(database.connection());
    return repository.save(practicePage_->session(), error);
}

} // namespace quizapp::ui
