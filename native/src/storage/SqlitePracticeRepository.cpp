#include "storage/SqlitePracticeRepository.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

#include <utility>

namespace quizapp::storage {
namespace {

void clearError(QString *error)
{
    if (error) {
        error->clear();
    }
}

QString uuidText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

QString nonNull(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString orderJson(const QVector<QUuid> &order)
{
    QJsonArray array;
    for (const QUuid &id : order) {
        array.append(uuidText(id));
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<QUuid> parseOrder(const QString &value, bool *valid)
{
    *valid = false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        return {};
    }
    QVector<QUuid> order;
    for (const QJsonValue &entry : document.array()) {
        const QUuid id(entry.toString());
        if (id.isNull()) {
            return {};
        }
        order.append(id);
    }
    *valid = true;
    return order;
}

QJsonObject parseViewport(const QString &value, bool *valid)
{
    *valid = false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    *valid = true;
    return document.object();
}

bool execPrepared(QSqlQuery &query, QString *error)
{
    if (query.exec()) {
        return true;
    }
    if (error) {
        *error = query.lastError().text();
    }
    return false;
}

bool rollback(QSqlDatabase database, const QString &message, QString *error)
{
    database.rollback();
    if (error) {
        *error = message;
    }
    return false;
}

bool validMode(int value)
{
    return value >= static_cast<int>(domain::PracticeMode::Sequential)
        && value <= static_cast<int>(domain::PracticeMode::Review);
}

} // namespace

SqlitePracticeRepository::SqlitePracticeRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool SqlitePracticeRepository::save(
    const domain::PracticeSession &session,
    QString *error)
{
    clearError(error);
    if (!database_.isOpen()) {
        if (error) {
            *error = QStringLiteral("Database is not open");
        }
        return false;
    }
    if (session.id.isNull() || session.scopeId.trimmed().isEmpty()
        || session.currentIndex < 0
        || (!session.questionOrder.isEmpty()
            && session.currentIndex >= session.questionOrder.size())) {
        if (error) {
            *error = QStringLiteral("Practice session has invalid identity, scope, or position");
        }
        return false;
    }
    QSet<QUuid> uniqueQuestions;
    for (const QUuid &questionId : session.questionOrder) {
        if (questionId.isNull() || uniqueQuestions.contains(questionId)) {
            if (error) {
                *error = QStringLiteral("Practice question order contains an invalid or duplicate ID");
            }
            return false;
        }
        uniqueQuestions.insert(questionId);
    }

    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSqlQuery sessionQuery(database_);
    sessionQuery.prepare(QStringLiteral(
        "INSERT INTO practice_sessions(id, scope_id, mode, current_index, question_order_json, "
        "viewport_json, is_complete, created_at, updated_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET scope_id=excluded.scope_id, mode=excluded.mode, "
        "current_index=excluded.current_index, question_order_json=excluded.question_order_json, "
        "viewport_json=excluded.viewport_json, is_complete=excluded.is_complete, "
        "updated_at=excluded.updated_at"));
    sessionQuery.addBindValue(uuidText(session.id));
    sessionQuery.addBindValue(session.scopeId);
    sessionQuery.addBindValue(static_cast<int>(session.mode));
    sessionQuery.addBindValue(session.currentIndex);
    sessionQuery.addBindValue(orderJson(session.questionOrder));
    sessionQuery.addBindValue(QString::fromUtf8(
        QJsonDocument(session.viewport).toJson(QJsonDocument::Compact)));
    sessionQuery.addBindValue(session.complete ? 1 : 0);
    sessionQuery.addBindValue(now);
    sessionQuery.addBindValue(now);
    QString queryError;
    if (!execPrepared(sessionQuery, &queryError)) {
        return rollback(database_, queryError, error);
    }

    QSqlQuery clearAnswers(database_);
    clearAnswers.prepare(QStringLiteral("DELETE FROM practice_answers WHERE session_id=?"));
    clearAnswers.addBindValue(uuidText(session.id));
    if (!execPrepared(clearAnswers, &queryError)) {
        return rollback(database_, queryError, error);
    }

    QSet<QUuid> answerIds;
    for (auto iterator = session.answers.cbegin(); iterator != session.answers.cend(); ++iterator) {
        answerIds.insert(iterator.key());
    }
    for (auto iterator = session.drafts.cbegin(); iterator != session.drafts.cend(); ++iterator) {
        answerIds.insert(iterator.key());
    }
    answerIds.unite(session.revealedAnswers);

    for (const QUuid &questionId : answerIds) {
        if (!uniqueQuestions.contains(questionId)) {
            return rollback(
                database_,
                QStringLiteral("Practice answer references a question outside the session"),
                error);
        }
        QSqlQuery answer(database_);
        answer.prepare(QStringLiteral(
            "INSERT INTO practice_answers(session_id, question_id, answer, draft, revealed, answered_at) "
            "VALUES(?, ?, ?, ?, ?, ?)"));
        answer.addBindValue(uuidText(session.id));
        answer.addBindValue(uuidText(questionId));
        answer.addBindValue(nonNull(session.answers.value(questionId)));
        answer.addBindValue(nonNull(session.drafts.value(questionId)));
        answer.addBindValue(session.revealedAnswers.contains(questionId) ? 1 : 0);
        answer.addBindValue(
            session.answers.contains(questionId) ? QVariant(now) : QVariant());
        if (!execPrepared(answer, &queryError)) {
            return rollback(database_, queryError, error);
        }
    }

    if (!database_.commit()) {
        return rollback(database_, database_.lastError().text(), error);
    }
    return true;
}

std::optional<domain::PracticeSession> SqlitePracticeRepository::load(
    const QUuid &sessionId,
    QString *error) const
{
    clearError(error);
    if (sessionId.isNull()) {
        if (error) {
            *error = QStringLiteral("Practice session ID is invalid");
        }
        return std::nullopt;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT scope_id, mode, current_index, question_order_json, viewport_json, is_complete "
        "FROM practice_sessions WHERE id=?"));
    query.addBindValue(uuidText(sessionId));
    if (!execPrepared(query, error) || !query.next()) {
        return std::nullopt;
    }
    const int mode = query.value(1).toInt();
    bool orderValid = false;
    bool viewportValid = false;
    const QVector<QUuid> order = parseOrder(query.value(3).toString(), &orderValid);
    const QJsonObject viewport = parseViewport(query.value(4).toString(), &viewportValid);
    if (!validMode(mode) || !orderValid || !viewportValid) {
        if (error) {
            *error = QStringLiteral("Stored practice session is corrupt");
        }
        return std::nullopt;
    }

    domain::PracticeSession session;
    session.id = sessionId;
    session.scopeId = query.value(0).toString();
    session.mode = static_cast<domain::PracticeMode>(mode);
    session.currentIndex = query.value(2).toLongLong();
    session.questionOrder = order;
    session.viewport = viewport;
    session.complete = query.value(5).toBool();
    session.dirty = false;
    if (session.currentIndex < 0
        || (!session.questionOrder.isEmpty()
            && session.currentIndex >= session.questionOrder.size())) {
        if (error) {
            *error = QStringLiteral("Stored practice position is outside the question order");
        }
        return std::nullopt;
    }

    QSqlQuery answers(database_);
    answers.prepare(QStringLiteral(
        "SELECT question_id, answer, draft, revealed FROM practice_answers WHERE session_id=?"));
    answers.addBindValue(uuidText(sessionId));
    if (!execPrepared(answers, error)) {
        return std::nullopt;
    }
    while (answers.next()) {
        const QUuid questionId(answers.value(0).toString());
        if (questionId.isNull() || !session.questionOrder.contains(questionId)) {
            if (error) {
                *error = QStringLiteral("Stored practice answer references an invalid question");
            }
            return std::nullopt;
        }
        const QString answer = answers.value(1).toString();
        const QString draft = answers.value(2).toString();
        if (!answer.isEmpty()) {
            session.answers.insert(questionId, answer);
        }
        if (!draft.isEmpty()) {
            session.drafts.insert(questionId, draft);
        }
        if (answers.value(3).toBool()) {
            session.revealedAnswers.insert(questionId);
        }
    }
    return session;
}

std::optional<domain::PracticeSession> SqlitePracticeRepository::latest(
    const QString &scopeId,
    domain::PracticeMode mode,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id FROM practice_sessions WHERE scope_id=? AND mode=? "
        "ORDER BY updated_at DESC, rowid DESC LIMIT 1"));
    query.addBindValue(scopeId);
    query.addBindValue(static_cast<int>(mode));
    if (!execPrepared(query, error) || !query.next()) {
        return std::nullopt;
    }
    const QUuid sessionId(query.value(0).toString());
    query.finish();
    return load(sessionId, error);
}

