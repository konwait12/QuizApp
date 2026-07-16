#include "storage/SqliteLibraryRepository.h"

#include <QSqlError>
#include <QSqlQuery>

namespace quizapp::storage {

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
            "SELECT (SELECT COUNT(*) FROM banks), "
            "(SELECT COUNT(*) FROM questions WHERE active=1), "
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

} // namespace quizapp::storage
