#pragma once

#include "domain/PracticeSession.h"
#include "domain/Notebook.h"
#include "domain/Question.h"
#include "domain/ReviewCard.h"
#include "domain/StudyEvent.h"
#include "services/BankDirectorySyncService.h"
#include "services/SharedStorageService.h"

#include <QMainWindow>
#include <QHash>
#include <QElapsedTimer>
#include <QString>
#include <QVector>
#include <optional>

class QStackedWidget;
class QButtonGroup;
class QCheckBox;
class QColor;
class QComboBox;
class QFrame;
class QGridLayout;
class QHBoxLayout;
class QVBoxLayout;
template <typename T> class QFutureWatcher;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QKeyEvent;
class QResizeEvent;
class QSlider;
class QTimer;
class QToolButton;
class QTreeWidget;

namespace quizapp::services {
class AiQuestionAnalysisService;
}

namespace quizapp::ui {

class HandwritingPage;
class AnswerTablePage;
class PracticePage;
class ReviewPage;
class StudyHubPage;
class ExamPage;
class ThemePreview;
class AiSettingsPanel;
class NotebookLibraryPage;

class AppWindow final : public QMainWindow {
    Q_OBJECT

public:
    enum class Section {
        Home,
        Library,
        Study,
        Settings,
    };

    explicit AppWindow(QWidget *parent = nullptr);
    AppWindow(QString databasePath, QString dataRoot, QWidget *parent = nullptr);
    ~AppWindow() override;

    Section currentSection() const;
    void navigateTo(Section section);
    bool isRailNavigationVisible() const;
    bool isBottomNavigationVisible() const;
    bool reduceMotion() const;
    bool isHandwritingVisible() const;
    bool isAnswerTableVisible() const;
    QString currentHandwritingNotePath() const;
    domain::NotebookLaunchContext lastRestoredPracticeContext() const;

public slots:
    void openHandwriting(const domain::NotebookLaunchContext &context);

signals:
    void handwritingReturned(const domain::NotebookLaunchContext &context);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QWidget *createHomePage();
    QWidget *createLibraryPage();
    QWidget *createStudyPage();
    QWidget *createSettingsPage();
    QToolButton *createNavigationButton(
        Section section,
        const QString &text,
        const QString &iconName);
    void buildNavigation();
    void applyResponsiveLayout();
    void applyTheme(const QString &theme);
    void refreshIconsForTheme(const QString &theme);
    void refreshIcons(
        const QColor &normal,
        const QColor &emphasis,
        const QColor &onPrimary);
    void refreshLibraryStats();
    void startXiaoyiDirectoryInstall();
    void startSharedBankSync(bool force);
    void finishXiaoyiDirectoryInstall();
    void refreshSharedStorageState();
    void refreshSharedFileTree();
    void requestSharedStorageAccess();
    void openSharedStorageDirectory();
    void openBankReleaseDialog(bool automatic = false);
    void scheduleAutomaticBankReleaseCheck();
    void openAppUpdateDialog(bool automatic = false);
    void scheduleAutomaticAppUpdateCheck();
    void openAnnouncementBoard();
    void checkAnnouncements(bool automatic);
    void scheduleAutomaticAnnouncementCheck();
    void updateAnnouncementUnreadState();
    void createSharedBankFolder();
    void importSharedBankJson();
    void renameSelectedSharedEntry();
    void moveSelectedSharedEntry();
    void recycleSelectedSharedEntry();
    void restoreSelectedSharedEntry();
    void permanentlyDeleteSelectedSharedEntry();
    void updateSharedFileActions();
    void saveSettings();
    void refreshLibraryIconPickers();
    int detectedLibraryHierarchyDepth() const;
    void choosePrimaryColor();
    void resetPrimaryColorToPalette();
    void updatePrimaryColorControl();
    bool automaticPracticePersistenceEnabled(domain::PracticeMode mode) const;
    bool mergedPracticeProgressEnabled() const;
    domain::PracticeMode answerStateStorageMode(domain::PracticeMode mode) const;
    void savePracticeManually();
    void resetActivePractice();
    QString resolvedTheme(const QString &theme) const;
    void handleHandwritingReturn(const domain::NotebookLaunchContext &context);
    void navigateLibraryPath(const QStringList &path);
    void saveLibraryNodeOrder(const QVector<QStringList> &orderedPaths);
    void removeLibraryNode(const QStringList &path);
    void refreshInstalledBankList();
    void startPracticeForBank(const QString &bankId, domain::PracticeMode mode);
    void startPracticeForPath(const QStringList &path, domain::PracticeMode mode);
    void setWrongBookMembership(const QUuid &questionId, bool included);
    void setReviewMembership(const QUuid &questionId, bool included);
    void refreshReviewData();
    void showStudyHub();
    void startDueReview();
    void showNextDueReview();
    void rateReviewCard(const QUuid &questionId, domain::ReviewRating rating);
    void removeReviewCard(const QUuid &questionId);
    void initializeStudyTracking();
    void setStudyActivity(domain::StudyActivity activity, const QString &scopeId);
    void clearStudyActivity();
    void pauseStudyActivity();
    void resumeStudyActivity();
    void flushStudyActivity(bool clearActivity);
    void reloadTodayStudyTotal();
    void refreshTodayStudyTime();
    void refreshStudyHistory(int days = -1);
    void openExamPage();
    void showNotebookLibrary();
    void openFreeNotebook(const domain::NotebookRecord &record);
    void handleFreeNotebookReturn(const QUuid &notebookId);
    void refreshExamQuestions();
    void refreshSavedProgressWidget();
    void updateSavedProgressWidgetSize();
    void resumeSavedProgress();
    void schedulePracticeSessionSave();
    bool saveActivePracticeSession(QString *error = nullptr);