std::optional<domain::PracticeSession> SqlitePracticeRepository::latestAcrossModes(
    const QString &scopeId,
    const QVector<domain::PracticeMode> &modes,
    QString *error) const
{
    clearError(error);
    if (scopeId.trimmed().isEmpty() || modes.isEmpty()) {
        return std::nullopt;
    }
    QStringList placeholders;
    placeholders.reserve(modes.size());
    for (qsizetype index = 0; index < modes.size(); ++index) {
        placeholders.append(QStringLiteral("?"));
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id FROM practice_sessions WHERE scope_id=? AND mode IN (%1) "
        "ORDER BY updated_at DESC, rowid DESC LIMIT 1")
                      .arg(placeholders.join(u',')));
    query.addBindValue(scopeId);
    for (const domain::PracticeMode mode : modes) {
        query.addBindValue(static_cast<int>(mode));
    }
    if (!execPrepared(query, error) || !query.next()) {
        return std::nullopt;
    }
    const QUuid sessionId(query.value(0).toString());
    query.finish();
    return load(sessionId, error);
}

std::optional<domain::PracticeSession>
SqlitePracticeRepository::latestIncompleteAcrossScopes(
    const QVector<domain::PracticeMode> &modes,
    QString *error) const
{
    clearError(error);
    if (modes.isEmpty()) {
        return std::nullopt;
    }
    QStringList placeholders;
    placeholders.reserve(modes.size());
    for (qsizetype index = 0; index < modes.size(); ++index) {
        placeholders.append(QStringLiteral("?"));
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id FROM practice_sessions WHERE is_complete=0 AND mode IN (%1) "
        "ORDER BY updated_at DESC, rowid DESC LIMIT 1")
                      .arg(placeholders.join(u',')));
    for (const domain::PracticeMode mode : modes) {
        query.addBindValue(static_cast<int>(mode));
    }
    if (!execPrepared(query, error) || !query.next()) {
        return std::nullopt;
    }
    const QUuid sessionId(query.value(0).toString());
    query.finish();
    return load(sessionId, error);
}

bool SqlitePracticeRepository::remove(const QUuid &sessionId, QString *error)
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM practice_sessions WHERE id=?"));
    query.addBindValue(uuidText(sessionId));
    return execPrepared(query, error);
}

bool SqlitePracticeRepository::removeScopeMode(
    const QString &scopeId,
    domain::PracticeMode mode,
    QString *error)
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "DELETE FROM practice_sessions WHERE scope_id=? AND mode=?"));
    query.addBindValue(scopeId);
    query.addBindValue(static_cast<int>(mode));
    return execPrepared(query, error);
}

} // namespace quizapp::storage
