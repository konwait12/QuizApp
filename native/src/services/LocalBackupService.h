#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <functional>

class QSettings;

namespace quizapp::services {

struct BackupEntryInfo {
    QString path;
    qint64 size = 0;
    QByteArray sha256;
};

struct BackupInspection {
    bool valid = false;
    int schemaVersion = 0;
    QString appVersion;
    QString buildCommit;
    QString platform;
    QDateTime createdAt;
    bool includesSecrets = false;
    qint64 payloadBytes = 0;
    QJsonObject counts;
    QVector<BackupEntryInfo> entries;
    QString error;
};

using BackupProgress = std::function<void(
    const QString &stage,
    qint64 completed,
    qint64 total)>;

class LocalBackupService final {
public:
    static constexpr int kSchemaVersion = 2;

    bool create(
        const QString &destinationPath,
        QSqlDatabase database,
        const QString &databasePath,
        const QString &dataRoot,
        const QString &sharedRoot,
        QSettings &settings,
        bool includeSecrets,
        const QString &appVersion,
        const QString &buildCommit,
        BackupProgress progress = {},
        QString *error = nullptr) const;

    BackupInspection inspect(
        const QString &archivePath,
        bool verifyPayload = true,
        BackupProgress progress = {}) const;

    bool stageRestore(
        const QString &archivePath,
        const QString &dataRoot,
        BackupProgress progress = {},
        QString *error = nullptr) const;

    bool hasPendingRestore(const QString &dataRoot) const;

    bool applyPendingRestore(
        const QString &dataRoot,
        const QString &databasePath,
        const QString &sharedRoot,
        QSettings &settings,
        QString *error = nullptr) const;

    static QString suggestedFileName(const QDateTime &time = QDateTime::currentDateTime());
    static bool isSensitiveSettingKey(const QString &key);
};

} // namespace quizapp::services
