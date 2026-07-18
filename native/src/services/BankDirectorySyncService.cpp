#include "services/BankDirectorySyncService.h"

#include "domain/ManagedBankSource.h"
#include "services/BankInstallService.h"
#include "services/BlobStore.h"
#include "storage/Database.h"
#include "storage/SqliteBankSourceRepository.h"
#include "storage/SqliteQuestionRepository.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace quizapp::services {
namespace {

const QString kManagedRoot = QStringLiteral("primary-shared-storage");

QString canonicalRelativePath(const QDir &root, const QString &absolutePath)
{
    return QDir::cleanPath(root.relativeFilePath(absolutePath)).replace(u'\\', u'/');
}

QString sourceKeyForRelativePath(const QString &relativePath)
{
    return QStringLiteral("shared:%1").arg(relativePath);
}

bool isSupportedJson(const QFileInfo &file)
{
    if (!file.isFile() || file.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) {
        return false;
    }
    const QString name = file.fileName();
    return name.compare(QStringLiteral("export-report.json"), Qt::CaseInsensitive) != 0
        && !name.startsWith(u'.');
}

bool readFile(const QString &path, QByteArray *bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    *bytes = file.readAll();
    return true;
}

} // namespace

QStringList BankDirectorySyncService::hierarchyForRelativePath(
    const QString &relativePath)
{
    QString normalized = QDir::cleanPath(relativePath).replace(u'\\', u'/');
    if (normalized == QStringLiteral(".") || normalized.startsWith(QStringLiteral("../"))) {
        return {};
    }
    QStringList segments = normalized.split(u'/', Qt::SkipEmptyParts);
    if (segments.isEmpty()) {
        return {};
    }
    segments.last() = QFileInfo(segments.constLast()).completeBaseName().trimmed();
    for (QString &segment : segments) {
        segment = segment.trimmed();
        if (segment.isEmpty() || segment == QStringLiteral(".") || segment == QStringLiteral("..")) {
            return {};
        }
    }
    return segments;
}

BankDirectoryScanResult BankDirectorySyncService::scan(
    const QString &questionBanksRoot) const
{
    BankDirectoryScanResult result;
    const QDir root(questionBanksRoot);
    if (!root.exists()) {
        result.error = QStringLiteral("题库目录不存在：%1").arg(questionBanksRoot);
        return result;
    }

    QDirIterator iterator(
        questionBanksRoot,
        QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolutePath = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (!isSupportedJson(info)) {
            continue;
        }
        ManagedBankFile file;
        file.absolutePath = absolutePath;
        file.relativePath = canonicalRelativePath(root, absolutePath);
        file.sourceKey = sourceKeyForRelativePath(file.relativePath);
        file.hierarchy = hierarchyForRelativePath(file.relativePath);
        file.fileSize = info.size();
        file.modifiedMsecs = info.lastModified().toMSecsSinceEpoch();
        if (file.hierarchy.isEmpty()) {
            result.issues.append({file.relativePath, QStringLiteral("文件路径不能生成有效题库层级")});
            continue;
        }
        result.files.append(file);
    }
    std::sort(result.files.begin(), result.files.end(), [](const auto &left, const auto &right) {
        return left.relativePath.compare(right.relativePath, Qt::CaseInsensitive) < 0;
    });
    return result;
}

