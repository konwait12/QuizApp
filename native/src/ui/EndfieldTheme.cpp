#include "ui/EndfieldTheme.h"

namespace quizapp::ui {

QString EndfieldTheme::styleSheet()
{
    return QStringLiteral(R"QSS(
        #appWindow { background: #101113; color: #f4f4f2; }
        QWidget { color: #f4f4f2; font-size: 14px; }

        #topBar, #navigationRail, #bottomNavigation {
            background: #18171a; border: 0 solid #3a3a40;
        }
        #topBar { border-bottom-width: 1px; }
        #navigationRail { border-right-width: 1px; }
        #bottomNavigation { border-top-width: 1px; }
        #brandMark, #railBrand {
            background: #fdfc00; color: #101113; border: 1px solid #fdfc00;
            border-radius: 2px; font-size: 18px; font-weight: 900;
        }
        #topBarTitle {
            color: #ffffff; font-size: 17px; font-weight: 800;
            min-width: 140px; padding-left: 2px;
        }
        #pageHeading {
            color: #ffffff; font-size: 24px; font-weight: 900;
            border-left: 3px solid #fdfc00; padding-left: 10px;
        }
        #sectionHeading, #emptyStateTitle {
            color: #ffffff; font-size: 17px; font-weight: 800;
        }
        #settingsSectionHeading { color: #ffffff; font-size: 17px; font-weight: 850; }
        #settingsFieldLabel { color: #a7a8ad; font-weight: 700; }
        #pageSupportingText, #secondaryText, #settingsStatus { color: #a7a8ad; }

        #summaryCard, #homeSummarySurface, #homeActionsSurface,
        #settingsSurface, #librarySurface,
        #libraryBrowserHeader, #libraryPathNodeCard,
        #practiceOptionsSurface, #practiceAnswerSurface,
        #studySummaryCard, #reviewOptionsSurface, #reviewAnswerSurface {
            background: #18191d; border: 1px solid #393a40; border-radius: 3px;
        }
        #summaryCard, #studySummaryCard { border-top: 2px solid #5b5c62; }
        #summaryValue, #studySummaryValue {
            color: #ffffff; font-size: 22px; font-weight: 900;
        }
        #settingsSurface, #librarySurface { border-left: 3px solid #fdfc00; }

        #studyDueList {
            background: #18191d; border: 1px solid #393a40; border-radius: 3px;
            padding: 4px; outline: 0;
        }
        #studyDueList::item {
            padding: 9px 10px; border-bottom: 1px solid #393a40;
        }
        #studyDueList::item:selected { background: #28292e; color: #ffffff; }
        #studyTrendChart {
            qproperty-lineColor: #fdfc00;
            qproperty-gridColor: #393a40;
            qproperty-textColor: #a7a8ad;
        }
        #studyStartReviewButton, #reviewRevealButton, #saveSettingsButton {
            background: #fdfc00; border-color: #fdfc00; color: #101113;
            font-weight: 850;
        }
        #studyStartReviewButton:hover, #reviewRevealButton:hover,
        #saveSettingsButton:hover { background: #fefd4d; border-color: #fefd4d; }
        #studyStartReviewButton:pressed, #reviewRevealButton:pressed,
        #saveSettingsButton:pressed { background: #e4e300; border-color: #e4e300; }
        #studyStartReviewButton:disabled {
            background: #25262b; border-color: #393a40; color: #707177;
        }

        #libraryPathTitle, #questionOverviewTitle {
            color: #ffffff; font-size: 17px; font-weight: 850;
        }
        #libraryPathSummary, #libraryLeafHint,
        #questionOverviewSection { color: #a7a8ad; }
        #questionOverviewSummaryBar { background: transparent; border: 0; }
        #questionOverviewStat {
            background: #18191d; border: 1px solid #3d3e44;
            border-radius: 2px; padding: 6px 8px; color: #a7a8ad; font-weight: 700;
        }
        #questionOverviewStat[answerStatus="correct"] {
            background: #2a2a21; border-color: #fdfc00; color: #fdfc00;
        }
        #questionOverviewStat[answerStatus="wrong"] {
            background: #351d20; border-color: #f96f68; color: #ff8b85;
        }
        #libraryPathNodeButton {
            text-align: left; min-height: 62px; padding: 0;
            background: transparent; border: 0;
        }
        #libraryPathNodeButton:hover { color: #fdfc00; }
        #libraryPathNodeTitle { color: #ffffff; font-weight: 800; }
        #libraryPathNodeCount { color: #8f9096; }
        #libraryBanksSurface { background: transparent; border: 0; }
        #practiceImage {
            background: #232429; border: 1px solid #3f4046;
            border-radius: 2px; padding: 6px;
        }

        #practicePage, #studyHubPage, #reviewPage, #answerTablePage,
        #answerTableHeader, #questionOverviewDialog, #handwritingPage {
            background: #101113;
        }
        #answerTableTitle { color: #ffffff; font-size: 16px; font-weight: 800; }
        #answerTableSummary { color: #a7a8ad; }
        #answerTableView {
            background: #18191d; alternate-background-color: #1d1e22;
            border: 1px solid #393a40; border-radius: 2px;
            selection-background-color: #fdfc00; selection-color: #101113;
        }
        #answerTableView::item { padding: 8px; border-bottom: 1px solid #303137; }
        #answerTableView::item:selected { color: #101113; font-weight: 800; }
        QHeaderView::section {
            background: #25262b; color: #c8c9cd; border: 0;
            border-bottom: 1px solid #4a4b51; padding: 8px; font-weight: 750;
        }

        #practiceTopBar, #practiceActions, #reviewTopBar, #reviewActions,
        #handwritingToolbar, #handwritingStatus, #handwritingPagePanel,
        #handwritingMobilePageBar {
            background: #18171a; border: 0 solid #3a3a40;
        }
        #practiceTopBar, #reviewTopBar, #handwritingToolbar { border-bottom-width: 1px; }
        #practiceActions, #reviewActions, #handwritingStatus { border-top-width: 1px; }
        #handwritingPagePanel { border-right-width: 1px; }
        #handwritingMobilePageBar { border-bottom-width: 1px; }
        #practiceBankTitle, #reviewTitle, #handwritingTitle {
            color: #ffffff; font-size: 16px; font-weight: 850;
        }
        #practiceProgressLabel, #practiceQuestionType, #reviewProgress, #reviewPath,
        #handwritingStatus { color: #a7a8ad; font-weight: 650; }
        #practicePrompt, #reviewPrompt {
            color: #ffffff; font-size: 18px; font-weight: 700;
        }
        #practiceAnswerLabel, #reviewAnswer { color: #fdfc00; font-weight: 850; }
        #practiceExplanationLabel { color: #d6d6d8; }
        #practiceOptionButton, #reviewOption {
            text-align: left; background: #18191d; border: 1px solid #3d3e44;
            border-radius: 3px; padding: 11px 12px;
        }
        #practiceOptionButton:hover, #reviewOption:hover { border-color: #8c8d92; }
        #practiceOptionButton:checked {
            background: #2a2a21; border-color: #fdfc00;
            color: #fdfc00; font-weight: 800;
        }
        #practiceOptionButton[answerState="wrong"] {
            background: #351d20; border-color: #f96f68;
            color: #ff8b85; font-weight: 800;
        }
        #practiceOptionButton[answerState="correct"] {
            background: #2a2a21; border: 2px solid #fdfc00;
            color: #fdfc00; font-weight: 850;
        }
        #practiceOptionButton[answerState="correctReveal"] {
            border: 2px solid #fdfc00; color: #fdfc00; font-weight: 850;
        }
        #practiceReviewButton {
            background: #2a2a21; border-color: #fdfc00; color: #fdfc00;
        }
        #reviewActions QPushButton { padding: 6px 3px; }
        #reviewRating1 {
            background: #351d20; border-color: #f96f68; color: #ff8b85;
        }
        #reviewRating3, #reviewRating4 {
            background: #2a2a21; border-color: #fdfc00; color: #fdfc00;
        }
        #reviewRating1:disabled, #reviewRating2:disabled,
        #reviewRating3:disabled, #reviewRating4:disabled {
            background: #25262b; border-color: #393a40; color: #707177;
        }

        #questionOverviewNumberButton {
            min-width: 44px; max-width: 44px; min-height: 44px; max-height: 44px;
            padding: 0; background: #18191d; border-color: #3d3e44;
        }
        #questionOverviewNumberButton[answerStatus="answered"] {
            background: #28292e; border-color: #8f9096;
        }
        #questionOverviewNumberButton[answerStatus="correct"] {
            background: #2a2a21; border-color: #fdfc00; color: #fdfc00;
        }
        #questionOverviewNumberButton[answerStatus="wrong"] {
            background: #351d20; border-color: #f96f68; color: #ff8b85;
        }
        #questionOverviewNumberButton[currentQuestion="true"] {
            border: 2px solid #fdfc00; font-weight: 900;
        }
        #libraryWrongBookButton, #practiceWrongBookButton {
            background: #351d20; border-color: #f96f68; color: #ff8b85;
        }
        #libraryWrongBookButton:disabled {
            background: #25262b; border-color: #393a40; color: #707177;
        }

        #handwritingPagePanelTitle { color: #ffffff; font-size: 15px; font-weight: 800; }
        #handwritingPageLabel, #handwritingMobilePageLabel,
        #handwritingZoomLabel { color: #a7a8ad; font-weight: 700; }
        #handwritingPageList QPushButton {
            text-align: left; min-height: 46px; background: #1b1c20;
        }
        #handwritingPageList QPushButton:checked {
            background: #2a2a21; border: 2px solid #fdfc00;
            color: #fdfc00; font-weight: 850;
        }
        #handwritingTopRow, #handwritingToolScroller, #handwritingToolStrip,
        #handwritingPageScroller, #handwritingPageList, #handwritingWorkspace {
            background: transparent; border: 0;
        }
        #questionDocumentViewport { background: #24252a; border: 0; }

        QToolButton {
            border: 0; border-radius: 3px; padding: 5px; color: #a7a8ad;
        }
        QToolButton:checked {
            background: #fdfc00; color: #101113; font-weight: 800;
        }
        #navigationRail QToolButton:checked {
            background: #222328; color: #fdfc00;
            border-left: 3px solid #fdfc00; border-radius: 1px;
        }
        #bottomNavigation QToolButton:checked {
            background: #222328; color: #fdfc00;
            border-top: 3px solid #fdfc00; border-radius: 1px;
        }
        QToolButton:hover { background: #2a2b30; color: #ffffff; }
        QToolButton:pressed { background: #34353a; }
        QPushButton {
            background: #1b1c20; border: 1px solid #414248;
            border-radius: 3px; padding: 8px 14px; font-weight: 700;
        }
        QPushButton:hover { border-color: #fdfc00; }
        QPushButton:pressed { background: #28292e; }
        QPushButton:disabled { color: #707177; border-color: #34353a; }
        QComboBox, QLineEdit {
            background: #1b1c20; border: 1px solid #414248;
            border-radius: 3px; padding: 7px 10px;
        }
        QComboBox:hover, QLineEdit:focus { border-color: #fdfc00; }
        QComboBox QAbstractItemView {
            background: #1b1c20; selection-background-color: #fdfc00;
            selection-color: #101113; border: 1px solid #4a4b51;
        }
        QCheckBox::indicator {
            width: 18px; height: 18px; border: 1px solid #5b5c62;
            border-radius: 2px; background: #18191d;
        }
        QCheckBox::indicator:checked {
            background: #fdfc00; border-color: #fdfc00;
        }
        QProgressBar {
            background: #28292e; border: 0; border-radius: 1px;
            height: 6px; text-align: center; color: transparent;
        }
        QProgressBar::chunk { background: #fdfc00; border-radius: 1px; }
        QScrollBar:vertical { background: #141519; width: 8px; margin: 0; }
        QScrollBar::handle:vertical {
            background: #515258; min-height: 32px; border-radius: 2px;
        }
        QScrollBar::handle:vertical:hover { background: #fdfc00; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0; background: transparent;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollArea, QScrollArea > QWidget > QWidget { background: #101113; }
    )QSS");
}

QColor EndfieldTheme::iconNormal()
{
    return QColor(QStringLiteral("#a7a8ad"));
}

QColor EndfieldTheme::iconEmphasis()
{
    return QColor(QStringLiteral("#fdfc00"));
}

QColor EndfieldTheme::iconOnPrimary()
{
    return QColor(QStringLiteral("#101113"));
}

} // namespace quizapp::ui
