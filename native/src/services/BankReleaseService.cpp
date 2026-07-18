#include "services/BankReleaseService.h"

#include "services/LegacyBankImporter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace quizapp::services {
namespace {

bool fail(const QString &message, QString *error)
{
    if (error) {
        *error = message;
    }
    return false;
}

QStringList normalizedPath(const QJsonValue &value)
{
    QStringList path;
    if (value.isArray()) {
        for (const QJsonValue &segment : value.toArray()) {
            const QString text = segment.toString().trimmed();
            if (!text.isEmpty()) {
                path.append(text);
            }
        }
    } else if (value.isString()) {
        for (const QString &segment : value.toString().split(u'/', Qt::SkipEmptyParts)) {
            if (!segment.trimmed().isEmpty()) {
                path.append(segment.trimmed());
            }
        }
    }
    return path;
}

QByteArray digestBytes(const QString &digest)
{
    const QString trimmed = digest.trimmed();
    const QString value = trimmed.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)
        ? trimmed.mid(7) : trimmed;
    const QByteArray decoded = QByteArray::fromHex(value.toLatin1());
    return decoded.size() == 32 ? decoded : QByteArray{};
}

bool validPathSegment(const QString &segment)
{
    static const QRegularExpression unsafe(QStringLiteral(R"([\\/:*?"<>|])"));
    const QString trimmed = segment.trimmed();
    return !trimmed.isEmpty() && trimmed != QStringLiteral(".")
        && trimmed != QStringLiteral("..") && !unsafe.match(trimmed).hasMatch();
}

QString safeTag(const QString &tag)
{
    QString value = tag;
    value.remove(QRegularExpression(QStringLiteral(R"([^0-9A-Za-z._-])")));
    return value.isEmpty() ? QStringLiteral("new") : value;
}

bool movePath(const QString &source, const QString &destination)
{
    return QFileInfo(source).isDir()
        ? QDir().rename(source, destination)
        : QFile::rename(source, destination);
}

void rollbackOperations(
    const QVector<QString> &destinations,
    const QVector<QString> &backups)
{
    for (qsizetype index = destinations.size(); index > 0; --index) {
        const qsizetype current = index - 1;
        QFile::remove(destinations.at(current));
        if (!backups.at(current).isEmpty() && QFileInfo::exists(backups.at(current))) {
            movePath(backups.at(current), destinations.at(current));
        }
    }
}

ReleaseAssetMetadata assetFromObject(const QJsonObject &object)
{
    ReleaseAssetMetadata asset;
    asset.name = object.value(QStringLiteral("name")).toString().trimmed();
    asset.downloadUrl = object.value(QStringLiteral("browser_download_url")).toString();
    if (asset.downloadUrl.isEmpty()) {
        asset.downloadUrl = object.value(QStringLiteral("url")).toString();
    }
    asset.byteSize = object.value(QStringLiteral("size")).toInteger(-1);
    asset.sha256 = digestBytes(object.value(QStringLiteral("digest")).toString());
    asset.apiId = object.value(QStringLiteral("id")).toInteger(-1);
    asset.updatedAt = object.value(QStringLiteral("updated_at")).toString();
    return asset;
}

QString entryId(
    const QString &assetName,
    const QStringList &path,
    const QString &name)
{
    const QByteArray source = assetName.toUtf8() + '\n'
        + path.join(u'/').toUtf8() + '\n' + name.toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex().left(20));
}

} // namespace

QStringList BankReleaseService::manifestAssetNames()
{
    return {
        QStringLiteral("quizapp-bank-manifest.json"),
        QStringLiteral("QuizApp-bank-manifest.json"),
        QStringLiteral("bank-manifest.json"),
    };
}

