#include "storage/SqliteAiRecordRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include <utility>

namespace quizapp::storage {
namespace {

void setError(const QSqlQuery &query, QString *error)
{
    if (error) *error = query.lastError().text();
}

} // namespace

SqliteAiRecordRepository::SqliteAiRecordRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

std::optional<domain::AiRecord> SqliteAiRecordRepository::find(
    const QString &recordType,
    const QString &sourceId,
    QString *error) const
{
    if (error) error->clear();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, record_type, source_id, model, content, source_hash, created_at "
        "FROM ai_records WHERE record_type=? AND source_id=?"));
    query.addBindValue(recordType);
    query.addBindValue(sourceId);
    if (!query.exec()) {
        setError(query, error);
        return std::nullopt;
    }
    if (!query.next()) return std::nullopt;

    domain::AiRecord record;
    record.id = QUuid(query.value(0).toString());
    record.recordType = query.value(1).toString();
    record.sourceId = query.value(2).toString();
    record.model = query.value(3).toString();
    record.content = query.value(4).toString();
    record.sourceHash = query.value(5).toByteArray();
    record.createdAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
    return record;
}

bool SqliteAiRecordRepository::upsert(
    const domain::AiRecord &record,
    QString *error) const
{
    if (error) error->clear();
    if (record.id.isNull() || record.recordType.trimmed().isEmpty()
        || record.sourceId.trimmed().isEmpty() || record.model.trimmed().isEmpty()
        || record.content.trimmed().isEmpty() || record.sourceHash.isEmpty()) {
        if (error) *error = QStringLiteral("AI 解析记录不完整");
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO ai_records(id, record_type, source_id, model, content, source_hash, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?) ON CONFLICT(record_type, source_id) DO UPDATE SET "
        "id=excluded.id, model=excluded.model, content=excluded.content, "
        "source_hash=excluded.source_hash, created_at=excluded.created_at"));
    query.addBindValue(record.id.toString(QUuid::WithoutBraces));
    query.addBindValue(record.recordType);
    query.addBindValue(record.sourceId);
    query.addBindValue(record.model);
    query.addBindValue(record.content);
    query.addBindValue(record.sourceHash);
    query.addBindValue((record.createdAt.isValid()
        ? record.createdAt : QDateTime::currentDateTimeUtc()).toUTC().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        setError(query, error);
        return false;
    }
    return true;
}

} // namespace quizapp::storage
