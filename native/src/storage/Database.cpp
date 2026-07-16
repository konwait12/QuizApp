#include "storage/Database.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <array>
#include <utility>

void initializeQuizAppNativeResources()
{
    Q_INIT_RESOURCE(quizapp_native);
}

namespace quizapp::storage {

Database::Database(QString connectionName)
    : connectionName_(std::move(connectionName))
{
}

Database::~Database()
{
    if (QSqlDatabase::contains(connectionName_)) {
        QSqlDatabase database = QSqlDatabase::database(connectionName_, false);
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool Database::open(const QString &path, QString *error)
{
    if (error) {
        error->clear();
    }
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database.setDatabaseName(path);
    if (!database.open()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }
    const std::array<QString, 3> pragmas{{
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral("PRAGMA journal_mode = WAL"),
        QStringLiteral("PRAGMA synchronous = NORMAL"),
    }};
    for (const QString &statement : pragmas) {
        QSqlQuery pragma(database);
        if (!pragma.exec(statement)) {
            if (error) {
                *error = pragma.lastError().text();
            }
            database.close();
            return false;
        }
    }
    return true;
}

bool Database::migrate(QString *error)
{
    if (error) {
        error->clear();
    }
    initializeQuizAppNativeResources();
    struct Migration {
        int version;
        const char *resourcePath;
    };
    const std::array<Migration, 3> migrations{{
        {1, ":/quizapp/schema/001_initial.sql"},
        {2, ":/quizapp/schema/002_question_paths.sql"},
        {3, ":/quizapp/schema/003_review_history.sql"},
    }};

    for (const Migration &migration : migrations) {
        bool applied = false;
        if (!migrationApplied(migration.version, &applied, error)) {
            return false;
        }
        if (applied) {
            continue;
        }
        QFile file(QString::fromUtf8(migration.resourcePath));
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = QStringLiteral("Cannot open embedded database migration %1")
                    .arg(migration.version);
            }
            return false;
        }
        if (!executeScript(QString::fromUtf8(file.readAll()), error)) {
            return false;
        }
    }
    return true;
}

QSqlDatabase Database::connection() const
{
    return QSqlDatabase::database(connectionName_);
}

bool Database::executeScript(const QString &script, QString *error)
{
    QSqlDatabase database = connection();
    if (!database.transaction()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }
    const QStringList statements = script.split(u';', Qt::SkipEmptyParts);
    for (const QString &statement : statements) {
        const QString trimmed = statement.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        QSqlQuery query(database);
        if (!query.exec(trimmed)) {
            database.rollback();
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }
    if (!database.commit()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }
    return true;
}

bool Database::migrationApplied(int version, bool *applied, QString *error) const
{
    *applied = false;
    QSqlDatabase database = connection();
    QSqlQuery tableQuery(database);
    if (!tableQuery.exec(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='schema_migrations'"))) {
        if (error) {
            *error = tableQuery.lastError().text();
        }
        return false;
    }
    if (!tableQuery.next()) {
        return true;
    }

    QSqlQuery versionQuery(database);
    versionQuery.prepare(QStringLiteral(
        "SELECT 1 FROM schema_migrations WHERE version = ?"));
    versionQuery.addBindValue(version);
    if (!versionQuery.exec()) {
        if (error) {
            *error = versionQuery.lastError().text();
        }
        return false;
    }
    *applied = versionQuery.next();
    return true;
}

} // namespace quizapp::storage
