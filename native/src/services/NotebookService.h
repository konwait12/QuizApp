#pragma once

#include "domain/Notebook.h"
#include "storage/SqliteNotebookRepository.h"

#include <QVector>

#include <optional>

namespace quizapp::services {

class NotebookService final {
public:
    NotebookService(QSqlDatabase database, QString dataRoot);

    std::optional<domain::NotebookRecord> createFree(
        const QString &title,
        QString *error = nullptr) const;
    QVector<domain::NotebookRecord> listFree(
        bool recycled,
        QString *error = nullptr) const;
    bool rename(const QUuid &id, const QString &title, QString *error = nullptr) const;
    bool recycle(const QUuid &id, QString *error = nullptr) const;
    bool restore(const QUuid &id, QString *error = nullptr) const;
    bool permanentlyDelete(const QUuid &id, QString *error = nullptr) const;
    bool markSaved(const QUuid &id, QString *error = nullptr) const;
    QString absoluteBundlePath(const domain::NotebookRecord &record) const;

private:
    QString normalizedTitle(const QString &title) const;
    bool safeRelativePath(const QString &path) const;

    storage::SqliteNotebookRepository repository_;
    QString dataRoot_;
};

} // namespace quizapp::services
