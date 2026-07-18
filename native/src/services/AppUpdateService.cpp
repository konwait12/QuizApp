#include "services/AppUpdateService.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <limits>

namespace quizapp::services {
namespace {

bool fail(const QString &message, QString *error)
{
    if (error) {
        *error = message;
    }
    return false;
}

QString normalizedVersion(QString value)
{
    value = value.trimmed();
    while (value.startsWith(u'v', Qt::CaseInsensitive)) {
        value.remove(0, 1);
    }
    const qsizetype buildIndex = value.indexOf(u'+');
    if (buildIndex >= 0) {
        value.truncate(buildIndex);
    }
    return value;
}

struct ParsedVersion {
    QVector<int> core;
    QStringList prerelease;
};

ParsedVersion parseVersion(const QString &source)
{
    const QString normalized = normalizedVersion(source);
    const qsizetype dash = normalized.indexOf(u'-');
    const QString coreText = dash >= 0 ? normalized.left(dash) : normalized;
    const QString prereleaseText = dash >= 0 ? normalized.mid(dash + 1) : QString{};
    ParsedVersion parsed;
    for (const QString &part : coreText.split(u'.', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match =
            QRegularExpression(QStringLiteral("^(\\d+)")).match(part);
        parsed.core.append(match.hasMatch() ? match.captured(1).toInt() : 0);
    }
    while (parsed.core.size() < 3) {
        parsed.core.append(0);
    }
    parsed.prerelease = prereleaseText.split(u'.', Qt::SkipEmptyParts);
    return parsed;
}

int compareIdentifier(const QString &left, const QString &right)
{
    bool leftNumeric = false;
    bool rightNumeric = false;
    const qlonglong leftNumber = left.toLongLong(&leftNumeric);
    const qlonglong rightNumber = right.toLongLong(&rightNumeric);
    if (leftNumeric && rightNumeric) {
        return leftNumber == rightNumber ? 0 : leftNumber < rightNumber ? -1 : 1;
    }
    if (leftNumeric != rightNumeric) {
        return leftNumeric ? -1 : 1;
    }
    return QString::compare(left, right, Qt::CaseInsensitive);
}

bool secureUrl(const QString &value)
{
    return value.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

int assetScore(const AppUpdateAsset &asset, AppUpdatePlatform platform)
{
    const QString name = asset.name.toLower();
    const QString suffix = platform == AppUpdatePlatform::Android
        ? QStringLiteral(".apk") : QStringLiteral(".exe");
    if (!name.endsWith(suffix) || !secureUrl(asset.downloadUrl)) {
        return std::numeric_limits<int>::min();
    }
    int score = 10;
    if (platform == AppUpdatePlatform::Android) {
        if (name.contains(QStringLiteral("arm64"))
            || name.contains(QStringLiteral("aarch64"))) {
            score += 8;
        }
        if (name.contains(QStringLiteral("x86"))) {
            score -= 20;
        }
    } else if (name.contains(QStringLiteral("setup"))
               || name.contains(QStringLiteral("install"))) {
        score += 4;
    }
    if (name.contains(QStringLiteral("debug"))) {
        score -= 8;
    }
    return score;
}

} // namespace

bool AppUpdateService::parseLatestRelease(
    const QByteArray &json,
    AppReleaseInfo *release,
    QString *error)
{
    if (!release) {
        return fail(QStringLiteral("更新元数据目标为空"), error);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("GitHub Release 响应格式无效"), error);
    }
    const QJsonObject object = document.object();
    AppReleaseInfo parsed;
    parsed.tagName = object.value(QStringLiteral("tag_name")).toString(
        object.value(QStringLiteral("tagName")).toString()).trimmed();
    parsed.targetCommit = object.value(QStringLiteral("target_commitish")).toString(
        object.value(QStringLiteral("targetCommitish")).toString()).trimmed();
    parsed.releasePageUrl = object.value(QStringLiteral("html_url")).toString(
        object.value(QStringLiteral("url")).toString()).trimmed();
    parsed.title = object.value(QStringLiteral("name")).toString(parsed.tagName).trimmed();
    parsed.body = object.value(QStringLiteral("body")).toString().trimmed();
    if (parsed.tagName.isEmpty()) {
        return fail(QStringLiteral("GitHub Release 缺少版本号"), error);
    }
    if (!parsed.releasePageUrl.isEmpty() && !secureUrl(parsed.releasePageUrl)) {
        parsed.releasePageUrl.clear();
    }
    for (const QJsonValue &value : object.value(QStringLiteral("assets")).toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject assetObject = value.toObject();
        AppUpdateAsset asset;
        asset.id = static_cast<qint64>(assetObject.value(QStringLiteral("id")).toDouble());
        asset.name = assetObject.value(QStringLiteral("name")).toString().trimmed();
        asset.downloadUrl = assetObject.value(
            QStringLiteral("browser_download_url")).toString(
                assetObject.value(QStringLiteral("url")).toString()).trimmed();
        asset.updatedAt = assetObject.value(QStringLiteral("updated_at")).toString().trimmed();
        asset.digest = assetObject.value(QStringLiteral("digest")).toString().trimmed();
        asset.size = static_cast<qint64>(
            assetObject.value(QStringLiteral("size")).toDouble());
        if (!asset.name.isEmpty() && secureUrl(asset.downloadUrl)) {
            parsed.assets.append(asset);
        }
    }
    *release = parsed;
    if (error) {
        error->clear();
    }
    return true;
}

int AppUpdateService::compareVersionTags(const QString &left, const QString &right)
{
    const ParsedVersion leftVersion = parseVersion(left);
    const ParsedVersion rightVersion = parseVersion(right);
    const qsizetype coreCount = std::max(leftVersion.core.size(), rightVersion.core.size());
    for (qsizetype index = 0; index < coreCount; ++index) {
        const int leftPart = index < leftVersion.core.size() ? leftVersion.core.at(index) : 0;
        const int rightPart = index < rightVersion.core.size() ? rightVersion.core.at(index) : 0;
        if (leftPart != rightPart) {
            return leftPart < rightPart ? -1 : 1;
        }
    }
    if (leftVersion.prerelease.isEmpty() != rightVersion.prerelease.isEmpty()) {
        return leftVersion.prerelease.isEmpty() ? 1 : -1;
    }
    const qsizetype preCount = std::max(
        leftVersion.prerelease.size(), rightVersion.prerelease.size());
    for (qsizetype index = 0; index < preCount; ++index) {
        if (index >= leftVersion.prerelease.size()) {
            return -1;
        }
        if (index >= rightVersion.prerelease.size()) {
            return 1;
        }
        const int compared = compareIdentifier(
            leftVersion.prerelease.at(index), rightVersion.prerelease.at(index));
        if (compared != 0) {
            return compared < 0 ? -1 : 1;
        }
    }
    return 0;
}

std::optional<AppUpdateAsset> AppUpdateService::selectAsset(
    const AppReleaseInfo &release,
    AppUpdatePlatform platform)
{
    std::optional<AppUpdateAsset> selected;
    int selectedScore = std::numeric_limits<int>::min();
    for (const AppUpdateAsset &asset : release.assets) {
        const int score = assetScore(asset, platform);
        if (score > selectedScore) {
            selected = asset;
            selectedScore = score;
        }
    }
    return selectedScore == std::numeric_limits<int>::min()
        ? std::nullopt : selected;
}

AppUpdateDecision AppUpdateService::evaluate(
    const AppReleaseInfo &release,
    const QString &currentVersion,
    const QString &currentBuildCommit,
    AppUpdatePlatform platform)
{
    AppUpdateDecision decision;
    decision.asset = selectAsset(release, platform);
    decision.fingerprint = releaseFingerprint(release, decision.asset);
    const int versionComparison = compareVersionTags(release.tagName, currentVersion);
    if (versionComparison > 0) {
        decision.updateAvailable = true;
        decision.reason = QStringLiteral("发现更高版本号");
        return decision;
    }
    if (versionComparison < 0) {
        decision.reason = QStringLiteral("远程版本低于当前版本");
        return decision;
    }
    const QString currentCommit = currentBuildCommit.trimmed().toLower();
    const QString releaseCommit = release.targetCommit.trimmed().toLower();
    if (!currentCommit.isEmpty() && currentCommit != QStringLiteral("dev")
        && !releaseCommit.isEmpty() && currentCommit != releaseCommit) {
        decision.updateAvailable = true;
        decision.reason = QStringLiteral("同版本 Release 构建已变化");
    } else {
        decision.reason = QStringLiteral("版本号与构建一致");
    }
    return decision;
}

QString AppUpdateService::releaseFingerprint(
    const AppReleaseInfo &release,
    const std::optional<AppUpdateAsset> &asset)
{
    QByteArray source = release.tagName.toUtf8() + '\n'
        + release.targetCommit.toUtf8() + '\n';
    if (asset) {
        source += QByteArray::number(asset->id) + '\n'
            + asset->name.toUtf8() + '\n'
            + QByteArray::number(asset->size) + '\n'
            + asset->updatedAt.toUtf8() + '\n'
            + asset->digest.toUtf8();
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
}

bool AppUpdateService::verifyDownloadedPackage(
    const AppUpdateAsset &asset,
    qint64 downloadedSize,
    const QByteArray &sha256,
    QString *error)
{
    if (downloadedSize <= 0) {
        return fail(QStringLiteral("下载文件为空"), error);
    }
    if (asset.size > 0 && asset.size != downloadedSize) {
        return fail(QStringLiteral("下载文件大小与 Release 不一致"), error);
    }
    if (!asset.digest.isEmpty()) {
        QString expected = asset.digest.trimmed().toLower();
        expected.remove(QStringLiteral("sha256:"));
        if (expected != QString::fromLatin1(sha256.toHex()).toLower()) {
            return fail(QStringLiteral("下载文件 SHA-256 校验失败"), error);
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace quizapp::services
