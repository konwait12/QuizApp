#include "storage/SqliteLibraryRepository.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace quizapp::storage {
namespace {

QString parentPathJson(const QStringList &path)
{
    return QString::fromUtf8(QJsonDocument(
        QJsonArray::fromStringList(path)).toJson(QJsonDocument::Compact));
}

QStringList pathFromJson(const QString &encoded)
{
    const QJsonDocument document = QJsonDocument::fromJson(encoded.toUtf8());
    if (!document.isArray()) {
        return {};
    }
    QStringList path;
    for (const QJsonValue &value : document.array()) {
        if (!value.isString() || value.toString().trimmed().isEmpty()) {
            return {};
        }
        path.append(value.toString());
    }
    return path;
}

} // namespace

SqliteLibraryRepository::SqliteLibraryRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

domain::LibraryStats SqliteLibraryRepository::stats(QString *error) const
{
    if (error) {
        error->clear();
    }
    domain::LibraryStats result;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT (SELECT COUNT(*) FROM banks WHERE active=1), "
            "(SELECT COUNT(*) FROM questions q JOIN banks b ON b.id=q.bank_id "
            "WHERE q.active=1 AND b.active=1), "
            "(SELECT COUNT(*) FROM blobs)"))
        || !query.next()) {
        if (error) {
            *error = query.lastError().text();
        }
        return {};
    }
    result.bankCount = query.value(0).toLongLong();
    result.questionCount = query.value(1).toLongLong();
    result.blobCount = query.value(2).toLongLong();
    return result;
}

QStringList SqliteLibraryRepository::childOrder(
    const QStringList &parentPath,
    QString *error) const
{
    if (error) {
        error->clear();
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT child_title FROM library_node_order "
        "WHERE parent_path_json=? ORDER BY sort_order"));
    query.addBindValue(parentPathJson(parentPath));
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return {};
    }
    QStringList result;
    while (query.next()) {
        result.append(query.value(0).toString());
    }
    return result;
}

