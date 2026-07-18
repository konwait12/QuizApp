#pragma once

#include "domain/AiRecord.h"
#include "domain/Question.h"

#include <QFrame>

#include <optional>

class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

namespace quizapp::ui {

class QuestionAiPanel final : public QFrame {
    Q_OBJECT

public:
    explicit QuestionAiPanel(QWidget *parent = nullptr);

    void setQuestion(
        const domain::Question &question,
        const std::optional<domain::AiRecord> &record = std::nullopt);
    void setLoading(const QUuid &questionId);
    void setRecord(const domain::AiRecord &record);
    void setError(const QUuid &questionId, const QString &message);
    void setCancelled(const QUuid &questionId);

signals:
    void analyzeRequested(const domain::Question &question, bool replaceCached);
    void cancelRequested();

private:
    void refresh();
    void exportRecord();

    domain::Question question_;
    std::optional<domain::AiRecord> record_;
    QString error_;
    bool loading_ = false;

    QLabel *status_ = nullptr;
    QTextBrowser *content_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPushButton *analyzeButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QPushButton *exportButton_ = nullptr;
};

} // namespace quizapp::ui
