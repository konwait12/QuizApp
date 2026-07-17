#include "services/SharedStorageFileService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

#include <array>

namespace quizapp::services {
namespace {

StorageOperationResult failure(const QString &message)
{
    StorageOperationResult result;
    result.error = message;
    return result;
}

QString normalizedAbsolutePath(const QString &path)
{
    return QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
}

QString uniqueSiblingPath(const QString &path)
{
    const QFileInfo info(path);
    const QString baseName = info.completeBaseName();
    const QString suffix = info.suffix();
    const QDir parent = info.dir();
    for (int copyIndex = 2; copyIndex < 10000; ++copyIndex) {
        const QString fileName = suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(baseName).arg(copyIndex)
            : QStringLiteral("%1 (%2).%3").arg(baseName).arg(copyIndex).arg(suffix);
        const QString candidate = parent.filePath(fileName);
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QString resolvedDestination(
    const QString &requested,
    StorageConflictPolicy conflictPolicy,
    QString *error)
{
    if (!QFileInfo::exists(requested)) {
        return requested;
    }
    if (conflictPolicy == StorageConflictPolicy::Skip) {
        return {};
    }
    if (conflictPolicy == StorageConflictPolicy::KeepBoth) {
        const QString unique = uniqueSiblingPath(requested);
        if (unique.isEmpty() && error) {
            *error = QStringLiteral("无法生成不重复的文件名");
        }
        return unique;
    }
    return requested;
}

bool removePath(const QString &path)
{
    const QFileInfo info(path);
    return !info.exists() || (info.isDir()
        ? QDir(path).removeRecursively()
        : QFile::remove(path));
}

bool movePath(const QString &source, const QString &destination)
{
    const QFileInfo info(source);
    if (info.isDir()) {
        return QDir().rename(source, destination);
    }
    return QFile::rename(source, destination);
}

QString areaForPath(const SharedStorageLayout &layout, const QString &path)
{
    const std::array<std::pair<QString, QString>, 4> areas{{
        {QStringLiteral("QuestionBanks"), layout.questionBanks},
        {QStringLiteral("Backups"), layout.backups},
        {QStringLiteral("Exports"), layout.exports},
        {QStringLiteral("Notes"), layout.notes},
    }};
    for (const auto &[name, root] : areas) {
        if (SharedStorageFileService::isPathInside(path, root)) {
            return name;
        }
    }
    return {};
}

QString rootForArea(const SharedStorageLayout &layout, const QString &area)
{
    if (area == QStringLiteral("QuestionBanks")) {
        return layout.questionBanks;
    }
    if (area == QStringLiteral("Backups")) {
        return layout.backups;
    }
    if (area == QStringLiteral("Exports")) {
        return layout.exports;
    }
    if (area == QStringLiteral("Notes")) {
        return layout.notes;
    }
    return {};
}

} // namespace

bool SharedStorageFileService::isPathInside(const QString &path, const QString &root)
{
    if (path.trimmed().isEmpty() || root.trimmed().isEmpty()) {
        return false;
    }
    const QString absolutePath = normalizedAbsolutePath(path);
    const QString absoluteRoot = normalizedAbsolutePath(root);
#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    if (absolutePath.compare(absoluteRoot, sensitivity) == 0) {
        return true;
    }
    const QString prefix = absoluteRoot + u'/';
    return absolutePath.startsWith(prefix, sensitivity);
}

StorageOperationResult SharedStorageFileService::createQuestionBankFolder(
    const SharedStorageLayout &layout,
    const QString &parentDirectory,
    const QString &folderName) const
{
    const QString trimmedName = folderName.trimmed();
    static const QRegularExpression unsafe(QStringLiteral(R"([\\/:*?"<>|])"));
    if (!layout.ready() || trimmedName.isEmpty() || unsafe.match(trimmedName).hasMatch()
        || trimmedName == QStringLiteral(".") || trimmedName == QStringLiteral("..")) {
        return failure(QStringLiteral("文件夹名称无效"));
    }
    const QString parent = parentDirectory.trimmed().isEmpty()
        ? layout.questionBanks : parentDirectory;
    if (!isPathInside(parent, layout.questionBanks) || !QFileInfo(parent).isDir()) {
        return failure(QStringLiteral("只能在 QuestionBanks 内创建文件夹"));
    }
    const QString destination = QDir(parent).filePath(trimmedName);
    if (QFileInfo::exists(destination)) {
        return failure(QStringLiteral("同名文件夹已存在"));
    }
    if (!QDir().mkpath(destination)) {
        return failure(QStringLiteral("无法创建文件夹"));
    }
    return {true, 1, destination, {}};
}

StorageOperationResult SharedStorageFileService::importJsonFiles(
    const SharedStorageLayout &layout,
    const QString &destinationDirectory,
    const QStringList &sourceFiles,
    StorageConflictPolicy conflictPolicy) const
{
    if (!layout.ready() || !isPathInside(destinationDirectory, layout.questionBanks)
        || !QFileInfo(destinationDirectory).isDir()) {
        return failure(QStringLiteral("导入目标必须是 QuestionBanks 内的文件夹"));
    }
    StorageOperationResult result;
    result.completed = true;
    for (const QString &sourcePath : sourceFiles) {
        const QFileInfo source(sourcePath);
        if (!source.isFile()
            || source.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) {
            result.completed = false;
            result.error = QStringLiteral("只能导入 JSON 文件：%1").arg(source.fileName());
            return result;
        }
        QString destination = QDir(destinationDirectory).filePath(source.fileName());
        QString destinationError;
        destination = resolvedDestination(destination, conflictPolicy, &destinationError);
        if (!destinationError.isEmpty()) {
            return failure(destinationError);
        }
        if (destination.isEmpty()) {
            continue;
        }
        if (QFileInfo::exists(destination)
            && conflictPolicy == StorageConflictPolicy::Overwrite) {
            const QString temporary = destination + QStringLiteral(".quizapp-import-")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
            const QString previous = destination + QStringLiteral(".quizapp-previous-")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (!QFile::copy(source.absoluteFilePath(), temporary)) {
                QFile::remove(temporary);
                return failure(QStringLiteral("无法安全覆盖题库文件：%1").arg(source.fileName()));
            }
            if (!movePath(destination, previous)) {
                QFile::remove(temporary);
                return failure(QStringLiteral("无法暂存旧题库文件：%1").arg(source.fileName()));
            }
            if (!QFile::rename(temporary, destination)) {
                movePath(previous, destination);
                QFile::remove(temporary);
                return failure(QStringLiteral("无法安全覆盖题库文件：%1").arg(source.fileName()));
            }
            if (!removePath(previous)) {
                return failure(QStringLiteral("题库已覆盖，但旧文件的临时备份未能清理"));
            }
        } else if (!QFile::copy(source.absoluteFilePath(), destination)) {
            return failure(QStringLiteral("无法复制题库文件：%1").arg(source.fileName()));
        }
        result.destinationPath = destination;
        ++result.affectedEntries;
    }
    return result;
}

StorageOperationResult SharedStorageFileService::moveToRecycleBin(
    const SharedStorageLayout &layout,
    const QString &sourcePath) const
{
    const QFileInfo source(sourcePath);
    if (!layout.ready() || !source.exists() || source.isSymLink()) {
        return failure(QStringLiteral("待回收的文件或文件夹无效"));
    }
    const QString area = areaForPath(layout, source.absoluteFilePath());
    const QString areaRoot = rootForArea(layout, area);
    if (area.isEmpty() || source.absoluteFilePath() == normalizedAbsolutePath(areaRoot)) {
        return failure(QStringLiteral("不能回收共享存储的固定根目录"));
    }
    const QString batch = QStringLiteral("%1-%2")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")))
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    const QString relative = QDir(areaRoot).relativeFilePath(source.absoluteFilePath());
    const QString destination = QDir(layout.recycleBin).filePath(
        QStringLiteral("%1/%2/%3").arg(batch, area, relative));
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())
        || !movePath(source.absoluteFilePath(), destination)) {
        return failure(QStringLiteral("无法把项目移入回收站"));
    }
    return {true, 1, destination, {}};
}

