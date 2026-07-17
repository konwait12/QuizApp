#pragma once

#include "domain/ManagedBankSource.h"

#include <QSqlDatabase>

#include <optional>

namespace quizapp::storage {

class SqliteBankSourceRepository final {
public:
    explicit SqliteBankSourceRepository(QSqlDatabase database);

    std::optional<domain::ManagedBankSource> findByKey(
        const QString &sourceKey,
        QString *error = nullptr) const;
    QVector<domain::ManagedBankSource> listByRoot(
        const QString &managedRoot,
        QString *error = nullptr) const;
    bool save(const domain::ManagedBankSource &source, QString *error = nullptr);
    bool setAvailability(
        const QString &sourceKey,
        bool available,
        QString *error = nullptr);

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
