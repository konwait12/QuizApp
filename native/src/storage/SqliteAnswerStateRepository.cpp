#include "storage/SqliteAnswerStateRepository.h"

#include <QDateTime>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

#include <utility>

#include <algorithm>

namespace quizapp::storage {
namespace {

bool supportedMode(domain::PracticeMode mode)
{
    return mode == domain::PracticeMode::Sequential
        || mode == domain::PracticeMode::Random;
}

QString uuidText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

void setError(const QString &message, QString *error)
{
    if (error) *error = message;
}

} // namespace

SqliteAnswerStateRepository::SqliteAnswerStateRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

QHash<QUuid, QString> SqliteAnswerStateRepository::load(
    const QVector<QUuid> &questionIds,
    domain::PracticeMode mode,
    QString *error) const
{
    if (error) error->clear();
    if (!supportedMode(mode) || questionIds.isEmpty()) return {};
    QHash<QUuid, QString> result;
    constexpr qsizetype kQueryChunkSize = 800;
    for (qsizetype offset = 0; offset < questionIds.size(); offset += kQueryChunkSize) {
        const qsizetype count = std::min(kQueryChunkSize, questionIds.size() - offset);
        QStringList placeholders;
        for (qsizetype index = 0; index < count; ++index) {
            placeholders.append(QStringLiteral("?"));
        }
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "SELECT question_id, answer FROM question_answer_state "
            "WHERE mode=? AND question_id IN (%1)").arg(placeholders.join(u',')));
        query.addBindValue(static_cast<int>(mode));
        for (qsizetype index = 0; index < count; ++index) {
            query.addBindValue(uuidText(questionIds.at(offset + index)));
        }
        if (!query.exec()) {
            setError(query.lastError().text(), error);
            return {};
        }
        while (query.next()) {
            const QUuid id(query.value(0).toString());
            if (!id.isNull()) result.insert(id, query.value(1).toString());
        }
    }
    return result;
}

bool SqliteAnswerStateRepository::saveSessionAnswers(
    const domain::PracticeSession &session,
    QString *error)
{
    if (error) error->clear();
    if (!supportedMode(session.mode)) return true;
    QSet<QUuid> unique;
    for (const QUuid &id : session.questionOrder) {
        if (id.isNull() || unique.contains(id)) {
            setError(QStringLiteral("练习题目顺序包含无效或重复 ID"), error);
            return false;
        }
        unique.insert(id);
    }
    if (!database_.transaction()) {
        setError(database_.lastError().text(), error);
        return false;
    }
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const QUuid &id : session.questionOrder) {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "INSERT INTO question_answer_state(question_id, mode, answer, updated_at) "
            "VALUES(?, ?, ?, ?) ON CONFLICT(question_id, mode) DO UPDATE SET "
            "answer=excluded.answer, updated_at=excluded.updated_at, legacy_migration_id=NULL"));
        query.addBindValue(uuidText(id));
        query.addBindValue(static_cast<int>(session.mode));
        const QString answer = session.answers.contains(id)
            ? session.answers.value(id).trimmed().toUpper()
            : QStringLiteral("");
        query.addBindValue(answer);
        query.addBindValue(timestamp);
        if (!query.exec()) {
            database_.rollback();
            setError(query.lastError().text(), error);
            return false;
        }
    }
    if (!database_.commit()) {
        setError(database_.lastError().text(), error);
        return false;
    }
    return true;
}

bool SqliteAnswerStateRepository::clear(
    const QVector<QUuid> &questionIds,
    domain::PracticeMode mode,
    QString *error)
{
    if (error) error->clear();
    if (!supportedMode(mode) || questionIds.isEmpty()) return true;
    QSet<QUuid> unique;
    for (const QUuid &id : questionIds) {
        if (id.isNull() || unique.contains(id)) {
            setError(QStringLiteral("练习题目列表包含无效或重复 ID"), error);
            return false;
        }
        unique.insert(id);
    }
    if (!database_.transaction()) {
        setError(database_.lastError().text(), error);
        return false;
    }
    constexpr qsizetype kQueryChunkSize = 800;
    for (qsizetype offset = 0; offset < questionIds.size(); offset += kQueryChunkSize) {
        const qsizetype count = std::min(kQueryChunkSize, questionIds.size() - offset);
        QStringList placeholders;
        placeholders.reserve(count);
        for (qsizetype index = 0; index < count; ++index) {
            placeholders.append(QStringLiteral("?"));
        }
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "DELETE FROM question_answer_state WHERE mode=? AND question_id IN (%1)")
                          .arg(placeholders.join(u',')));
        query.addBindValue(static_cast<int>(mode));
        for (qsizetype index = 0; index < count; ++index) {
            query.addBindValue(uuidText(questionIds.at(offset + index)));
        }
        if (!query.exec()) {
            database_.rollback();
            setError(query.lastError().text(), error);
            return false;
        }
    }
    if (!database_.commit()) {
        setError(database_.lastError().text(), error);
        return false;
    }
    return true;
}

bool SqliteAnswerStateRepository::applyToSession(
    domain::PracticeSession *session,
    QString *error) const
{
    if (error) error->clear();
    if (!session || !supportedMode(session->mode)) return true;
    const QHash<QUuid, QString> state = load(session->questionOrder, session->mode, error);
    if (error && !error->isEmpty()) return false;
    for (auto iterator = state.constBegin(); iterator != state.constEnd(); ++iterator) {
        if (iterator.value().isEmpty()) session->answers.remove(iterator.key());
        else session->answers.insert(iterator.key(), iterator.value());
        session->drafts.remove(iterator.key());
    }
    return true;
}

} // namespace quizapp::storage
