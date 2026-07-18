#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUuid>

#include <optional>

namespace quizapp::domain {

struct NotebookRecord {
    QUuid id;
    std::optional<QUuid> questionId;
    QString title;
    int formatVersion = 3;
    QString relativePath;
    QByteArray contentHash;
    bool completed = false;
    QDateTime createdAt;
    QDateTime updatedAt;
    std::optional<QDateTime> deletedAt;
};

} // namespace quizapp::domain
