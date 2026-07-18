#pragma once

#include "services/SharedStorageService.h"

#include <QString>
#include <QStringList>

namespace quizapp::services {

enum class StorageConflictPolicy {
    Skip,
    Overwrite,
    KeepBoth,
};

struct StorageOperationResult {
    bool completed = false;
    int affectedEntries = 0;
    QString destinationPath;
    QString error;
};

class SharedStorageFileService final {
public:
    StorageOperationResult createQuestionBankFolder(
        const SharedStorageLayout &layout,
        const QString &parentDirectory,
        const QString &folderName) const;
    StorageOperationResult importJsonFiles(
        const SharedStorageLayout &layout,
        const QString &destinationDirectory,
        const QStringList &sourceFiles,
        StorageConflictPolicy conflictPolicy) const;
    StorageOperationResult renameQuestionBankEntry(
        const SharedStorageLayout &layout,
        const QString &sourcePath,
        const QString &newName) const;
    StorageOperationResult moveQuestionBankEntry(
        const SharedStorageLayout &layout,
        const QString &sourcePath,
        const QString &destinationDirectory,
        StorageConflictPolicy conflictPolicy) const;
    StorageOperationResult moveToRecycleBin(
        const SharedStorageLayout &layout,
        const QString &sourcePath) const;
    StorageOperationResult restoreFromRecycleBin(
        const SharedStorageLayout &layout,
        const QString &recycledPath,
        StorageConflictPolicy conflictPolicy) const;
    StorageOperationResult permanentlyDelete(
        const SharedStorageLayout &layout,
        const QString &recycledPath) const;

    static bool isPathInside(const QString &path, const QString &root);
};

} // namespace quizapp::services
