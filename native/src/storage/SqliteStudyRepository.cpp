#include "storage/SqliteStudyRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include <utility>

namespace quizapp::storage {
namespace {

QString activityText(domain::StudyActivity activity)
{
    switch (activity) {
    case domain::StudyActivity::Sequential:
        return QStringLiteral("sequential");
    case domain::StudyActivity::Random:
        return QStringLiteral("random");
    case domain::StudyActivity::Memorize:
        return QStringLiteral("memorize");
    case domain::StudyActivity::AnswerTable:
        return QStringLiteral("answer_table");
    case domain::StudyActivity::WrongBook:
        return QStringLiteral("wrong_book");
    case domain::StudyActivity::Review:
        return QStringLiteral("review");
    case domain::StudyActivity::Handwriting:
        return QStringLiteral("handwriting");
    }
    return {};
}

std::optional<domain::StudyActivity> parseActivity(const QString &value)
{
    if (value == QStringLiteral("sequential")) return domain::StudyActivity::Sequential;
    if (value == QStringLiteral("random")) return domain::StudyActivity::Random;
    if (value == QStringLiteral("memorize")) return domain::StudyActivity::Memorize;
    if (value == QStringLiteral("answer_table")) return domain::StudyActivity::AnswerTable;
    if (value == QStringLiteral("wrong_book")) return domain::StudyActivity::WrongBook;
    if (value == QStringLiteral("review")) return domain::StudyActivity::Review;
    if (value == QStringLiteral("handwriting")) return domain::StudyActivity::Handwriting;
    return std::nullopt;
}

QString dateText(const QDateTime &value)
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}

} // namespace

SqliteStudyRepository::SqliteStudyRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool SqliteStudyRepository::append(const domain::StudyEvent &event, QString *error)
{
    if (error) {
        error->clear();
    }
    const QString activity = activityText(event.activity);
    if (activity.isEmpty() || !event.startedAt.isValid() || event.durationSeconds <= 0) {
        if (error) {
            *error = QStringLiteral("Invalid study event");
        }
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO study_events(activity, scope_id, started_at, duration_seconds) "
        "VALUES(?, ?, ?, ?)"));
    query.addBindValue(activity);
    query.addBindValue(event.scopeId.isNull() ? QString() : event.scopeId);
    query.addBindValue(dateText(event.startedAt));
    query.addBindValue(event.durationSeconds);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    return true;
}

QVector<domain::StudyEvent> SqliteStudyRepository::listStartedBetween(
    const QDateTime &from,
    const QDateTime &until,
    QString *error) const
{
    if (error) {
        error->clear();
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, activity, scope_id, started_at, duration_seconds "
        "FROM study_events WHERE started_at>=? AND started_at<? ORDER BY started_at, id"));
    query.addBindValue(dateText(from));
    query.addBindValue(dateText(until));
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return {};
    }
    QVector<domain::StudyEvent> events;
    while (query.next()) {
        const auto activity = parseActivity(query.value(1).toString());
        if (!activity.has_value()) {
            continue;
        }
        domain::StudyEvent event;
        event.id = query.value(0).toLongLong();
        event.activity = *activity;
        event.scopeId = query.value(2).toString();
        event.startedAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        event.durationSeconds = query.value(4).toInt();
        events.append(event);
    }
    return events;
}

} // namespace quizapp::storage

