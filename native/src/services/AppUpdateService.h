#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace quizapp::services {

enum class AppUpdatePlatform {
    Android,
    Windows,
};

struct AppUpdateAsset {
    qint64 id = 0;
    QString name;
    QString downloadUrl;
    QString updatedAt;
    QString digest;
    qint64 size = 0;
};

struct AppReleaseInfo {
    QString tagName;
    QString targetCommit;
    QString releasePageUrl;
    QString title;
    QString body;
    QVector<AppUpdateAsset> assets;
};

struct AppUpdateDecision {
    bool updateAvailable = false;
    QString fingerprint;
    QString reason;
    std::optional<AppUpdateAsset> asset;
};

class AppUpdateService
{
public:
    static bool parseLatestRelease(
        const QByteArray &json,
        AppReleaseInfo *release,
        QString *error = nullptr);
    static int compareVersionTags(const QString &left, const QString &right);
    static std::optional<AppUpdateAsset> selectAsset(
        const AppReleaseInfo &release,
        AppUpdatePlatform platform);
    static AppUpdateDecision evaluate(
        const AppReleaseInfo &release,
        const QString &currentVersion,
        const QString &currentBuildCommit,
        AppUpdatePlatform platform);
    static QString releaseFingerprint(
        const AppReleaseInfo &release,
        const std::optional<AppUpdateAsset> &asset);
    static bool verifyDownloadedPackage(
        const AppUpdateAsset &asset,
        qint64 downloadedSize,
        const QByteArray &sha256,
        QString *error = nullptr);
};

} // namespace quizapp::services
