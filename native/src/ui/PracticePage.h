#pragma once

#include "domain/PracticeSession.h"
#include "domain/AiRecord.h"
#include "domain/Question.h"
#include "domain/QuestionBank.h"
#include "services/PracticeService.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QWidget>

#include <optional>

class QLabel;
class QGridLayout;
class QPushButton;
class QFrame;
class QResizeEvent;
class QToolButton;
class QVBoxLayout;

namespace quizapp::ui {

class QuestionAiPanel;

class PracticePage final : public QWidget {
    Q_OBJECT

public:
    explicit PracticePage(QString dataRoot = QString(), QWidget *parent = nullptr);

    void start(
        const domain::InstalledBankSummary &bank,
        const QVector<domain::Question> &questions,
        domain::PracticeMode mode = domain::PracticeMode::Sequential,
        std::optional<domain::PracticeSession> restoredSession = std::nullopt);
    bool hasActiveSession() const;
    domain::PracticeSession session() const;
    std::optional<QUuid> currentQuestionId() const;
    void restoreNotebookContext(const domain::NotebookLaunchContext &context);
    void setDataRoot(const QString &dataRoot);
    void setWrongBookQuestionIds(const QSet<QUuid> &questionIds);
    void setWrongBookMembership(const QUuid &questionId, bool included);
    void setReviewQuestionIds(const QSet<QUuid> &questionIds);
    void setReviewMembership(const QUuid &questionId, bool included);
    void applyPersistedAnswers(const QHash<QUuid, QString> &answers);
    void applyMergedProgress(const domain::PracticeSession &progress);
    void resetSession(bool reshuffleRandom);
    void showSaveStatus(const QString &message, bool error = false);
    void setAiRecord(const domain::AiRecord &record);
    void setAiLoading(const QUuid &questionId);
    void setAiError(const QUuid &questionId, const QString &message);
    void setAiCancelled(const QUuid &questionId);
    bool selectQuestion(qsizetype questionIndex, bool renderPage = true);
    domain::NotebookLaunchContext notebookContext() const;

signals:
    void backRequested();
    void handwritingRequested(const domain::NotebookLaunchContext &context);
    void sessionChanged();
    void manualSaveRequested();
    void resetRequested();
    void wrongBookToggleRequested(const QUuid &questionId, bool included);
    void reviewToggleRequested(const QUuid &questionId, bool included);
    void currentQuestionChanged(const domain::Question &question);
    void aiAnalysisRequested(const domain::Question &question, bool replaceCached);
    void aiAnalysisCancelRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void render();
    const domain::Question *currentQuestion() const;
    QString selectedAnswerForCurrentQuestion() const;
    QString optionKey(qsizetype optionIndex) const;
    QString questionTypeText(domain::QuestionType type) const;
    QString practiceModeText(domain::PracticeMode mode) const;
    void clearOptions();
    void clearImageLayout(QVBoxLayout *layout);
    void addImages(QVBoxLayout *layout, const domain::Question &question, const QStringList &blobIds);
    QPushButton *createOptionButton(
        const QString &key,
        const QString &text,
        const QString &selectedAnswer);
    void updateNavigationButtons();
    void updateResponsiveHeader();
    void showQuestionOverview();
    void moveToQuestion(qsizetype questionIndex);
    bool currentAnswerIsWrong() const;
    void updateOptionStates(
        const domain::Question &question,
        const QString &selectedAnswer,
        bool answerOnlyMode,
        bool revealed);

    services::PracticeService practiceService_;
    QString dataRoot_;
    domain::InstalledBankSummary bank_;
    QVector<domain::Question> questions_;
    QHash<QUuid, qsizetype> questionIndexes_;
    domain::PracticeSession session_;
    QSet<QUuid> wrongBookQuestionIds_;
    QSet<QUuid> reviewQuestionIds_;
    QUuid renderedQuestionId_;
    QStringList renderedOptionTexts_;
    QVector<QPushButton *> optionButtons_;
    bool renderedAnswerOnlyMode_ = false;

    QLabel *bankTitle_ = nullptr;
    QGridLayout *topLayout_ = nullptr;
    QPushButton *backButton_ = nullptr;
    QWidget *headerActions_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QLabel *saveStatusLabel_ = nullptr;
    QToolButton *resetButton_ = nullptr;
    QToolButton *saveButton_ = nullptr;
    QToolButton *overviewButton_ = nullptr;
    QLabel *typeLabel_ = nullptr;
    QLabel *promptLabel_ = nullptr;
    QVBoxLayout *questionImagesLayout_ = nullptr;
    QLabel *answerLabel_ = nullptr;
    QLabel *explanationLabel_ = nullptr;
    QVBoxLayout *explanationImagesLayout_ = nullptr;
    QFrame *answerSurface_ = nullptr;
    QuestionAiPanel *aiPanel_ = nullptr;
    QPushButton *wrongBookButton_ = nullptr;
    QPushButton *reviewButton_ = nullptr;
    QVBoxLayout *optionsLayout_ = nullptr;
    QPushButton *previousButton_ = nullptr;
    QPushButton *nextButton_ = nullptr;
    QPushButton *revealButton_ = nullptr;
    QPushButton *handwritingButton_ = nullptr;
    QPushButton *aiButton_ = nullptr;
};

} // namespace quizapp::ui