bool BankReleaseService::parseReleaseMetadata(
    const QByteArray &releaseJson,
    BankReleaseMetadata *metadata,
    QString *error)
{
    if (error) {
        error->clear();
    }
    if (!metadata) {
        return fail(QStringLiteral("Release metadata target is missing"), error);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(releaseJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("GitHub Release 响应格式无效"), error);
    }
    const QJsonObject root = document.object();
    BankReleaseMetadata parsed;
    parsed.tagName = root.value(QStringLiteral("tag_name")).toString().trimmed();
    parsed.releasePageUrl = root.value(QStringLiteral("html_url")).toString();
    if (parsed.tagName.isEmpty()) {
        return fail(QStringLiteral("GitHub Release 缺少版本号"), error);
    }
    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    const QStringList manifestNames = manifestAssetNames();
    for (const QJsonValue &value : assets) {
        if (!value.isObject()) {
            continue;
        }
        const ReleaseAssetMetadata asset = assetFromObject(value.toObject());
        if (asset.name.isEmpty() || asset.downloadUrl.isEmpty()) {
            continue;
        }
        if (parsed.manifestAssetIndex < 0 && manifestNames.contains(asset.name)) {
            parsed.manifestAssetIndex = parsed.assets.size();
        }
        parsed.assets.append(asset);
    }
    *metadata = parsed;
    return true;
}

bool BankReleaseService::buildCatalog(
    const BankReleaseMetadata &metadata,
    const QByteArray &manifestJson,
    BankReleaseCatalog *catalog,
    QString *error)
{
    if (error) {
        error->clear();
    }
    if (!catalog) {
        return fail(QStringLiteral("题库目录目标无效"), error);
    }
    QHash<QString, ReleaseAssetMetadata> assetsByName;
    for (const ReleaseAssetMetadata &asset : metadata.assets) {
        assetsByName.insert(asset.name, asset);
    }
    QVector<QJsonObject> objects;
    if (metadata.manifestAssetIndex >= 0) {
        QJsonParseError parseError;
        const QJsonDocument manifest = QJsonDocument::fromJson(manifestJson, &parseError);
        if (parseError.error != QJsonParseError::NoError
            || (!manifest.isObject() && !manifest.isArray())) {
            return fail(QStringLiteral("题库清单格式无效"), error);
        }
        QJsonArray entries = manifest.isArray()
            ? manifest.array() : manifest.object().value(QStringLiteral("banks")).toArray();
        for (const QJsonValue &value : entries) {
            if (value.isObject()) {
                objects.append(value.toObject());
            }
        }
    } else {
        for (const ReleaseAssetMetadata &asset : metadata.assets) {
            const QString lower = asset.name.toLower();
            if (lower.startsWith(QStringLiteral("quizapp-bank-"))
                && lower.endsWith(QStringLiteral(".json"))
                && !manifestAssetNames().contains(asset.name)) {
                objects.append(QJsonObject{{QStringLiteral("file"), asset.name}});
            }
        }
    }

    BankReleaseCatalog parsed;
    parsed.tagName = metadata.tagName;
    parsed.releasePageUrl = metadata.releasePageUrl;
    for (qsizetype index = 0; index < objects.size(); ++index) {
        const QJsonObject object = objects.at(index);
        QJsonObject embedded;
        if (object.value(QStringLiteral("bank")).isObject()) {
            embedded = object.value(QStringLiteral("bank")).toObject();
        } else if (object.value(QStringLiteral("data")).isObject()) {
            embedded = object.value(QStringLiteral("data")).toObject();
        } else if (object.value(QStringLiteral("questions")).isArray()) {
            embedded = object;
        }
        const QString assetName = object.value(QStringLiteral("file")).toString(
            object.value(QStringLiteral("asset")).toString(
                object.value(QStringLiteral("name")).toString())).trimmed();
        const auto asset = assetsByName.constFind(assetName);
        QStringList path = normalizedPath(object.value(QStringLiteral("path")));
        if (path.isEmpty() && !embedded.isEmpty()) {
            path = normalizedPath(embedded.value(QStringLiteral("path")));
        }
        const QString subject = object.value(QStringLiteral("subject")).toString(
            embedded.value(QStringLiteral("subject")).toString()).trimmed();
        const QString chapter = object.value(QStringLiteral("chapter")).toString(
            embedded.value(QStringLiteral("chapter")).toString()).trimmed();
        if (path.isEmpty() && (!subject.isEmpty() || !chapter.isEmpty())) {
            if (!subject.isEmpty()) {
                path.append(subject);
            }
            if (!chapter.isEmpty() && chapter != subject) {
                path.append(chapter);
            }
        }
        if (path.isEmpty()) {
            const QString fallback = QFileInfo(assetName).completeBaseName().trimmed();
            if (!fallback.isEmpty()) {
                path = {QStringLiteral("未分类"), fallback};
            }
        }
        if (path.isEmpty()
            || std::any_of(path.cbegin(), path.cend(), [](const QString &segment) {
                   return !validPathSegment(segment);
               })) {
            continue;
        }
        BankReleaseEntry entry;
        entry.assetName = assetName;
        entry.path = path;
        entry.name = object.value(QStringLiteral("name")).toString(
            embedded.value(QStringLiteral("name")).toString(path.constLast()));
        entry.questionCount = object.value(QStringLiteral("questionCount")).toInteger(
            embedded.value(QStringLiteral("questions")).toArray().size());
        entry.downloadUrl = object.value(QStringLiteral("url")).toString();
        entry.expectedByteSize = object.value(QStringLiteral("byteSize")).toInteger(
            object.value(QStringLiteral("size")).toInteger(-1));
        entry.expectedSha256 = digestBytes(
            object.value(QStringLiteral("sha256")).toString(
                object.value(QStringLiteral("digest")).toString()));
        if (asset != assetsByName.cend()) {
            entry.downloadUrl = asset->downloadUrl;
            entry.expectedByteSize = asset->byteSize;
            if (!asset->sha256.isEmpty()) {
                entry.expectedSha256 = asset->sha256;
            }
            entry.assetApiId = asset->apiId;
            entry.assetUpdatedAt = asset->updatedAt;
        }
        if (!embedded.isEmpty()) {
            entry.embeddedPayload = QJsonDocument(embedded).toJson(QJsonDocument::Compact);
        }
        if (entry.embeddedPayload.isEmpty() && entry.downloadUrl.isEmpty()) {
            continue;
        }
        entry.id = entryId(entry.assetName, entry.path, entry.name);
        parsed.entries.append(entry);
    }
    if (parsed.entries.isEmpty()) {
        return fail(QStringLiteral("当前 Release 没有可用题库"), error);
    }
    *catalog = parsed;
    return true;
}

