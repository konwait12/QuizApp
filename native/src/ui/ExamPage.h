#pragma once

#include "domain/ExamSession.h"
#include "domain/PracticeSession.h"
#include "services/ExamService.h"

#include <QHash>
#include <QVector>
#include <QWidget>

class QComboBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class QResizeEvent;

namespace quizapp::ui {

class ExamPage final : public QWidget {
    Q_OBJECT

public:
    explicit ExamPage(QString databasePath, QWidget *parent = nullptr);

    void setQuestions(const QVector<domain::Question> &questions);
    void showSetup();
    void handleBack();
    bool hasActiveExam() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void backRequested();
    void handwritingRequested(const domain::NotebookLaunchContext &context);
    void examActivityChanged(bool active);

private:
    QWidget *createSetupPage();
    QWidget *createExamPage();
    QWidget *createResultPage();
    void rebuildScopeChoices();
    void refreshSetup();
    void startConfiguredExam();
    void resumeActiveExam();
    void renderExam();
    void renderResult(const domain::ExamSession &session, bool fromHistory);
    void saveActiveExam();
    void submitExam(bool timedOut);
    void requestExitExam();
    void showOverview();
    void selectOption(QChar option);
    std::optional<domain::ExamSession> loadActive(QString *error = nullptr) const;
    QVector<domain::ExamSession> loadHistory(QString *error = nullptr) const;
    QHash<QUuid, domain::Question> currentQuestionMap() const;
    QString formattedTime(int seconds) const;
    void setStatus(const QString &message, bool error = false);
    void applyResponsiveHeader();

    QString databasePath_;
    services::ExamService service_;
    QVector<domain::Question> questions_;
    QHash<QUuid, domain::Question> questionsById_;
    std::optional<domain::ExamSession> activeSession_;
    QTimer *timer_ = nullptr;
    int ticksSinceSave_ = 0;

    QStackedWidget *pages_ = nullptr;
    QWidget *setupPage_ = nullptr;
    QWidget *examPage_ = nullptr;
    QWidget *resultPage_ = nullptr;
    QComboBox *scopeChoice_ = nullptr;
    QComboBox *countChoice_ = nullptr;
    QComboBox *durationChoice_ = nullptr;
    QPushButton *resumeButton_ = nullptr;
    QListWidget *historyList_ = nullptr;
    QLabel *setupStatus_ = nullptr;
    QLabel *examTitle_ = nullptr;
    QGridLayout *examHeaderLayout_ = nullptr;
    QPushButton *examBackButton_ = nullptr;
    QLabel *timerLabel_ = nullptr;
    QLabel *questionMeta_ = nullptr;
    QLabel *questionPrompt_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QGridLayout *optionsLayout_ = nullptr;
    QVector<QPushButton *> optionButtons_;
    QPushButton *previousButton_ = nullptr;
    QPushButton *nextButton_ = nullptr;
    QPushButton *pauseButton_ = nullptr;
    QPushButton *submitButton_ = nullptr;
    QLabel *pauseCover_ = nullptr;
    QLabel *resultScore_ = nullptr;
    QLabel *resultSummary_ = nullptr;
    QListWidget *resultList_ = nullptr;
};

} // namespace quizapp::ui
