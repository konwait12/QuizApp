#include "ui/AppFont.h"
#include "ui/AnswerTablePage.h"
#include "ui/AiSettingsPanel.h"
#include "ui/AnnouncementDialog.h"
#include "ui/AppUpdateDialog.h"
#include "ui/AppWindow.h"
#include "ui/BankReleaseDialog.h"
#include "ui/BackupSettingsPanel.h"
#include "ui/ChoiceComboBox.h"
#include "ui/EndfieldTheme.h"
#include "ui/ExamPage.h"
#include "ui/HandwritingPage.h"
#include "ui/MaterialIconProvider.h"
#include "ui/NotebookLibraryPage.h"
#include "ui/PracticePage.h"
#include "ui/ReviewPage.h"
#include "ui/StudyHubPage.h"
#include "ui/ThemePreview.h"
#include "ui/ThemePalette.h"

#include "domain/QuestionOrdering.h"
#include "platform/SharedStoragePlatform.h"
#include "services/AiQuestionAnalysisService.h"

#include "storage/Database.h"
#include "storage/SqliteBankSourceRepository.h"
#include "storage/SqliteAnswerStateRepository.h"
#include "storage/SqliteLibraryRepository.h"
#include "storage/SqlitePracticeRepository.h"
#include "storage/SqliteQuestionRepository.h"
#include "storage/SqliteReviewRepository.h"
#include "storage/SqliteStudyRepository.h"
#include "storage/SqliteWrongBookRepository.h"
#include "services/ReviewService.h"
#include "services/AnnouncementService.h"
#include "services/BankReleaseStateStore.h"
#include "services/SharedStorageFileService.h"
#include "services/StudyService.h"
#include "services/WrongBookService.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QProgressBar>
#include <QPromise>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyleHints>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace quizapp::ui {
namespace {

constexpr int kTabletNavigationBreakpoint = 840;
constexpr int kSavedProgressPhoneMinWidth = 300;
constexpr int kSavedProgressPhoneMaxWidth = 430;
constexpr int kSavedProgressPhoneDefaultWidth = 360;
constexpr int kSavedProgressTabletMinWidth = 440;
constexpr int kSavedProgressTabletMaxWidth = 720;
constexpr int kSavedProgressTabletDefaultWidth = 520;
constexpr int kStoragePathRole = Qt::UserRole + 1;
constexpr int kStorageDirectoryRole = Qt::UserRole + 2;
constexpr int kHiddenLibraryPathRole = Qt::UserRole + 3;
const QUrl kLatestReleaseApiUrl(
    QStringLiteral("https://api.github.com/repos/konwait12/QuizApp/releases/latest"));
const QUrl kAnnouncementFeedUrl(
    QStringLiteral("https://raw.githubusercontent.com/konwait12/QuizApp/master/"
                   "distribution/quizapp-announcements.json"));
#ifndef QUIZAPP_APP_VERSION
#define QUIZAPP_APP_VERSION "2.0.0-alpha.3"
#endif
#ifndef QUIZAPP_BUILD_COMMIT
#define QUIZAPP_BUILD_COMMIT "dev"
#endif
const QString kCurrentAppVersion = QString::fromLatin1(QUIZAPP_APP_VERSION);
const QString kCurrentBuildCommit = QString::fromLatin1(QUIZAPP_BUILD_COMMIT);

struct LibraryIconOption {
    const char *key;
    const char *label;
    const char *emoji;
};

constexpr std::array<LibraryIconOption, 8> kLibraryIconOptions{{
    {"folder", "文件夹", "📁"},
    {"library", "书库", "📚"},
    {"book", "题册", "📖"},
    {"notebook", "笔记", "📒"},
    {"memo", "练习", "📝"},
    {"target", "目标", "🎯"},
    {"star", "重点", "⭐"},
    {"bookmark", "书签", "🔖"},
}};

const LibraryIconOption &libraryIconOption(const QString &key, const char *fallback)
{
    for (const LibraryIconOption &option : kLibraryIconOptions) {
        if (key == QString::fromLatin1(option.key)) {
            return option;
        }
    }
    for (const LibraryIconOption &option : kLibraryIconOptions) {
        if (QString::fromLatin1(option.key) == QString::fromLatin1(fallback)) {
            return option;
        }
    }
    return kLibraryIconOptions.front();
}

QString defaultLibraryIconKey(int zeroBasedLevel)
{
    if (zeroBasedLevel == 0) {
        return QStringLiteral("library");
    }
    if (zeroBasedLevel == 1) {
        return QStringLiteral("book");
    }
    return QStringLiteral("folder");
}

QStringList configuredLibraryIconKeys(const QSettings &settings, int levelCount)
{
    const QStringList stored = settings.value(
        QStringLiteral("ui/levelIconStyles")).toStringList();
    QStringList keys;
    keys.reserve(levelCount);
    for (int level = 0; level < levelCount; ++level) {
        const QString fallback = defaultLibraryIconKey(level);
        QString key;
        if (level < stored.size()) {
            key = stored.at(level);
        } else if (level == 0) {
            key = settings.value(
                QStringLiteral("ui/subjectIconStyle"), fallback).toString();
        } else if (level == 1) {
            key = settings.value(
                QStringLiteral("ui/chapterIconStyle"), fallback).toString();
        } else {
            key = fallback;
        }
        keys.append(QString::fromLatin1(
            libraryIconOption(key, fallback.toLatin1().constData()).key));
    }
    return keys;
}

const QString kLibraryNodeMimeType = QStringLiteral(
    "application/x-quizapp-library-path");

QByteArray encodedLibraryPath(const QStringList &path)
{
    return QJsonDocument(QJsonArray::fromStringList(path))
        .toJson(QJsonDocument::Compact);
}

QStringList decodedLibraryPath(const QByteArray &encoded)
{
    const QJsonDocument document = QJsonDocument::fromJson(encoded);
    if (!document.isArray()) {
        return {};
    }
    QStringList path;
    for (const QJsonValue &value : document.array()) {
        if (!value.isString() || value.toString().trimmed().isEmpty()) {
            return {};
        }
        path.append(value.toString());
    }
    return path;
}

class LibraryNodeCard final : public QFrame {
public:
    explicit LibraryNodeCard(QStringList path, QWidget *parent = nullptr)
        : QFrame(parent)
        , path_(std::move(path))
    {
        installEventFilter(this);
    }

    void watchDragSource(QWidget *source)
    {
        if (source) {
            source->installEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!property("libraryReorderEnabled").toBool()) {
            pressed_ = false;
            return QFrame::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseButtonPress) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                pressed_ = true;
                pressPosition_ = mouse->globalPosition().toPoint();
                pressElapsed_.start();
            }
        } else if (event->type() == QEvent::MouseMove && pressed_) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            const int distance = (mouse->globalPosition().toPoint() - pressPosition_)
                .manhattanLength();
            if ((mouse->buttons() & Qt::LeftButton) && pressElapsed_.elapsed() >= 320
                && distance >= QApplication::startDragDistance()) {
                pressed_ = false;
                QDrag drag(this);
                auto *mime = new QMimeData;
                mime->setData(kLibraryNodeMimeType, encodedLibraryPath(path_));
                drag.setMimeData(mime);
                QPixmap preview = grab();
                if (preview.width() > 420) {
                    preview = preview.scaledToWidth(420, Qt::SmoothTransformation);
                }
                drag.setPixmap(preview);
                drag.setHotSpot(QPoint(
                    std::clamp(preview.width() / 3, 0, preview.width() - 1),
                    std::clamp(preview.height() / 2, 0, preview.height() - 1)));
                drag.exec(Qt::MoveAction);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            pressed_ = false;
        }
        return QFrame::eventFilter(watched, event);
    }

private:
    QStringList path_;
    QPoint pressPosition_;
    QElapsedTimer pressElapsed_;
    bool pressed_ = false;
};

class LibraryDropSurface final : public QFrame {
public:
    explicit LibraryDropSurface(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setAcceptDrops(true);
    }

    std::function<void(const QVector<QStringList> &)> orderChanged;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (property("libraryReorderEnabled").toBool()
            && event->mimeData()->hasFormat(kLibraryNodeMimeType)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        }
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (property("libraryReorderEnabled").toBool()
            && event->mimeData()->hasFormat(kLibraryNodeMimeType)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        if (!property("libraryReorderEnabled").toBool() || !layout()) {
            return;
        }
        const QStringList sourcePath = decodedLibraryPath(
            event->mimeData()->data(kLibraryNodeMimeType));
        if (sourcePath.isEmpty()) {
            return;
        }
        QVector<QStringList> orderedPaths;
        QVector<QWidget *> rows;
        for (int index = 0; index < layout()->count(); ++index) {
            QWidget *row = layout()->itemAt(index)->widget();
            if (!row) {
                continue;
            }
            const QStringList path = row->property("libraryPath").toStringList();
            if (!path.isEmpty()) {
                orderedPaths.append(path);
                rows.append(row);
            }
        }
        const qsizetype sourceIndex = orderedPaths.indexOf(sourcePath);
        if (sourceIndex < 0 || orderedPaths.size() < 2) {
            return;
        }
        qsizetype targetIndex = orderedPaths.size();
        const int dropY = event->position().toPoint().y();
        for (qsizetype index = 0; index < rows.size(); ++index) {
            if (dropY < rows.at(index)->geometry().center().y()) {
                targetIndex = index;
                break;
            }
        }
        orderedPaths.removeAt(sourceIndex);
        if (sourceIndex < targetIndex) {
            --targetIndex;
        }
        targetIndex = std::clamp<qsizetype>(targetIndex, 0, orderedPaths.size());
        orderedPaths.insert(targetIndex, sourcePath);
        if (orderChanged) {
            orderChanged(orderedPaths);
        }
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
};

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

QString practiceModeTitle(domain::PracticeMode mode)
{
    switch (mode) {
    case domain::PracticeMode::Sequential:
        return QStringLiteral("顺序练习");
    case domain::PracticeMode::Random:
        return QStringLiteral("随机练习");
    case domain::PracticeMode::Memorize:
        return QStringLiteral("背题模式");
    case domain::PracticeMode::AnswerLookup:
        return QStringLiteral("答案表");
    case domain::PracticeMode::WrongBook:
        return QStringLiteral("错题集");
    case domain::PracticeMode::Review:
        return QStringLiteral("复习");
    }
    return {};
}

double relativeLuminance(const QColor &color)
{
    const auto channel = [](int value) {
        const double normalized = value / 255.0;
        return normalized <= 0.04045
            ? normalized / 12.92
            : std::pow((normalized + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red())
        + 0.7152 * channel(color.green())
        + 0.0722 * channel(color.blue());
}

double contrastRatio(const QColor &left, const QColor &right)
{
    const double brighter = std::max(relativeLuminance(left), relativeLuminance(right));
    const double darker = std::min(relativeLuminance(left), relativeLuminance(right));
    return (brighter + 0.05) / (darker + 0.05);
}

QColor readableOnPrimary(const QColor &primary)
{
    const QColor black(QStringLiteral("#101113"));
    const QColor white(QStringLiteral("#ffffff"));
    return contrastRatio(primary, black) >= contrastRatio(primary, white)
        ? black : white;
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
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

QString absoluteCleanPath(const QString &path)
{
    return path.trimmed().isEmpty()
        ? QString()
        : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
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

QString componentRadiusStyleSheet(int radius)
{
    const int bounded = std::clamp(radius, 0, 18);
    const int compact = std::max(0, bounded - 2);
    const int tiny = std::max(0, bounded - 4);
    return QStringLiteral(R"CSS(
        #summaryCard, #homeSummarySurface, #homeActionsSurface,
        #homeSavedProgressCard,
        #settingsSurface, #practiceSettingsSurface, #bankUpdateSettingsSurface,
        #backupSettingsSurface, #aiSettingsSurface,
        #librarySurface, #libraryBrowserHeader, #bankReleaseTree,
        #libraryPathNodeCard, #practiceOptionsSurface, #practiceAnswerSurface,
        #questionAiSurface,
        #studySummaryCard, #reviewOptionsSurface, #reviewAnswerSurface,
        #studyDueList, #answerTableView, #sharedStorageFileTree,
        #announcementTree {
            border-radius: %1px;
        }
        QPushButton, QToolButton, QComboBox, QLineEdit, QTextEdit, QTextBrowser,
        QSpinBox, QDoubleSpinBox,
        QTreeWidget, QListWidget, QTableView {
            border-radius: %2px;
        }
        QComboBox::drop-down {
            border-top-right-radius: %2px;
            border-bottom-right-radius: %2px;
        }
        QComboBox QAbstractItemView { border-radius: %2px; }
        #brandMark, #railBrand, #practiceImage,
        #questionOverviewStat, #questionOverviewNumberButton {
            border-radius: %3px;
        }
    )CSS").arg(bounded).arg(compact).arg(tiny);
}

QString blendedColor(const QColor &foreground, const QColor &background, qreal amount)
{
    const qreal bounded = std::clamp(amount, 0.0, 1.0);
    return QColor::fromRgbF(
        background.redF() + (foreground.redF() - background.redF()) * bounded,
        background.greenF() + (foreground.greenF() - background.greenF()) * bounded,
        background.blueF() + (foreground.blueF() - background.blueF()) * bounded)
        .name();
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
    sharedStorageRoot_ = platform::SharedStoragePlatform::defaultRootPath(dataRoot_);
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
    announcementButtonContainer_ = new QWidget(topBar);
    announcementButtonContainer_->setObjectName(
        QStringLiteral("announcementButtonContainer"));
    announcementButtonContainer_->setFixedSize(40, 40);
    announcementButton_ = new QToolButton(announcementButtonContainer_);
    announcementButton_->setObjectName(QStringLiteral("announcementButton"));
    announcementButton_->setAccessibleName(QStringLiteral("公告栏"));
    announcementButton_->setToolTip(QStringLiteral("公告栏"));
    announcementButton_->setGeometry(0, 0, 40, 40);
    assignMaterialIcon(announcementButton_, QStringLiteral("mail"));
    announcementUnreadDot_ = new QLabel(announcementButtonContainer_);
    announcementUnreadDot_->setObjectName(QStringLiteral("announcementUnreadDot"));
    announcementUnreadDot_->setFixedSize(10, 10);
    announcementUnreadDot_->move(27, 3);
    announcementUnreadDot_->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(announcementButton_, &QToolButton::clicked,
            this, &AppWindow::openAnnouncementBoard);
    topLayout->addWidget(brandMark_);
    topLayout->addWidget(pageTitle_);
    topLayout->addWidget(announcementButtonContainer_);
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
    connect(handwritingPage_, &HandwritingPage::returnToNotebookLibrary,
            this, &AppWindow::handleFreeNotebookReturn);
    rootStack_->addWidget(shellRoot_);
    rootStack_->addWidget(handwritingPage_);
    setCentralWidget(rootStack_);

    buildNavigation();
    QSettings settings;
    QString theme = settings.value(QStringLiteral("ui/theme"), QStringLiteral("light"))
                        .toString();
    if (theme == QStringLiteral("endfield")) {
        theme = QStringLiteral("dark");
    }
    applyTheme(theme);
    applyResponsiveLayout();
    navigateTo(Section::Home);
    refreshLibraryStats();
    refreshInstalledBankList();
    refreshSharedStorageState();
    refreshReviewData();
    initializeStudyTracking();
    updateAnnouncementUnreadState();
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive) {
                    return;
                }
                refreshSharedStorageState();
                if (platform::SharedStoragePlatform::hasDirectAccess()) {
                    startSharedBankSync(false);
                }
            });
    QTimer::singleShot(0, this, [this] {
        if (platform::SharedStoragePlatform::hasDirectAccess()) {
            startSharedBankSync(false);
        }
    });
    QTimer::singleShot(2500, this, &AppWindow::scheduleAutomaticBankReleaseCheck);
    QTimer::singleShot(1200, this, &AppWindow::scheduleAutomaticAnnouncementCheck);
    QTimer::singleShot(3600, this, &AppWindow::scheduleAutomaticAppUpdateCheck);
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
    if (announcementButtonContainer_) {
        announcementButtonContainer_->setVisible(section == Section::Home);
    }
    if (section == Section::Settings) {
        refreshLibraryIconPickers();
    }
    pages_->setCurrentIndex(sectionIndex(section));
    pageTitle_->setText(sectionTitle(section));
    if (auto *button = railButtons_->button(sectionIndex(section))) {
        button->setChecked(true);
    }
    if (auto *button = bottomButtons_->button(sectionIndex(section))) {
        button->setChecked(true);
    }
    if (section == Section::Home) {
        refreshSavedProgressWidget();
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
            && examPage_ && studyStack_->currentWidget() == examPage_) {
            examPage_->handleBack();
            event->accept();
            return;
        }
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
        if (currentSection_ == Section::Study && studyStack_
            && notebookLibraryPage_ && studyStack_->currentWidget() == notebookLibraryPage_) {
            showStudyHub();
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
            if (automaticPracticePersistenceEnabled(practicePage_->session().mode)) {
                saveActivePracticeSession();
            }
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
            QStringList parentPath = libraryPath_;
            parentPath.removeLast();
            navigateLibraryPath(parentPath);
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

    auto *savedProgressRow = new QHBoxLayout;
    savedProgressRow->setContentsMargins(0, 0, 0, 0);
    savedProgressRow->setSpacing(0);
    homeSavedProgressCard_ = new QFrame(content);
    homeSavedProgressCard_->setObjectName(QStringLiteral("homeSavedProgressCard"));
    homeSavedProgressCard_->setAccessibleName(QStringLiteral("上次学习进度"));
    auto *savedProgressLayout = new QVBoxLayout(homeSavedProgressCard_);
    savedProgressLayout->setContentsMargins(16, 14, 16, 14);
    savedProgressLayout->setSpacing(8);
    homeSavedProgressHint_ = new QWidget(homeSavedProgressCard_);
    homeSavedProgressHint_->setObjectName(QStringLiteral("homeSavedProgressHint"));
    auto *hintLayout = new QHBoxLayout(homeSavedProgressHint_);
    hintLayout->setContentsMargins(10, 7, 8, 7);
    hintLayout->setSpacing(8);
    auto *hintText = new QLabel(
        QStringLiteral("这里可以继续上一次练习"), homeSavedProgressHint_);
    hintText->setObjectName(QStringLiteral("homeSavedProgressHintText"));
    auto *dismissHint = new QPushButton(
        QStringLiteral("不再提示"), homeSavedProgressHint_);
    dismissHint->setObjectName(QStringLiteral("homeSavedProgressHintDismiss"));
    dismissHint->setFlat(true);
    connect(dismissHint, &QPushButton::clicked, this, [this] {
        QSettings settings;
        settings.setValue(QStringLiteral("home/showSavedProgressHint"), false);
        settings.sync();
        if (homeSavedProgressHint_) {
            homeSavedProgressHint_->hide();
        }
    });
    hintLayout->addWidget(hintText, 1);
    hintLayout->addWidget(dismissHint);
    savedProgressLayout->addWidget(homeSavedProgressHint_);
    homeSavedProgressTitle_ = new QLabel(homeSavedProgressCard_);
    homeSavedProgressTitle_->setObjectName(QStringLiteral("homeSavedProgressTitle"));
    homeSavedProgressTitle_->setWordWrap(true);
    homeSavedProgressSummary_ = new QLabel(homeSavedProgressCard_);
    homeSavedProgressSummary_->setObjectName(QStringLiteral("homeSavedProgressSummary"));
    homeSavedProgressSummary_->setWordWrap(true);
    homeSavedProgressButton_ = new QPushButton(
        QStringLiteral("继续学习"), homeSavedProgressCard_);
    homeSavedProgressButton_->setObjectName(QStringLiteral("homeSavedProgressButton"));
    assignMaterialIcon(homeSavedProgressButton_, QStringLiteral("chevron_right"));
    homeSavedProgressButton_->setMinimumHeight(40);
    connect(homeSavedProgressButton_, &QPushButton::clicked,
            this, &AppWindow::resumeSavedProgress);
    savedProgressLayout->addWidget(homeSavedProgressTitle_);
    savedProgressLayout->addWidget(homeSavedProgressSummary_);
    savedProgressLayout->addWidget(homeSavedProgressButton_);
    homeSavedProgressCard_->hide();
    savedProgressRow->addWidget(homeSavedProgressCard_, 0, Qt::AlignLeft);
    savedProgressRow->addStretch(1);
    layout->addLayout(savedProgressRow);

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
    auto *notebooks = new QPushButton(QStringLiteral("自由笔记"), homeActionsSection_);
    assignMaterialIcon(notebooks, QStringLiteral("stylus"));
    notebooks->setObjectName(QStringLiteral("homeNotebookAction"));
    notebooks->setMinimumHeight(48);
    connect(notebooks, &QPushButton::clicked, this, &AppWindow::showNotebookLibrary);
    homeActionButtons_ = {library, study, notebooks};
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
        QStringLiteral("共享题库目录"), QStringLiteral("emptyStateTitle"));
    librarySummaryText_ = heading(QString(), QStringLiteral("pageSupportingText"));
    surfaceLayout->addWidget(libraryEmptyTitle_);
    surfaceLayout->addWidget(librarySummaryText_);
    auto *actions = new QGridLayout;
    actions->setHorizontalSpacing(8);
    actions->setVerticalSpacing(8);
    libraryImportButton_ = new QPushButton(QStringLiteral("扫描题库"), surface);
    libraryImportButton_->setObjectName(QStringLiteral("installXiaoyiButton"));
    libraryImportButton_->setMinimumHeight(44);
    assignMaterialIcon(libraryImportButton_, QStringLiteral("download"));
    connect(libraryImportButton_, &QPushButton::clicked,
            this, &AppWindow::startXiaoyiDirectoryInstall);
    libraryOpenStorageButton_ = new QPushButton(QStringLiteral("打开目录"), surface);
    libraryOpenStorageButton_->setObjectName(QStringLiteral("openSharedStorageButton"));
    libraryOpenStorageButton_->setMinimumHeight(44);
    assignMaterialIcon(libraryOpenStorageButton_, QStringLiteral("library_books"));
    connect(libraryOpenStorageButton_, &QPushButton::clicked,
            this, &AppWindow::openSharedStorageDirectory);
    libraryStorageAccessButton_ = new QPushButton(QStringLiteral("授权访问"), surface);
    libraryStorageAccessButton_->setObjectName(QStringLiteral("sharedStorageAccessButton"));
    libraryStorageAccessButton_->setMinimumHeight(44);
    assignMaterialIcon(libraryStorageAccessButton_, QStringLiteral("settings"));
    connect(libraryStorageAccessButton_, &QPushButton::clicked,
            this, &AppWindow::requestSharedStorageAccess);
    libraryCancelButton_ = new QPushButton(QStringLiteral("取消"), surface);
    libraryCancelButton_->setObjectName(QStringLiteral("cancelXiaoyiInstallButton"));
    libraryCancelButton_->setMinimumHeight(44);
    libraryCancelButton_->hide();
    actions->addWidget(libraryImportButton_, 0, 0);
    actions->addWidget(libraryOpenStorageButton_, 0, 1);
    actions->addWidget(libraryStorageAccessButton_, 1, 0);
    actions->addWidget(libraryCancelButton_, 1, 1);
    surfaceLayout->addLayout(actions);

