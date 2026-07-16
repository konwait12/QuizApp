#include "storage/SqliteReviewRepository.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace quizapp::storage {
namespace {

QString uuidText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

QString dateText(const QDateTime &value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime parseDate(const QVariant &value)
{
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}

void clearError(QString *error)
{
    if (error) {
        error->clear();
    }
}

void setQueryError(const QSqlQuery &query, QString *error)
{
    if (error) {
        *error = query.lastError().text();
    }
}

QString compatibilityStateJson(const domain::ReviewCard &card)
{
    QJsonObject state;
    state.insert(QStringLiteral("version"), card.schedulerVersion);
    state.insert(QStringLiteral("hasMemoryState"), card.hasMemoryState);
    if (card.hasMemoryState) {
        state.insert(QStringLiteral("stability"), card.memory.stability);
        state.insert(QStringLiteral("difficulty"), card.memory.difficulty);
    }
    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

QString pathJson(const QStringList &path)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(path)).toJson(QJsonDocument::Compact));
}

domain::ReviewCard readCard(const QSqlQuery &query)
{
    domain::ReviewCard card;
    card.questionId = QUuid(query.value(0).toString());
    card.dueAt = parseDate(query.value(1));
    card.updatedAt = parseDate(query.value(2));
    card.desiredRetention = query.value(3).toDouble();
    card.hasMemoryState = !query.value(4).isNull() && !query.value(5).isNull();
    card.memory.stability = query.value(4).toDouble();
    card.memory.difficulty = query.value(5).toDouble();
    card.lastReviewAt = parseDate(query.value(6));
    card.scheduledDays = query.value(7).toInt();
    card.reviewCount = query.value(8).toInt();
    card.lapseCount = query.value(9).toInt();
    card.schedulerVersion = query.value(10).toString();
    card.createdAt = parseDate(query.value(11));
    if (!card.hasMemoryState) {
        const QJsonDocument legacy = QJsonDocument::fromJson(query.value(12).toString().toUtf8());
        if (legacy.isObject()) {
            QJsonObject state = legacy.object();
            if (state.value(QStringLiteral("memory_state")).isObject()) {
                state = state.value(QStringLiteral("memory_state")).toObject();
            }
            const double stability = state.value(QStringLiteral("stability")).toDouble();
            const double difficulty = state.value(QStringLiteral("difficulty")).toDouble();
            domain::ReviewMemoryState memory{stability, difficulty};
            if (memory.isValid()) {
                card.hasMemoryState = true;
                card.memory = memory;
            }
        }
    }
    return card;
}

QString cardColumns()
{
    return QStringLiteral(
        "rc.question_id, rc.due_at, rc.updated_at, rc.desired_retention, "
        "rc.stability, rc.difficulty, rc.last_review_at, rc.scheduled_days, "
        "rc.review_count, rc.lapse_count, rc.scheduler_version, rc.created_at, "
        "rc.fsrs_state_json");
}

} // namespace

SqliteReviewRepository::SqliteReviewRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool SqliteReviewRepository::upsert(const domain::ReviewCard &card, QString *error)
{
    clearError(error);
    return upsertWithoutTransaction(card, error);
}

