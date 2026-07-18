#include "storage/SqliteExamRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>
#include <utility>

namespace quizapp::storage {
namespace {

QString uuidText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

void clearError(QString *error)
{
    if (error) {
        error->clear();
    }
}

bool fail(QSqlDatabase database, const QString &message, QString *error)
{
    database.rollback();
    if (error) {
        *error = message;
    }
    return false;
}

bool execQuery(QSqlQuery &query, QString *error)
{
    if (query.exec()) {
        return true;
    }
    if (error) {
        *error = query.lastError().text();
    }
    return false;
}

QString orderJson(const QVector<QUuid> &order)
{
    QJsonArray values;
    for (const QUuid &id : order) {
        values.append(uuidText(id));
    }
    return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));
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
        if (id.isNull() || order.contains(id)) {
            return {};
        }
        order.append(id);
    }
    *valid = true;
    return order;
}

QJsonObject questionJson(const domain::Question &question)
{
    QJsonObject explanation{
        {QStringLiteral("text"), question.builtinExplanation.text},
        {QStringLiteral("provider"), question.builtinExplanation.provider},
        {QStringLiteral("sourceId"), question.builtinExplanation.sourceId},
    };
    QJsonArray path;
    for (const QString &entry : question.path) {
        path.append(entry);
    }
    QJsonArray options;
    for (const QString &option : question.options) {
        options.append(option);
    }
    return {
        {QStringLiteral("id"), uuidText(question.id)},
        {QStringLiteral("bankId"), question.bankId},
        {QStringLiteral("path"), path},
        {QStringLiteral("type"), static_cast<int>(question.type)},
        {QStringLiteral("prompt"), question.prompt},
        {QStringLiteral("options"), options},
        {QStringLiteral("correctAnswer"), question.correctAnswer},
        {QStringLiteral("explanation"), explanation},
    };
}

std::optional<domain::Question> parseQuestion(const QString &value)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    domain::Question question;
    question.id = QUuid(object.value(QStringLiteral("id")).toString());
    question.bankId = object.value(QStringLiteral("bankId")).toString();
    question.type = static_cast<domain::QuestionType>(
        object.value(QStringLiteral("type")).toInt(-1));
    question.prompt = object.value(QStringLiteral("prompt")).toString();
    question.correctAnswer = object.value(QStringLiteral("correctAnswer")).toString();
    for (const QJsonValue &entry : object.value(QStringLiteral("path")).toArray()) {
        question.path.append(entry.toString());
    }
    for (const QJsonValue &entry : object.value(QStringLiteral("options")).toArray()) {
        question.options.append(entry.toString());
    }
    const QJsonObject explanation = object.value(QStringLiteral("explanation")).toObject();
    question.builtinExplanation.text = explanation.value(QStringLiteral("text")).toString();
    question.builtinExplanation.provider = explanation.value(QStringLiteral("provider")).toString();
    question.builtinExplanation.sourceId = explanation.value(QStringLiteral("sourceId")).toString();
    if (question.id.isNull() || question.prompt.trimmed().isEmpty()
        || question.type < domain::QuestionType::Single
        || question.type > domain::QuestionType::Subjective) {
        return std::nullopt;
    }
    return question;
}

} // namespace