BankDirectorySyncResult BankDirectorySyncService::synchronize(
    const QString &questionBanksRoot,
    const QString &databasePath,
    const QString &dataRoot,
    bool force,
    const ProgressCallback &progress) const
{
    BankDirectorySyncResult result;
    BankDirectoryScanResult scanned = scan(questionBanksRoot);
    result.issues = scanned.issues;
    if (!scanned.error.isEmpty()) {
        result.error = scanned.error;
        return result;
    }
    result.discoveredFiles = scanned.files.size();

    storage::Database database(
        QStringLiteral("bank-directory-sync-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!database.open(databasePath, &result.error) || !database.migrate(&result.error)) {
        return result;
    }
    storage::SqliteQuestionRepository questionRepository(database.connection());
    storage::SqliteBankSourceRepository sourceRepository(database.connection());
    BlobStore blobStore(dataRoot);
    BankInstallService installer;

    QString repositoryError;
    const QVector<domain::ManagedBankSource> previousSources =
        sourceRepository.listByRoot(kManagedRoot, &repositoryError);
    if (!repositoryError.isEmpty()) {
        result.error = repositoryError;
        return result;
    }
    QHash<QString, domain::ManagedBankSource> previousByKey;
    for (const domain::ManagedBankSource &source : previousSources) {
        previousByKey.insert(source.sourceKey, source);
    }

    QSet<QString> currentKeys;
    for (const ManagedBankFile &file : std::as_const(scanned.files)) {
        currentKeys.insert(file.sourceKey);
    }
    QSet<QString> relocatedPreviousKeys;
    for (int index = 0; index < scanned.files.size(); ++index) {
        ManagedBankFile file = scanned.files.at(index);
        if (progress && !progress(index, scanned.files.size(), file.relativePath)) {
            result.canceled = true;
            return result;
        }

        const auto previous = previousByKey.constFind(file.sourceKey);
        const bool metadataMatches = previous != previousByKey.cend()
            && previous->fileSize == file.fileSize
            && previous->modifiedMsecs == file.modifiedMsecs;
        if (!force && metadataMatches && previous->available) {
            if (!previous->bankId.isEmpty()
                && !sourceRepository.applyManagedOverride(
                    previous->bankId, file.hierarchy, &repositoryError)) {
                result.error = repositoryError;
                return result;
            }
            if (!previous->lastError.isEmpty()) {
                result.issues.append({file.relativePath, previous->lastError});
            }
            ++result.unchangedFiles;
            continue;
        }

        QByteArray json;
        QString fileError;
        if (!readFile(file.absolutePath, &json, &fileError)) {
            result.issues.append({file.relativePath, fileError});
            continue;
        }
        file.sha256 = QCryptographicHash::hash(json, QCryptographicHash::Sha256);

        const domain::ManagedBankSource *relocatedFrom = nullptr;
        if (previous == previousByKey.cend()) {
            const domain::ManagedBankSource *candidate = nullptr;
            bool ambiguous = false;
            for (const domain::ManagedBankSource &oldSource : previousSources) {
                if (!oldSource.available || oldSource.bankId.isEmpty()
                    || !oldSource.lastError.isEmpty() || oldSource.sha256.isEmpty()
                    || currentKeys.contains(oldSource.sourceKey)
                    || relocatedPreviousKeys.contains(oldSource.sourceKey)
                    || oldSource.sha256 != file.sha256) {
                    continue;
                }
                if (candidate) {
                    ambiguous = true;
                    break;
                }
                candidate = &oldSource;
            }
            if (!ambiguous) {
                relocatedFrom = candidate;
            }
        }
        if (!force && previous != previousByKey.cend()
            && previous->sha256 == file.sha256 && previous->lastError.isEmpty()) {
            domain::ManagedBankSource refreshed = previous.value();
            refreshed.fileSize = file.fileSize;
            refreshed.modifiedMsecs = file.modifiedMsecs;
            refreshed.available = true;
            refreshed.lastSyncedAt = QDateTime::currentDateTimeUtc();
            if (!sourceRepository.save(refreshed, &repositoryError)) {
                result.error = repositoryError;
                return result;
            }
            if (!refreshed.bankId.isEmpty()
                && !sourceRepository.applyManagedOverride(
                    refreshed.bankId, file.hierarchy, &repositoryError)) {
                result.error = repositoryError;
                return result;
            }
            if (previous->available) {
                ++result.unchangedFiles;
            } else {
                ++result.restoredFiles;
            }
            continue;
        }

        QString identitySourceKey = file.sourceKey;
        if (relocatedFrom) {
            identitySourceKey = sourceRepository.identitySourceKeyForBank(
                relocatedFrom->bankId, &repositoryError);
            if (!repositoryError.isEmpty()) {
                result.error = repositoryError;
                return result;
            }
            if (identitySourceKey.isEmpty()) {
                identitySourceKey = relocatedFrom->sourceKey;
            }
        }
        const BankInstallResult installed = installer.installJson(
            json,
            identitySourceKey,
            blobStore,
            questionRepository,
            {},
            file.hierarchy);
        domain::ManagedBankSource source;
        source.sourceKey = file.sourceKey;
        source.managedRoot = kManagedRoot;
        source.relativePath = file.relativePath;
        source.fileSize = file.fileSize;
        source.modifiedMsecs = file.modifiedMsecs;
        source.sha256 = file.sha256;
        source.available = true;
        source.lastSyncedAt = QDateTime::currentDateTimeUtc();
        if (!installed.installed) {
            source.bankId = relocatedFrom
                ? relocatedFrom->bankId
                : previous == previousByKey.cend() ? QString() : previous->bankId;
            source.lastError = installed.error;
            result.issues.append({file.relativePath, installed.error});
            const bool saved = relocatedFrom
                ? sourceRepository.replaceKey(
                      relocatedFrom->sourceKey, source, &repositoryError)
                : sourceRepository.save(source, &repositoryError);
            if (!saved) {
                result.error = repositoryError;
                return result;
            }
            if (relocatedFrom) {
                relocatedPreviousKeys.insert(relocatedFrom->sourceKey);
            }
            continue;
        }
        source.bankId = installed.import.package->bank.id;
        const bool saved = relocatedFrom
            ? sourceRepository.replaceKey(
                  relocatedFrom->sourceKey, source, &repositoryError)
            : sourceRepository.save(source, &repositoryError);
        if (!saved) {
            result.error = repositoryError;
            return result;
        }
        if (!sourceRepository.applyManagedOverride(
                source.bankId, file.hierarchy, &repositoryError)) {
            result.error = repositoryError;
            return result;
        }
        result.installedQuestions += installed.import.acceptedQuestionCount;
        if (relocatedFrom) {
            relocatedPreviousKeys.insert(relocatedFrom->sourceKey);
            ++result.relocatedFiles;
        } else if (previous == previousByKey.cend() || previous->bankId.isEmpty()) {
            ++result.installedFiles;
        } else {
            ++result.updatedFiles;
        }
    }

    for (const domain::ManagedBankSource &source : previousSources) {
        if (!currentKeys.contains(source.sourceKey) && source.available
            && !relocatedPreviousKeys.contains(source.sourceKey)) {
            if (!sourceRepository.setAvailability(source.sourceKey, false, &repositoryError)) {
                result.error = repositoryError;
                return result;
            }
            ++result.missingFiles;
        }
    }
    if (progress) {
        progress(scanned.files.size(), scanned.files.size(), {});
    }
    return result;
}

} // namespace quizapp::services
