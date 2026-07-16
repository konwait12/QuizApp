#pragma once

#include "domain/LibraryStats.h"

#include <QSqlDatabase>
#include <QString>

namespace quizapp::storage {

class SqliteLibraryRepository final {
public:
    explicit SqliteLibraryRepository(QSqlDatabase database);

    domain::LibraryStats stats(QString *error = nullptr) const;

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