    Section currentSection_ = Section::Home;
    QStackedWidget *rootStack_ = nullptr;
    QWidget *shellRoot_ = nullptr;
    HandwritingPage *handwritingPage_ = nullptr;
    AnswerTablePage *answerTablePage_ = nullptr;
    PracticePage *practicePage_ = nullptr;
    ReviewPage *reviewPage_ = nullptr;
    StudyHubPage *studyHubPage_ = nullptr;
    ExamPage *examPage_ = nullptr;
    NotebookLibraryPage *notebookLibraryPage_ = nullptr;
    QStackedWidget *studyStack_ = nullptr;
    QFrame *rail_ = nullptr;
    QFrame *bottomBar_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QLabel *brandMark_ = nullptr;
    QLabel *pageTitle_ = nullptr;
    QWidget *announcementButtonContainer_ = nullptr;
    QToolButton *announcementButton_ = nullptr;
    QLabel *announcementUnreadDot_ = nullptr;
    QButtonGroup *railButtons_ = nullptr;
    QButtonGroup *bottomButtons_ = nullptr;
    QGridLayout *homeResponsiveLayout_ = nullptr;
    QWidget *homeSummarySection_ = nullptr;
    QWidget *homeActionsSection_ = nullptr;
    QGridLayout *homeActionsLayout_ = nullptr;
    QVector<QPushButton *> homeActionButtons_;
    QFrame *homeSavedProgressCard_ = nullptr;
    QLabel *homeSavedProgressTitle_ = nullptr;
    QLabel *homeSavedProgressSummary_ = nullptr;
    QWidget *homeSavedProgressHint_ = nullptr;
    QPushButton *homeSavedProgressButton_ = nullptr;
    std::optional<domain::PracticeSession> homeSavedProgressSession_;
    QGridLayout *summaryGrid_ = nullptr;
    QVector<QWidget *> summaryCards_;
    QVector<QLabel *> summaryValues_;
    QComboBox *themeChoice_ = nullptr;
    QComboBox *paletteChoice_ = nullptr;
    ThemePreview *themePreview_ = nullptr;
    AiSettingsPanel *aiSettingsPanel_ = nullptr;
    services::AiQuestionAnalysisService *aiQuestionAnalysisService_ = nullptr;
    QPushButton *primaryColorButton_ = nullptr;
    QLabel *primaryColorValue_ = nullptr;
    QString primaryColorHex_;
    QWidget *libraryIconPickersContainer_ = nullptr;
    QGridLayout *libraryIconPickersLayout_ = nullptr;
    QVector<QButtonGroup *> levelIconChoiceGroups_;
    QCheckBox *reduceMotionChoice_ = nullptr;
    QCheckBox *autoSaveOnExitChoice_ = nullptr;
    QCheckBox *mergePracticeProgressChoice_ = nullptr;
    QCheckBox *randomReshuffleChoice_ = nullptr;
    QCheckBox *rememberReviewPositionChoice_ = nullptr;
    QCheckBox *rememberAnswerLookupPositionChoice_ = nullptr;
    QCheckBox *showSavedProgressChoice_ = nullptr;
    QSlider *savedProgressPhoneWidthChoice_ = nullptr;
    QSlider *savedProgressTabletWidthChoice_ = nullptr;
    QLabel *savedProgressPhoneWidthValue_ = nullptr;
    QLabel *savedProgressTabletWidthValue_ = nullptr;
    QSlider *cornerRadiusChoice_ = nullptr;
    QLabel *cornerRadiusValue_ = nullptr;
    QLabel *settingsStatus_ = nullptr;
    QLabel *libraryEmptyTitle_ = nullptr;
    QLabel *librarySummaryText_ = nullptr;
    QLabel *libraryBanksTitle_ = nullptr;
    QVBoxLayout *libraryBanksLayout_ = nullptr;
    QGridLayout *libraryResponsiveLayout_ = nullptr;
    QWidget *librarySourceColumn_ = nullptr;
    QWidget *libraryBrowserColumn_ = nullptr;
    QWidget *libraryBanksSurface_ = nullptr;
    QScrollArea *libraryPageScroll_ = nullptr;
    QToolButton *libraryEditButton_ = nullptr;
    QPushButton *libraryPathBackButton_ = nullptr;
    QLabel *libraryPathTitle_ = nullptr;
    QLabel *libraryPathSummary_ = nullptr;
    QGridLayout *libraryScopeModesLayout_ = nullptr;
    QVector<QPushButton *> libraryScopeModeButtons_;
    QLabel *libraryImportStatus_ = nullptr;
    QPushButton *libraryImportButton_ = nullptr;
    QPushButton *libraryOpenStorageButton_ = nullptr;
    QPushButton *libraryStorageAccessButton_ = nullptr;
    QPushButton *libraryCancelButton_ = nullptr;
    QPushButton *checkBankUpdatesButton_ = nullptr;
    QCheckBox *autoBankUpdateCheckChoice_ = nullptr;
    QLabel *bankUpdateStatus_ = nullptr;
    QCheckBox *autoAppUpdateCheckChoice_ = nullptr;
    QPushButton *checkAppUpdatesButton_ = nullptr;
    QLabel *appUpdateStatus_ = nullptr;
    QCheckBox *autoAnnouncementCheckChoice_ = nullptr;
    QPushButton *checkAnnouncementsButton_ = nullptr;
    QLabel *announcementCheckStatus_ = nullptr;
    QProgressBar *libraryImportProgress_ = nullptr;
    QTreeWidget *libraryFilesTree_ = nullptr;
    QGridLayout *libraryFileActionsLayout_ = nullptr;
    QPushButton *libraryNewFolderButton_ = nullptr;
    QPushButton *libraryImportJsonButton_ = nullptr;
    QPushButton *libraryRenameButton_ = nullptr;
    QPushButton *libraryMoveButton_ = nullptr;
    QPushButton *libraryRecycleButton_ = nullptr;
    QPushButton *libraryRestoreButton_ = nullptr;
    QPushButton *libraryPermanentDeleteButton_ = nullptr;
    QFutureWatcher<services::BankDirectorySyncResult> *libraryImportWatcher_ = nullptr;
    QTimer *practiceSaveTimer_ = nullptr;
    QString databasePath_;
    QString dataRoot_;
    QString sharedStorageRoot_;
    services::SharedStorageLayout sharedStorageLayout_;
    QStringList libraryPath_;
    bool libraryEditMode_ = false;
    QVector<domain::ReviewCard> dueReviewCards_;
    QHash<QUuid, domain::Question> dueReviewQuestions_;
    QUuid reviewSessionId_;
    bool hasStudyActivity_ = false;
    domain::StudyActivity currentStudyActivity_ = domain::StudyActivity::Sequential;
    QString currentStudyScope_;
    QDateTime currentStudyStartedAt_;
    QElapsedTimer studyElapsed_;
    bool studyElapsedRunning_ = false;
    qint64 studyRemainderMilliseconds_ = 0;
    int persistedTodayStudySeconds_ = 0;
    QDate studyTotalDate_;
    QTimer *studyUiTimer_ = nullptr;
    std::optional<domain::StudyActivity> activityBeforeHandwriting_;
    QString scopeBeforeHandwriting_;
    int studyHistoryDays_ = 7;
    domain::NotebookLaunchContext lastRestoredPracticeContext_;
};

} // namespace quizapp::ui