bool BankReleaseService::verifyPayload(
    const BankReleaseEntry &entry,
    const QByteArray &payload,
    QString *error)
{
    if (error) {
        error->clear();
    }
    if (payload.isEmpty()) {
        return fail(QStringLiteral("题库文件为空：%1").arg(entry.name), error);
    }
    if (entry.expectedByteSize >= 0 && payload.size() != entry.expectedByteSize) {
        return fail(QStringLiteral("题库文件大小校验失败：%1").arg(entry.name), error);
    }
    if (!entry.expectedSha256.isEmpty()
        && QCryptographicHash::hash(payload, QCryptographicHash::Sha256)
            != entry.expectedSha256) {
        return fail(QStringLiteral("题库 SHA-256 校验失败：%1").arg(entry.name), error);
    }
    LegacyBankImporter importer;
    const auto imported = importer.importJson(
        payload,
        QStringLiteral("release:%1").arg(entry.id),
        entry.path);
    if (!imported.succeeded()) {
        return fail(QStringLiteral("题库内容校验失败：%1").arg(entry.name), error);
    }
    if (entry.questionCount > 0 && imported.acceptedQuestionCount != entry.questionCount) {
        return fail(QStringLiteral("题库题量校验失败：%1").arg(entry.name), error);
    }
    return true;
}

QString BankReleaseService::destinationPath(
    const SharedStorageLayout &layout,
    const BankReleaseEntry &entry)
{
    if (!layout.ready() || entry.path.isEmpty()
        || std::any_of(entry.path.cbegin(), entry.path.cend(), [](const QString &segment) {
               return !validPathSegment(segment);
           })) {
        return {};
    }
    const QStringList parent = entry.path.mid(0, entry.path.size() - 1);
    const QString directory = QDir(layout.questionBanks).filePath(parent.join(u'/'));
    return QDir(directory).filePath(entry.path.constLast() + QStringLiteral(".json"));
}