    libraryImportProgress_ = new QProgressBar(surface);
    libraryImportProgress_->setObjectName(QStringLiteral("xiaoyiImportProgress"));
    libraryImportProgress_->setTextVisible(true);
    libraryImportProgress_->hide();
    surfaceLayout->addWidget(libraryImportProgress_);
    libraryImportStatus_ = heading(QString(), QStringLiteral("pageSupportingText"));
    libraryImportStatus_->setObjectName(QStringLiteral("libraryImportStatus"));
    libraryImportStatus_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    libraryImportStatus_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    surfaceLayout->addWidget(libraryImportStatus_);
    surfaceLayout->addWidget(heading(QStringLiteral("存储文件"), QStringLiteral("sectionHeading")));
    libraryFilesTree_ = new QTreeWidget(surface);
    libraryFilesTree_->setObjectName(QStringLiteral("sharedStorageFileTree"));
    libraryFilesTree_->setHeaderLabels({QStringLiteral("名称"), QStringLiteral("状态")});
    libraryFilesTree_->setRootIsDecorated(true);
    libraryFilesTree_->setAnimated(true);
    libraryFilesTree_->setMinimumHeight(220);
    libraryFilesTree_->setMaximumHeight(340);
    libraryFilesTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    libraryFilesTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    surfaceLayout->addWidget(libraryFilesTree_);
    connect(libraryFilesTree_, &QTreeWidget::itemSelectionChanged,
            this, &AppWindow::updateSharedFileActions);

    libraryFileActionsLayout_ = new QGridLayout;
    libraryFileActionsLayout_->setHorizontalSpacing(6);
    libraryFileActionsLayout_->setVerticalSpacing(6);
    libraryNewFolderButton_ = new QPushButton(QStringLiteral("新建层级"), surface);
    libraryNewFolderButton_->setObjectName(QStringLiteral("sharedStorageNewFolderButton"));
    connect(libraryNewFolderButton_, &QPushButton::clicked,
            this, &AppWindow::createSharedBankFolder);
    libraryImportJsonButton_ = new QPushButton(QStringLiteral("导入 JSON"), surface);
    libraryImportJsonButton_->setObjectName(QStringLiteral("sharedStorageImportJsonButton"));
    connect(libraryImportJsonButton_, &QPushButton::clicked,
            this, &AppWindow::importSharedBankJson);
    libraryRenameButton_ = new QPushButton(QStringLiteral("重命名"), surface);
    libraryRenameButton_->setObjectName(QStringLiteral("sharedStorageRenameButton"));
    connect(libraryRenameButton_, &QPushButton::clicked,
            this, &AppWindow::renameSelectedSharedEntry);
    libraryMoveButton_ = new QPushButton(QStringLiteral("移动"), surface);
    libraryMoveButton_->setObjectName(QStringLiteral("sharedStorageMoveButton"));
    connect(libraryMoveButton_, &QPushButton::clicked,
            this, &AppWindow::moveSelectedSharedEntry);
    libraryRecycleButton_ = new QPushButton(QStringLiteral("移入回收站"), surface);
    libraryRecycleButton_->setObjectName(QStringLiteral("sharedStorageRecycleButton"));
    connect(libraryRecycleButton_, &QPushButton::clicked,
            this, &AppWindow::recycleSelectedSharedEntry);
    libraryRestoreButton_ = new QPushButton(QStringLiteral("恢复"), surface);
    libraryRestoreButton_->setObjectName(QStringLiteral("sharedStorageRestoreButton"));
    connect(libraryRestoreButton_, &QPushButton::clicked,
            this, &AppWindow::restoreSelectedSharedEntry);
    libraryPermanentDeleteButton_ = new QPushButton(QStringLiteral("彻底删除"), surface);
    libraryPermanentDeleteButton_->setObjectName(
        QStringLiteral("sharedStoragePermanentDeleteButton"));
    connect(libraryPermanentDeleteButton_, &QPushButton::clicked,
            this, &AppWindow::permanentlyDeleteSelectedSharedEntry);
    libraryFileActionsLayout_->addWidget(libraryNewFolderButton_, 0, 0);
    libraryFileActionsLayout_->addWidget(libraryImportJsonButton_, 0, 1);
    libraryFileActionsLayout_->addWidget(libraryRenameButton_, 0, 2);
    libraryFileActionsLayout_->addWidget(libraryMoveButton_, 1, 0);
    libraryFileActionsLayout_->addWidget(libraryRecycleButton_, 1, 1, 1, 2);
    libraryFileActionsLayout_->addWidget(libraryRestoreButton_, 0, 0);
    libraryFileActionsLayout_->addWidget(libraryPermanentDeleteButton_, 0, 1, 1, 2);
    surfaceLayout->addLayout(libraryFileActionsLayout_);
    surface->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
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
            QStringList parentPath = libraryPath_;
            parentPath.removeLast();
            navigateLibraryPath(parentPath);
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
    libraryEditButton_ = new QToolButton(browserHeader);
    libraryEditButton_->setObjectName(QStringLiteral("libraryEditOrderButton"));
    libraryEditButton_->setText(QStringLiteral("排序"));
    libraryEditButton_->setToolTip(QStringLiteral("编辑当前层级顺序"));
    libraryEditButton_->setAccessibleName(QStringLiteral("编辑当前层级顺序"));
    libraryEditButton_->setProperty("quizappIcon", QStringLiteral("grid_view"));
    libraryEditButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    libraryEditButton_->setCheckable(true);
    libraryEditButton_->setMinimumSize(76, 40);
    connect(libraryEditButton_, &QToolButton::toggled, this, [this](bool enabled) {
        libraryEditMode_ = enabled;
        for (QPushButton *button : std::as_const(libraryScopeModeButtons_)) {
            button->setVisible(!enabled);
        }
        if (libraryBanksSurface_) {
            libraryBanksSurface_->setProperty("libraryReorderEnabled", enabled);
        }
        refreshInstalledBankList();
    });
    pathRow->addWidget(libraryEditButton_);
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

    auto *banksSurface = new LibraryDropSurface(libraryBrowserColumn_);
    banksSurface->setObjectName(QStringLiteral("libraryBanksSurface"));
    banksSurface->setProperty("libraryReorderEnabled", libraryEditMode_);
    banksSurface->orderChanged = [this](const QVector<QStringList> &orderedPaths) {
        saveLibraryNodeOrder(orderedPaths);
    };
    libraryBanksSurface_ = banksSurface;
    libraryBanksLayout_ = new QVBoxLayout(banksSurface);
    libraryBanksLayout_->setContentsMargins(0, 0, 0, 0);
    libraryBanksLayout_->setSpacing(8);
    browserColumnLayout->addWidget(banksSurface);
    browserColumnLayout->addStretch();

    libraryResponsiveLayout_->addWidget(librarySourceColumn_, 0, 0);
    libraryResponsiveLayout_->addWidget(libraryBrowserColumn_, 1, 0);
    layout->addLayout(libraryResponsiveLayout_, 1);

    libraryImportWatcher_ = new QFutureWatcher<services::BankDirectorySyncResult>(this);
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
    libraryPageScroll_ = qobject_cast<QScrollArea *>(centeredPage(content));
    return libraryPageScroll_;
}

