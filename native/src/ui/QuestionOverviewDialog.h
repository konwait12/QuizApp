#pragma once

#include "domain/PracticeSession.h"
#include "domain/Question.h"

#include <QDialog>
#include <QHash>
#include <QVector>

namespace quizapp::ui {

class QuestionOverviewDialog final : public QDialog {
    Q_OBJECT

public:
    QuestionOverviewDialog(
        const QVector<domain::Question> &questions,
        const domain::PracticeSession &session,
        QWidget *parent = nullptr);

signals:
    void questionSelected(qsizetype questionIndex);

private:
    QString statusFor(
        const domain::Question &question,
        const domain::PracticeSession &session) const;
    QString questionTypeText(domain::QuestionType type) const;
};

} // namespace quizapp::ui
