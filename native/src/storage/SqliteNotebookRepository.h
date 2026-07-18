#pragma once

#include "domain/Notebook.h"

#include <QSqlDatabase>
#include <QVector>

#include <optional>

namespace quizapp::storage {

class SqliteNotebookRepository final {
public:
    explicit SqliteNotebookRepository(QSqlDatabase database);

    bool create(const domain::NotebookRecord &record, QString *error = nullptr) const;
    QVector<domain::NotebookRecord> listFree(
        bool recycled,
        QString *error = nullptr) const;
    std::optional<domain::NotebookRecord> findById(
        const QUuid &id,
        QString *error = nullptr) const;
    bool rename(const QUuid &id, const QString &title, QString *error = nullptr) const;
    bool setRecycled(const QUuid &id, bool recycled, QString *error = nullptr) const;
    bool touch(
        const QUuid &id,
        const QByteArray &contentHash,
        QString *error = nullptr) const;
    bool remove(const QUuid &id, QString *error = nullptr) const;

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