QWidget *AppWindow::createStudyPage()
{
    studyStack_ = new QStackedWidget;
    studyStack_->setObjectName(QStringLiteral("studyStack"));
    studyHubPage_ = new StudyHubPage(studyStack_);
    practicePage_ = new PracticePage(dataRoot_, studyStack_);
    aiQuestionAnalysisService_ = new services::AiQuestionAnalysisService(
        databasePath_, dataRoot_, this);
    answerTablePage_ = new AnswerTablePage(studyStack_);
    reviewPage_ = new ReviewPage(dataRoot_, studyStack_);
    examPage_ = new ExamPage(databasePath_, studyStack_);
    notebookLibraryPage_ = new NotebookLibraryPage(
        databasePath_, dataRoot_, studyStack_);
    practiceSaveTimer_ = new QTimer(this);
    practiceSaveTimer_->setSingleShot(true);
    practiceSaveTimer_->setInterval(450);
    connect(practiceSaveTimer_, &QTimer::timeout, this, [this] {
        if (practicePage_ && practicePage_->hasActiveSession()
            && automaticPracticePersistenceEnabled(practicePage_->session().mode)) {
            saveActivePracticeSession();
        }
    });
    connect(practicePage_, &PracticePage::backRequested, this, [this] {
        if (practiceSaveTimer_ && practiceSaveTimer_->isActive()) {
            practiceSaveTimer_->stop();
        }
        if (automaticPracticePersistenceEnabled(practicePage_->session().mode)) {
            QString error;
            if (!saveActivePracticeSession(&error) && libraryImportStatus_) {
                libraryImportStatus_->setText(QStringLiteral("保存练习失败：%1").arg(error));
            }
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
    connect(practicePage_, &PracticePage::manualSaveRequested,
            this, &AppWindow::savePracticeManually);
    connect(practicePage_, &PracticePage::resetRequested,
            this, &AppWindow::resetActivePractice);
    connect(practicePage_, &PracticePage::sessionChanged, this, [this] {
        schedulePracticeSessionSave();
    });
    connect(practicePage_, &PracticePage::wrongBookToggleRequested,
            this, &AppWindow::setWrongBookMembership);
    connect(practicePage_, &PracticePage::reviewToggleRequested,
            this, &AppWindow::setReviewMembership);
    connect(practicePage_, &PracticePage::currentQuestionChanged,
            this, [this](const domain::Question &question) {
                QString error;
                const auto record = aiQuestionAnalysisService_->cachedRecord(question, &error);
                if (error.isEmpty() && record.has_value()) {
                    practicePage_->setAiRecord(*record);
                }
            });
    connect(practicePage_, &PracticePage::aiAnalysisRequested,
            aiQuestionAnalysisService_, &services::AiQuestionAnalysisService::analyze);
    connect(practicePage_, &PracticePage::aiAnalysisCancelRequested,
            aiQuestionAnalysisService_, &services::AiQuestionAnalysisService::cancel);
    connect(aiQuestionAnalysisService_,
            &services::AiQuestionAnalysisService::analysisStarted,
            practicePage_, &PracticePage::setAiLoading);
    connect(aiQuestionAnalysisService_,
            &services::AiQuestionAnalysisService::analysisFinished,
            practicePage_, &PracticePage::setAiRecord);
    connect(aiQuestionAnalysisService_,
            &services::AiQuestionAnalysisService::analysisFailed,
            practicePage_, &PracticePage::setAiError);
    connect(aiQuestionAnalysisService_,
            &services::AiQuestionAnalysisService::analysisCancelled,
            practicePage_, &PracticePage::setAiCancelled);
    connect(answerTablePage_, &AnswerTablePage::backRequested, this, [this] {
        if (practiceSaveTimer_ && practiceSaveTimer_->isActive()) {
            practiceSaveTimer_->stop();
        }
        if (automaticPracticePersistenceEnabled(practicePage_->session().mode)) {
            QString error;
            if (!saveActivePracticeSession(&error) && libraryImportStatus_) {
                libraryImportStatus_->setText(
                    QStringLiteral("保存答案表进度失败：%1").arg(error));
            }
        }
        navigateTo(Section::Library);
    });
    connect(answerTablePage_, &AnswerTablePage::manualSaveRequested,
            this, &AppWindow::savePracticeManually);
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
    connect(studyHubPage_, &StudyHubPage::openExamRequested,
            this, &AppWindow::openExamPage);
    connect(examPage_, &ExamPage::backRequested, this, &AppWindow::showStudyHub);
    connect(examPage_, &ExamPage::handwritingRequested,
            this, &AppWindow::openHandwriting);
    connect(notebookLibraryPage_, &NotebookLibraryPage::backRequested,
            this, &AppWindow::showStudyHub);
    connect(notebookLibraryPage_, &NotebookLibraryPage::openRequested,
            this, &AppWindow::openFreeNotebook);
    connect(examPage_, &ExamPage::examActivityChanged, this, [this](bool active) {
        if (active) {
            setStudyActivity(domain::StudyActivity::Exam, QStringLiteral("exam"));
        } else if (studyStack_ && examPage_
                   && studyStack_->currentWidget() == examPage_) {
            clearStudyActivity();
        }
    });
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
    studyStack_->addWidget(examPage_);
    studyStack_->addWidget(notebookLibraryPage_);
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
    surface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *surfaceLayout = new QGridLayout(surface);
    surfaceLayout->setContentsMargins(20, 20, 20, 20);
    surfaceLayout->setHorizontalSpacing(16);
    surfaceLayout->setVerticalSpacing(12);

    auto *sectionTitle = new QLabel(QStringLiteral("外观与动效"), surface);
    sectionTitle->setObjectName(QStringLiteral("settingsSectionHeading"));

    auto *themeLabel = new QLabel(QStringLiteral("外观模式"), surface);
    themeLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    themeChoice_ = new ChoiceComboBox(surface);
    themeChoice_->setObjectName(QStringLiteral("themeChoice"));
    themeChoice_->addItem(QStringLiteral("跟随系统"), QStringLiteral("system"));
    themeChoice_->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
    themeChoice_->addItem(QStringLiteral("深色"), QStringLiteral("dark"));
    themeChoice_->setMinimumHeight(42);

    QSettings settings;
    QString theme = settings.value(QStringLiteral("ui/theme"), QStringLiteral("light"))
                        .toString();
    QString paletteId = settings.value(QStringLiteral("ui/palette"), QStringLiteral("forest"))
                            .toString();
    if (theme == QStringLiteral("endfield")) {
        theme = QStringLiteral("dark");
        paletteId = QStringLiteral("endfield");
    }
    const int themeIndex = themeChoice_->findData(theme);
    themeChoice_->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);

    auto *paletteLabel = new QLabel(QStringLiteral("颜色主题"), surface);
    paletteLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    paletteChoice_ = new ChoiceComboBox(surface);
    paletteChoice_->setObjectName(QStringLiteral("paletteChoice"));
    for (const ThemePalette &preset : ThemePalettes::legacyPresets()) {
        paletteChoice_->addItem(preset.name, preset.id);
    }
    paletteChoice_->addItem(
        QStringLiteral("终末地风格（高级）"), QStringLiteral("endfield"));
    paletteChoice_->setMinimumHeight(42);
    const int paletteIndex = paletteChoice_->findData(paletteId);
    paletteChoice_->setCurrentIndex(paletteIndex >= 0 ? paletteIndex : 1);

    const QColor savedPrimary(
        settings.value(QStringLiteral("ui/primary")).toString());
    primaryColorHex_ = savedPrimary.isValid()
        ? savedPrimary.name(QColor::HexRgb)
        : ThemePalettes::find(paletteChoice_->currentData().toString())
              .primary.name(QColor::HexRgb);
    auto *primaryLabel = new QLabel(QStringLiteral("自定义主色"), surface);
    primaryLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    auto *primaryControl = new QWidget(surface);
    auto *primaryLayout = new QHBoxLayout(primaryControl);
    primaryLayout->setContentsMargins(0, 0, 0, 0);
    primaryLayout->setSpacing(10);
    primaryColorButton_ = new QPushButton(primaryControl);
    primaryColorButton_->setObjectName(QStringLiteral("primaryColorButton"));
    primaryColorButton_->setFixedSize(44, 34);
    primaryColorButton_->setToolTip(QStringLiteral("选择自定义主色"));
    primaryColorButton_->setAccessibleName(QStringLiteral("选择自定义主色"));
    primaryColorValue_ = new QLabel(primaryControl);
    primaryColorValue_->setObjectName(QStringLiteral("primaryColorValue"));
    primaryColorValue_->setMinimumWidth(0);
    primaryColorValue_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *resetPrimary = new QPushButton(QStringLiteral("恢复主题色"), primaryControl);
    resetPrimary->setObjectName(QStringLiteral("resetPrimaryColorButton"));
    connect(primaryColorButton_, &QPushButton::clicked,
            this, &AppWindow::choosePrimaryColor);
    connect(resetPrimary, &QPushButton::clicked,
            this, &AppWindow::resetPrimaryColorToPalette);
    primaryLayout->addWidget(primaryColorButton_);
    primaryLayout->addWidget(primaryColorValue_, 1);
    primaryLayout->addWidget(resetPrimary);

    auto *hierarchyIconsLabel = new QLabel(QStringLiteral("题库层级图案"), surface);
    hierarchyIconsLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    libraryIconPickersContainer_ = new QWidget(surface);
    libraryIconPickersContainer_->setObjectName(
        QStringLiteral("libraryIconPickersContainer"));
    libraryIconPickersLayout_ = new QGridLayout(libraryIconPickersContainer_);
    libraryIconPickersLayout_->setContentsMargins(0, 0, 0, 0);
    libraryIconPickersLayout_->setHorizontalSpacing(10);
    libraryIconPickersLayout_->setVerticalSpacing(6);
    libraryIconPickersLayout_->setColumnStretch(1, 1);
    refreshLibraryIconPickers();

    auto *previewLabel = new QLabel(QStringLiteral("主题预览"), surface);
    previewLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    themePreview_ = new ThemePreview(surface);
    themePreview_->setThemeId(themeChoice_->currentData().toString());
    themePreview_->setPaletteId(paletteChoice_->currentData().toString());
    themePreview_->setPrimaryColor(QColor(primaryColorHex_));
    updatePrimaryColorControl();
    connect(themeChoice_, &QComboBox::currentIndexChanged, this, [this] {
        themePreview_->setThemeId(themeChoice_->currentData().toString());
    });
    connect(paletteChoice_, &QComboBox::currentIndexChanged, this, [this] {
        themePreview_->setPaletteId(paletteChoice_->currentData().toString());
        const bool endfield = paletteChoice_->currentData().toString()
            == QStringLiteral("endfield");
        if (!endfield) {
            primaryColorHex_ = ThemePalettes::find(
                paletteChoice_->currentData().toString()).primary.name(QColor::HexRgb);
        }
        updatePrimaryColorControl();
        if (endfield) {
            if (themeChoice_->isEnabled()) {
                themeChoice_->setProperty(
                    "modeBeforeEndfield", themeChoice_->currentData().toString());
            }
            const int darkIndex = themeChoice_->findData(QStringLiteral("dark"));
            themeChoice_->setCurrentIndex(darkIndex);
            themeChoice_->setEnabled(false);
        } else if (!themeChoice_->isEnabled()) {
            themeChoice_->setEnabled(true);
            const int previousIndex = themeChoice_->findData(
                themeChoice_->property("modeBeforeEndfield").toString());
            if (previousIndex >= 0) {
                themeChoice_->setCurrentIndex(previousIndex);
            }
        }
    });
    if (paletteChoice_->currentData().toString() == QStringLiteral("endfield")) {
        const int darkIndex = themeChoice_->findData(QStringLiteral("dark"));
        themeChoice_->setCurrentIndex(darkIndex);
        themeChoice_->setEnabled(false);
    }

    auto *cornerRadiusLabel = new QLabel(QStringLiteral("组件圆角"), surface);
    cornerRadiusLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    auto *cornerRadiusControl = new QWidget(surface);
    auto *cornerRadiusLayout = new QHBoxLayout(cornerRadiusControl);
    cornerRadiusLayout->setContentsMargins(0, 0, 0, 0);
    cornerRadiusLayout->setSpacing(10);
    cornerRadiusChoice_ = new QSlider(Qt::Horizontal, cornerRadiusControl);
    cornerRadiusChoice_->setObjectName(QStringLiteral("cornerRadiusChoice"));
    cornerRadiusChoice_->setRange(0, 18);
    cornerRadiusChoice_->setSingleStep(1);
    cornerRadiusChoice_->setPageStep(2);
    cornerRadiusChoice_->setValue(
        std::clamp(settings.value(QStringLiteral("ui/cornerRadius"), 7).toInt(), 0, 18));
    themePreview_->setCornerRadius(cornerRadiusChoice_->value());
    cornerRadiusValue_ = new QLabel(
        QStringLiteral("%1 px").arg(cornerRadiusChoice_->value()), cornerRadiusControl);
    cornerRadiusValue_->setObjectName(QStringLiteral("cornerRadiusValue"));
    cornerRadiusValue_->setMinimumWidth(42);
    cornerRadiusValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    cornerRadiusLayout->addWidget(cornerRadiusChoice_, 1);
    cornerRadiusLayout->addWidget(cornerRadiusValue_);
    connect(cornerRadiusChoice_, &QSlider::valueChanged, this, [this](int value) {
        cornerRadiusValue_->setText(QStringLiteral("%1 px").arg(value));
        themePreview_->setCornerRadius(value);
        applyTheme(themeChoice_->currentData().toString());
    });

    reduceMotionChoice_ = new QCheckBox(QStringLiteral("减少界面动画"), surface);
    reduceMotionChoice_->setObjectName(QStringLiteral("reduceMotionChoice"));
    reduceMotionChoice_->setChecked(
        settings.value(QStringLiteral("ui/reduceMotion"), false).toBool());

    auto *practiceSurface = new QFrame(content);
    practiceSurface->setObjectName(QStringLiteral("practiceSettingsSurface"));
    practiceSurface->setMaximumWidth(760);
    practiceSurface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *practiceLayout = new QVBoxLayout(practiceSurface);
    practiceLayout->setContentsMargins(20, 20, 20, 20);
    practiceLayout->setSpacing(12);
    auto *practiceTitle = new QLabel(QStringLiteral("练习与保存"), practiceSurface);
    practiceTitle->setObjectName(QStringLiteral("settingsSectionHeading"));
    practiceLayout->addWidget(practiceTitle);
    autoSaveOnExitChoice_ = new QCheckBox(
        QStringLiteral("退出顺序或随机练习时自动保存"), practiceSurface);
    autoSaveOnExitChoice_->setObjectName(QStringLiteral("autoSaveOnExitChoice"));
    autoSaveOnExitChoice_->setChecked(
        settings.value(QStringLiteral("practice/autoSaveOnExit"), true).toBool());
    mergePracticeProgressChoice_ = new QCheckBox(
        QStringLiteral("顺序与随机练习共享作答进度"), practiceSurface);
    mergePracticeProgressChoice_->setObjectName(
        QStringLiteral("mergePracticeProgressChoice"));
    mergePracticeProgressChoice_->setChecked(
        settings.value(QStringLiteral("practice/progressScopePolicy"),
                       QStringLiteral("separate")).toString()
        == QStringLiteral("merged"));
    randomReshuffleChoice_ = new QCheckBox(
        QStringLiteral("随机练习重做时重新打乱题序"), practiceSurface);
    randomReshuffleChoice_->setObjectName(QStringLiteral("randomReshuffleChoice"));
    randomReshuffleChoice_->setChecked(
        settings.value(QStringLiteral("practice/randomReshuffleOnReset"), true).toBool());
    rememberReviewPositionChoice_ = new QCheckBox(
        QStringLiteral("记住背题模式上次位置"), practiceSurface);
    rememberReviewPositionChoice_->setObjectName(
        QStringLiteral("rememberReviewPositionChoice"));
    rememberReviewPositionChoice_->setChecked(
        settings.value(QStringLiteral("view/rememberReviewPosition"), true).toBool());
    rememberAnswerLookupPositionChoice_ = new QCheckBox(
        QStringLiteral("记住答案表上次位置"), practiceSurface);
    rememberAnswerLookupPositionChoice_->setObjectName(
        QStringLiteral("rememberAnswerLookupPositionChoice"));
    rememberAnswerLookupPositionChoice_->setChecked(
        settings.value(QStringLiteral("view/rememberAnswerLookupPosition"), true).toBool());
    showSavedProgressChoice_ = new QCheckBox(
        QStringLiteral("首页显示上次学习进度"), practiceSurface);
    showSavedProgressChoice_->setObjectName(QStringLiteral("showSavedProgressChoice"));
    showSavedProgressChoice_->setChecked(
        settings.value(QStringLiteral("home/showSavedProgressEntry"), true).toBool());

    auto *savedProgressWidthGrid = new QGridLayout;
    savedProgressWidthGrid->setContentsMargins(0, 2, 0, 0);
    savedProgressWidthGrid->setHorizontalSpacing(10);
    savedProgressWidthGrid->setVerticalSpacing(8);
    auto *phoneWidthLabel = new QLabel(QStringLiteral("手机小组件宽度"), practiceSurface);
    phoneWidthLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    savedProgressPhoneWidthChoice_ = new QSlider(Qt::Horizontal, practiceSurface);
    savedProgressPhoneWidthChoice_->setObjectName(
        QStringLiteral("savedProgressPhoneWidthChoice"));
    savedProgressPhoneWidthChoice_->setRange(
        kSavedProgressPhoneMinWidth, kSavedProgressPhoneMaxWidth);
    savedProgressPhoneWidthChoice_->setSingleStep(10);
    savedProgressPhoneWidthChoice_->setPageStep(20);
    savedProgressPhoneWidthChoice_->setValue(std::clamp(
        settings.value(QStringLiteral("home/savedProgressWidthPhone"),
                       kSavedProgressPhoneDefaultWidth).toInt(),
        kSavedProgressPhoneMinWidth, kSavedProgressPhoneMaxWidth));
    savedProgressPhoneWidthValue_ = new QLabel(practiceSurface);
    savedProgressPhoneWidthValue_->setObjectName(
        QStringLiteral("savedProgressPhoneWidthValue"));
    savedProgressPhoneWidthValue_->setMinimumWidth(52);
    savedProgressPhoneWidthValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    savedProgressPhoneWidthValue_->setText(
        QStringLiteral("%1 px").arg(savedProgressPhoneWidthChoice_->value()));

    auto *tabletWidthLabel = new QLabel(QStringLiteral("平板小组件宽度"), practiceSurface);
    tabletWidthLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    savedProgressTabletWidthChoice_ = new QSlider(Qt::Horizontal, practiceSurface);
    savedProgressTabletWidthChoice_->setObjectName(
        QStringLiteral("savedProgressTabletWidthChoice"));
    savedProgressTabletWidthChoice_->setRange(
        kSavedProgressTabletMinWidth, kSavedProgressTabletMaxWidth);
    savedProgressTabletWidthChoice_->setSingleStep(20);
    savedProgressTabletWidthChoice_->setPageStep(40);
    savedProgressTabletWidthChoice_->setValue(std::clamp(
        settings.value(QStringLiteral("home/savedProgressWidthTablet"),
                       kSavedProgressTabletDefaultWidth).toInt(),
        kSavedProgressTabletMinWidth, kSavedProgressTabletMaxWidth));
    savedProgressTabletWidthValue_ = new QLabel(practiceSurface);
    savedProgressTabletWidthValue_->setObjectName(
        QStringLiteral("savedProgressTabletWidthValue"));
    savedProgressTabletWidthValue_->setMinimumWidth(52);
    savedProgressTabletWidthValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    savedProgressTabletWidthValue_->setText(
        QStringLiteral("%1 px").arg(savedProgressTabletWidthChoice_->value()));

    savedProgressWidthGrid->addWidget(phoneWidthLabel, 0, 0);
    savedProgressWidthGrid->addWidget(savedProgressPhoneWidthChoice_, 0, 1);
    savedProgressWidthGrid->addWidget(savedProgressPhoneWidthValue_, 0, 2);
    savedProgressWidthGrid->addWidget(tabletWidthLabel, 1, 0);
    savedProgressWidthGrid->addWidget(savedProgressTabletWidthChoice_, 1, 1);
    savedProgressWidthGrid->addWidget(savedProgressTabletWidthValue_, 1, 2);
    savedProgressWidthGrid->setColumnStretch(1, 1);
    connect(showSavedProgressChoice_, &QCheckBox::toggled,
            this, &AppWindow::refreshSavedProgressWidget);
    connect(savedProgressPhoneWidthChoice_, &QSlider::valueChanged,
            this, [this](int value) {
                savedProgressPhoneWidthValue_->setText(
                    QStringLiteral("%1 px").arg(value));
                updateSavedProgressWidgetSize();
            });
    connect(savedProgressTabletWidthChoice_, &QSlider::valueChanged,
            this, [this](int value) {
                savedProgressTabletWidthValue_->setText(
                    QStringLiteral("%1 px").arg(value));
                updateSavedProgressWidgetSize();
            });
    practiceLayout->addWidget(autoSaveOnExitChoice_);
    practiceLayout->addWidget(mergePracticeProgressChoice_);
    practiceLayout->addWidget(randomReshuffleChoice_);
    practiceLayout->addWidget(rememberReviewPositionChoice_);
    practiceLayout->addWidget(rememberAnswerLookupPositionChoice_);
    practiceLayout->addWidget(showSavedProgressChoice_);
    practiceLayout->addLayout(savedProgressWidthGrid);

    auto *backupSurface = new BackupSettingsPanel(
        databasePath_, dataRoot_, sharedStorageRoot_, content);
    aiSettingsPanel_ = new AiSettingsPanel(content);

    auto *bankUpdateSurface = new QFrame(content);
    bankUpdateSurface->setObjectName(QStringLiteral("bankUpdateSettingsSurface"));
    bankUpdateSurface->setMaximumWidth(760);
    bankUpdateSurface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *bankUpdateLayout = new QVBoxLayout(bankUpdateSurface);
    bankUpdateLayout->setContentsMargins(20, 20, 20, 20);
    bankUpdateLayout->setSpacing(10);
    auto *bankUpdateTitle = new QLabel(
        QStringLiteral("版本、题库与公告"), bankUpdateSurface);
    bankUpdateTitle->setObjectName(QStringLiteral("settingsSectionHeading"));
    auto *bankUpdateDescription = new QLabel(
        QStringLiteral("应用、公告和题库都直接读取项目 GitHub Release，不上传本地学习数据。"),
        bankUpdateSurface);
    bankUpdateDescription->setObjectName(QStringLiteral("pageSupportingText"));
    bankUpdateDescription->setWordWrap(true);
    auto *appVersionLabel = new QLabel(
        QStringLiteral("当前版本：%1 · 构建：%2")
            .arg(kCurrentAppVersion,
                 kCurrentBuildCommit == QStringLiteral("dev")
                     ? QStringLiteral("本地开发版")
                     : kCurrentBuildCommit.left(12)),
        bankUpdateSurface);
    appVersionLabel->setObjectName(QStringLiteral("appVersionLabel"));
    appVersionLabel->setWordWrap(true);
    autoAppUpdateCheckChoice_ = new QCheckBox(
        QStringLiteral("启动后自动检查应用更新"), bankUpdateSurface);
    autoAppUpdateCheckChoice_->setObjectName(
        QStringLiteral("autoAppUpdateCheckChoice"));
    autoAppUpdateCheckChoice_->setChecked(settings.value(
        QStringLiteral("updates/autoAppCheck"), true).toBool());
    checkAppUpdatesButton_ = new QPushButton(
        QStringLiteral("检查应用更新"), bankUpdateSurface);
    checkAppUpdatesButton_->setObjectName(QStringLiteral("checkAppUpdatesButton"));
    checkAppUpdatesButton_->setMinimumHeight(44);
    assignMaterialIcon(checkAppUpdatesButton_, QStringLiteral("download"));
    connect(checkAppUpdatesButton_, &QPushButton::clicked, this, [this] {
        openAppUpdateDialog(false);
    });
    appUpdateStatus_ = new QLabel(bankUpdateSurface);
    appUpdateStatus_->setObjectName(QStringLiteral("pageSupportingText"));
    appUpdateStatus_->setWordWrap(true);
    const QDateTime appLastChecked = settings.value(
        QStringLiteral("updates/appLastCheckedAt")).toDateTime();
    const QString appLastTag = settings.value(
        QStringLiteral("updates/appLastCheckedTag")).toString();
    appUpdateStatus_->setText(appLastChecked.isValid()
        ? QStringLiteral("上次应用检查：%1 · Release %2")
              .arg(appLastChecked.toLocalTime().toString(QStringLiteral("MM-dd HH:mm")),
                   appLastTag)
        : QStringLiteral("尚未检查应用更新"));
    checkBankUpdatesButton_ = new QPushButton(
        QStringLiteral("检查题库更新"), bankUpdateSurface);
    checkBankUpdatesButton_->setObjectName(QStringLiteral("checkBankUpdatesButton"));
    checkBankUpdatesButton_->setMinimumHeight(44);
    assignMaterialIcon(checkBankUpdatesButton_, QStringLiteral("download"));
    connect(checkBankUpdatesButton_, &QPushButton::clicked, this, [this] {
        openBankReleaseDialog(false);
    });
    autoBankUpdateCheckChoice_ = new QCheckBox(
        QStringLiteral("启动后自动检查题库更新"), bankUpdateSurface);
    autoBankUpdateCheckChoice_->setObjectName(
        QStringLiteral("autoBankUpdateCheckChoice"));
    autoBankUpdateCheckChoice_->setChecked(settings.value(
        QStringLiteral("updates/autoBankCheck"), true).toBool());
    bankUpdateStatus_ = new QLabel(bankUpdateSurface);
    bankUpdateStatus_->setObjectName(QStringLiteral("pageSupportingText"));
    bankUpdateStatus_->setWordWrap(true);
    const services::BankReleaseState bankState =
        services::BankReleaseStateStore::load(settings);
    bankUpdateStatus_->setText(
        bankState.lastCheckedAt.isValid()
            ? QStringLiteral("上次检查：%1 · Release %2")
                  .arg(bankState.lastCheckedAt.toLocalTime().toString(
                           QStringLiteral("MM-dd HH:mm")),
                       bankState.lastCheckedTag)
            : QStringLiteral("尚未检查题库更新"));
    autoAnnouncementCheckChoice_ = new QCheckBox(
        QStringLiteral("启动后自动检查新公告"), bankUpdateSurface);
    autoAnnouncementCheckChoice_->setObjectName(
        QStringLiteral("autoAnnouncementCheckChoice"));
    autoAnnouncementCheckChoice_->setChecked(settings.value(
        QStringLiteral("updates/autoAnnouncementCheck"), true).toBool());
    checkAnnouncementsButton_ = new QPushButton(
        QStringLiteral("检查公告"), bankUpdateSurface);
    checkAnnouncementsButton_->setObjectName(
        QStringLiteral("checkAnnouncementsButton"));
    checkAnnouncementsButton_->setMinimumHeight(44);
    assignMaterialIcon(checkAnnouncementsButton_, QStringLiteral("mail"));
    connect(checkAnnouncementsButton_, &QPushButton::clicked, this, [this] {
        checkAnnouncements(false);
    });
    announcementCheckStatus_ = new QLabel(bankUpdateSurface);
    announcementCheckStatus_->setObjectName(QStringLiteral("pageSupportingText"));
    announcementCheckStatus_->setWordWrap(true);
    const services::AnnouncementState announcementState =
        services::AnnouncementStateStore::load(settings);
    announcementCheckStatus_->setText(
        announcementState.lastCheckedAt.isValid()
            ? QStringLiteral("上次公告检查：%1")
                  .arg(announcementState.lastCheckedAt.toLocalTime().toString(
                      QStringLiteral("MM-dd HH:mm")))
            : QStringLiteral("尚未检查远程公告"));
    bankUpdateLayout->addWidget(bankUpdateTitle);
    bankUpdateLayout->addWidget(bankUpdateDescription);
    bankUpdateLayout->addWidget(appVersionLabel);
    bankUpdateLayout->addWidget(autoAppUpdateCheckChoice_);
    bankUpdateLayout->addWidget(checkAppUpdatesButton_);
    bankUpdateLayout->addWidget(appUpdateStatus_);
    bankUpdateLayout->addWidget(autoBankUpdateCheckChoice_);
    bankUpdateLayout->addWidget(checkBankUpdatesButton_);
    bankUpdateLayout->addWidget(bankUpdateStatus_);
    bankUpdateLayout->addWidget(autoAnnouncementCheckChoice_);
    bankUpdateLayout->addWidget(checkAnnouncementsButton_);
    bankUpdateLayout->addWidget(announcementCheckStatus_);

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
    surfaceLayout->addWidget(paletteLabel, 2, 0);
    surfaceLayout->addWidget(paletteChoice_, 2, 1);
    surfaceLayout->addWidget(primaryLabel, 3, 0);
    surfaceLayout->addWidget(primaryControl, 3, 1);
    surfaceLayout->addWidget(hierarchyIconsLabel, 4, 0, 1, 2);
    surfaceLayout->addWidget(libraryIconPickersContainer_, 5, 0, 1, 2);
    surfaceLayout->addWidget(previewLabel, 6, 0, 1, 2);
    surfaceLayout->addWidget(themePreview_, 7, 0, 1, 2);
    surfaceLayout->addWidget(cornerRadiusLabel, 8, 0);
    surfaceLayout->addWidget(cornerRadiusControl, 8, 1);
    surfaceLayout->addWidget(reduceMotionChoice_, 9, 0, 1, 2);
    surfaceLayout->setColumnStretch(1, 1);
    layout->addWidget(surface);
    layout->addWidget(practiceSurface);
    layout->addWidget(aiSettingsPanel_);
    layout->addWidget(backupSurface);
    layout->addWidget(bankUpdateSurface);
    layout->addLayout(saveRow);
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
    updateSavedProgressWidgetSize();
    updatePrimaryColorControl();

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
        for (qsizetype column = 0; column < homeActionButtons_.size(); ++column) {
            homeActionsLayout_->setColumnStretch(
                static_cast<int>(column), tablet ? (column == 0 ? 1 : 0) : 1);
        }
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
        librarySourceColumn_->setMinimumWidth(tablet ? 360 : 0);
        librarySourceColumn_->setMaximumWidth(tablet ? 420 : QWIDGETSIZE_MAX);
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
    if (libraryFileActionsLayout_) {
        clearLayoutItems(libraryFileActionsLayout_);
        if (tablet) {
            libraryFileActionsLayout_->addWidget(libraryNewFolderButton_, 0, 0);
            libraryFileActionsLayout_->addWidget(libraryImportJsonButton_, 0, 1);
            libraryFileActionsLayout_->addWidget(libraryRenameButton_, 0, 2);
            libraryFileActionsLayout_->addWidget(libraryMoveButton_, 1, 0);
            libraryFileActionsLayout_->addWidget(libraryRecycleButton_, 1, 1, 1, 2);
            libraryFileActionsLayout_->addWidget(libraryRestoreButton_, 0, 0);
            libraryFileActionsLayout_->addWidget(
                libraryPermanentDeleteButton_, 0, 1, 1, 2);
            for (int column = 0; column < 3; ++column) {
                libraryFileActionsLayout_->setColumnStretch(column, 1);
            }
        } else {
            libraryFileActionsLayout_->addWidget(libraryNewFolderButton_, 0, 0);
            libraryFileActionsLayout_->addWidget(libraryImportJsonButton_, 0, 1);
            libraryFileActionsLayout_->addWidget(libraryRenameButton_, 1, 0);
            libraryFileActionsLayout_->addWidget(libraryMoveButton_, 1, 1);
            libraryFileActionsLayout_->addWidget(libraryRecycleButton_, 2, 0, 1, 2);
            libraryFileActionsLayout_->addWidget(libraryRestoreButton_, 0, 0);
            libraryFileActionsLayout_->addWidget(
                libraryPermanentDeleteButton_, 0, 1);
            libraryFileActionsLayout_->setColumnStretch(0, 1);
            libraryFileActionsLayout_->setColumnStretch(1, 1);
            libraryFileActionsLayout_->setColumnStretch(2, 0);
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
    QSettings settings;
    const int componentRadius = std::clamp(
        cornerRadiusChoice_
            ? cornerRadiusChoice_->value()
            : settings.value(QStringLiteral("ui/cornerRadius"), 7).toInt(),
        0,
        18);
    QString paletteId = paletteChoice_
        ? paletteChoice_->currentData().toString()
        : settings.value(QStringLiteral("ui/palette"), QStringLiteral("forest")).toString();
    if (theme == QStringLiteral("endfield")) {
        paletteId = QStringLiteral("endfield");
    }
    if (paletteId == QStringLiteral("endfield")) {
        QApplication::instance()->setProperty(
            "quizappChoiceAccent", QColor(QStringLiteral("#fdfc00")));
        QApplication::instance()->setProperty(
            "quizappChoiceAccentSoft", QColor(QStringLiteral("#303026")));
        QApplication::instance()->setProperty(
            "quizappChoiceText", QColor(QStringLiteral("#f1f1f2")));
        QApplication::instance()->setProperty(
            "quizappChoiceMuted", QColor(QStringLiteral("#a7a8ad")));
        QApplication::instance()->setProperty(
            "quizappChoiceSurface", QColor(QStringLiteral("#1b1c20")));
        setStyleSheet(EndfieldTheme::styleSheet()
            + componentRadiusStyleSheet(componentRadius));
        refreshIconsForTheme(QStringLiteral("endfield"));
        return;
    }

    const bool dark = resolvedTheme(theme) == QStringLiteral("dark");
    const ThemePalette &palette = ThemePalettes::find(paletteId);
    const QColor backgroundColor = dark ? palette.darkBackground : palette.lightBackground;
    const QColor surfaceColor = dark ? palette.darkSurface : palette.lightSurface;
    const QString background = backgroundColor.name();
    const QString surface = surfaceColor.name();
    const QString surfaceMuted = (dark ? palette.darkLine : palette.lightBackground).name();
    const QString text = (dark ? palette.darkText : palette.lightText).name();
    const QString muted = (dark ? palette.darkMuted : palette.lightMuted).name();
    const QString line = (dark ? palette.darkLine : palette.lightLine).name();
    QColor primaryColor(primaryColorHex_.isEmpty()
        ? settings.value(QStringLiteral("ui/primary")).toString()
        : primaryColorHex_);
    if (!primaryColor.isValid()) {
        primaryColor = palette.primary;
    }
    const QColor onPrimaryColor = readableOnPrimary(primaryColor);
    const QString primary = primaryColor.name(QColor::HexRgb);
    const QString onPrimary = onPrimaryColor.name(QColor::HexRgb);
    const QString primarySoft = blendedColor(
        primaryColor, surfaceColor, dark ? 0.24 : 0.13);
    const QString wrong = palette.danger.name();
    const QString wrongSoft = blendedColor(
        palette.danger, surfaceColor, dark ? 0.24 : 0.13);
    QApplication::instance()->setProperty("quizappChoiceAccent", primaryColor);
    QApplication::instance()->setProperty(
        "quizappChoiceAccentSoft", QColor(primarySoft));
    QApplication::instance()->setProperty("quizappChoiceText", QColor(text));
    QApplication::instance()->setProperty("quizappChoiceMuted", QColor(muted));
    QApplication::instance()->setProperty("quizappChoiceSurface", surfaceColor);
    const QString comboArrow = dark
        ? QStringLiteral(":/quizapp/icons/combo_chevron_light.svg")
        : QStringLiteral(":/quizapp/icons/combo_chevron_dark.svg");
    const QString treeArrow = dark
        ? QStringLiteral(":/quizapp/icons/tree_chevron_right_light.svg")
        : QStringLiteral(":/quizapp/icons/tree_chevron_right_dark.svg");

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
            background: %7; color: %11; border-radius: 6px;
            font-size: 18px; font-weight: 800;
        }
        #topBarTitle { font-size: 18px; font-weight: 750; }
        #announcementButton {
            background: %2; border: 1px solid %6; padding: 7px;
        }
        #announcementButton:hover { background: %8; border-color: %7; }
        #announcementUnreadDot {
            background: %9; border: 2px solid %2; border-radius: 5px;
        }
        #pageHeading { font-size: 24px; font-weight: 800; }
        #sectionHeading, #emptyStateTitle { font-size: 17px; font-weight: 750; }
        #settingsSectionHeading { font-size: 17px; font-weight: 750; }
        #settingsFieldLabel { color: %5; font-weight: 650; }
        #pageSupportingText, #secondaryText, #settingsStatus { color: %5; }
        #summaryCard, #homeSummarySurface, #homeActionsSurface,
        #settingsSurface, #practiceSettingsSurface, #bankUpdateSettingsSurface,
        #backupSettingsSurface, #aiSettingsSurface,
        #librarySurface,
        #libraryBrowserHeader, #libraryPathNodeCard,
        #practiceOptionsSurface, #practiceAnswerSurface, #questionAiSurface,
        #studySummaryCard, #reviewOptionsSurface, #reviewAnswerSurface {
            background: %2; border: 1px solid %6; border-radius: 7px;
        }
        #homeSavedProgressCard { border-color: %7; }
        #homeSavedProgressTitle { font-size: 16px; font-weight: 800; }
        #homeSavedProgressSummary, #homeSavedProgressHintText { color: %5; }
        #homeSavedProgressHint {
            background: %8; border: 1px solid %7; border-radius: 5px;
        }
        #homeSavedProgressHintDismiss {
            color: %7; border: 0; background: transparent; padding: 3px 5px;
            font-weight: 700;
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
            background: %7; border-color: %7; color: %11;
        }
        #practiceAiButton, #questionAiAnalyzeButton {
            background: %7; border-color: %7; color: %11; font-weight: 700;
        }
        #questionAiTitle { font-size: 16px; font-weight: 750; }
        #questionAiStatus { color: %5; }
        #questionAiContent {
            background: %3; border: 1px solid %6; padding: 8px;
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
        #practicePage, #studyHubPage, #reviewPage, #examPageRoot { background: %1; }
        #answerTablePage { background: %1; }
        #studyExamSurface, #examSetupSurface {
            background: %2; border: 1px solid %6; border-radius: 7px;
        }
        #examStartButton, #examSubmitButton {
            background: %7; border-color: %7; color: %11;
        }
        #examTimerLabel { color: %7; font-weight: 800; }
        #examQuestionPrompt { color: %4; font-size: 17px; font-weight: 650; }
        #examOptionButton { text-align: left; padding: 10px 12px; }
        #examOptionButton:checked {
            background: %8; border: 2px solid %7; color: %4;
        }
        #examPauseCover {
            background: %3; border: 1px solid %6; border-radius: 7px;
            color: %5; padding: 18px;
        }
        #examResultScore { color: %7; font-size: 42px; font-weight: 850; }
        #examHistoryList, #examResultList {
            background: %2; border: 1px solid %6; border-radius: 7px;
            outline: 0; padding: 4px;
        }
        #examHistoryList::item, #examResultList::item {
            padding: 9px 10px; border-bottom: 1px solid %6;
        }
        #examHistoryList::item:hover { background: %3; }
        #examSetupStatus[error="true"] { color: %9; }
        #bankReleaseDialog { background: %1; }
        #appUpdateDialog { background: %1; }
        #appUpdateStatus, #appUpdateVersions { color: %5; }
        #appUpdateNotes {
            background: %2; border: 1px solid %6; border-radius: 6px;
            padding: 10px; color: %4;
        }
        #appUpdateDownloadButton { background: %7; color: %11; border-color: %7; }
        #backupSecurityNote, #backupStatus, #backupPreviewMeta { color: %5; }
        #backupProgress { min-height: 8px; max-height: 8px; }
        #backupProgress::chunk { background: %7; }
        #aiSettingsStatus { color: %5; }
        #aiRequestProgress { min-height: 8px; max-height: 8px; }
        #aiRequestProgress::chunk { background: %7; }
        #notebookList {
            background: %2; border: 1px solid %6; padding: 4px; outline: 0;
        }
        #notebookList::item { padding: 8px 12px; border-bottom: 1px solid %6; }
        #notebookList::item:hover { background: %3; }
        #notebookList::item:selected { background: %8; color: %7; }
        #notebookEmptyLabel, #notebookStatus { color: %5; }
        #notebookStatus[error="true"] { color: %9; }
        #notebookRecycleViewButton:checked { background: %8; color: %7; border-color: %7; }
        #backupPreviewDialog { background: %1; }
        #backupPreviewStat {
            background: %2; border: 1px solid %6; border-radius: 6px;
        }
        #backupPreviewValue { color: %4; font-size: 20px; font-weight: 800; }
        #backupIntegrity {
            background: %8; border: 1px solid %7; border-radius: 6px;
            color: %7; padding: 9px; font-weight: 700;
        }
        #backupSecretWarning {
            background: %10; border: 1px solid %9; border-radius: 6px;
            color: %9; padding: 9px; font-weight: 700;
        }
        #backupConfirmRestoreButton { background: %7; color: %11; border-color: %7; }
        #announcementDialog { background: %1; }
        #announcementStatus { color: %5; }
        #announcementTree {
            background: %2; border: 1px solid %6; border-radius: 7px;
            outline: 0; padding: 4px;
        }
        #announcementTree::item {
            min-height: 38px; padding: 6px 8px; border-bottom: 1px solid %6;
        }
        #announcementTree::item:hover { background: %3; }
        #announcementBody {
            background: %2; color: %4; border: 0; padding: 8px;
        }
        #bankReleaseStatus, #bankReleaseSelectionSummary { color: %5; }
        #bankReleaseTree {
            background: %2; border: 1px solid %6; border-radius: 7px;
            outline: 0; padding: 4px;
        }
        #bankReleaseTree::item { min-height: 34px; padding: 4px 6px; }
        #bankReleaseTree::item:selected { background: %8; color: %7; }
        QTreeView::branch { background: transparent; }
        QTreeView::branch:has-children:closed {
            image: url(%13);
        }
        QTreeView::branch:has-children:open {
            image: url(%12);
        }
        QTreeView::branch:!has-children { image: none; }
        #bankReleaseTree::indicator {
            width: 18px; height: 18px; border: 1px solid %6;
            border-radius: 4px; background: %2;
        }
        #bankReleaseTree::indicator:checked {
            background: %7; border-color: %7;
            image: url(:/quizapp/icons/check_white.svg);
        }
        #bankReleaseTree::indicator:indeterminate {
            background: %7; border-color: %7;
            image: url(:/quizapp/icons/minus_white.svg);
        }
        #bankReleaseDownloadButton { background: %7; color: %11; border-color: %7; }
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
        #practiceSaveStatus { color: %7; font-weight: 700; }
        #practiceSaveStatus[error="true"] { color: %9; }
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
        QPushButton:disabled { background: %3; border-color: %6; color: %5; }
        #saveSettingsButton { background: %7; color: %11; border-color: %7; }
        QComboBox {
            background: %2; border: 1px solid %6; border-radius: 6px;
            padding: 8px 40px 8px 12px;
            selection-background-color: %8; selection-color: %7;
        }
        QComboBox:hover { border-color: %5; }
        QComboBox:focus, QComboBox:on {
            border-color: %7; background: %2;
        }
        QComboBox:disabled {
            background: %3; border-color: %6; color: %5;
        }
        QComboBox::drop-down {
            subcontrol-origin: border; subcontrol-position: top right;
            width: 38px; background: %2; border: 0;
            border-top-right-radius: 6px; border-bottom-right-radius: 6px;
        }
        QComboBox::drop-down:hover, QComboBox::drop-down:on {
            background: %2;
        }
        QComboBox::drop-down:disabled { background: %3; }
        QComboBox::down-arrow {
            image: url(%12); width: 16px; height: 16px;
        }
        QComboBox QAbstractItemView {
            background: %2; color: %4; border: 1px solid %6;
            border-radius: 6px; outline: 0; padding: 6px;
            selection-background-color: %8; selection-color: %7;
        }
        QComboBox QAbstractItemView::item {
            min-height: 40px; padding: 7px 12px; border: 0;
        }
        QComboBox QAbstractItemView::item:hover,
        QComboBox QAbstractItemView::item:selected {
            background: %8; color: %7;
        }
        QLineEdit, QTextEdit, QTextBrowser, QSpinBox, QDoubleSpinBox {
            background: %2; border: 1px solid %6; border-radius: 6px;
            padding: 7px 10px;
        }
        QLineEdit:focus, QTextEdit:focus, QTextBrowser:focus,
        QSpinBox:focus, QDoubleSpinBox:focus {
            border-color: %7;
        }
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
             wrong, wrongSoft, onPrimary, comboArrow, treeArrow)
        + componentRadiusStyleSheet(componentRadius));
    refreshIcons(QColor(muted), primaryColor, onPrimaryColor);
}

