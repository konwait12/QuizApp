#pragma once

#include "domain/StudyEvent.h"
#include "repositories/IStudyRepository.h"

#include <QTimeZone>

namespace quizapp::services {

class StudyService final {
public:
    explicit StudyService(repositories::IStudyRepository &repository);

    bool record(
        domain::StudyActivity activity,
        const QString &scopeId,
        const QDateTime &startedAt,
        int durationSeconds,
        QString *error = nullptr);
    QVector<domain::DailyStudyTotal> dailyTotals(
        const QDate &from,
        const QDate &until,
        const QTimeZone &zone = QTimeZone::systemTimeZone(),
        QString *error = nullptr) const;
    int totalForDate(
        const QDate &date,
        const QTimeZone &zone = QTimeZone::systemTimeZone(),
        QString *error = nullptr) const;

private:
    repositories::IStudyRepository &repository_;
};

} // namespace quizapp::services

