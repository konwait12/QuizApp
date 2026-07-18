#pragma once

#include "domain/Question.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QUuid>
#include <QVector>

namespace quizapp::domain {

enum class ExamStatus {
    Active = 0,
    Submitted = 1,
};

struct ExamResultItem {
    QUuid questionId;
    Question questionSnapshot;
    QString answer;
    bool correct = false;
    bool unanswered = true;
};

struct ExamSession {
    QUuid id;
    QString scopeId;
    QString title;
    ExamStatus status = ExamStatus::Active;
    int durationSeconds = 0;
    int remainingSeconds = 0;
    qsizetype currentIndex = 0;
    bool paused = false;
    QVector<QUuid> questionOrder;
    QHash<QUuid, QString> answers;
    QVector<ExamResultItem> resultItems;
    int correctCount = 0;
    int wrongCount = 0;
    int unansweredCount = 0;
    int score = 0;
    bool timedOut = false;
    QDateTime createdAt;
    QDateTime submittedAt;
};

} // namespace quizapp::domain
