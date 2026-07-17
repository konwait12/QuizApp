#include "services/XiaoyiDirectoryInstallService.h"

#include "services/BankInstallService.h"
#include "services/BlobStore.h"
#include "storage/Database.h"
#include "storage/SqliteQuestionRepository.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>

namespace quizapp::services {

DirectoryInstallResult XiaoyiDirectoryInstallService::install(
    const QString &inputDirectory,
    const QString &databasePath,
    const QString &dataRoot,
    const ProgressCallback &progress) const
{
    DirectoryInstallResult result;
    const QDir input(inputDirectory);
    if (!input.exists() || databasePath.trimmed().isEmpty() || dataRoot.trimmed().isEmpty()) {
        result.error = QStringLiteral("题库目录或本地数据路径无效");
        return result;
    }

    QStringList files;
    QDirIterator iterator(
        inputDirectory,
        {QStringLiteral("*.json")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (QFileInfo(path).fileName() != QStringLiteral("export-report.json")) {
            files.append(path);
        }
    }
    std::sort(files.begin(), files.end());
    result.discoveredSections = files.size();
    if (files.isEmpty()) {
        result.error = QStringLiteral("所选目录没有 27考研题库包 JSON");
        return result;
    }

    storage::Database database(
        QStringLiteral("xiaoyi-install-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!database.open(databasePath, &result.error) || !database.migrate(&result.error)) {
        return result;
    }
    storage::SqliteQuestionRepository repository(database.connection());
    BlobStore blobStore(dataRoot);
    BankInstallService installer;

    for (int index = 0; index < files.size(); ++index) {
        const QString sourceKey = input.relativeFilePath(files.at(index)).replace(u'\\', u'/');
        if (progress && !progress(index, files.size(), sourceKey)) {
            result.canceled = true;
            return result;
        }
        QFile file(files.at(index));
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("无法读取题库文件：%1").arg(sourceKey);
            return result;
        }
        const BankInstallResult installed = installer.installJson(
            file.readAll(), sourceKey, blobStore, repository, QStringLiteral("xiaoyivip"));
        if (!installed.installed) {
            result.error = QStringLiteral("%1：%2").arg(sourceKey, installed.error);
            return result;
        }
        result.installedQuestions += installed.import.acceptedQuestionCount;
        ++result.installedSections;
    }
    if (progress) {
        progress(files.size(), files.size(), QString());
    }
    return result;
}

} // namespace quizapp::services