bool SqliteReviewRepository::upsertWithoutTransaction(
    const domain::ReviewCard &card,
    QString *error) const
{
    if (card.questionId.isNull() || !card.dueAt.isValid()
        || card.desiredRetention <= 0.0 || card.desiredRetention >= 1.0
        || (card.hasMemoryState && !card.memory.isValid())) {
        if (error) {
            *error = QStringLiteral("Invalid review card");
        }
        return false;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime createdAt = card.createdAt.isValid() ? card.createdAt : now;
    const QDateTime updatedAt = card.updatedAt.isValid() ? card.updatedAt : now;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO review_cards("
        "question_id, fsrs_state_json, due_at, updated_at, desired_retention, "
        "stability, difficulty, last_review_at, scheduled_days, review_count, "
        "lapse_count, scheduler_version, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(question_id) DO UPDATE SET "
        "fsrs_state_json=excluded.fsrs_state_json, due_at=excluded.due_at, "
        "updated_at=excluded.updated_at, desired_retention=excluded.desired_retention, "
        "stability=excluded.stability, difficulty=excluded.difficulty, "
        "last_review_at=excluded.last_review_at, scheduled_days=excluded.scheduled_days, "
        "review_count=excluded.review_count, lapse_count=excluded.lapse_count, "
        "scheduler_version=excluded.scheduler_version"));
    query.addBindValue(uuidText(card.questionId));
    query.addBindValue(compatibilityStateJson(card));
    query.addBindValue(dateText(card.dueAt));
    query.addBindValue(dateText(updatedAt));
    query.addBindValue(card.desiredRetention);
    query.addBindValue(card.hasMemoryState ? QVariant(card.memory.stability) : QVariant());
    query.addBindValue(card.hasMemoryState ? QVariant(card.memory.difficulty) : QVariant());
    query.addBindValue(card.lastReviewAt.isValid() ? QVariant(dateText(card.lastReviewAt))
                                                   : QVariant());
    query.addBindValue(card.scheduledDays);
    query.addBindValue(card.reviewCount);
    query.addBindValue(card.lapseCount);
    query.addBindValue(card.schedulerVersion);
    query.addBindValue(dateText(createdAt));
    if (!query.exec()) {
        setQueryError(query, error);
        return false;
    }
    return true;
}

