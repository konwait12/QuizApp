#pragma once

#include "domain/LibraryStats.h"

#include <QSqlDatabase>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace quizapp::storage {

struct HiddenLibraryNode {
    QStringList path;
    qsizetype bankCount = 0;
    QDateTime hiddenAt;
};

class SqliteLibraryRepository final {
public:
    explicit SqliteLibraryRepository(QSqlDatabase database);

    domain::LibraryStats stats(QString *error = nullptr) const;
    QStringList childOrder(
        const QStringList &parentPath,
        QString *error = nullptr) const;
    bool setChildOrder(
        const QStringList &parentPath,
        const QStringList &childTitles,
        QString *error = nullptr);
    bool deactivateForRemoval(
        const QStringList &nodePath,
        const QStringList &locallyHiddenBankIds,
        const QStringList &sharedSourceKeys,
        QString *error = nullptr);
    QVector<HiddenLibraryNode> hiddenNodes(QString *error = nullptr) const;
    bool restoreHiddenNode(
        const QStringList &nodePath,
        QString *error = nullptr);

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