void AppWindow::refreshIconsForTheme(const QString &theme)
{
    QSettings settings;
    QString paletteId = paletteChoice_
        ? paletteChoice_->currentData().toString()
        : settings.value(QStringLiteral("ui/palette"), QStringLiteral("forest")).toString();
    if (theme == QStringLiteral("endfield")) {
        paletteId = QStringLiteral("endfield");
    }
    if (paletteId == QStringLiteral("endfield")) {
        refreshIcons(
            EndfieldTheme::iconNormal(),
            EndfieldTheme::iconEmphasis(),
            EndfieldTheme::iconOnPrimary());
        return;
    }
    const bool dark = resolvedTheme(theme) == QStringLiteral("dark");
    const ThemePalette &palette = ThemePalettes::find(paletteId);
    QColor primary(primaryColorHex_.isEmpty()
        ? settings.value(QStringLiteral("ui/primary")).toString()
        : primaryColorHex_);
    if (!primary.isValid()) {
        primary = palette.primary;
    }
    refreshIcons(
        dark ? palette.darkMuted : palette.lightMuted,
        primary,
        readableOnPrimary(primary));
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
    libraryEmptyTitle_->setText(QStringLiteral("共享题库目录"));
    if (stats.bankCount == 0) {
        librarySummaryText_->setText(QStringLiteral("尚未安装题库。把 JSON 放入下方目录后扫描。"));
    } else {
        librarySummaryText_->setText(
            QStringLiteral("已安装 %1 个分区 · %2 道题 · %3 个媒体资源")
                .arg(stats.bankCount)
                .arg(stats.questionCount)
                .arg(stats.blobCount));
    }
}