BankReleaseInstallResult BankReleaseService::install(
    const SharedStorageLayout &layout,
    const QString &releaseTag,
    const QVector<BankReleaseSelection> &selections,
    const QHash<QString, QByteArray> &payloads) const
{
    BankReleaseInstallResult result;
    if (!layout.ready() || selections.isEmpty()) {
        result.error = QStringLiteral("题库目录或下载选择无效");
        return result;
    }
    const QString stagingRoot = QDir(layout.backups).filePath(
        QStringLiteral(".release-staging-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(stagingRoot)) {
        result.error = QStringLiteral("无法创建题库更新暂存目录");
        return result;
    }
    struct PendingFile {
        QString staged;
        QString destination;
    };
    QVector<PendingFile> pending;
    QSet<QString> usedDestinations;
    QString validationError;
    for (qsizetype index = 0; index < selections.size(); ++index) {
        const BankReleaseSelection &selection = selections.at(index);
        const QByteArray payload = selection.entry.embeddedPayload.isEmpty()
            ? payloads.value(selection.entry.id) : selection.entry.embeddedPayload;
        if (!verifyPayload(selection.entry, payload, &validationError)) {
            QDir(stagingRoot).removeRecursively();
            result.error = validationError;
            return result;
        }
        QString destination = destinationPath(layout, selection.entry);
        if (destination.isEmpty()) {
            QDir(stagingRoot).removeRecursively();
            result.error = QStringLiteral("题库路径无效：%1").arg(selection.entry.name);
            return result;
        }
        if (QFileInfo::exists(destination)) {
            if (selection.conflictPolicy == BankReleaseConflictPolicy::Skip) {
                ++result.skippedEntries;
                continue;
            }
            if (selection.conflictPolicy == BankReleaseConflictPolicy::KeepBoth) {
                const QFileInfo info(destination);
                const QString stem = info.completeBaseName();
                const QString tag = safeTag(releaseTag);
                int copyIndex = 1;
                do {
                    const QString suffix = copyIndex == 1
                        ? QStringLiteral(" (%1)").arg(tag)
                        : QStringLiteral(" (%1-%2)").arg(tag).arg(copyIndex);
                    destination = info.dir().filePath(stem + suffix + QStringLiteral(".json"));
                    ++copyIndex;
                } while (QFileInfo::exists(destination)
                         || usedDestinations.contains(QDir::cleanPath(destination)));
            }
        }
        const QString normalizedDestination = QDir::cleanPath(destination);
        if (usedDestinations.contains(normalizedDestination)) {
            QDir(stagingRoot).removeRecursively();
            result.error = QStringLiteral("多个题库会写入同一文件：%1")
                .arg(selection.entry.name);
            return result;
        }
        usedDestinations.insert(normalizedDestination);
        const QString staged = QDir(stagingRoot).filePath(
            QStringLiteral("payload-%1.json").arg(index));
        QSaveFile file(staged);
        if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size()
            || !file.commit()) {
            QDir(stagingRoot).removeRecursively();
            result.error = QStringLiteral("无法暂存题库：%1").arg(selection.entry.name);
            return result;
        }
        pending.append({staged, destination});
    }

    QVector<QString> committedDestinations;
    QVector<QString> committedBackups;
    for (qsizetype index = 0; index < pending.size(); ++index) {
        const PendingFile &file = pending.at(index);
        if (!QDir().mkpath(QFileInfo(file.destination).absolutePath())) {
            rollbackOperations(committedDestinations, committedBackups);
            QDir(stagingRoot).removeRecursively();
            result.error = QStringLiteral("无法创建题库目标目录");
            return result;
        }
        QString backup;
        if (QFileInfo::exists(file.destination)) {
            backup = QDir(stagingRoot).filePath(QStringLiteral("backup-%1.json").arg(index));
            if (!movePath(file.destination, backup)) {
                rollbackOperations(committedDestinations, committedBackups);
                QDir(stagingRoot).removeRecursively();
                result.error = QStringLiteral("无法暂存本地同名题库");
                return result;
            }
        }
        if (!movePath(file.staged, file.destination)) {
            if (!backup.isEmpty()) {
                movePath(backup, file.destination);
            }
            rollbackOperations(committedDestinations, committedBackups);
            QDir(stagingRoot).removeRecursively();
            result.error = QStringLiteral("无法提交下载题库");
            return result;
        }
        committedDestinations.append(file.destination);
        committedBackups.append(backup);
    }
    result.installedEntries = committedDestinations.size();
    result.destinationPaths = committedDestinations;
    if (!QDir(stagingRoot).removeRecursively()) {
        result.error = QStringLiteral("题库已更新，但暂存目录清理失败");
    }
    return result;
}

} // namespace quizapp::services
