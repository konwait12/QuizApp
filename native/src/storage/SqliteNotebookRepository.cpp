#include "storage/SqliteNotebookRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace quizapp::storage {
namespace {

void clearError(QString *error)
{
    if (error) error->clear();
}

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

QString idText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

domain::NotebookRecord recordFromQuery(const QSqlQuery &query)
{
    domain::NotebookRecord record;
    record.id = QUuid(query.value(0).toString());
    const QUuid questionId(query.value(1).toString());
    if (!questionId.isNull()) record.questionId = questionId;
    record.title = query.value(2).toString();
    record.formatVersion = query.value(3).toInt();
    record.relativePath = query.value(4).toString();
    record.contentHash = query.value(5).toByteArray();
    record.completed = query.value(6).toBool();
    record.createdAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODateWithMs);
    record.updatedAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs);
    const QDateTime deleted = QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
    if (deleted.isValid()) record.deletedAt = deleted;
    return record;
}

QString selectColumns()
{
    return QStringLiteral(
        "id, question_id, title, format_version, relative_path, content_hash, "
        "completed, created_at, updated_at, deleted_at");
}

} // namespace

SqliteNotebookRepository::SqliteNotebookRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool SqliteNotebookRepository::create(
    const domain::NotebookRecord &record, QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO notebooks(id, question_id, title, format_version, relative_path, "
        "content_hash, completed, created_at, updated_at, deleted_at) "
        "VALUES(?, NULL, ?, ?, ?, ?, ?, ?, ?, NULL)"));
    query.addBindValue(idText(record.id));
    query.addBindValue(record.title);
    query.addBindValue(record.formatVersion);
    query.addBindValue(record.relativePath);
    query.addBindValue(record.contentHash);
    query.addBindValue(record.completed ? 1 : 0);
    query.addBindValue(record.createdAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(record.updatedAt.toUTC().toString(Qt::ISODateWithMs));
    return query.exec() || fail(error, query.lastError().text());
}

QVector<domain::NotebookRecord> SqliteNotebookRepository::listFree(
    bool recycled, QString *error) const
{
    clearError(error);
    QVector<domain::NotebookRecord> records;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT %1 FROM notebooks WHERE question_id IS NULL "
                                 "AND deleted_at IS %2 NULL ORDER BY updated_at DESC")
                      .arg(selectColumns(), recycled ? QStringLiteral("NOT") : QString()));
    if (!query.exec()) {
        fail(error, query.lastError().text());
        return records;
    }
    while (query.next()) records.append(recordFromQuery(query));
    return records;
}

std::optional<domain::NotebookRecord> SqliteNotebookRepository::findById(
    const QUuid &id, QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT %1 FROM notebooks WHERE id=?").arg(selectColumns()));
    query.addBindValue(idText(id));
    if (!query.exec()) {
        fail(error, query.lastError().text());
        return std::nullopt;
    }
    return query.next() ? std::optional<domain::NotebookRecord>(recordFromQuery(query))
                        : std::nullopt;
}

bool SqliteNotebookRepository::rename(
    const QUuid &id, const QString &title, QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE notebooks SET title=?, updated_at=? WHERE id=?"));
    query.addBindValue(title);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(idText(id));
    return query.exec() || fail(error, query.lastError().text());
}

bool SqliteNotebookRepository::setRecycled(
    const QUuid &id, bool recycled, QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE notebooks SET deleted_at=?, updated_at=? WHERE id=? AND question_id IS NULL"));
    query.addBindValue(recycled
        ? QVariant(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
        : QVariant());
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(idText(id));
    return query.exec() || fail(error, query.lastError().text());
}

bool SqliteNotebookRepository::touch(
    const QUuid &id, const QByteArray &contentHash, QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE notebooks SET content_hash=?, updated_at=? WHERE id=?"));
    query.addBindValue(contentHash);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(idText(id));
    return query.exec() || fail(error, query.lastError().text());
}

bool SqliteNotebookRepository::remove(const QUuid &id, QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "DELETE FROM notebooks WHERE id=? AND question_id IS NULL AND deleted_at IS NOT NULL"));
    query.addBindValue(idText(id));
    return query.exec() || fail(error, query.lastError().text());
}

} // namespace quizapp::storage
