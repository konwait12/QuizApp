#include "services/SharedStorageService.h"

#include <QDir>
#include <QFileInfo>

namespace quizapp::services {

QString SharedStorageService::desktopRootForDataRoot(const QString &dataRoot)
{
    if (dataRoot.trimmed().isEmpty()) {
        return {};
    }
    return QDir(dataRoot).filePath(QStringLiteral("SharedStorage"));
}

SharedStorageLayout SharedStorageService::prepare(const QString &rootPath) const
{
    SharedStorageLayout layout;
    if (rootPath.trimmed().isEmpty()) {
        layout.error = QStringLiteral("共享存储根目录为空");
        return layout;
    }

    layout.root = QDir::cleanPath(QFileInfo(rootPath).absoluteFilePath());
    const QDir root(layout.root);
    layout.questionBanks = root.filePath(QStringLiteral("QuestionBanks"));
    layout.backups = root.filePath(QStringLiteral("Backups"));
    layout.exports = root.filePath(QStringLiteral("Exports"));
    layout.notes = root.filePath(QStringLiteral("Notes"));
    layout.recycleBin = root.filePath(QStringLiteral("RecycleBin"));

    const QStringList directories{
        layout.root,
        layout.questionBanks,
        layout.backups,
        layout.exports,
        layout.notes,
        layout.recycleBin,
    };
    for (const QString &directory : directories) {
        if (!QDir().mkpath(directory)) {
            layout.error = QStringLiteral("无法创建共享存储目录：%1").arg(directory);
            return layout;
        }
    }
    return layout;
}

} // namespace quizapp::services
