#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>

namespace quizapp::domain {

enum class StudyActivity {
    Sequential,
    Random,
    Memorize,
    AnswerTable,
    WrongBook,
    Review,
    Handwriting,
    Exam,
};

struct StudyEvent {
    qint64 id = 0;
    StudyActivity activity = StudyActivity::Sequential;
    QString scopeId;
    QDateTime startedAt;
    int durationSeconds = 0;
};

struct DailyStudyTotal {
    QDate date;
    int durationSeconds = 0;
};

} // namespace quizapp::domain