StorageOperationResult SharedStorageFileService::restoreFromRecycleBin(
    const SharedStorageLayout &layout,
    const QString &recycledPath,
    StorageConflictPolicy conflictPolicy) const
{
    const QFileInfo source(recycledPath);
    if (!layout.ready() || !source.exists() || source.isSymLink()
        || !isPathInside(source.absoluteFilePath(), layout.recycleBin)
        || source.absoluteFilePath() == normalizedAbsolutePath(layout.recycleBin)) {
        return failure(QStringLiteral("回收站项目无效"));
    }
    const QString relative = QDir(layout.recycleBin).relativeFilePath(source.absoluteFilePath())
                                 .replace(u'\\', u'/');
    const QStringList segments = relative.split(u'/', Qt::SkipEmptyParts);
    if (segments.size() < 3) {
        return failure(QStringLiteral("回收站项目缺少原始路径信息"));
    }
    const QString areaRoot = rootForArea(layout, segments.at(1));
    if (areaRoot.isEmpty()) {
        return failure(QStringLiteral("回收站项目的原始目录无效"));
    }
    const QString requested = QDir(areaRoot).filePath(segments.mid(2).join(u'/'));
    QString destinationError;
    const QString destination = resolvedDestination(
        requested, conflictPolicy, &destinationError);
    if (!destinationError.isEmpty()) {
        return failure(destinationError);
    }
    if (destination.isEmpty()) {
        return {true, 0, {}, {}};
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        return failure(QStringLiteral("无法创建恢复目标目录"));
    }
    QString replacedBackup;
    if (QFileInfo::exists(destination)
        && conflictPolicy == StorageConflictPolicy::Overwrite) {
        replacedBackup = destination + QStringLiteral(".quizapp-restore-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!movePath(destination, replacedBackup)) {
            return failure(QStringLiteral("无法暂存同名项目，恢复已取消"));
        }
    }
    if (!movePath(source.absoluteFilePath(), destination)) {
        if (!replacedBackup.isEmpty()) {
            movePath(replacedBackup, destination);
        }
        return failure(QStringLiteral("无法恢复回收站项目"));
    }
    if (!replacedBackup.isEmpty() && !removePath(replacedBackup)) {
        return failure(QStringLiteral("项目已恢复，但旧同名项目的临时备份未能清理"));
    }
    return {true, 1, destination, {}};
}

StorageOperationResult SharedStorageFileService::permanentlyDelete(
    const SharedStorageLayout &layout,
    const QString &recycledPath) const
{
    const QFileInfo source(recycledPath);
    if (!layout.ready() || !source.exists() || source.isSymLink()
        || !isPathInside(source.absoluteFilePath(), layout.recycleBin)
        || source.absoluteFilePath() == normalizedAbsolutePath(layout.recycleBin)) {
        return failure(QStringLiteral("只能彻底删除回收站内的项目"));
    }
    const bool removed = source.isDir()
        ? QDir(source.absoluteFilePath()).removeRecursively()
        : QFile::remove(source.absoluteFilePath());
    if (!removed) {
        return failure(QStringLiteral("无法彻底删除所选项目"));
    }
    return {true, 1, {}, {}};
}

} // namespace quizapp::services