bool SqliteReviewRepository::saveReview(
    const domain::ReviewCard &card,
    const domain::ReviewLog &log,
    QString *error)
{
    clearError(error);
    if (log.questionId != card.questionId || !log.reviewedAt.isValid()
        || !log.dueAfter.isValid() || log.scheduledDays < 1
        || !log.memoryAfter.isValid()) {
        if (error) {
            *error = QStringLiteral("Invalid review log");
        }
        return false;
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    if (!upsertWithoutTransaction(card, error)) {
        database_.rollback();
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO review_logs("
        "question_id, rating, reviewed_at, due_before, due_after, elapsed_days, "
        "scheduled_days, had_memory_state, stability_before, difficulty_before, "
        "stability_after, difficulty_after, scheduler_version) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(uuidText(log.questionId));
    query.addBindValue(static_cast<int>(log.rating));
    query.addBindValue(dateText(log.reviewedAt));
    query.addBindValue(log.dueBefore.isValid() ? QVariant(dateText(log.dueBefore)) : QVariant());
    query.addBindValue(dateText(log.dueAfter));
    query.addBindValue(log.elapsedDays);
    query.addBindValue(log.scheduledDays);
    query.addBindValue(log.hadMemoryState ? 1 : 0);
    query.addBindValue(log.hadMemoryState ? QVariant(log.memoryBefore.stability) : QVariant());
    query.addBindValue(log.hadMemoryState ? QVariant(log.memoryBefore.difficulty) : QVariant());
    query.addBindValue(log.memoryAfter.stability);
    query.addBindValue(log.memoryAfter.difficulty);
    query.addBindValue(log.schedulerVersion);
    if (!query.exec()) {
        database_.rollback();
        setQueryError(query, error);
        return false;
    }
    if (!database_.commit()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    return true;
}

bool SqliteReviewRepository::remove(const QUuid &questionId, QString *error)
{
    clearError(error);
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM review_logs WHERE question_id=?"));
    query.addBindValue(uuidText(questionId));
    if (!query.exec()) {
        database_.rollback();
        setQueryError(query, error);
        return false;
    }
    query.prepare(QStringLiteral("DELETE FROM review_cards WHERE question_id=?"));
    query.addBindValue(uuidText(questionId));
    if (!query.exec()) {
        database_.rollback();
        setQueryError(query, error);
        return false;
    }
    if (!database_.commit()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    return true;
}

std::optional<domain::ReviewCard> SqliteReviewRepository::find(
    const QUuid &questionId,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT %1 FROM review_cards rc WHERE rc.question_id=?")
                      .arg(cardColumns()));
    query.addBindValue(uuidText(questionId));
    if (!query.exec()) {
        setQueryError(query, error);
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return readCard(query);
}

QVector<domain::ReviewCard> SqliteReviewRepository::listDue(
    const QDateTime &now,
    int limit,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM review_cards rc JOIN questions q ON q.id=rc.question_id "
        "WHERE q.active=1 AND rc.due_at<=? ORDER BY rc.due_at, rc.created_at LIMIT ?")
                      .arg(cardColumns()));
    query.addBindValue(dateText(now));
    query.addBindValue(std::max(1, limit));
    if (!query.exec()) {
        setQueryError(query, error);
        return {};
    }
    QVector<domain::ReviewCard> cards;
    while (query.next()) {
        cards.append(readCard(query));
    }
    return cards;
}

QVector<domain::ReviewLog> SqliteReviewRepository::history(
    const QUuid &questionId,
    int limit,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, question_id, rating, reviewed_at, due_before, due_after, "
        "elapsed_days, scheduled_days, had_memory_state, stability_before, "
        "difficulty_before, stability_after, difficulty_after, scheduler_version "
        "FROM review_logs WHERE question_id=? ORDER BY reviewed_at DESC, id DESC LIMIT ?"));
    query.addBindValue(uuidText(questionId));
    query.addBindValue(std::max(1, limit));
    if (!query.exec()) {
        setQueryError(query, error);
        return {};
    }
    QVector<domain::ReviewLog> logs;
    while (query.next()) {
        domain::ReviewLog log;
        log.id = query.value(0).toLongLong();
        log.questionId = QUuid(query.value(1).toString());
        log.rating = static_cast<domain::ReviewRating>(query.value(2).toInt());
        log.reviewedAt = parseDate(query.value(3));
        log.dueBefore = parseDate(query.value(4));
        log.dueAfter = parseDate(query.value(5));
        log.elapsedDays = query.value(6).toInt();
        log.scheduledDays = query.value(7).toInt();
        log.hadMemoryState = query.value(8).toBool();
        log.memoryBefore.stability = query.value(9).toDouble();
        log.memoryBefore.difficulty = query.value(10).toDouble();
        log.memoryAfter.stability = query.value(11).toDouble();
        log.memoryAfter.difficulty = query.value(12).toDouble();
        log.schedulerVersion = query.value(13).toString();
        logs.append(log);
    }
    return logs;
}

QSet<QUuid> SqliteReviewRepository::listQuestionIdsByPathPrefix(
    const QStringList &pathPrefix,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    if (pathPrefix.isEmpty()) {
        query.prepare(QStringLiteral(
            "SELECT rc.question_id FROM review_cards rc "
            "JOIN questions q ON q.id=rc.question_id WHERE q.active=1"));
    } else {
        const QString serializedPath = pathJson(pathPrefix);
        const QString descendantPrefix = serializedPath.left(serializedPath.size() - 1)
            + QStringLiteral(",");
        query.prepare(QStringLiteral(
            "SELECT rc.question_id FROM review_cards rc "
            "JOIN questions q ON q.id=rc.question_id "
            "WHERE q.active=1 AND (q.path_json=? OR substr(q.path_json, 1, ?)=?)"));
        query.addBindValue(serializedPath);
        query.addBindValue(descendantPrefix.size());
        query.addBindValue(descendantPrefix);
    }
    if (!query.exec()) {
        setQueryError(query, error);
        return {};
    }
    QSet<QUuid> result;
    while (query.next()) {
        const QUuid questionId(query.value(0).toString());
        if (!questionId.isNull()) {
            result.insert(questionId);
        }
    }
    return result;
}

domain::ReviewStats SqliteReviewRepository::stats(
    const QDateTime &now,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*), "
        "COALESCE(SUM(CASE WHEN rc.due_at<=? THEN 1 ELSE 0 END), 0), "
        "COALESCE(SUM(CASE WHEN rc.review_count=0 THEN 1 ELSE 0 END), 0), "
        "COALESCE(SUM(CASE WHEN rc.review_count>0 THEN 1 ELSE 0 END), 0) "
        "FROM review_cards rc JOIN questions q ON q.id=rc.question_id WHERE q.active=1"));
    query.addBindValue(dateText(now));
    if (!query.exec() || !query.next()) {
        setQueryError(query, error);
        return {};
    }
    domain::ReviewStats result;
    result.total = query.value(0).toInt();
    result.due = query.value(1).toInt();
    result.fresh = query.value(2).toInt();
    result.learned = query.value(3).toInt();
    return result;
}

} // namespace quizapp::storage