void AppWindow::startXiaoyiDirectoryInstall()
{
    startSharedBankSync(true);
}

void AppWindow::startSharedBankSync(bool force)
{
    if (!libraryImportWatcher_ || libraryImportWatcher_->isRunning()) {
        return;
    }
    refreshSharedStorageState();
    if (!platform::SharedStoragePlatform::hasDirectAccess()) {
        libraryImportStatus_->setText(QStringLiteral("请先授权访问安卓根目录，再返回软件扫描"));
        return;
    }
    if (!sharedStorageLayout_.ready()) {
        libraryImportStatus_->setText(sharedStorageLayout_.error);
        return;
    }

    libraryImportButton_->setEnabled(false);
    libraryCancelButton_->show();
    libraryImportProgress_->setRange(0, 0);
    libraryImportProgress_->show();
    libraryImportStatus_->setText(QStringLiteral("正在检查共享题库目录"));
    const QString inputDirectory = sharedStorageLayout_.questionBanks;
    const QString databasePath = databasePath_;
    const QString dataRoot = dataRoot_;
    auto future = QtConcurrent::run(
        [inputDirectory, databasePath, dataRoot, force](
            QPromise<services::BankDirectorySyncResult> &promise) {
            services::BankDirectorySyncService service;
            const services::BankDirectorySyncResult result = service.synchronize(
                inputDirectory,
                databasePath,
                dataRoot,
                force,
                [&promise](int current, int total, const QString &sourceKey) {
                    promise.setProgressRange(0, total);
                    promise.setProgressValueAndText(
                        current,
                        sourceKey.isEmpty()
                            ? QStringLiteral("正在完成题库同步")
                            : QStringLiteral("%1/%2  %3")
                                  .arg(current)
                                  .arg(total)
                                  .arg(sourceKey));
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
        libraryImportStatus_->setText(QStringLiteral("扫描已取消，已完成的文件仍可使用"));
        refreshLibraryStats();
        refreshInstalledBankList();
        refreshSharedFileTree();
        refreshLibraryIconPickers();
        return;
    }
    const services::BankDirectorySyncResult result = libraryImportWatcher_->result();
    if (result.canceled) {
        libraryImportStatus_->setText(QStringLiteral("题库扫描已取消"));
    } else if (!result.error.isEmpty()) {
        libraryImportStatus_->setText(QStringLiteral("同步失败：%1").arg(result.error));
    } else {
        libraryImportStatus_->setText(
            QStringLiteral("扫描完成：%1 个文件 · 新增 %2 · 更新 %3 · 移动 %4 · 未变 %5 · 异常 %6")
                .arg(result.discoveredFiles)
                .arg(result.installedFiles)
                .arg(result.updatedFiles)
                .arg(result.relocatedFiles)
                .arg(result.unchangedFiles)
                .arg(result.issues.size()));
    }
    refreshLibraryStats();
    refreshInstalledBankList();
    refreshSharedFileTree();
    refreshLibraryIconPickers();
}

void AppWindow::refreshSharedStorageState()
{
    if (!libraryImportButton_) {
        return;
    }
    const bool hasAccess = platform::SharedStoragePlatform::hasDirectAccess();
    const bool needsPermission = platform::SharedStoragePlatform::requiresDirectAccessPermission();
    libraryStorageAccessButton_->setVisible(needsPermission && !hasAccess);
    libraryImportButton_->setEnabled(hasAccess && !databasePath_.isEmpty() && !dataRoot_.isEmpty());
    libraryOpenStorageButton_->setEnabled(!sharedStorageRoot_.isEmpty());
    if (!hasAccess) {
        sharedStorageLayout_ = {};
        libraryImportStatus_->setText(
            QStringLiteral("需要“所有文件访问”权限，才能自动读取安卓根目录下的题库"));
        refreshSharedFileTree();
        return;
    }
    services::SharedStorageService service;
    sharedStorageLayout_ = service.prepare(sharedStorageRoot_);
    if (!sharedStorageLayout_.ready()) {
        libraryImportStatus_->setText(sharedStorageLayout_.error);
        libraryImportButton_->setEnabled(false);
    } else if (!libraryImportWatcher_ || !libraryImportWatcher_->isRunning()) {
        libraryImportStatus_->setText(QStringLiteral("共享目录已就绪，可手动放入 JSON 或文件夹"));
    }
    refreshSharedFileTree();
    refreshLibraryIconPickers();
}

void AppWindow::refreshSharedFileTree()
{
    if (!libraryFilesTree_) {
        return;
    }
    QString selectedPath;
    if (const QTreeWidgetItem *selected = libraryFilesTree_->currentItem()) {
        selectedPath = selected->data(0, kStoragePathRole).toString();
    }
    QSet<QString> expandedPaths;
    for (QTreeWidgetItemIterator iterator(libraryFilesTree_); *iterator; ++iterator) {
        if ((*iterator)->isExpanded()) {
            const QString path = (*iterator)->data(0, kStoragePathRole).toString();
            if (!path.isEmpty()) {
                expandedPaths.insert(absoluteCleanPath(path));
            }
        }
    }
    libraryFilesTree_->clear();
    auto *rootItem = new QTreeWidgetItem(
        libraryFilesTree_, {QStringLiteral("QuizApp"), QStringLiteral("共享存储")});
    rootItem->setData(0, kStoragePathRole, sharedStorageRoot_);
    rootItem->setData(0, kStorageDirectoryRole, true);
    rootItem->setExpanded(true);
    QHash<QString, domain::ManagedBankSource> sourceStates;
    QVector<storage::HiddenLibraryNode> hiddenNodes;
    if (!databasePath_.isEmpty()) {
        storage::Database database(
            QStringLiteral("shared-tree-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QString error;
        if (database.open(databasePath_, &error) && database.migrate(&error)) {
            const storage::SqliteBankSourceRepository repository(database.connection());
            const auto sources = repository.listByRoot(
                QStringLiteral("primary-shared-storage"), &error);
            for (const domain::ManagedBankSource &source : sources) {
                sourceStates.insert(source.sourceKey, source);
            }
            const storage::SqliteLibraryRepository libraryRepository(database.connection());
            hiddenNodes = libraryRepository.hiddenNodes(&error);
        }
    }
    if (!hiddenNodes.isEmpty()) {
        auto *hiddenRoot = new QTreeWidgetItem(
            rootItem,
            {QStringLiteral("本机隐藏"),
             QStringLiteral("%1 项").arg(hiddenNodes.size())});
        hiddenRoot->setExpanded(true);
        for (const storage::HiddenLibraryNode &node : std::as_const(hiddenNodes)) {
            auto *hiddenItem = new QTreeWidgetItem(
                hiddenRoot,
                {node.path.join(QStringLiteral(" / ")),
                 QStringLiteral("内置题库")});
            hiddenItem->setData(0, kHiddenLibraryPathRole, node.path);
            hiddenItem->setToolTip(
                0, QStringLiteral("%1 个题库，仅在本机隐藏").arg(node.bankCount));
        }
    }
    if (!sharedStorageLayout_.ready()) {
        new QTreeWidgetItem(
            rootItem, {QStringLiteral("目录暂不可访问"), QStringLiteral("等待授权")});
        libraryFilesTree_->setFixedHeight(std::clamp(
            180 + static_cast<int>(hiddenNodes.size()) * 26, 180, 340));
        updateSharedFileActions();
        return;
    }

    int visibleEntries = 0;
    const auto addDirectory = [&](auto &&self,
                                  QTreeWidgetItem *parent,
                                  const QString &absolutePath,
                                  const QString &bankRoot,
                                  int depth) -> void {
        if (depth > 8 || visibleEntries >= 800) {
            return;
        }
        QDir directory(absolutePath);
        const QFileInfoList entries = directory.entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : entries) {
            if (entry.fileName().startsWith(u'.') || visibleEntries >= 800) {
                continue;
            }
            ++visibleEntries;
            QString status;
            QString statusTooltip;
            if (entry.isFile() && entry.suffix().compare(
                    QStringLiteral("json"), Qt::CaseInsensitive) == 0
                && !bankRoot.isEmpty()) {
                const QString relative = QDir(bankRoot)
                    .relativeFilePath(entry.absoluteFilePath())
                    .replace(u'\\', u'/');
                const auto found = sourceStates.constFind(
                    QStringLiteral("shared:%1").arg(relative));
                if (found == sourceStates.cend()) {
                    status = QStringLiteral("待扫描");
                } else if (!found->lastError.isEmpty()) {
                    status = QStringLiteral("异常");
                } else if (!found->available) {
                    status = QStringLiteral("已移除");
                } else {
                    status = QStringLiteral("已同步");
                }
                if (found != sourceStates.cend() && !found->lastError.isEmpty()) {
                    statusTooltip = found->lastError;
                }
            }
            auto *item = new QTreeWidgetItem(parent, {entry.fileName(), status});
            item->setToolTip(1, statusTooltip);
            item->setData(0, kStoragePathRole, entry.absoluteFilePath());
            item->setData(0, kStorageDirectoryRole, entry.isDir());
            if (entry.isDir()) {
                self(self, item, entry.absoluteFilePath(), bankRoot, depth + 1);
            }
        }
    };

    const std::array<std::pair<QString, QString>, 5> directories{{
        {QStringLiteral("QuestionBanks"), sharedStorageLayout_.questionBanks},
        {QStringLiteral("Backups"), sharedStorageLayout_.backups},
        {QStringLiteral("Exports"), sharedStorageLayout_.exports},
        {QStringLiteral("Notes"), sharedStorageLayout_.notes},
        {QStringLiteral("RecycleBin"), sharedStorageLayout_.recycleBin},
    }};
    for (const auto &[name, path] : directories) {
        auto *item = new QTreeWidgetItem(rootItem, {name, QStringLiteral("目录")});
        item->setData(0, kStoragePathRole, path);
        item->setData(0, kStorageDirectoryRole, true);
        addDirectory(
            addDirectory,
            item,
            path,
            name == QStringLiteral("QuestionBanks")
                ? sharedStorageLayout_.questionBanks : QString(),
            0);
        item->setExpanded(name == QStringLiteral("QuestionBanks"));
    }
    if (visibleEntries >= 800) {
        new QTreeWidgetItem(
            rootItem, {QStringLiteral("其余文件未展开"), QStringLiteral("达到显示上限")});
    }
    libraryFilesTree_->setFixedHeight(
        std::clamp(38 + (visibleEntries + 6) * 26, 180, 340));
    QTreeWidgetItem *restoredSelection = nullptr;
    const auto restoreTreeState = [&](auto &&self, QTreeWidgetItem *item) -> void {
        const QString path = item->data(0, kStoragePathRole).toString();
        if (!path.isEmpty()) {
            const QString normalized = absoluteCleanPath(path);
            if (expandedPaths.contains(normalized)) {
                item->setExpanded(true);
            }
            if (!selectedPath.isEmpty()
                && normalized == absoluteCleanPath(selectedPath)) {
                restoredSelection = item;
            }
        }
        for (int index = 0; index < item->childCount(); ++index) {
            self(self, item->child(index));
        }
    };
    restoreTreeState(restoreTreeState, rootItem);
    if (restoredSelection) {
        for (QTreeWidgetItem *parent = restoredSelection->parent(); parent;
             parent = parent->parent()) {
            parent->setExpanded(true);
        }
        libraryFilesTree_->setCurrentItem(restoredSelection);
        libraryFilesTree_->scrollToItem(restoredSelection);
    }
    updateSharedFileActions();
}

void AppWindow::requestSharedStorageAccess()
{
    QString error;
    if (!platform::SharedStoragePlatform::requestDirectAccess(&error)) {
        libraryImportStatus_->setText(error);
        return;
    }
    libraryImportStatus_->setText(QStringLiteral("完成系统授权后返回 QuizApp，软件会自动扫描"));
}

void AppWindow::openSharedStorageDirectory()
{
    QString error;
    if (!platform::SharedStoragePlatform::openInSystemFileManager(
            sharedStorageRoot_, &error)) {
        libraryImportStatus_->setText(error);
    }
}

void AppWindow::openBankReleaseDialog(bool automatic)
{
    if (!sharedStorageLayout_.ready()) {
        refreshSharedStorageState();
    }
    if (!sharedStorageLayout_.ready()) {
        if (!automatic && settingsStatus_) {
            settingsStatus_->setText(QStringLiteral("题库目录暂不可用，请先完成存储授权"));
        }
        return;
    }
    auto *dialog = new BankReleaseDialog(sharedStorageLayout_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const bool compact = width() < kTabletNavigationBreakpoint;
    dialog->resize(
        compact ? std::max(350, width() - 16) : 760,
        compact ? std::max(540, height() - 24) : 680);
    connect(dialog, &BankReleaseDialog::banksInstalled, this, [this](int count) {
        if (settingsStatus_) {
            settingsStatus_->setText(
                QStringLiteral("已下载 %1 个题库，正在同步目录").arg(count));
        }
        refreshSharedFileTree();
        startSharedBankSync(true);
    });
    connect(dialog, &BankReleaseDialog::checkCompleted,
            this, [this, automatic](int availableUpdates, const QString &error) {
                if (!bankUpdateStatus_ || (automatic && !error.isEmpty())) {
                    return;
                }
                const services::BankReleaseState state =
                    services::BankReleaseStateStore::load(QSettings());
                if (!error.isEmpty()) {
                    bankUpdateStatus_->setText(
                        QStringLiteral("题库更新检查失败：%1").arg(error));
                    return;
                }
                const QString checkedAt = state.lastCheckedAt.toLocalTime().toString(
                    QStringLiteral("MM-dd HH:mm"));
                bankUpdateStatus_->setText(
                    availableUpdates > 0
                        ? QStringLiteral("发现 %1 个可更新题库 · %2")
                              .arg(availableUpdates)
                              .arg(checkedAt)
                        : QStringLiteral("当前题库已是最新 · %1").arg(checkedAt));
            });
    dialog->startCheck(kLatestReleaseApiUrl, automatic);
    if (!automatic) {
        dialog->open();
    }
}

void AppWindow::openAppUpdateDialog(bool automatic)
{
    auto *dialog = new AppUpdateDialog(
        kCurrentAppVersion, kCurrentBuildCommit, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const bool compact = width() < kTabletNavigationBreakpoint;
    dialog->resize(
        compact ? std::max(350, width() - 16) : 620,
        compact ? std::max(500, height() - 24) : 560);
    connect(dialog, &AppUpdateDialog::checkCompleted,
            this, [this, automatic](bool updateAvailable, const QString &error) {
                if (!appUpdateStatus_ || (automatic && !error.isEmpty())) {
                    return;
                }
                if (!error.isEmpty()) {
                    appUpdateStatus_->setText(
                        QStringLiteral("应用更新检查失败：%1").arg(error));
                    return;
                }
                const QSettings settings;
                const QDateTime checkedAt = settings.value(
                    QStringLiteral("updates/appLastCheckedAt")).toDateTime();
                const QString tag = settings.value(
                    QStringLiteral("updates/appLastCheckedTag")).toString();
                appUpdateStatus_->setText(updateAvailable
                    ? QStringLiteral("发现新版本 %1 · %2")
                          .arg(tag, checkedAt.toLocalTime().toString(
                              QStringLiteral("MM-dd HH:mm")))
                    : QStringLiteral("当前应用已是最新 · %1")
                          .arg(checkedAt.toLocalTime().toString(
                              QStringLiteral("MM-dd HH:mm"))));
            });
    dialog->startCheck(kLatestReleaseApiUrl, automatic);
    if (!automatic) {
        dialog->open();
    }
}

void AppWindow::scheduleAutomaticAppUpdateCheck()
{
    if (qEnvironmentVariableIsSet("QUIZAPP_DISABLE_NETWORK_CHECKS")) {
        return;
    }
    const bool enabled = autoAppUpdateCheckChoice_
        ? autoAppUpdateCheckChoice_->isChecked()
        : QSettings().value(QStringLiteral("updates/autoAppCheck"), true).toBool();
    if (!enabled) {
        return;
    }
    if (QApplication::activeModalWidget()) {
        QTimer::singleShot(5000, this, &AppWindow::scheduleAutomaticAppUpdateCheck);
        return;
    }
    const QDateTime lastChecked = QSettings().value(
        QStringLiteral("updates/appLastCheckedAt")).toDateTime();
    if (lastChecked.isValid()
        && lastChecked.secsTo(QDateTime::currentDateTimeUtc()) < 12 * 60 * 60) {
        return;
    }
    openAppUpdateDialog(true);
}

void AppWindow::scheduleAutomaticBankReleaseCheck()
{
    if (qEnvironmentVariableIsSet("QUIZAPP_DISABLE_NETWORK_CHECKS")) {
        return;
    }
    const bool enabled = autoBankUpdateCheckChoice_
        ? autoBankUpdateCheckChoice_->isChecked()
        : QSettings().value(QStringLiteral("updates/autoBankCheck"), true).toBool();
    if (!enabled) {
        return;
    }
    if (QApplication::activeModalWidget()) {
        QTimer::singleShot(5000, this, &AppWindow::scheduleAutomaticBankReleaseCheck);
        return;
    }
    const services::BankReleaseState state =
        services::BankReleaseStateStore::load(QSettings());
    if (state.lastCheckedAt.isValid()
        && state.lastCheckedAt.secsTo(QDateTime::currentDateTimeUtc()) < 12 * 60 * 60) {
        return;
    }
    openBankReleaseDialog(true);
}

void AppWindow::openAnnouncementBoard()
{
    const services::AnnouncementState state =
        services::AnnouncementStateStore::load(QSettings());
    if (state.cachedCatalog.items.isEmpty()) {
        checkAnnouncements(false);
        return;
    }
    auto *dialog = new AnnouncementDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const bool compact = width() < kTabletNavigationBreakpoint;
    dialog->resize(
        compact ? std::max(350, width() - 16) : 680,
        compact ? std::max(520, height() - 24) : 680);
    connect(dialog, &AnnouncementDialog::unreadStateChanged,
            this, [this](bool) { updateAnnouncementUnreadState(); });
    dialog->showCached(state.cachedCatalog);
    dialog->open();
}

void AppWindow::checkAnnouncements(bool automatic)
{
    auto *dialog = new AnnouncementDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const bool compact = width() < kTabletNavigationBreakpoint;
    dialog->resize(
        compact ? std::max(350, width() - 16) : 680,
        compact ? std::max(520, height() - 24) : 680);
    connect(dialog, &AnnouncementDialog::unreadStateChanged,
            this, [this](bool) { updateAnnouncementUnreadState(); });
    connect(dialog, &AnnouncementDialog::checkCompleted,
            this, [this, automatic](int unreadCount, const QString &error) {
                if (automatic && !error.isEmpty()) {
                    return;
                }
                if (announcementCheckStatus_) {
                    const services::AnnouncementState state =
                        services::AnnouncementStateStore::load(QSettings());
                    const QString checkedAt = state.lastCheckedAt.toLocalTime().toString(
                        QStringLiteral("MM-dd HH:mm"));
                    announcementCheckStatus_->setText(
                        !error.isEmpty()
                            ? QStringLiteral("公告检查失败：%1").arg(error)
                            : unreadCount > 0
                                ? QStringLiteral("发现 %1 篇未读公告 · %2")
                                      .arg(unreadCount)
                                      .arg(checkedAt)
                                : QStringLiteral("公告已经是最新 · %1").arg(checkedAt));
                }
            });
    dialog->startCheck(kAnnouncementFeedUrl, kLatestReleaseApiUrl, automatic);
    if (!automatic) {
        dialog->open();
    }
}

void AppWindow::scheduleAutomaticAnnouncementCheck()
{
    if (qEnvironmentVariableIsSet("QUIZAPP_DISABLE_NETWORK_CHECKS")) {
        return;
    }
    const bool enabled = autoAnnouncementCheckChoice_
        ? autoAnnouncementCheckChoice_->isChecked()
        : QSettings().value(
              QStringLiteral("updates/autoAnnouncementCheck"), true).toBool();
    if (!enabled) {
        return;
    }
    if (QApplication::activeModalWidget()) {
        QTimer::singleShot(5000, this, &AppWindow::scheduleAutomaticAnnouncementCheck);
        return;
    }
    const services::AnnouncementState state =
        services::AnnouncementStateStore::load(QSettings());
    if (state.lastCheckedAt.isValid()
        && state.lastCheckedAt.secsTo(QDateTime::currentDateTimeUtc()) < 6 * 60 * 60) {
        return;
    }
    checkAnnouncements(true);
}

void AppWindow::updateAnnouncementUnreadState()
{
    if (!announcementUnreadDot_) {
        return;
    }
    const services::AnnouncementState state =
        services::AnnouncementStateStore::load(QSettings());
    const bool unread = !services::AnnouncementStateStore::unreadIds(state).isEmpty();
    announcementUnreadDot_->setVisible(unread);
    if (announcementButton_) {
        announcementButton_->setAccessibleDescription(
            unread ? QStringLiteral("有未读公告") : QStringLiteral("没有未读公告"));
    }
}

void AppWindow::updateSharedFileActions()
{
    if (!libraryNewFolderButton_) {
        return;
    }
    const QTreeWidgetItem *item = libraryFilesTree_ ? libraryFilesTree_->currentItem() : nullptr;
    const QStringList hiddenPath = item
        ? item->data(0, kHiddenLibraryPathRole).toStringList() : QStringList{};
    if (!hiddenPath.isEmpty()) {
        libraryNewFolderButton_->hide();
        libraryImportJsonButton_->hide();
        libraryRenameButton_->hide();
        libraryMoveButton_->hide();
        libraryRecycleButton_->hide();
        libraryRestoreButton_->show();
        libraryRestoreButton_->setEnabled(true);
        libraryPermanentDeleteButton_->hide();
        return;
    }
    const QString path = item ? item->data(0, kStoragePathRole).toString() : QString();
    const bool isDirectory = item && item->data(0, kStorageDirectoryRole).toBool();
    const bool ready = sharedStorageLayout_.ready();
    const bool inQuestionBanks = ready && !path.isEmpty()
        && services::SharedStorageFileService::isPathInside(
            path, sharedStorageLayout_.questionBanks);
    const bool inRecycleBin = ready && !path.isEmpty()
        && services::SharedStorageFileService::isPathInside(
            path, sharedStorageLayout_.recycleBin);
    const QStringList managedRoots{
        sharedStorageLayout_.questionBanks,
        sharedStorageLayout_.backups,
        sharedStorageLayout_.exports,
        sharedStorageLayout_.notes,
    };
    const bool inManagedArea = std::any_of(
        managedRoots.cbegin(), managedRoots.cend(), [&path](const QString &root) {
            return services::SharedStorageFileService::isPathInside(path, root);
        });
    const bool fixedManagedRoot = std::any_of(
        managedRoots.cbegin(), managedRoots.cend(), [&path](const QString &root) {
            return absoluteCleanPath(path) == absoluteCleanPath(root);
        });
    const bool fixedRecycleRoot = absoluteCleanPath(path)
        == absoluteCleanPath(sharedStorageLayout_.recycleBin);
    const int recyclePathDepth = inRecycleBin
        ? QDir(sharedStorageLayout_.recycleBin)
              .relativeFilePath(path)
              .replace(u'\\', u'/')
              .split(u'/', Qt::SkipEmptyParts)
              .size()
        : 0;

    libraryNewFolderButton_->setVisible(!inRecycleBin);
    libraryImportJsonButton_->setVisible(!inRecycleBin);
    libraryRenameButton_->setVisible(!inRecycleBin);
    libraryMoveButton_->setVisible(!inRecycleBin);
    libraryRecycleButton_->setVisible(!inRecycleBin);
    libraryRestoreButton_->setVisible(inRecycleBin);
    libraryPermanentDeleteButton_->setVisible(inRecycleBin);
    libraryNewFolderButton_->setEnabled(
        ready && (!item || (isDirectory && inQuestionBanks)));
    libraryImportJsonButton_->setEnabled(
        ready && (!item || inQuestionBanks));
    libraryRenameButton_->setEnabled(
        ready && inQuestionBanks && !fixedManagedRoot);
    libraryMoveButton_->setEnabled(
        ready && inQuestionBanks && !fixedManagedRoot);
    libraryRecycleButton_->setEnabled(
        ready && inManagedArea && !fixedManagedRoot);
    libraryRestoreButton_->setEnabled(inRecycleBin && recyclePathDepth >= 3);
    libraryPermanentDeleteButton_->setEnabled(inRecycleBin && !fixedRecycleRoot);
}

void AppWindow::createSharedBankFolder()
{
    if (!sharedStorageLayout_.ready()) {
        return;
    }
    QString parent = sharedStorageLayout_.questionBanks;
    if (QTreeWidgetItem *item = libraryFilesTree_->currentItem()) {
        const QString selected = item->data(0, kStoragePathRole).toString();
        if (item->data(0, kStorageDirectoryRole).toBool()
            && services::SharedStorageFileService::isPathInside(
                selected, sharedStorageLayout_.questionBanks)) {
            parent = selected;
        }
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this,
        QStringLiteral("新建题库层级"),
        QStringLiteral("文件夹名称"),
        QLineEdit::Normal,
        {},
        &accepted);
    if (!accepted) {
        return;
    }
    services::SharedStorageFileService service;
    const auto result = service.createQuestionBankFolder(
        sharedStorageLayout_, parent, name);
    libraryImportStatus_->setText(result.completed
        ? QStringLiteral("已创建层级：%1").arg(QFileInfo(result.destinationPath).fileName())
        : result.error);
    refreshSharedFileTree();
    if (result.completed) {
        refreshLibraryIconPickers();
    }
}

void AppWindow::importSharedBankJson()
{
    if (!sharedStorageLayout_.ready()) {
        return;
    }
    QString destination = sharedStorageLayout_.questionBanks;
    if (QTreeWidgetItem *item = libraryFilesTree_->currentItem()) {
        const QString selected = item->data(0, kStoragePathRole).toString();
        if (services::SharedStorageFileService::isPathInside(
                selected, sharedStorageLayout_.questionBanks)) {
            destination = item->data(0, kStorageDirectoryRole).toBool()
                ? selected : QFileInfo(selected).absolutePath();
        }
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("选择题库 JSON"),
        {},
        QStringLiteral("JSON 题库 (*.json)"));
    if (files.isEmpty()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("同名文件处理"),
        QStringLiteral("如果目标层级已有同名文件：\n选择“是”覆盖，选择“否”保留两个文件。"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::No);
    if (answer == QMessageBox::Cancel) {
        return;
    }
    const auto policy = answer == QMessageBox::Yes
        ? services::StorageConflictPolicy::Overwrite
        : services::StorageConflictPolicy::KeepBoth;
    services::SharedStorageFileService service;
    const auto result = service.importJsonFiles(
        sharedStorageLayout_, destination, files, policy);
    libraryImportStatus_->setText(result.completed
        ? QStringLiteral("已导入 %1 个 JSON，正在同步题库").arg(result.affectedEntries)
        : result.error);
    refreshSharedFileTree();
    if (result.completed && result.affectedEntries > 0) {
        startSharedBankSync(true);
    }
}

void AppWindow::renameSelectedSharedEntry()
{
    QTreeWidgetItem *item = libraryFilesTree_ ? libraryFilesTree_->currentItem() : nullptr;
    if (!item || !sharedStorageLayout_.ready()) {
        return;
    }
    const QString path = item->data(0, kStoragePathRole).toString();
    const QFileInfo source(path);
    const QString currentName = source.isFile()
        ? source.completeBaseName() : source.fileName();
    bool accepted = false;
    const QString newName = QInputDialog::getText(
        this,
        QStringLiteral("重命名题库项目"),
        source.isDir() ? QStringLiteral("层级名称") : QStringLiteral("题库名称"),
        QLineEdit::Normal,
        currentName,
        &accepted);
    if (!accepted) {
        return;
    }
    services::SharedStorageFileService service;
    const auto result = service.renameQuestionBankEntry(
        sharedStorageLayout_, path, newName);
    libraryImportStatus_->setText(result.completed
        ? QStringLiteral("已重命名为：%1")
              .arg(QFileInfo(result.destinationPath).fileName())
        : result.error);
    refreshSharedFileTree();
    if (result.completed) {
        refreshLibraryIconPickers();
        startSharedBankSync(true);
    }
}

void AppWindow::moveSelectedSharedEntry()
{
    QTreeWidgetItem *item = libraryFilesTree_ ? libraryFilesTree_->currentItem() : nullptr;
    if (!item || !sharedStorageLayout_.ready()) {
        return;
    }
    const QString sourcePath = item->data(0, kStoragePathRole).toString();
    const QFileInfo source(sourcePath);
    const QString currentParent = absoluteCleanPath(source.absolutePath());
    QStringList labels;
    QHash<QString, QString> pathsByLabel;
    const auto addDestination = [&](const QString &path) {
        if (absoluteCleanPath(path) == currentParent
            || (source.isDir()
                && services::SharedStorageFileService::isPathInside(path, sourcePath))) {
            return;
        }
        const QString relative = QDir(sharedStorageLayout_.questionBanks)
            .relativeFilePath(path).replace(u'\\', u'/');
        const QString label = relative == QStringLiteral(".")
            ? QStringLiteral("题库根目录")
            : relative.split(u'/', Qt::SkipEmptyParts).join(QStringLiteral(" / "));
        labels.append(label);
        pathsByLabel.insert(label, path);
    };
    addDestination(sharedStorageLayout_.questionBanks);
    QDirIterator directories(
        sharedStorageLayout_.questionBanks,
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (directories.hasNext()) {
        addDestination(directories.next());
    }
    labels.sort(Qt::CaseInsensitive);
    if (labels.isEmpty()) {
        libraryImportStatus_->setText(QStringLiteral("没有可移动到的其他层级"));
        return;
    }

    bool accepted = false;
    const QString selectedLabel = QInputDialog::getItem(
        this,
        QStringLiteral("移动题库项目"),
        QStringLiteral("目标层级"),
        labels,
        0,
        false,
        &accepted);
    if (!accepted || !pathsByLabel.contains(selectedLabel)) {
        return;
    }
    const QString destinationDirectory = pathsByLabel.value(selectedLabel);
    const QString requested = QDir(destinationDirectory).filePath(source.fileName());
    services::StorageConflictPolicy policy = services::StorageConflictPolicy::Skip;
    if (QFileInfo::exists(requested)) {
        const auto answer = QMessageBox::question(
            this,
            QStringLiteral("同名项目处理"),
            QStringLiteral("目标层级已有同名项目。覆盖现有项目，还是保留两个？"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::No);
        if (answer == QMessageBox::Cancel) {
            return;
        }
        policy = answer == QMessageBox::Yes
            ? services::StorageConflictPolicy::Overwrite
            : services::StorageConflictPolicy::KeepBoth;
    }
    services::SharedStorageFileService service;
    const auto result = service.moveQuestionBankEntry(
        sharedStorageLayout_, sourcePath, destinationDirectory, policy);
    libraryImportStatus_->setText(result.completed
        ? QStringLiteral("已移动到：%1").arg(selectedLabel)
        : result.error);
    refreshSharedFileTree();
    if (result.completed && result.affectedEntries > 0) {
        refreshLibraryIconPickers();
        startSharedBankSync(true);
    }
}

void AppWindow::recycleSelectedSharedEntry()
{
    QTreeWidgetItem *item = libraryFilesTree_ ? libraryFilesTree_->currentItem() : nullptr;
    if (!item) {
        return;
    }
    const QString path = item->data(0, kStoragePathRole).toString();
    if (QMessageBox::question(
            this,
            QStringLiteral("移入回收站"),
            QStringLiteral("确定将“%1”移入回收站？").arg(QFileInfo(path).fileName()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    services::SharedStorageFileService service;
    const auto result = service.moveToRecycleBin(sharedStorageLayout_, path);
    libraryImportStatus_->setText(result.completed
        ? QStringLiteral("已移入回收站") : result.error);
    refreshSharedFileTree();
    if (result.completed) {
        startSharedBankSync(false);
    }
}

void AppWindow::restoreSelectedSharedEntry()
{
    QTreeWidgetItem *item = libraryFilesTree_ ? libraryFilesTree_->currentItem() : nullptr;
    if (!item) {
        return;
    }
    const QStringList hiddenPath = item->data(
        0, kHiddenLibraryPathRole).toStringList();
    if (!hiddenPath.isEmpty()) {
        storage::Database database(
            QStringLiteral("library-hidden-restore-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QString error;
        if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
            libraryImportStatus_->setText(QStringLiteral("恢复失败：%1").arg(error));
            return;
        }
        storage::SqliteLibraryRepository repository(database.connection());
        if (!repository.restoreHiddenNode(hiddenPath, &error)) {
            libraryImportStatus_->setText(QStringLiteral("恢复失败：%1").arg(error));
            return;
        }
        libraryImportStatus_->setText(QStringLiteral("已恢复内置题库"));
        refreshSharedFileTree();
        refreshInstalledBankList();
        refreshLibraryStats();
        refreshLibraryIconPickers();
        return;
    }
    services::SharedStorageFileService service;
    const auto result = service.restoreFromRecycleBin(
        sharedStorageLayout_,
        item->data(0, kStoragePathRole).toString(),
        services::StorageConflictPolicy::KeepBoth);
    libraryImportStatus_->setText(result.completed
        ? QStringLiteral("已恢复回收站项目") : result.error);
    refreshSharedFileTree();
    if (result.completed) {
        startSharedBankSync(true);
    }
}

void AppWindow::permanentlyDeleteSelectedSharedEntry()
{
    QTreeWidgetItem *item = libraryFilesTree_ ? libraryFilesTree_->currentItem() : nullptr;
    if (!item) {
        return;
    }
    const QString path = item->data(0, kStoragePathRole).toString();
    if (QMessageBox::warning(
            this,
            QStringLiteral("彻底删除"),
            QStringLiteral("彻底删除“%1”？此操作无法恢复。").arg(QFileInfo(path).fileName()),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    services::SharedStorageFileService service;
    const auto result = service.permanentlyDelete(sharedStorageLayout_, path);
    libraryImportStatus_->setText(result.completed
        ? QStringLiteral("已彻底删除") : result.error);
    refreshSharedFileTree();
}

void AppWindow::choosePrimaryColor()
{
    QColor current(primaryColorHex_);
    if (!current.isValid()) {
        current = ThemePalettes::find(
            paletteChoice_->currentData().toString()).primary;
    }
    QColor selected = QColorDialog::getColor(
        current, this, QStringLiteral("选择自定义主色"),
        QColorDialog::DontUseNativeDialog);
    if (!selected.isValid()) {
        return;
    }
    selected.setAlpha(255);
    const ThemePalette &palette = ThemePalettes::find(
        paletteChoice_->currentData().toString());
    const bool dark = resolvedTheme(themeChoice_->currentData().toString())
        == QStringLiteral("dark");
    const QColor surface = dark ? palette.darkSurface : palette.lightSurface;
    if (contrastRatio(selected, surface) < 1.5) {
        QMessageBox::warning(
            this,
            QStringLiteral("主色不可用"),
            QStringLiteral("该颜色与当前界面背景过于接近，请选择更清晰的颜色。"));
        return;
    }
    primaryColorHex_ = selected.name(QColor::HexRgb);
    updatePrimaryColorControl();
    applyTheme(themeChoice_->currentData().toString());
}

void AppWindow::resetPrimaryColorToPalette()
{
    if (paletteChoice_->currentData().toString() == QStringLiteral("endfield")) {
        return;
    }
    primaryColorHex_ = ThemePalettes::find(
        paletteChoice_->currentData().toString()).primary.name(QColor::HexRgb);
    updatePrimaryColorControl();
    applyTheme(themeChoice_->currentData().toString());
}

void AppWindow::updatePrimaryColorControl()
{
    if (!primaryColorButton_ || !primaryColorValue_) {
        return;
    }
    const bool endfield = paletteChoice_
        && paletteChoice_->currentData().toString() == QStringLiteral("endfield");
    QColor primary(primaryColorHex_);
    if (!primary.isValid()) {
        primary = ThemePalettes::find(
            paletteChoice_ ? paletteChoice_->currentData().toString()
                           : QStringLiteral("forest")).primary;
        primaryColorHex_ = primary.name(QColor::HexRgb);
    }
    const QColor foreground = readableOnPrimary(primary);
    const double ratio = contrastRatio(primary, foreground);
    primaryColorButton_->setStyleSheet(QStringLiteral(
        "QPushButton#primaryColorButton { background: %1; border: 2px solid %2; "
        "border-radius: 5px; padding: 0; }")
        .arg(primary.name(QColor::HexRgb), foreground.name(QColor::HexRgb)));
    primaryColorButton_->setEnabled(!endfield);
    const QString primaryDescription = endfield
        ? QStringLiteral("高级主题固定配色")
        : QStringLiteral("%1 · 可读性 %2:1")
              .arg(primary.name(QColor::HexRgb).toUpper())
              .arg(ratio, 0, 'f', 1);
    primaryColorValue_->setText(width() < kTabletNavigationBreakpoint
        ? (endfield ? QStringLiteral("固定配色")
                    : primary.name(QColor::HexRgb).toUpper())
        : primaryDescription);
    primaryColorValue_->setToolTip(primaryDescription);
    primaryColorValue_->setVisible(width() >= kTabletNavigationBreakpoint);
    if (themePreview_) {
        themePreview_->setPrimaryColor(endfield ? QColor() : primary);
    }
}

int AppWindow::detectedLibraryHierarchyDepth() const
{
    int maximumDepth = 2;
    if (!databasePath_.isEmpty()) {
        storage::Database database(
            QStringLiteral("library-icon-depth-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QString error;
        if (database.open(databasePath_, &error) && database.migrate(&error)) {
            const storage::SqliteQuestionRepository repository(database.connection());
            const QVector<domain::InstalledBankSummary> banks =
                repository.listInstalledBanks(&error);
            for (const domain::InstalledBankSummary &bank : banks) {
                maximumDepth = std::max(
                    maximumDepth, static_cast<int>(bank.path.size()));
            }
        }
    }

    const QString questionBanksRoot = sharedStorageLayout_.ready()
        ? sharedStorageLayout_.questionBanks
        : QDir(sharedStorageRoot_).filePath(QStringLiteral("QuestionBanks"));
    if (!questionBanksRoot.isEmpty() && QDir(questionBanksRoot).exists()) {
        const QDir root(questionBanksRoot);
        QDirIterator iterator(
            questionBanksRoot,
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString relative = root.relativeFilePath(iterator.next())
                .replace(u'\\', u'/');
            if (relative.startsWith(QStringLiteral("../"))) {
                continue;
            }
            maximumDepth = std::max(
                maximumDepth,
                static_cast<int>(relative.split(
                    u'/', Qt::SkipEmptyParts).size()));
        }
    }
    return maximumDepth;
}

void AppWindow::refreshLibraryIconPickers()
{
    if (!libraryIconPickersContainer_ || !libraryIconPickersLayout_) {
        return;
    }

    levelIconChoiceGroups_.clear();
    while (libraryIconPickersLayout_->count() > 0) {
        QLayoutItem *item = libraryIconPickersLayout_->takeAt(0);
        delete item->widget();
        delete item;
    }

    const int levelCount = detectedLibraryHierarchyDepth();
    const QStringList selectedKeys = configuredLibraryIconKeys(
        QSettings(), levelCount);
    levelIconChoiceGroups_.reserve(levelCount);
    for (int level = 0; level < levelCount; ++level) {
        const int displayLevel = level + 1;
        auto *label = new QLabel(
            level == 0 ? QStringLiteral("科目")
                       : level == 1 ? QStringLiteral("章节")
                                    : QStringLiteral("第%1层").arg(displayLevel),
            libraryIconPickersContainer_);
        label->setObjectName(QStringLiteral("settingsFieldLabel"));
        label->setProperty("libraryIconLevel", displayLevel);
        label->setMinimumWidth(54);

        auto *scroll = new QScrollArea(libraryIconPickersContainer_);
        scroll->setObjectName(
            QStringLiteral("levelIconPicker-%1").arg(displayLevel));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(false);
        scroll->setMinimumWidth(0);
        scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        scroll->setFixedHeight(48);

        auto *picker = new QWidget(scroll);
        auto *pickerLayout = new QHBoxLayout(picker);
        pickerLayout->setContentsMargins(0, 0, 0, 0);
        pickerLayout->setSpacing(4);
        auto *group = new QButtonGroup(picker);
        group->setExclusive(true);
        const QString objectPrefix = level == 0
            ? QStringLiteral("subjectIconChoice-")
            : level == 1
                ? QStringLiteral("chapterIconChoice-")
                : QStringLiteral("level%1IconChoice-").arg(displayLevel);
        for (const LibraryIconOption &option : kLibraryIconOptions) {
            const QString key = QString::fromLatin1(option.key);
            auto *button = new QToolButton(picker);
            button->setObjectName(objectPrefix + key);
            button->setText(QString::fromUtf8(option.emoji));
            button->setToolTip(QString::fromUtf8(option.label));
            button->setAccessibleName(
                QStringLiteral("第%1层：%2")
                    .arg(displayLevel)
                    .arg(QString::fromUtf8(option.label)));
            button->setProperty("libraryIconKey", key);
            button->setCheckable(true);
            button->setChecked(key == selectedKeys.at(level));
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setFixedSize(38, 38);
            QFont iconFont = button->font();
            iconFont.setPointSize(17);
            button->setFont(iconFont);
            group->addButton(button);
            pickerLayout->addWidget(button);
        }
        picker->setFixedSize(
            static_cast<int>(kLibraryIconOptions.size()) * 38
                + (static_cast<int>(kLibraryIconOptions.size()) - 1) * 4,
            42);
        scroll->setWidget(picker);
        libraryIconPickersLayout_->addWidget(label, level, 0);
        libraryIconPickersLayout_->addWidget(scroll, level, 1);
        levelIconChoiceGroups_.append(group);
    }
    libraryIconPickersContainer_->updateGeometry();
}

void AppWindow::saveSettings()
{
    const QString theme = themeChoice_->currentData().toString();
    QSettings settings;
    settings.setValue(QStringLiteral("ui/theme"), theme);
    settings.setValue(QStringLiteral("ui/palette"), paletteChoice_->currentData().toString());
    settings.setValue(QStringLiteral("ui/reduceMotion"), reduceMotionChoice_->isChecked());
    settings.setValue(QStringLiteral("ui/cornerRadius"), cornerRadiusChoice_->value());
    settings.setValue(QStringLiteral("ui/primary"), primaryColorHex_);
    QStringList levelIconKeys = settings.value(
        QStringLiteral("ui/levelIconStyles")).toStringList();
    for (qsizetype level = 0; level < levelIconChoiceGroups_.size(); ++level) {
        while (levelIconKeys.size() <= level) {
            levelIconKeys.append(defaultLibraryIconKey(levelIconKeys.size()));
        }
        QButtonGroup *group = levelIconChoiceGroups_.at(level);
        if (group && group->checkedButton()) {
            levelIconKeys[level] =
                group->checkedButton()->property("libraryIconKey").toString();
        } else {
            levelIconKeys[level] = defaultLibraryIconKey(level);
        }
    }
    settings.setValue(QStringLiteral("ui/levelIconStyles"), levelIconKeys);
    if (!levelIconKeys.isEmpty()) {
        settings.setValue(QStringLiteral("ui/subjectIconStyle"), levelIconKeys.at(0));
    }
    if (levelIconKeys.size() > 1) {
        settings.setValue(QStringLiteral("ui/chapterIconStyle"), levelIconKeys.at(1));
    }
    settings.setValue(
        QStringLiteral("practice/autoSaveOnExit"), autoSaveOnExitChoice_->isChecked());
    settings.setValue(
        QStringLiteral("practice/progressScopePolicy"),
        mergePracticeProgressChoice_->isChecked()
            ? QStringLiteral("merged") : QStringLiteral("separate"));
    settings.setValue(
        QStringLiteral("practice/randomReshuffleOnReset"),
        randomReshuffleChoice_->isChecked());
    settings.setValue(
        QStringLiteral("view/rememberReviewPosition"),
        rememberReviewPositionChoice_->isChecked());
    settings.setValue(
        QStringLiteral("view/rememberAnswerLookupPosition"),
        rememberAnswerLookupPositionChoice_->isChecked());
    settings.setValue(
        QStringLiteral("home/showSavedProgressEntry"),
        showSavedProgressChoice_->isChecked());
    settings.setValue(
        QStringLiteral("home/savedProgressWidthPhone"),
        savedProgressPhoneWidthChoice_->value());
    settings.setValue(
        QStringLiteral("home/savedProgressWidthTablet"),
        savedProgressTabletWidthChoice_->value());
    settings.setValue(
        QStringLiteral("updates/autoAppCheck"),
        autoAppUpdateCheckChoice_->isChecked());
    settings.setValue(
        QStringLiteral("updates/autoBankCheck"),
        autoBankUpdateCheckChoice_->isChecked());
    settings.setValue(
        QStringLiteral("updates/autoAnnouncementCheck"),
        autoAnnouncementCheckChoice_->isChecked());
    QString aiError;
    const bool aiSaved = aiSettingsPanel_ && aiSettingsPanel_->save(&aiError);
    settings.sync();
    if (!autoSaveOnExitChoice_->isChecked()
        && practiceSaveTimer_ && practiceSaveTimer_->isActive()) {
        practiceSaveTimer_->stop();
    }
    applyTheme(theme);
    refreshSavedProgressWidget();
    refreshInstalledBankList();
    settingsStatus_->setText(settings.status() == QSettings::NoError && aiSaved
        ? QStringLiteral("设置已保存")
        : (aiError.isEmpty() ? QStringLiteral("设置保存失败") : aiError));
}

bool AppWindow::automaticPracticePersistenceEnabled(domain::PracticeMode mode) const
{
    const QSettings settings;
    switch (mode) {
    case domain::PracticeMode::Sequential:
    case domain::PracticeMode::Random:
        return settings.value(QStringLiteral("practice/autoSaveOnExit"), true).toBool();
    case domain::PracticeMode::Memorize:
        return settings.value(QStringLiteral("view/rememberReviewPosition"), true).toBool();
    case domain::PracticeMode::AnswerLookup:
        return settings.value(
            QStringLiteral("view/rememberAnswerLookupPosition"), true).toBool();
    case domain::PracticeMode::WrongBook:
    case domain::PracticeMode::Review:
        return false;
    }
    return false;
}

bool AppWindow::mergedPracticeProgressEnabled() const
{
    return QSettings().value(
        QStringLiteral("practice/progressScopePolicy"),
        QStringLiteral("separate")).toString() == QStringLiteral("merged");
}

domain::PracticeMode AppWindow::answerStateStorageMode(domain::PracticeMode mode) const
{
    if (mergedPracticeProgressEnabled()
        && (mode == domain::PracticeMode::Sequential
            || mode == domain::PracticeMode::Random)) {
        return domain::PracticeMode::Sequential;
    }
    return mode;
}

void AppWindow::savePracticeManually()
{
    QString error;
    const bool saved = saveActivePracticeSession(&error);
    const QString message = saved ? QStringLiteral("已保存")
                                  : QStringLiteral("保存失败：%1").arg(error);
    if (answerTablePage_ && studyStack_
        && studyStack_->currentWidget() == answerTablePage_) {
        answerTablePage_->showSaveStatus(message);
    } else if (practicePage_) {
        practicePage_->showSaveStatus(message, !saved);
    }
}

void AppWindow::resetActivePractice()
{
    if (!practicePage_ || !practicePage_->hasActiveSession() || databasePath_.isEmpty()) {
        return;
    }
    if (QMessageBox::question(
            this,
            QStringLiteral("重做练习"),
            QStringLiteral("清除当前范围的作答和进度并重新开始？错题集、复习卡和笔记不会删除。"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    storage::Database database(
        QStringLiteral("practice-reset-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        practicePage_->showSaveStatus(QStringLiteral("重做失败：%1").arg(error), true);
        return;
    }
    const domain::PracticeSession session = practicePage_->session();
    storage::SqlitePracticeRepository practiceRepository(database.connection());
    QVector<domain::PracticeMode> modes{session.mode};
    if (mergedPracticeProgressEnabled()
        && (session.mode == domain::PracticeMode::Sequential
            || session.mode == domain::PracticeMode::Random)) {
        modes = {domain::PracticeMode::Sequential, domain::PracticeMode::Random};
    }
    for (const domain::PracticeMode mode : modes) {
        if (!practiceRepository.removeScopeMode(session.scopeId, mode, &error)) {
            practicePage_->showSaveStatus(QStringLiteral("重做失败：%1").arg(error), true);
            return;
        }
    }
    storage::SqliteAnswerStateRepository answerRepository(database.connection());
    QVector<domain::PracticeMode> answerModes;
    if (session.mode == domain::PracticeMode::Sequential
        || session.mode == domain::PracticeMode::Random) {
        answerModes = mergedPracticeProgressEnabled()
            ? QVector<domain::PracticeMode>{
                  domain::PracticeMode::Sequential, domain::PracticeMode::Random}
            : QVector<domain::PracticeMode>{session.mode};
    }
    for (const domain::PracticeMode mode : answerModes) {
        if (!answerRepository.clear(session.questionOrder, mode, &error)) {
            practicePage_->showSaveStatus(QStringLiteral("重做失败：%1").arg(error), true);
            return;
        }
    }
    const bool reshuffle = QSettings().value(
        QStringLiteral("practice/randomReshuffleOnReset"), true).toBool();
    practicePage_->resetSession(reshuffle);
    if (answerTablePage_ && answerTablePage_->hasContent()) {
        answerTablePage_->setCurrentQuestionIndex(0);
    }
    practicePage_->showSaveStatus(QStringLiteral("已重置"));
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
    if (context.practiceMode != domain::PracticeMode::Review
        && automaticPracticePersistenceEnabled(context.practiceMode)) {
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
            widget->hide();
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

    const storage::SqliteLibraryRepository libraryRepository(database.connection());
    const QStringList savedChildOrder = libraryRepository.childOrder(libraryPath_, &error);
    QHash<QString, int> savedOrderIndexes;
    for (qsizetype index = 0; index < savedChildOrder.size(); ++index) {
        savedOrderIndexes.insert(savedChildOrder.at(index), static_cast<int>(index));
    }
    QVector<ChildNode> sortedNodes = childNodes.values();
    std::sort(sortedNodes.begin(), sortedNodes.end(),
              [&savedOrderIndexes](const ChildNode &left, const ChildNode &right) {
        const int unranked = std::numeric_limits<int>::max();
        const int leftRank = savedOrderIndexes.value(left.title, unranked);
        const int rightRank = savedOrderIndexes.value(right.title, unranked);
        if (leftRank != rightRank) {
            return leftRank < rightRank;
        }
        return domain::naturalLibraryTitleLess(left.title, right.title);
    });
    const QSettings settings;
    const QStringList levelIconKeys = configuredLibraryIconKeys(
        settings, std::max(2, static_cast<int>(libraryPath_.size() + 1)));
    for (const ChildNode &node : sortedNodes) {
        auto *row = new QWidget(this);
        row->setObjectName(QStringLiteral("libraryPathNodeRow"));
        row->setProperty("libraryPath", node.path);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);
        rowLayout->addStretch(1);
        auto *card = new LibraryNodeCard(node.path, row);
        card->setObjectName(QStringLiteral("libraryPathNodeCard"));
        card->setProperty("libraryReorderEnabled", libraryEditMode_);
        card->setMaximumWidth(760);
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 8, 8);
        cardLayout->setSpacing(8);
        const int zeroBasedLevel = static_cast<int>(node.path.size() - 1);
        const QString iconKey = levelIconKeys.value(
            zeroBasedLevel, defaultLibraryIconKey(zeroBasedLevel));
        const LibraryIconOption &iconOption = libraryIconOption(
            iconKey, defaultLibraryIconKey(zeroBasedLevel).toLatin1().constData());
        auto *nodeIcon = new QLabel(QString::fromUtf8(iconOption.emoji), card);
        nodeIcon->setObjectName(QStringLiteral("libraryPathNodeIcon"));
        nodeIcon->setToolTip(QString::fromUtf8(iconOption.label));
        nodeIcon->setAccessibleName(zeroBasedLevel == 0
            ? QStringLiteral("科目图案")
            : zeroBasedLevel == 1
                ? QStringLiteral("章节图案")
                : QStringLiteral("第%1层图案").arg(zeroBasedLevel + 1));
        nodeIcon->setAlignment(Qt::AlignCenter);
        nodeIcon->setFixedSize(36, 36);
        QFont nodeIconFont = nodeIcon->font();
        nodeIconFont.setPointSize(19);
        nodeIcon->setFont(nodeIconFont);
        cardLayout->addWidget(nodeIcon);
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
            navigateLibraryPath(path);
        };
        connect(button, &QPushButton::clicked, this, openPath);
        card->watchDragSource(button);
        cardLayout->addWidget(button, 1);
        auto *chevron = new QToolButton(card);
        if (libraryEditMode_) {
            chevron->setObjectName(QStringLiteral("libraryPathDeleteButton"));
            chevron->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
            chevron->setIconSize(QSize(16, 16));
            chevron->setToolTip(QStringLiteral("删除%1").arg(node.title));
            chevron->setAccessibleName(QStringLiteral("删除%1").arg(node.title));
            chevron->setFixedSize(32, 32);
            connect(chevron, &QToolButton::clicked, this, [this, path = node.path] {
                removeLibraryNode(path);
            });
            cardLayout->addWidget(chevron, 0, Qt::AlignTop);
        } else {
            chevron->setObjectName(QStringLiteral("libraryPathChevron"));
            chevron->setProperty("quizappIcon", QStringLiteral("chevron_right"));
            chevron->setToolTip(QStringLiteral("进入%1").arg(node.title));
            chevron->setAccessibleName(QStringLiteral("进入%1").arg(node.title));
            chevron->setFixedSize(42, 42);
            connect(chevron, &QToolButton::clicked, this, openPath);
            card->watchDragSource(chevron);
            cardLayout->addWidget(chevron);
        }
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
    refreshIconsForTheme(
        settings.value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString());
}

void AppWindow::saveLibraryNodeOrder(const QVector<QStringList> &orderedPaths)
{
    if (databasePath_.isEmpty() || orderedPaths.isEmpty()) {
        return;
    }
    QStringList titles;
    titles.reserve(orderedPaths.size());
    for (const QStringList &path : orderedPaths) {
        if (path.size() != libraryPath_.size() + 1
            || !pathStartsWith(path, libraryPath_)) {
            return;
        }
        titles.append(path.constLast());
    }
    storage::Database database(
        QStringLiteral("library-order-save-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        libraryImportStatus_->setText(QStringLiteral("保存题库顺序失败：%1").arg(error));
        return;
    }
    storage::SqliteLibraryRepository repository(database.connection());
    if (!repository.setChildOrder(libraryPath_, titles, &error)) {
        libraryImportStatus_->setText(QStringLiteral("保存题库顺序失败：%1").arg(error));
        return;
    }
    libraryImportStatus_->setText(QStringLiteral("当前层级顺序已保存"));
    refreshInstalledBankList();
}

void AppWindow::removeLibraryNode(const QStringList &path)
{
    if (path.isEmpty() || databasePath_.isEmpty()) {
        return;
    }
    storage::Database database(
        QStringLiteral("library-remove-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        libraryImportStatus_->setText(QStringLiteral("删除失败：%1").arg(error));
        return;
    }
    const storage::SqliteQuestionRepository questionRepository(database.connection());
    const auto banks = questionRepository.listInstalledBanks(&error);
    QSet<QString> affectedBankIds;
    for (const domain::InstalledBankSummary &bank : banks) {
        if (pathStartsWith(bank.path, path)) {
            affectedBankIds.insert(bank.id);
        }
    }
    if (!error.isEmpty() || affectedBankIds.isEmpty()) {
        libraryImportStatus_->setText(error.isEmpty()
            ? QStringLiteral("该层级没有可删除的题库")
            : QStringLiteral("删除失败：%1").arg(error));
        return;
    }

    const storage::SqliteBankSourceRepository sourceRepository(database.connection());
    const auto sources = sourceRepository.listByRoot(
        QStringLiteral("primary-shared-storage"), &error);
    if (!error.isEmpty()) {
        libraryImportStatus_->setText(QStringLiteral("删除失败：%1").arg(error));
        return;
    }
    QStringList sharedSourceKeys;
    QSet<QString> sharedBankIds;
    QStringList sharedPaths;
    const QString nodeDirectory = sharedStorageLayout_.ready()
        ? QDir(sharedStorageLayout_.questionBanks).filePath(path.join(u'/'))
        : QString();
    for (const domain::ManagedBankSource &source : sources) {
        if (!source.available || !affectedBankIds.contains(source.bankId)) {
            continue;
        }
        sharedSourceKeys.append(source.sourceKey);
        sharedBankIds.insert(source.bankId);
        if (!sharedStorageLayout_.ready()) {
            continue;
        }
        const QString sourcePath = QDir(sharedStorageLayout_.questionBanks)
            .filePath(source.relativePath);
        const QString target = QFileInfo(nodeDirectory).isDir()
            && services::SharedStorageFileService::isPathInside(sourcePath, nodeDirectory)
            ? nodeDirectory : sourcePath;
        if (QFileInfo::exists(target)) {
            sharedPaths.append(absoluteCleanPath(target));
        }
    }
    sharedPaths.removeDuplicates();
    std::sort(sharedPaths.begin(), sharedPaths.end(), [](const QString &left, const QString &right) {
        return left.size() < right.size();
    });
    QStringList collapsedPaths;
    for (const QString &candidate : std::as_const(sharedPaths)) {
        const bool covered = std::any_of(
            collapsedPaths.cbegin(), collapsedPaths.cend(), [&candidate](const QString &parent) {
                return services::SharedStorageFileService::isPathInside(candidate, parent);
            });
        if (!covered) {
            collapsedPaths.append(candidate);
        }
    }
    QStringList hiddenBankIds;
    for (const QString &bankId : std::as_const(affectedBankIds)) {
        if (!sharedBankIds.contains(bankId)) {
            hiddenBankIds.append(bankId);
        }
    }
    if (!sharedSourceKeys.isEmpty() && !sharedStorageLayout_.ready()) {
        libraryImportStatus_->setText(QStringLiteral("共享题库目录当前不可访问，无法安全移入回收站"));
        return;
    }
    QStringList effects;
    if (!sharedSourceKeys.isEmpty()) {
        effects.append(QStringLiteral("%1 个共享题库移入回收站").arg(sharedSourceKeys.size()));
    }
    if (!hiddenBankIds.isEmpty()) {
        effects.append(QStringLiteral("%1 个内置题库仅在本机隐藏").arg(hiddenBankIds.size()));
    }
    if (QMessageBox::question(
            this,
            QStringLiteral("删除题库层级"),
            QStringLiteral("确定删除“%1”？\n%2\n之后可在题库管理中恢复。")
                .arg(path.constLast(), effects.join(u'；')),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    services::SharedStorageFileService fileService;
    QStringList recycledPaths;
    for (const QString &sourcePath : std::as_const(collapsedPaths)) {
        const auto result = fileService.moveToRecycleBin(sharedStorageLayout_, sourcePath);
        if (!result.completed) {
            for (auto iterator = recycledPaths.crbegin(); iterator != recycledPaths.crend(); ++iterator) {
                fileService.restoreFromRecycleBin(
                    sharedStorageLayout_, *iterator,
                    services::StorageConflictPolicy::Overwrite);
            }
            libraryImportStatus_->setText(QStringLiteral("删除失败：%1").arg(result.error));
            return;
        }
        recycledPaths.append(result.destinationPath);
    }
    storage::SqliteLibraryRepository libraryRepository(database.connection());
    if (!libraryRepository.deactivateForRemoval(
            path, hiddenBankIds, sharedSourceKeys, &error)) {
        for (auto iterator = recycledPaths.crbegin(); iterator != recycledPaths.crend(); ++iterator) {
            fileService.restoreFromRecycleBin(
                sharedStorageLayout_, *iterator,
                services::StorageConflictPolicy::Overwrite);
        }
        libraryImportStatus_->setText(QStringLiteral("删除失败：%1").arg(error));
        return;
    }
    libraryImportStatus_->setText(QStringLiteral("已删除，可从题库管理中恢复"));
    refreshSharedFileTree();
    refreshInstalledBankList();
    refreshLibraryStats();
    refreshLibraryIconPickers();
    if (!sharedSourceKeys.isEmpty()) {
        startSharedBankSync(false);
    }
}

void AppWindow::navigateLibraryPath(const QStringList &path)
{
    libraryPath_ = path;
    refreshInstalledBankList();
    if (!libraryPageScroll_) {
        return;
    }
    QTimer::singleShot(0, libraryPageScroll_, [scroll = libraryPageScroll_] {
        scroll->verticalScrollBar()->setValue(0);
    });
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
    const bool mergeModes = mergedPracticeProgressEnabled()
        && (mode == domain::PracticeMode::Sequential
            || mode == domain::PracticeMode::Random);
    const bool rememberPosition = mode != domain::PracticeMode::Memorize
            && mode != domain::PracticeMode::AnswerLookup
        ? mode != domain::PracticeMode::WrongBook
        : automaticPracticePersistenceEnabled(mode);
    std::optional<domain::PracticeSession> restored;
    std::optional<domain::PracticeSession> mergedProgress;
    if (rememberPosition && mergeModes) {
        const auto latest = practiceRepository.latestAcrossModes(
            scope.id,
            {domain::PracticeMode::Sequential, domain::PracticeMode::Random},
            &error);
        if (latest.has_value() && latest->mode == mode) restored = latest;
        else mergedProgress = latest;
    } else if (rememberPosition) {
        restored = practiceRepository.latest(scope.id, mode, &error);
    }
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("读取练习进度失败：%1").arg(error));
        }
        error.clear();
    }
    practicePage_->setDataRoot(dataRoot_);
    practicePage_->start(scope, questions, mode, restored);
    if (mergedProgress.has_value()) {
        practicePage_->applyMergedProgress(*mergedProgress);
    }
    storage::SqliteAnswerStateRepository answerStateRepository(database.connection());
    const QHash<QUuid, QString> persistedAnswers = answerStateRepository.load(
        practicePage_->session().questionOrder, answerStateStorageMode(mode), &error);
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("读取全局答案状态失败：%1").arg(error));
        }
        return;
    }
    practicePage_->applyPersistedAnswers(persistedAnswers);
    practicePage_->setWrongBookQuestionIds(wrongQuestionIds);
    practicePage_->setReviewQuestionIds(reviewQuestionIds);
    if (mode == domain::PracticeMode::AnswerLookup) {
        answerTablePage_->start(scope, questions, practicePage_->session());
        studyStack_->setCurrentWidget(answerTablePage_);
    } else {
        studyStack_->setCurrentWidget(practicePage_);
    }
    setStudyActivity(studyActivityForMode(mode), scope.id);
    if (automaticPracticePersistenceEnabled(mode)
        && !saveActivePracticeSession(&error) && libraryImportStatus_) {
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

void AppWindow::refreshExamQuestions()
{
    if (!examPage_ || databasePath_.isEmpty()) {
        return;
    }
    storage::Database database(
        QStringLiteral("exam-questions-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        examPage_->setQuestions({});
        return;
    }
    storage::SqliteQuestionRepository repository(database.connection());
    QVector<domain::Question> questions;
    const auto banks = repository.listInstalledBanks(&error);
    if (!error.isEmpty()) {
        examPage_->setQuestions({});
        return;
    }
    for (const domain::InstalledBankSummary &bank : banks) {
        const auto bankQuestions = repository.listByBankId(bank.id, &error);
        if (!error.isEmpty()) {
            examPage_->setQuestions({});
            return;
        }
        questions += bankQuestions;
    }
    examPage_->setQuestions(questions);
}

void AppWindow::openExamPage()
{
    if (!examPage_ || !studyStack_) {
        return;
    }
    refreshExamQuestions();
    examPage_->showSetup();
    studyStack_->setCurrentWidget(examPage_);
    navigateTo(Section::Study);
}

void AppWindow::showNotebookLibrary()
{
    if (!notebookLibraryPage_ || !studyStack_) return;
    notebookLibraryPage_->refresh();
    studyStack_->setCurrentWidget(notebookLibraryPage_);
    navigateTo(Section::Study);
    setStudyActivity(domain::StudyActivity::Handwriting, QStringLiteral("notebook-library"));
}

void AppWindow::openFreeNotebook(const domain::NotebookRecord &record)
{
    if (!handwritingPage_ || !rootStack_) return;
    if (!handwritingPage_->openFreeNotebook(record)) return;
    activityBeforeHandwriting_.reset();
    scopeBeforeHandwriting_.clear();
    setStudyActivity(
        domain::StudyActivity::Handwriting,
        record.id.toString(QUuid::WithoutBraces));
    rootStack_->setCurrentWidget(handwritingPage_);
    handwritingPage_->setFocus();
}

void AppWindow::handleFreeNotebookReturn(const QUuid &notebookId)
{
    if (notebookLibraryPage_) notebookLibraryPage_->markSaved(notebookId);
    if (rootStack_ && shellRoot_) rootStack_->setCurrentWidget(shellRoot_);
    if (studyStack_ && notebookLibraryPage_) {
        studyStack_->setCurrentWidget(notebookLibraryPage_);
    }
    navigateTo(Section::Study);
    setStudyActivity(domain::StudyActivity::Handwriting, QStringLiteral("notebook-library"));
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
    const bool mergeModes = mergedPracticeProgressEnabled()
        && (mode == domain::PracticeMode::Sequential
            || mode == domain::PracticeMode::Random);
    const bool rememberPosition = mode != domain::PracticeMode::Memorize
            && mode != domain::PracticeMode::AnswerLookup
        ? mode != domain::PracticeMode::WrongBook
        : automaticPracticePersistenceEnabled(mode);
    std::optional<domain::PracticeSession> restored;
    std::optional<domain::PracticeSession> mergedProgress;
    if (rememberPosition && mergeModes) {
        const auto latest = practiceRepository.latestAcrossModes(
            bankId,
            {domain::PracticeMode::Sequential, domain::PracticeMode::Random},
            &error);
        if (latest.has_value() && latest->mode == mode) restored = latest;
        else mergedProgress = latest;
    } else if (rememberPosition) {
        restored = practiceRepository.latest(bankId, mode, &error);
    }
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("读取练习进度失败：%1").arg(error));
        }
        error.clear();
    }
    practicePage_->setDataRoot(dataRoot_);
    practicePage_->start(*found, questions, mode, restored);
    if (mergedProgress.has_value()) {
        practicePage_->applyMergedProgress(*mergedProgress);
    }
    storage::SqliteAnswerStateRepository answerStateRepository(database.connection());
    const QHash<QUuid, QString> persistedAnswers = answerStateRepository.load(
        practicePage_->session().questionOrder, answerStateStorageMode(mode), &error);
    if (!error.isEmpty()) {
        if (libraryImportStatus_) {
            libraryImportStatus_->setText(QStringLiteral("读取全局答案状态失败：%1").arg(error));
        }
        return;
    }
    practicePage_->applyPersistedAnswers(persistedAnswers);
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
    if (automaticPracticePersistenceEnabled(mode)
        && !saveActivePracticeSession(&error) && libraryImportStatus_) {
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

void AppWindow::refreshSavedProgressWidget()
{
    homeSavedProgressSession_.reset();
    if (!homeSavedProgressCard_) {
        return;
    }
    const bool enabled = showSavedProgressChoice_
        ? showSavedProgressChoice_->isChecked()
        : QSettings().value(
              QStringLiteral("home/showSavedProgressEntry"), true).toBool();
    if (!enabled || databasePath_.isEmpty()) {
        homeSavedProgressCard_->hide();
        return;
    }

    storage::Database database(
        QStringLiteral("saved-progress-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        homeSavedProgressCard_->hide();
        return;
    }
    storage::SqlitePracticeRepository practiceRepository(database.connection());
    const auto session = practiceRepository.latestIncompleteAcrossScopes(
        {domain::PracticeMode::Sequential,
         domain::PracticeMode::Random,
         domain::PracticeMode::Memorize,
         domain::PracticeMode::AnswerLookup},
        &error);
    if (!session.has_value() || !error.isEmpty() || session->questionOrder.isEmpty()) {
        homeSavedProgressCard_->hide();
        return;
    }

    const storage::SqliteQuestionRepository questionRepository(database.connection());
    const QVector<domain::InstalledBankSummary> banks =
        questionRepository.listInstalledBanks(&error);
    if (!error.isEmpty()) {
        homeSavedProgressCard_->hide();
        return;
    }

    QString scopeTitle;
    QString scopeType;
    QString scopeBankId;
    QStringList scopePath;
    for (const domain::InstalledBankSummary &bank : banks) {
        if (bank.id == session->scopeId) {
            scopeTitle = bank.title;
            scopeType = QStringLiteral("bank");
            scopeBankId = bank.id;
            break;
        }
    }
    if (scopeType.isEmpty()) {
        for (const domain::InstalledBankSummary &bank : banks) {
            for (qsizetype depth = 0; depth <= bank.path.size(); ++depth) {
                const QStringList candidate = bank.path.mid(0, depth);
                if (scopeIdForPath(candidate) != session->scopeId) {
                    continue;
                }
                scopeTitle = candidate.isEmpty()
                    ? QStringLiteral("全部题库") : candidate.constLast();
                scopeType = QStringLiteral("path");
                scopePath = candidate;
                break;
            }
            if (!scopeType.isEmpty()) {
                break;
            }
        }
    }
    if (scopeType.isEmpty()) {
        homeSavedProgressCard_->hide();
        return;
    }

    homeSavedProgressSession_ = session;
    homeSavedProgressCard_->setProperty("savedScopeType", scopeType);
    homeSavedProgressCard_->setProperty("savedScopeBankId", scopeBankId);
    homeSavedProgressCard_->setProperty("savedScopePath", scopePath);
    homeSavedProgressTitle_->setText(QStringLiteral("继续：%1").arg(scopeTitle));
    const qsizetype total = session->questionOrder.size();
    const qsizetype position = std::clamp<qsizetype>(
        session->currentIndex + 1, 1, total);
    homeSavedProgressSummary_->setText(
        QStringLiteral("%1 · 第 %2 / %3 题 · 已答 %4")
            .arg(practiceModeTitle(session->mode))
            .arg(position)
            .arg(total)
            .arg(session->answers.size()));
    homeSavedProgressHint_->setVisible(QSettings().value(
        QStringLiteral("home/showSavedProgressHint"), true).toBool());
    homeSavedProgressCard_->show();
    updateSavedProgressWidgetSize();
}

void AppWindow::updateSavedProgressWidgetSize()
{
    if (!homeSavedProgressCard_) {
        return;
    }
    const bool tablet = width() >= kTabletNavigationBreakpoint;
    const QSettings settings;
    const int configured = tablet
        ? (savedProgressTabletWidthChoice_
               ? savedProgressTabletWidthChoice_->value()
               : settings.value(QStringLiteral("home/savedProgressWidthTablet"),
                                kSavedProgressTabletDefaultWidth).toInt())
        : (savedProgressPhoneWidthChoice_
               ? savedProgressPhoneWidthChoice_->value()
               : settings.value(QStringLiteral("home/savedProgressWidthPhone"),
                                kSavedProgressPhoneDefaultWidth).toInt());
    const int minimum = tablet
        ? kSavedProgressTabletMinWidth : kSavedProgressPhoneMinWidth;
    const int maximum = tablet
        ? kSavedProgressTabletMaxWidth : kSavedProgressPhoneMaxWidth;
    const int available = std::max(220, width() - (tablet ? 160 : 48));
    homeSavedProgressCard_->setFixedWidth(
        std::min(std::clamp(configured, minimum, maximum), available));
}

void AppWindow::resumeSavedProgress()
{
    if (!homeSavedProgressSession_.has_value() || !homeSavedProgressCard_) {
        refreshSavedProgressWidget();
        return;
    }
    const domain::PracticeMode mode = homeSavedProgressSession_->mode;
    const QString scopeType =
        homeSavedProgressCard_->property("savedScopeType").toString();
    if (scopeType == QStringLiteral("bank")) {
        const QString bankId =
            homeSavedProgressCard_->property("savedScopeBankId").toString();
        if (!bankId.isEmpty()) {
            startPracticeForBank(bankId, mode);
            return;
        }
    } else if (scopeType == QStringLiteral("path")) {
        startPracticeForPath(
            homeSavedProgressCard_->property("savedScopePath").toStringList(), mode);
        return;
    }
    QMessageBox::warning(
        this,
        QStringLiteral("继续学习"),
        QStringLiteral("保存的题库位置已不存在，请重新选择题库。"));
    refreshSavedProgressWidget();
}

void AppWindow::schedulePracticeSessionSave()
{
    if (!practicePage_ || !practicePage_->hasActiveSession()
        || !automaticPracticePersistenceEnabled(practicePage_->session().mode)) {
        if (practiceSaveTimer_ && practiceSaveTimer_->isActive()) {
            practiceSaveTimer_->stop();
        }
        return;
    }
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
    storage::SqliteAnswerStateRepository answerStateRepository(database.connection());
    const domain::PracticeSession session = practicePage_->session();
    domain::PracticeSession answerStateSession = session;
    answerStateSession.mode = answerStateStorageMode(session.mode);
    if (!answerStateRepository.saveSessionAnswers(answerStateSession, error)) {
        return false;
    }
    return repository.save(session, error);
}

} // namespace quizapp::ui
