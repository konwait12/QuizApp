#include "storage/SqliteBankSourceRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QJsonArray>
#include <QJsonDocument>

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

QString pathJson(const QStringList &path)
{
    return QString::fromUtf8(QJsonDocument(
        QJsonArray::fromStringList(path)).toJson(QJsonDocument::Compact));
}

bool releaseOverridesInTransaction(
    QSqlDatabase database,
    const QString &bankId,
    QString *error)
{
    QSqlQuery restore(database);
    restore.prepare(QStringLiteral(
        "UPDATE banks SET active=1 WHERE id IN ("
        "SELECT overridden_bank_id FROM managed_bank_overrides "
        "WHERE overriding_bank_id=?) AND id NOT IN ("
        "SELECT bank_id FROM library_hidden_banks)"));
    restore.addBindValue(bankId);
    if (!restore.exec()) {
        if (error) {
            *error = restore.lastError().text();
        }
        return false;
    }
    QSqlQuery remove(database);
    remove.prepare(QStringLiteral(
        "DELETE FROM managed_bank_overrides WHERE overriding_bank_id=?"));
    remove.addBindValue(bankId);
    if (!remove.exec()) {
        if (error) {
            *error = remove.lastError().text();
        }
        return false;
    }
    return true;
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

QString SqliteBankSourceRepository::identitySourceKeyForBank(
    const QString &bankId,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT source_id FROM banks WHERE id=?"));
    query.addBindValue(bankId);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return {};
    }
    return query.next() ? query.value(0).toString() : QString();
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

bool SqliteBankSourceRepository::replaceKey(
    const QString &previousSourceKey,
    const domain::ManagedBankSource &source,
    QString *error)
{
    clearError(error);
    if (previousSourceKey == source.sourceKey) {
        return save(source, error);
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    QSqlQuery removePrevious(database_);
    removePrevious.prepare(QStringLiteral(
        "DELETE FROM bank_sources WHERE source_key=?"));
    removePrevious.addBindValue(previousSourceKey);
    if (!removePrevious.exec()) {
        database_.rollback();
        if (error) {
            *error = removePrevious.lastError().text();
        }
        return false;
    }
    if (!save(source, error)) {
        database_.rollback();
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
        if (!available
            && !releaseOverridesInTransaction(database_, source->bankId, error)) {
            database_.rollback();
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

bool SqliteBankSourceRepository::applyManagedOverride(
    const QString &bankId,
    const QStringList &path,
    QString *error)
{
    clearError(error);
    if (bankId.trimmed().isEmpty() || path.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Managed bank override path is invalid");
        }
        return false;
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    if (!releaseOverridesInTransaction(database_, bankId, error)) {
        database_.rollback();
        return false;
    }
    QSqlQuery candidates(database_);
    candidates.prepare(QStringLiteral(
        "SELECT DISTINCT b.id FROM banks b "
        "JOIN questions q ON q.bank_id=b.id AND q.active=1 "
        "LEFT JOIN bank_sources s ON s.bank_id=b.id "
        "WHERE b.id<>? AND b.active=1 AND q.path_json=? "
        "AND (s.bank_id IS NULL OR s.available=0)"));
    candidates.addBindValue(bankId);
    candidates.addBindValue(pathJson(path));
    if (!candidates.exec()) {
        database_.rollback();
        if (error) {
            *error = candidates.lastError().text();
        }
        return false;
    }
    QStringList overriddenBankIds;
    while (candidates.next()) {
        overriddenBankIds.append(candidates.value(0).toString());
    }
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const QString &overriddenBankId : overriddenBankIds) {
        QSqlQuery insert(database_);
        insert.prepare(QStringLiteral(
            "INSERT INTO managed_bank_overrides("
            "overriding_bank_id, overridden_bank_id, created_at) VALUES(?, ?, ?)"));
        insert.addBindValue(bankId);
        insert.addBindValue(overriddenBankId);
        insert.addBindValue(now);
        if (!insert.exec()) {
            database_.rollback();
            if (error) {
                *error = insert.lastError().text();
            }
            return false;
        }
        QSqlQuery deactivate(database_);
        deactivate.prepare(QStringLiteral("UPDATE banks SET active=0 WHERE id=?"));
        deactivate.addBindValue(overriddenBankId);
        if (!deactivate.exec()) {
            database_.rollback();
            if (error) {
                *error = deactivate.lastError().text();
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

bool SqliteBankSourceRepository::releaseManagedOverrides(
    const QString &bankId,
    QString *error)
{
    clearError(error);
    if (bankId.trimmed().isEmpty()) {
        return true;
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    if (!releaseOverridesInTransaction(database_, bankId, error)) {
        database_.rollback();
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

} // namespace quizapp::storage