SqliteExamRepository::SqliteExamRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool SqliteExamRepository::save(const domain::ExamSession &session, QString *error)
{
    clearError(error);
    if (!database_.isOpen() || session.id.isNull() || session.scopeId.trimmed().isEmpty()
        || session.questionOrder.isEmpty() || session.durationSeconds <= 0
        || session.remainingSeconds < 0
        || session.remainingSeconds > session.durationSeconds
        || session.currentIndex < 0 || session.currentIndex >= session.questionOrder.size()) {
        if (error) {
            *error = QStringLiteral("Exam session is invalid");
        }
        return false;
    }
    if (session.status == domain::ExamStatus::Submitted
        && session.resultItems.size() != session.questionOrder.size()) {
        if (error) {
            *error = QStringLiteral("Submitted exam does not contain a complete result snapshot");
        }
        return false;
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime created = session.createdAt.isValid() ? session.createdAt : now;
    QSqlQuery upsert(database_);
    upsert.prepare(QStringLiteral(
        "INSERT INTO exam_sessions(id, scope_id, status, duration_seconds, elapsed_seconds, "
        "question_order_json, score, created_at, submitted_at, title, current_index, "
        "remaining_seconds, paused, correct_count, wrong_count, unanswered_count, timed_out, "
        "updated_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET scope_id=excluded.scope_id, status=excluded.status, "
        "duration_seconds=excluded.duration_seconds, elapsed_seconds=excluded.elapsed_seconds, "
        "question_order_json=excluded.question_order_json, score=excluded.score, "
        "submitted_at=excluded.submitted_at, title=excluded.title, "
        "current_index=excluded.current_index, remaining_seconds=excluded.remaining_seconds, "
        "paused=excluded.paused, correct_count=excluded.correct_count, "
        "wrong_count=excluded.wrong_count, unanswered_count=excluded.unanswered_count, "
        "timed_out=excluded.timed_out, updated_at=excluded.updated_at"));
    upsert.addBindValue(uuidText(session.id));
    upsert.addBindValue(session.scopeId);
    upsert.addBindValue(static_cast<int>(session.status));
    upsert.addBindValue(session.durationSeconds);
    upsert.addBindValue(session.durationSeconds - session.remainingSeconds);
    upsert.addBindValue(orderJson(session.questionOrder));
    upsert.addBindValue(session.status == domain::ExamStatus::Submitted
        ? QVariant(session.score) : QVariant());
    upsert.addBindValue(created.toString(Qt::ISODateWithMs));
    upsert.addBindValue(session.submittedAt.isValid()
        ? QVariant(session.submittedAt.toString(Qt::ISODateWithMs)) : QVariant());
    upsert.addBindValue(session.title);
    upsert.addBindValue(session.currentIndex);
    upsert.addBindValue(session.remainingSeconds);
    upsert.addBindValue(session.paused ? 1 : 0);
    upsert.addBindValue(session.correctCount);
    upsert.addBindValue(session.wrongCount);
    upsert.addBindValue(session.unansweredCount);
    upsert.addBindValue(session.timedOut ? 1 : 0);
    upsert.addBindValue(now.toString(Qt::ISODateWithMs));
    QString queryError;
    if (!execQuery(upsert, &queryError)) {
        return fail(database_, queryError, error);
    }

    QSqlQuery clearAnswers(database_);
    clearAnswers.prepare(QStringLiteral("DELETE FROM exam_answers WHERE exam_id=?"));
    clearAnswers.addBindValue(uuidText(session.id));
    if (!execQuery(clearAnswers, &queryError)) {
        return fail(database_, queryError, error);
    }
    for (auto iterator = session.answers.cbegin(); iterator != session.answers.cend(); ++iterator) {
        if (!session.questionOrder.contains(iterator.key())) {
            return fail(database_, QStringLiteral("Exam answer is outside the question order"), error);
        }
        QSqlQuery answer(database_);
        answer.prepare(QStringLiteral(
            "INSERT INTO exam_answers(exam_id, question_id, answer) VALUES(?, ?, ?)"));
        answer.addBindValue(uuidText(session.id));
        answer.addBindValue(uuidText(iterator.key()));
        answer.addBindValue(iterator.value());
        if (!execQuery(answer, &queryError)) {
            return fail(database_, queryError, error);
        }
    }

    QSqlQuery clearResults(database_);
    clearResults.prepare(QStringLiteral("DELETE FROM exam_result_items WHERE exam_id=?"));
    clearResults.addBindValue(uuidText(session.id));
    if (!execQuery(clearResults, &queryError)) {
        return fail(database_, queryError, error);
    }
    for (qsizetype index = 0; index < session.resultItems.size(); ++index) {
        const domain::ExamResultItem &item = session.resultItems.at(index);
        QSqlQuery result(database_);
        result.prepare(QStringLiteral(
            "INSERT INTO exam_result_items(exam_id, question_id, sort_order, answer, correct, "
            "unanswered, question_json) VALUES(?, ?, ?, ?, ?, ?, ?)"));
        result.addBindValue(uuidText(session.id));
        result.addBindValue(uuidText(item.questionId));
        result.addBindValue(index);
        result.addBindValue(item.answer);
        result.addBindValue(item.correct ? 1 : 0);
        result.addBindValue(item.unanswered ? 1 : 0);
        result.addBindValue(QString::fromUtf8(
            QJsonDocument(questionJson(item.questionSnapshot)).toJson(QJsonDocument::Compact)));
        if (!execQuery(result, &queryError)) {
            return fail(database_, queryError, error);
        }
    }
    if (!database_.commit()) {
        return fail(database_, database_.lastError().text(), error);
    }
    return true;
}

std::optional<domain::ExamSession> SqliteExamRepository::load(
    const QUuid &sessionId,
    QString *error) const
{
    clearError(error);
    if (sessionId.isNull()) {
        return std::nullopt;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT scope_id, status, duration_seconds, question_order_json, score, created_at, "
        "submitted_at, title, current_index, remaining_seconds, paused, correct_count, "
        "wrong_count, unanswered_count, timed_out FROM exam_sessions WHERE id=?"));
    query.addBindValue(uuidText(sessionId));
    if (!execQuery(query, error) || !query.next()) {
        return std::nullopt;
    }
    bool orderValid = false;
    domain::ExamSession session;
    session.id = sessionId;
    session.scopeId = query.value(0).toString();
    session.status = static_cast<domain::ExamStatus>(query.value(1).toInt());
    session.durationSeconds = query.value(2).toInt();
    session.questionOrder = parseOrder(query.value(3).toString(), &orderValid);
    session.score = query.value(4).toInt();
    session.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs);
    session.submittedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
    session.title = query.value(7).toString();
    session.currentIndex = query.value(8).toLongLong();
    session.remainingSeconds = query.value(9).toInt();
    session.paused = query.value(10).toBool();
    session.correctCount = query.value(11).toInt();
    session.wrongCount = query.value(12).toInt();
    session.unansweredCount = query.value(13).toInt();
    session.timedOut = query.value(14).toBool();
    if (!orderValid || session.questionOrder.isEmpty() || session.currentIndex < 0
        || session.currentIndex >= session.questionOrder.size()
        || session.status < domain::ExamStatus::Active
        || session.status > domain::ExamStatus::Submitted) {
        if (error) {
            *error = QStringLiteral("Stored exam session is corrupt");
        }
        return std::nullopt;
    }

    QSqlQuery answers(database_);
    answers.prepare(QStringLiteral(
        "SELECT question_id, answer FROM exam_answers WHERE exam_id=?"));
    answers.addBindValue(uuidText(sessionId));
    if (!execQuery(answers, error)) {
        return std::nullopt;
    }
    while (answers.next()) {
        const QUuid id(answers.value(0).toString());
        if (id.isNull() || !session.questionOrder.contains(id)) {
            if (error) {
                *error = QStringLiteral("Stored exam answer is corrupt");
            }
            return std::nullopt;
        }
        session.answers.insert(id, answers.value(1).toString());
    }

    QSqlQuery results(database_);
    results.prepare(QStringLiteral(
        "SELECT question_id, answer, correct, unanswered, question_json "
        "FROM exam_result_items WHERE exam_id=? ORDER BY sort_order"));
    results.addBindValue(uuidText(sessionId));
    if (!execQuery(results, error)) {
        return std::nullopt;
    }
    while (results.next()) {
        const auto question = parseQuestion(results.value(4).toString());
        if (!question) {
            if (error) {
                *error = QStringLiteral("Stored exam result snapshot is corrupt");
            }
            return std::nullopt;
        }
        domain::ExamResultItem item;
        item.questionId = QUuid(results.value(0).toString());
        item.answer = results.value(1).toString();
        item.correct = results.value(2).toBool();
        item.unanswered = results.value(3).toBool();
        item.questionSnapshot = *question;
        session.resultItems.append(item);
    }
    if (session.status == domain::ExamStatus::Submitted
        && session.resultItems.size() != session.questionOrder.size()) {
        if (error) {
            *error = QStringLiteral("Stored exam result is incomplete");
        }
        return std::nullopt;
    }
    return session;
}

