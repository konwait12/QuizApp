#pragma once

#include <QSqlDatabase>
#include <QString>

namespace quizapp::storage {

class Database final {
public:
    explicit Database(QString connectionName);
    ~Database();

    bool open(const QString &path, QString *error = nullptr);
    bool migrate(QString *error = nullptr);
    QSqlDatabase connection() const;

private:
    bool executeScript(const QString &script, QString *error);
    bool migrationApplied(int version, bool *applied, QString *error) const;

    QString connectionName_;
};

} // namespace quizapp::storage
