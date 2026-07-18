#pragma once

#include "domain/AiRecord.h"

#include <QSqlDatabase>

#include <optional>

namespace quizapp::storage {

class SqliteAiRecordRepository final {
public:
    explicit SqliteAiRecordRepository(QSqlDatabase database);

    std::optional<domain::AiRecord> find(
        const QString &recordType,
        const QString &sourceId,
        QString *error = nullptr) const;
    bool upsert(const domain::AiRecord &record, QString *error = nullptr) const;

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
