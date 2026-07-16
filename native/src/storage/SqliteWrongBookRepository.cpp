#include "storage/SqliteWrongBookRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

#include <utility>

namespace quizapp::storage {
namespace {

QString uuidText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

QString pathJson(const QStringList &path)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(path)).toJson(QJsonDocument::Compact));
}

void setQueryError(const QSqlQuery &query, QString *error)
{
    if (error) {
        *error = query.lastError().text();
    }
}

} // namespace

SqliteWrongBookRepository::SqliteWrongBookRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool SqliteWrongBookRepository::upsert(
    const domain::WrongBookEntry &entry,
    QString *error)
{
    if (error) {
        error->clear();
    }
    if (entry.questionId.isNull()) {
        if (error) {
            *error = QStringLiteral("Wrong-book question id is empty");
        }
        return false;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString addedAt = (entry.addedAt.isValid() ? entry.addedAt : now)
                                .toUTC().toString(Qt::ISODateWithMs);
    const QString updatedAt = (entry.updatedAt.isValid() ? entry.updatedAt : now)
                                  .toUTC().toString(Qt::ISODateWithMs);
    const QString tags = QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(entry.reasonTags))
            .toJson(QJsonDocument::Compact));
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO wrong_book(question_id, reason_tags_json, note, added_at, updated_at) "
        "VALUES(?, ?, ?, ?, ?) ON CONFLICT(question_id) DO UPDATE SET "
        "reason_tags_json=excluded.reason_tags_json, note=excluded.note, "
        "updated_at=excluded.updated_at"));
    query.addBindValue(uuidText(entry.questionId));
    query.addBindValue(tags);
    query.addBindValue(entry.note.isNull() ? QStringLiteral("") : entry.note);
    query.addBindValue(addedAt);
    query.addBindValue(updatedAt);
    if (!query.exec()) {
        setQueryError(query, error);
        return false;
    }
    return true;
}

bool SqliteWrongBookRepository::remove(const QUuid &questionId, QString *error)
{
    if (error) {
        error->clear();
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM wrong_book WHERE question_id=?"));
    query.addBindValue(uuidText(questionId));
    if (!query.exec()) {
        setQueryError(query, error);
        return false;
    }
    return true;
}

bool SqliteWrongBookRepository::contains(
    const QUuid &questionId,
    bool *contained,
    QString *error) const
{
    if (error) {
        error->clear();
    }
    if (!contained) {
        if (error) {
            *error = QStringLiteral("Wrong-book output pointer is null");
        }
        return false;
    }
    *contained = false;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT 1 FROM wrong_book WHERE question_id=?"));
    query.addBindValue(uuidText(questionId));
    if (!query.exec()) {
        setQueryError(query, error);
        return false;
    }
    *contained = query.next();
    return true;
}

QSet<QUuid> SqliteWrongBookRepository::listQuestionIdsByPathPrefix(
    const QStringList &pathPrefix,
    QString *error) const
{
    if (error) {
        error->clear();
    }
    QSqlQuery query(database_);
    if (pathPrefix.isEmpty()) {
        query.prepare(QStringLiteral(
            "SELECT wb.question_id FROM wrong_book wb "
            "JOIN questions q ON q.id=wb.question_id WHERE q.active=1 "
            "ORDER BY wb.updated_at DESC"));
    } else {
        const QString serializedPath = pathJson(pathPrefix);
        const QString descendantPrefix = serializedPath.left(serializedPath.size() - 1)
            + QStringLiteral(",");
        query.prepare(QStringLiteral(
            "SELECT wb.question_id FROM wrong_book wb "
            "JOIN questions q ON q.id=wb.question_id "
            "WHERE q.active=1 AND (q.path_json=? OR substr(q.path_json, 1, ?)=?) "
            "ORDER BY wb.updated_at DESC"));
        query.addBindValue(serializedPath);
        query.addBindValue(descendantPrefix.size());
        query.addBindValue(descendantPrefix);
    }
    if (!query.exec()) {
        setQueryError(query, error);
        return {};
    }
    QSet<QUuid> questionIds;
    while (query.next()) {
        const QUuid questionId(query.value(0).toString());
        if (!questionId.isNull()) {
            questionIds.insert(questionId);
        }
    }
    return questionIds;
}

} // namespace quizapp::storage
