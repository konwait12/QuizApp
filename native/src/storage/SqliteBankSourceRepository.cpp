#include "storage/SqliteBankSourceRepository.h"

#include <QSqlError>
#include <QSqlQuery>

namespace quizapp::storage {
namespace {

void clearError(QString *error)
{
    if (error) {
        error->clear();
    }
}

domain::ManagedBankSource sourceFromQuery(const QSqlQuery &query)
{
    domain::ManagedBankSource source;
    source.sourceKey = query.value(0).toString();
    source.managedRoot = query.value(1).toString();
    source.relativePath = query.value(2).toString();
    source.bankId = query.value(3).toString();
    source.fileSize = query.value(4).toLongLong();
    source.modifiedMsecs = query.value(5).toLongLong();
    source.sha256 = query.value(6).toByteArray();
    source.available = query.value(7).toBool();
    source.lastError = query.value(8).toString();
    source.lastSyncedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
    return source;
}

} // namespace

SqliteBankSourceRepository::SqliteBankSourceRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

std::optional<domain::ManagedBankSource> SqliteBankSourceRepository::findByKey(
    const QString &sourceKey,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT source_key, managed_root, relative_path, bank_id, file_size, "
        "modified_msecs, sha256, available, last_error, last_synced_at "
        "FROM bank_sources WHERE source_key=?"));
    query.addBindValue(sourceKey);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return sourceFromQuery(query);
}

QVector<domain::ManagedBankSource> SqliteBankSourceRepository::listByRoot(
    const QString &managedRoot,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT source_key, managed_root, relative_path, bank_id, file_size, "
        "modified_msecs, sha256, available, last_error, last_synced_at "
        "FROM bank_sources WHERE managed_root=? ORDER BY relative_path"));
    query.addBindValue(managedRoot);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return {};
    }
    QVector<domain::ManagedBankSource> sources;
    while (query.next()) {
        sources.append(sourceFromQuery(query));
    }
    return sources;
}

bool SqliteBankSourceRepository::save(
    const domain::ManagedBankSource &source,
    QString *error)
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO bank_sources(source_key, managed_root, relative_path, bank_id, "
        "file_size, modified_msecs, sha256, available, last_error, last_synced_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(source_key) DO UPDATE SET managed_root=excluded.managed_root, "
        "relative_path=excluded.relative_path, bank_id=excluded.bank_id, "
        "file_size=excluded.file_size, modified_msecs=excluded.modified_msecs, "
        "sha256=excluded.sha256, available=excluded.available, "
        "last_error=excluded.last_error, last_synced_at=excluded.last_synced_at"));
    query.addBindValue(source.sourceKey);
    query.addBindValue(source.managedRoot);
    query.addBindValue(source.relativePath);
    query.addBindValue(source.bankId.isEmpty() ? QVariant() : QVariant(source.bankId));
    query.addBindValue(source.fileSize);
    query.addBindValue(source.modifiedMsecs);
    query.addBindValue(source.sha256);
    query.addBindValue(source.available);
    query.addBindValue(source.lastError.isNull() ? QStringLiteral("") : source.lastError);
    query.addBindValue(source.lastSyncedAt.toUTC().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    if (source.bankId.isEmpty()) {
        return true;
    }
    QSqlQuery bank(database_);
    bank.prepare(QStringLiteral("UPDATE banks SET active=? WHERE id=?"));
    bank.addBindValue(source.available);
    bank.addBindValue(source.bankId);
    if (!bank.exec()) {
        if (error) {
            *error = bank.lastError().text();
        }
        return false;
    }
    return true;
}

bool SqliteBankSourceRepository::setAvailability(
    const QString &sourceKey,
    bool available,
    QString *error)
{
    clearError(error);
    const auto source = findByKey(sourceKey, error);
    if (!source) {
        return error == nullptr || error->isEmpty();
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    QSqlQuery updateSource(database_);
    updateSource.prepare(QStringLiteral(
        "UPDATE bank_sources SET available=?, last_synced_at=? WHERE source_key=?"));
    updateSource.addBindValue(available);
    updateSource.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    updateSource.addBindValue(sourceKey);
    if (!updateSource.exec()) {
        database_.rollback();
        if (error) {
            *error = updateSource.lastError().text();
        }
        return false;
    }
    if (!source->bankId.isEmpty()) {
        QSqlQuery updateBank(database_);
        updateBank.prepare(QStringLiteral("UPDATE banks SET active=? WHERE id=?"));
        updateBank.addBindValue(available);
        updateBank.addBindValue(source->bankId);
        if (!updateBank.exec()) {
            database_.rollback();
            if (error) {
                *error = updateBank.lastError().text();
            }
            return false;
        }
    }
    if (!database_.commit()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    return true;
}

} // namespace quizapp::storage
