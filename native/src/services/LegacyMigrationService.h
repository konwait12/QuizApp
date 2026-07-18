#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSettings>
#include <QString>

#include <functional>

namespace quizapp::services {

enum class LegacyMigrationStatus {
    Imported,
    AlreadyImported,
    InvalidPackage,
    Failed,
};

struct LegacyMigrationSummary {
    int localStorageRecords = 0;
    int indexedDbRecords = 0;
    int studyDays = 0;
    int pendingQuestionRecords = 0;
    int resolvedQuestionRecords = 0;
    int practiceSessions = 0;
    int notebookRecords = 0;
    int redactedSecrets = 0;
};

struct LegacyMigrationResult {
    LegacyMigrationStatus status = LegacyMigrationStatus::InvalidPackage;
    QString migrationId;
    QByteArray sourceHash;
    QString sourceVersion;
    QDateTime exportedAt;
    LegacyMigrationSummary summary;
    QString error;

    bool imported() const { return status == LegacyMigrationStatus::Imported; }
};

class LegacyMigrationService final {
public:
    using ImportGate = std::function<bool(const QString &stage)>;

    LegacyMigrationResult importPackage(
        const QByteArray &json,
        QSqlDatabase database,
        const ImportGate &gate = {}) const;
    bool applyUiSettings(
        QSqlDatabase database,
        QSettings &settings,
        int *appliedCount = nullptr,
        QString *error = nullptr) const;
};

} // namespace quizapp::services
