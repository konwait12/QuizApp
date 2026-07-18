#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace quizapp::services {

struct ManagedBankFile {
    QString absolutePath;
    QString relativePath;
    QString sourceKey;
    QStringList hierarchy;
    qint64 fileSize = 0;
    qint64 modifiedMsecs = 0;
    QByteArray sha256;
};

struct BankSyncIssue {
    QString relativePath;
    QString message;
};

struct BankDirectoryScanResult {
    QVector<ManagedBankFile> files;
    QVector<BankSyncIssue> issues;
    QString error;
};

struct BankDirectorySyncResult {
    int discoveredFiles = 0;
    int installedFiles = 0;
    int updatedFiles = 0;
    int unchangedFiles = 0;
    int restoredFiles = 0;
    int relocatedFiles = 0;
    int missingFiles = 0;
    qsizetype installedQuestions = 0;
    bool canceled = false;
    QVector<BankSyncIssue> issues;
    QString error;

    bool succeeded() const { return error.isEmpty() && !canceled; }
};

class BankDirectorySyncService final {
public:
    using ProgressCallback = std::function<bool(int current, int total, const QString &relativePath)>;

    BankDirectoryScanResult scan(const QString &questionBanksRoot) const;
    BankDirectorySyncResult synchronize(
        const QString &questionBanksRoot,
        const QString &databasePath,
        const QString &dataRoot,
        bool force = false,
        const ProgressCallback &progress = {}) const;

    static QStringList hierarchyForRelativePath(const QString &relativePath);
};

} // namespace quizapp::services
