#include "services/StudyService.h"

#include <QMap>

namespace quizapp::services {

StudyService::StudyService(repositories::IStudyRepository &repository)
    : repository_(repository)
{
}

bool StudyService::record(
    domain::StudyActivity activity,
    const QString &scopeId,
    const QDateTime &startedAt,
    int durationSeconds,
    QString *error)
{
    domain::StudyEvent event;
    event.activity = activity;
    event.scopeId = scopeId;
    event.startedAt = startedAt.toUTC();
    event.durationSeconds = durationSeconds;
    return repository_.append(event, error);
}

QVector<domain::DailyStudyTotal> StudyService::dailyTotals(
    const QDate &from,
    const QDate &until,
    const QTimeZone &zone,
    QString *error) const
{
    if (error) {
        error->clear();
    }
    if (!from.isValid() || !until.isValid() || from > until || !zone.isValid()) {
        if (error) {
            *error = QStringLiteral("Invalid study statistics range");
        }
        return {};
    }
    const QDateTime rangeStart(from.addDays(-1), QTime(0, 0), zone);
    const QDateTime rangeEnd(until.addDays(2), QTime(0, 0), zone);
    const QVector<domain::StudyEvent> events = repository_.listStartedBetween(
        rangeStart.toUTC(), rangeEnd.toUTC(), error);
    if (error && !error->isEmpty()) {
        return {};
    }
    QMap<QDate, int> totals;
    for (QDate date = from; date <= until; date = date.addDays(1)) {
        totals.insert(date, 0);
    }
    for (const domain::StudyEvent &event : events) {
        QDateTime cursor = event.startedAt.toTimeZone(zone);
        const QDateTime eventEnd = event.startedAt.addSecs(event.durationSeconds).toTimeZone(zone);
        while (cursor < eventEnd) {
            const QDate date = cursor.date();
            const QDateTime nextDay(date.addDays(1), QTime(0, 0), zone);
            const QDateTime segmentEnd = std::min(eventEnd, nextDay);
            if (totals.contains(date)) {
                totals[date] += static_cast<int>(cursor.secsTo(segmentEnd));
            }
            cursor = segmentEnd;
        }
    }
    QVector<domain::DailyStudyTotal> result;
    result.reserve(totals.size());
    for (auto iterator = totals.cbegin(); iterator != totals.cend(); ++iterator) {
        result.append({iterator.key(), iterator.value()});
    }
    return result;
}

int StudyService::totalForDate(
    const QDate &date,
    const QTimeZone &zone,
    QString *error) const
{
    const auto totals = dailyTotals(date, date, zone, error);
    return totals.isEmpty() ? 0 : totals.constFirst().durationSeconds;
}

} // namespace quizapp::services