bool SqliteLibraryRepository::setChildOrder(
    const QStringList &parentPath,
    const QStringList &childTitles,
    QString *error)
{
    if (error) {
        error->clear();
    }
    QSet<QString> uniqueTitles;
    for (const QString &title : childTitles) {
        if (title.trimmed().isEmpty() || uniqueTitles.contains(title)) {
            if (error) {
                *error = QStringLiteral("Library child order contains invalid titles");
            }
            return false;
        }
        uniqueTitles.insert(title);
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    const QString pathJson = parentPathJson(parentPath);
    QSqlQuery remove(database_);
    remove.prepare(QStringLiteral(
        "DELETE FROM library_node_order WHERE parent_path_json=?"));
    remove.addBindValue(pathJson);
    if (!remove.exec()) {
        database_.rollback();
        if (error) {
            *error = remove.lastError().text();
        }
        return false;
    }
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (qsizetype index = 0; index < childTitles.size(); ++index) {
        QSqlQuery insert(database_);
        insert.prepare(QStringLiteral(
            "INSERT INTO library_node_order(parent_path_json, child_title, sort_order, updated_at) "
            "VALUES(?, ?, ?, ?)"));
        insert.addBindValue(pathJson);
        insert.addBindValue(childTitles.at(index));
        insert.addBindValue(index);
        insert.addBindValue(now);
        if (!insert.exec()) {
            database_.rollback();
            if (error) {
                *error = insert.lastError().text();
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

bool SqliteLibraryRepository::deactivateForRemoval(
    const QStringList &nodePath,
    const QStringList &locallyHiddenBankIds,
    const QStringList &sharedSourceKeys,
    QString *error)
{
    if (error) {
        error->clear();
    }
    if (nodePath.isEmpty()
        || (locallyHiddenBankIds.isEmpty() && sharedSourceKeys.isEmpty())) {
        if (error) {
            *error = QStringLiteral("Library removal does not identify any banks");
        }
        return false;
    }
    QSet<QString> uniqueBankIds;
    for (const QString &bankId : locallyHiddenBankIds) {
        if (bankId.trimmed().isEmpty() || uniqueBankIds.contains(bankId)) {
            if (error) {
                *error = QStringLiteral("Library removal contains invalid bank IDs");
            }
            return false;
        }
        uniqueBankIds.insert(bankId);
    }
    QSet<QString> uniqueSourceKeys;
    for (const QString &sourceKey : sharedSourceKeys) {
        if (sourceKey.trimmed().isEmpty() || uniqueSourceKeys.contains(sourceKey)) {
            if (error) {
                *error = QStringLiteral("Library removal contains invalid source keys");
            }
            return false;
        }
        uniqueSourceKeys.insert(sourceKey);
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    const QString pathJson = parentPathJson(nodePath);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const QString &bankId : locallyHiddenBankIds) {
        QSqlQuery insert(database_);
        insert.prepare(QStringLiteral(
            "INSERT INTO library_hidden_banks(bank_id, path_json, hidden_at) "
            "SELECT id, ?, ? FROM banks WHERE id=? AND active=1 "
            "ON CONFLICT(bank_id) DO UPDATE SET path_json=excluded.path_json, "
            "hidden_at=excluded.hidden_at"));
        insert.addBindValue(pathJson);
        insert.addBindValue(now);
        insert.addBindValue(bankId);
        if (!insert.exec() || insert.numRowsAffected() != 1) {
            database_.rollback();
            if (error) {
                *error = insert.lastError().text().isEmpty()
                    ? QStringLiteral("Library bank is unavailable")
                    : insert.lastError().text();
            }
            return false;
        }
        QSqlQuery deactivate(database_);
        deactivate.prepare(QStringLiteral("UPDATE banks SET active=0 WHERE id=?"));
        deactivate.addBindValue(bankId);
        if (!deactivate.exec()) {
            database_.rollback();
            if (error) {
                *error = deactivate.lastError().text();
            }
            return false;
        }
    }
    for (const QString &sourceKey : sharedSourceKeys) {
        QSqlQuery deactivateSource(database_);
        deactivateSource.prepare(QStringLiteral(
            "UPDATE bank_sources SET available=0, last_synced_at=? WHERE source_key=?"));
        deactivateSource.addBindValue(now);
        deactivateSource.addBindValue(sourceKey);
        if (!deactivateSource.exec() || deactivateSource.numRowsAffected() != 1) {
            database_.rollback();
            if (error) {
                *error = deactivateSource.lastError().text().isEmpty()
                    ? QStringLiteral("Managed bank source is unavailable")
                    : deactivateSource.lastError().text();
            }
            return false;
        }
        QSqlQuery deactivateBank(database_);
        deactivateBank.prepare(QStringLiteral(
            "UPDATE banks SET active=0 WHERE id=("
            "SELECT bank_id FROM bank_sources WHERE source_key=?)"));
        deactivateBank.addBindValue(sourceKey);
        if (!deactivateBank.exec()) {
            database_.rollback();
            if (error) {
                *error = deactivateBank.lastError().text();
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

QVector<HiddenLibraryNode> SqliteLibraryRepository::hiddenNodes(QString *error) const
{
    if (error) {
        error->clear();
    }
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT path_json, COUNT(*), MIN(hidden_at) FROM library_hidden_banks "
            "GROUP BY path_json ORDER BY MIN(hidden_at) DESC"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return {};
    }
    QVector<HiddenLibraryNode> nodes;
    while (query.next()) {
        HiddenLibraryNode node;
        node.path = pathFromJson(query.value(0).toString());
        if (node.path.isEmpty()) {
            continue;
        }
        node.bankCount = query.value(1).toLongLong();
        node.hiddenAt = QDateTime::fromString(query.value(2).toString(), Qt::ISODateWithMs);
        nodes.append(node);
    }
    return nodes;
}

bool SqliteLibraryRepository::restoreHiddenNode(
    const QStringList &nodePath,
    QString *error)
{
    if (error) {
        error->clear();
    }
    if (nodePath.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Hidden library path is invalid");
        }
        return false;
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }
    const QString pathJson = parentPathJson(nodePath);
    QSqlQuery restore(database_);
    restore.prepare(QStringLiteral(
        "UPDATE banks SET active=1 WHERE id IN ("
        "SELECT bank_id FROM library_hidden_banks WHERE path_json=?)"));
    restore.addBindValue(pathJson);
    if (!restore.exec() || restore.numRowsAffected() < 1) {
        database_.rollback();
        if (error) {
            *error = restore.lastError().text().isEmpty()
                ? QStringLiteral("Hidden library entry no longer exists")
                : restore.lastError().text();
        }
        return false;
    }
    QSqlQuery remove(database_);
    remove.prepare(QStringLiteral(
        "DELETE FROM library_hidden_banks WHERE path_json=?"));
    remove.addBindValue(pathJson);
    if (!remove.exec()) {
        database_.rollback();
        if (error) {
            *error = remove.lastError().text();
        }
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
