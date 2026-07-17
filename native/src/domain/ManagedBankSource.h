#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace quizapp::domain {

struct ManagedBankSource {
    QString sourceKey;
    QString managedRoot;
    QString relativePath;
    QString bankId;
    qint64 fileSize = 0;
    qint64 modifiedMsecs = 0;
    QByteArray sha256;
    bool available = true;
    QString lastError;
    QDateTime lastSyncedAt;
};

} // namespace quizapp::domain