std::optional<domain::ExamSession> SqliteExamRepository::latestActive(QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id FROM exam_sessions WHERE status=? ORDER BY updated_at DESC, rowid DESC LIMIT 1"));
    query.addBindValue(static_cast<int>(domain::ExamStatus::Active));
    if (!execQuery(query, error) || !query.next()) {
        return std::nullopt;
    }
    const QUuid id(query.value(0).toString());
    query.finish();
    return load(id, error);
}

QVector<domain::ExamSession> SqliteExamRepository::history(
    qsizetype limit,
    QString *error) const
{
    clearError(error);
    QVector<domain::ExamSession> sessions;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id FROM exam_sessions WHERE status=? "
        "ORDER BY submitted_at DESC, rowid DESC LIMIT ?"));
    query.addBindValue(static_cast<int>(domain::ExamStatus::Submitted));
    query.addBindValue(std::clamp<qsizetype>(limit, 1, 100));
    if (!execQuery(query, error)) {
        return {};
    }
    QVector<QUuid> ids;
    while (query.next()) {
        ids.append(QUuid(query.value(0).toString()));
    }
    query.finish();
    for (const QUuid &id : std::as_const(ids)) {
        const auto session = load(id, error);
        if (!session) {
            return {};
        }
        sessions.append(*session);
    }
    return sessions;
}

bool SqliteExamRepository::remove(const QUuid &sessionId, QString *error)
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM exam_sessions WHERE id=?"));
    query.addBindValue(uuidText(sessionId));
    return execQuery(query, error);
}

} // namespace quizapp::storage
