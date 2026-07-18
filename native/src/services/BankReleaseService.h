#pragma once

#include "services/SharedStorageFileService.h"
#include "services/SharedStorageService.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace quizapp::services {

struct ReleaseAssetMetadata {
    QString name;
    QString downloadUrl;
    qint64 byteSize = -1;
    QByteArray sha256;
    qint64 apiId = -1;
    QString updatedAt;
};

struct BankReleaseMetadata {
    QString tagName;
    QString releasePageUrl;
    QVector<ReleaseAssetMetadata> assets;
    int manifestAssetIndex = -1;
};

struct BankReleaseEntry {
    QString id;
    QString name;
    QStringList path;
    qsizetype questionCount = 0;
    QString assetName;
    QString downloadUrl;
    qint64 expectedByteSize = -1;
    QByteArray expectedSha256;
    qint64 assetApiId = -1;
    QString assetUpdatedAt;
    QByteArray embeddedPayload;
};

struct BankReleaseCatalog {
    QString tagName;
    QString releasePageUrl;
    QVector<BankReleaseEntry> entries;
};

enum class BankReleaseConflictPolicy {
    Overwrite,
    KeepBoth,
    Skip,
};

struct BankReleaseSelection {
    BankReleaseEntry entry;
    BankReleaseConflictPolicy conflictPolicy = BankReleaseConflictPolicy::Overwrite;
};

struct BankReleaseInstallResult {
    int installedEntries = 0;
    int skippedEntries = 0;
    QStringList destinationPaths;
    QString error;

    bool succeeded() const { return error.isEmpty(); }
};

class BankReleaseService final {
public:
    static QStringList manifestAssetNames();
    static bool parseReleaseMetadata(
        const QByteArray &releaseJson,
        BankReleaseMetadata *metadata,
        QString *error = nullptr);
    static bool buildCatalog(
        const BankReleaseMetadata &metadata,
        const QByteArray &manifestJson,
        BankReleaseCatalog *catalog,
        QString *error = nullptr);
    static bool verifyPayload(
        const BankReleaseEntry &entry,
        const QByteArray &payload,
        QString *error = nullptr);
    static QString destinationPath(
        const SharedStorageLayout &layout,
        const BankReleaseEntry &entry);

    BankReleaseInstallResult install(
        const SharedStorageLayout &layout,
        const QString &releaseTag,
        const QVector<BankReleaseSelection> &selections,
        const QHash<QString, QByteArray> &payloads) const;
};

} // namespace quizapp::services
