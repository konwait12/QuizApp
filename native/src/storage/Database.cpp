#include "storage/Database.h"

#include <QDateTime>
#include <QFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>
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
    const std::array<Migration, 11> migrations{{
        {1, ":/quizapp/schema/001_initial.sql"},
        {2, ":/quizapp/schema/002_question_paths.sql"},
        {3, ":/quizapp/schema/003_review_history.sql"},
        {4, ":/quizapp/schema/004_managed_bank_sources.sql"},
        {5, ":/quizapp/schema/005_legacy_migration_staging.sql"},
        {6, ":/quizapp/schema/006_question_answer_state.sql"},
        {7, ":/quizapp/schema/007_library_node_order.sql"},
        {8, ":/quizapp/schema/008_library_hidden_banks.sql"},
        {9, ":/quizapp/schema/009_managed_bank_overrides.sql"},
        {10, ":/quizapp/schema/010_exam_completion.sql"},
        {11, ":/quizapp/schema/011_notebook_recycle.sql"},
    }};

    for (const Migration &migration : migrations) {
        bool applied = false;
        if (!migrationApplied(migration.version, &applied, error)) {
            return false;
        }
        if (applied) {
            continue;
        }
        if (migration.version == 10) {
            QSet<QString> columns;
            QSqlQuery columnQuery(connection());
            if (columnQuery.exec(QStringLiteral("PRAGMA table_info(exam_sessions)"))) {
                while (columnQuery.next()) {
                    columns.insert(columnQuery.value(1).toString());
                }
            }
            QSqlQuery resultTableQuery(connection());
            const bool hasResultTable = resultTableQuery.exec(QStringLiteral(
                "SELECT 1 FROM sqlite_master WHERE type='table' "
                "AND name='exam_result_items'"))
                && resultTableQuery.next();
            const QStringList requiredColumns{
                QStringLiteral("title"), QStringLiteral("current_index"),
                QStringLiteral("remaining_seconds"), QStringLiteral("paused"),
                QStringLiteral("correct_count"), QStringLiteral("wrong_count"),
                QStringLiteral("unanswered_count"), QStringLiteral("timed_out"),
                QStringLiteral("updated_at"),
            };
            const bool hasAllColumns = std::all_of(
                requiredColumns.cbegin(), requiredColumns.cend(),
                [&columns](const QString &column) { return columns.contains(column); });
            if (hasAllColumns && hasResultTable) {
                QSqlQuery repair(connection());
                repair.prepare(QStringLiteral(
                    "INSERT OR IGNORE INTO schema_migrations(version, applied_at) "
                    "VALUES(10, ?)"));
                repair.addBindValue(
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                if (!repair.exec()) {
                    if (error) {
                        *error = repair.lastError().text();
                    }
                    return false;
                }
                continue;
            }
        }
        if (migration.version == 11) {
            bool hasDeletedAt = false;
            QSqlQuery columnQuery(connection());
            if (columnQuery.exec(QStringLiteral("PRAGMA table_info(notebooks)"))) {
                while (columnQuery.next()) {
                    hasDeletedAt = hasDeletedAt
                        || columnQuery.value(1).toString() == QStringLiteral("deleted_at");
                }
            }
            if (hasDeletedAt) {
                QSqlQuery repair(connection());
                if (!repair.exec(QStringLiteral(
                        "CREATE INDEX IF NOT EXISTS idx_notebooks_free_updated "
                        "ON notebooks(question_id, deleted_at, updated_at DESC)"))) {
                    if (error) *error = repair.lastError().text();
                    return false;
                }
                repair.prepare(QStringLiteral(
                    "INSERT OR IGNORE INTO schema_migrations(version, applied_at) VALUES(11, ?)"));
                repair.addBindValue(
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                if (!repair.exec()) {
                    if (error) *error = repair.lastError().text();
                    return false;
                }
                continue;
            }
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
