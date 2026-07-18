#include "services/DefaultBankBundleBootstrapService.h"

#include "storage/Database.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <array>

namespace quizapp::services {
namespace {

constexpr auto kExpectedDisplayName = "27考研题库包";
constexpr auto kExpectedProvider = "xiaoyivip";

struct BundleManifest {
    QString displayName;
    QString provider;
    int sectionCount = 0;
    qsizetype questionCount = 0;
    int blobCount = 0;
    qint64 blobBytes = 0;
};

bool fail(const QString &message, QString *error)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool safeRelativePath(const QString &path)
{
    const QString normalized = QDir::cleanPath(path).replace(u'\\', u'/');
    return !normalized.isEmpty()
        && normalized != QStringLiteral(".")
        && !normalized.startsWith(u'/')
        && !normalized.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(normalized);
}

bool streamCopy(const QString &sourcePath, const QString &targetPath, QString *error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("无法读取预置题库文件：%1").arg(source.errorString()), error);
    }
    const QFileInfo targetInfo(targetPath);
    if (!QDir().mkpath(targetInfo.absolutePath())) {
        return fail(QStringLiteral("无法创建预置题库目录"), error);
    }
    QSaveFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly)) {
        return fail(QStringLiteral("无法写入预置题库文件：%1").arg(target.errorString()), error);
    }
    // Android worker threads have a comparatively small stack. Keeping the
    // transfer buffer on the heap avoids a first-launch stack overflow while
    // preserving bounded-memory streaming for the bundled database and media.
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (!source.atEnd()) {
        const qint64 read = source.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (read <= 0 || target.write(buffer.data(), read) != read) {
            target.cancelWriting();
            return fail(QStringLiteral("复制预置题库文件时中断"), error);
        }
    }
    if (!target.commit()) {
        return fail(QStringLiteral("无法原子提交预置题库文件"), error);
    }
    return true;
}

bool readManifest(const QString &path, BundleManifest *manifest, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("27考研题库包缺少 manifest.json"), error);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("27考研题库包清单格式无效"), error);
    }
    const QJsonObject object = document.object();
    manifest->displayName = object.value(QStringLiteral("displayName")).toString();
    manifest->provider = object.value(QStringLiteral("provider")).toString();
    manifest->sectionCount = object.value(QStringLiteral("sectionCount")).toInt(-1);
    manifest->questionCount = object.value(QStringLiteral("questionCount")).toInteger(-1);
    manifest->blobCount = object.value(QStringLiteral("blobCount")).toInt(-1);
    manifest->blobBytes = object.value(QStringLiteral("blobBytes")).toInteger(-1);
    if (object.value(QStringLiteral("schemaVersion")).toInt() != 1
        || manifest->displayName != QString::fromUtf8(kExpectedDisplayName)
        || manifest->provider != QString::fromLatin1(kExpectedProvider)
        || manifest->sectionCount <= 0
        || manifest->questionCount <= 0
        || manifest->blobCount < 0
        || manifest->blobBytes < 0) {
        return fail(QStringLiteral("27考研题库包清单字段不符合要求"), error);
    }
    return true;
}

QStringList bundleFiles(const QString &bundleRoot, QString *error)
{
    QFile index(QDir(bundleRoot).filePath(QStringLiteral("files.txt")));
    QStringList files;
    if (index.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(index.readAll());
        for (const QString &line : content.split(u'\n', Qt::SkipEmptyParts)) {
            const QString relative = QDir::cleanPath(line.trimmed()).replace(u'\\', u'/');
            if (!safeRelativePath(relative)) {
                fail(QStringLiteral("预置题库文件索引包含越界路径"), error);
                return {};
            }
            files.append(relative);
        }
    } else if (QFileInfo(bundleRoot).isDir()) {
        const QDir root(bundleRoot);
        QDirIterator iterator(
            bundleRoot,
            QDir::Files | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString absolute = iterator.next();
            const QString relative = root.relativeFilePath(absolute).replace(u'\\', u'/');
            if (relative != QStringLiteral("files.txt")) {
                files.append(relative);
            }
        }
    } else {
        fail(QStringLiteral("无法读取 27考研题库包文件索引"), error);
        return {};
    }
    files.removeDuplicates();
    files.sort(Qt::CaseSensitive);
    if (!files.contains(QStringLiteral("manifest.json"))
        || !files.contains(QStringLiteral("quizapp.sqlite"))) {
        fail(QStringLiteral("27考研题库包缺少数据库或清单"), error);
        return {};
    }
    return files;
}

bool queryCount(QSqlDatabase database, const QString &table, qint64 *count, QString *error)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)) || !query.next()) {
        return fail(query.lastError().text(), error);
    }
    *count = query.value(0).toLongLong();
    return true;
}

bool validateDatabase(
    const QString &databasePath,
    const BundleManifest &manifest,
    QString *error)
{
    const QString connectionName = QStringLiteral("default-bundle-validate-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool valid = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            fail(database.lastError().text(), error);
        } else {
            qint64 sections = 0;
            qint64 questions = 0;
            qint64 blobs = 0;
            QSqlQuery provider(database);
            provider.prepare(QStringLiteral(
                "SELECT COUNT(*) FROM banks WHERE source_provider<>?"));
            provider.addBindValue(manifest.provider);
            valid = queryCount(database, QStringLiteral("banks"), &sections, error)
                && queryCount(database, QStringLiteral("questions"), &questions, error)
                && queryCount(database, QStringLiteral("blobs"), &blobs, error)
                && provider.exec() && provider.next() && provider.value(0).toLongLong() == 0
                && sections == manifest.sectionCount
                && questions == manifest.questionCount
                && blobs == manifest.blobCount;
            if (!valid && error && error->isEmpty()) {
                *error = QStringLiteral("27考研题库包数据库计数或来源校验失败");
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return valid;
}

bool validateBlobFiles(
    const QString &bundleRoot,
    const BundleManifest &manifest,
    QString *error)
{
    int count = 0;
    qint64 bytes = 0;
    QDirIterator iterator(
        QDir(bundleRoot).filePath(QStringLiteral("blobs")),
        QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        ++count;
        bytes += iterator.fileInfo().size();
    }
    if (count != manifest.blobCount || bytes != manifest.blobBytes) {
        return fail(QStringLiteral("27考研题库包媒体文件计数或大小不一致"), error);
    }
    return true;
}

bool existingDatabaseIsEmpty(const QString &databasePath, bool *empty, QString *error)
{
    *empty = true;
    if (!QFileInfo::exists(databasePath)) {
        return true;
    }
    storage::Database database(
        QStringLiteral("default-bundle-existing-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!database.open(databasePath, error) || !database.migrate(error)) {
        return false;
    }
    const std::array<QString, 24> protectedTables{{
        QStringLiteral("subjects"),
        QStringLiteral("banks"),
        QStringLiteral("questions"),
        QStringLiteral("question_options"),
        QStringLiteral("blobs"),
        QStringLiteral("question_blobs"),
        QStringLiteral("bank_sources"),
        QStringLiteral("practice_sessions"),
        QStringLiteral("practice_answers"),
        QStringLiteral("wrong_book"),
        QStringLiteral("review_cards"),
        QStringLiteral("review_logs"),
        QStringLiteral("exam_sessions"),
        QStringLiteral("exam_answers"),
        QStringLiteral("notebooks"),
        QStringLiteral("notebook_links"),
        QStringLiteral("study_events"),
        QStringLiteral("ai_records"),
        QStringLiteral("settings"),
        QStringLiteral("question_search"),
        QStringLiteral("notebook_search"),
        QStringLiteral("legacy_migrations"),
        QStringLiteral("legacy_records"),
        QStringLiteral("question_answer_state"),
    }};
    for (const QString &table : protectedTables) {
        qint64 count = 0;
        if (!queryCount(database.connection(), table, &count, error)) {
            return false;
        }
        if (count > 0) {
            *empty = false;
            return true;
        }
    }
    QSqlQuery checkpoint(database.connection());
    checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    return true;
}

bool sameFile(const QString &leftPath, const QString &rightPath)
{
    QFile left(leftPath);
    QFile right(rightPath);
    if (!left.open(QIODevice::ReadOnly) || !right.open(QIODevice::ReadOnly)
        || left.size() != right.size()) {
        return false;
    }
    QCryptographicHash leftHash(QCryptographicHash::Sha256);
    QCryptographicHash rightHash(QCryptographicHash::Sha256);
    return leftHash.addData(&left) && rightHash.addData(&right)
        && leftHash.result() == rightHash.result();
}

} // namespace

DefaultBankBundleBootstrapResult DefaultBankBundleBootstrapService::install(
    const QString &bundleRoot,
    const QString &dataRoot,
    const QString &databasePath,
    const CopyGate &copyGate,
    const ProgressCallback &progress) const
{
    DefaultBankBundleBootstrapResult result;
    const QString manifestPath = QDir(bundleRoot).filePath(QStringLiteral("manifest.json"));
    if (!QFileInfo::exists(manifestPath)) {
        result.status = DefaultBankBundleBootstrapStatus::Unavailable;
        return result;
    }

    BundleManifest manifest;
    if (!readManifest(manifestPath, &manifest, &result.error)) {
        result.status = DefaultBankBundleBootstrapStatus::Failed;
        return result;
    }
    result.displayName = manifest.displayName;
    result.questionCount = manifest.questionCount;
    result.sectionCount = manifest.sectionCount;
    result.blobCount = manifest.blobCount;

    bool empty = false;
    if (!existingDatabaseIsEmpty(databasePath, &empty, &result.error)) {
        result.status = DefaultBankBundleBootstrapStatus::Failed;
        return result;
    }
    if (!empty) {
        result.status = DefaultBankBundleBootstrapStatus::SkippedNonEmpty;
        return result;
    }

    if (!QDir().mkpath(dataRoot)) {
        result.status = DefaultBankBundleBootstrapStatus::Failed;
        result.error = QStringLiteral("无法创建应用数据目录");
        return result;
    }
    const QString stagingParent = QDir(dataRoot).filePath(QStringLiteral("bootstrap-staging"));
    if (QFileInfo::exists(stagingParent) && !QDir(stagingParent).removeRecursively()) {
        result.status = DefaultBankBundleBootstrapStatus::Failed;
        result.error = QStringLiteral("无法清理上次中断的预置题库临时目录");
        return result;
    }
    const QString stagingRoot = QDir(stagingParent).filePath(
        QStringLiteral("%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(stagingRoot)) {
        result.status = DefaultBankBundleBootstrapStatus::Failed;
        result.error = QStringLiteral("无法创建预置题库临时目录");
        return result;
    }
    auto cleanupStaging = [&stagingRoot]() {
        QDir(stagingRoot).removeRecursively();
    };

    QString fileListError;
    const QStringList files = bundleFiles(bundleRoot, &fileListError);
    if (!fileListError.isEmpty()) {
        cleanupStaging();
        result.status = DefaultBankBundleBootstrapStatus::Failed;
        result.error = fileListError;
        return result;
    }
    const int totalWork = files.size() + manifest.blobCount + 2;
    int completedWork = 0;
    auto reportProgress = [&](const QString &phase) {
        if (progress) {
            progress(phase, completedWork, totalWork);
        }
    };
    reportProgress(QStringLiteral("正在复制题库资源"));
    for (const QString &relativePath : files) {
        if (copyGate && !copyGate(relativePath)) {
            cleanupStaging();
            result.status = DefaultBankBundleBootstrapStatus::Failed;
            result.error = QStringLiteral("预置题库复制已中断");
            return result;
        }
        if (!streamCopy(
                QDir(bundleRoot).filePath(relativePath),
                QDir(stagingRoot).filePath(relativePath),
                &result.error)) {
            cleanupStaging();
            result.status = DefaultBankBundleBootstrapStatus::Failed;
            return result;
        }
        ++completedWork;
        reportProgress(QStringLiteral("正在复制题库资源"));
    }
    reportProgress(QStringLiteral("正在校验题库"));
    if (!validateDatabase(
            QDir(stagingRoot).filePath(QStringLiteral("quizapp.sqlite")),
            manifest,
            &result.error)
        || !validateBlobFiles(stagingRoot, manifest, &result.error)) {
        cleanupStaging();
        result.status = DefaultBankBundleBootstrapStatus::Failed;
        return result;
    }
    ++completedWork;
    reportProgress(QStringLiteral("正在安装题库"));

    QString backupPath;
    const bool existingDatabase = QFileInfo::exists(databasePath);
    if (existingDatabase) {
        const QString backupRoot = QDir(dataRoot).filePath(QStringLiteral("bootstrap-backups"));
        backupPath = QDir(backupRoot).filePath(
            QStringLiteral("quizapp-empty-%1.sqlite")
                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"))));
        if (!streamCopy(databasePath, backupPath, &result.error)) {
            cleanupStaging();
            result.status = DefaultBankBundleBootstrapStatus::Failed;
            return result;
        }
    }
    QFile::remove(databasePath + QStringLiteral("-wal"));
    QFile::remove(databasePath + QStringLiteral("-shm"));

    QStringList createdBlobs;
    QDirIterator blobIterator(
        QDir(stagingRoot).filePath(QStringLiteral("blobs")),
        QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    const QDir stagingDir(stagingRoot);
    bool targetReady = true;
    while (blobIterator.hasNext()) {
        const QString sourcePath = blobIterator.next();
        const QString relativePath = stagingDir.relativeFilePath(sourcePath);
        const QString targetPath = QDir(dataRoot).filePath(relativePath);
        if (QFileInfo::exists(targetPath)) {
            if (!sameFile(sourcePath, targetPath)) {
                result.error = QStringLiteral("本地存在同名但内容不同的媒体文件");
                targetReady = false;
                break;
            }
            ++completedWork;
            reportProgress(QStringLiteral("正在安装题库"));
            continue;
        }
        if (!streamCopy(sourcePath, targetPath, &result.error)) {
            targetReady = false;
            break;
        }
        createdBlobs.append(targetPath);
        ++completedWork;
        reportProgress(QStringLiteral("正在安装题库"));
    }

    const QString stagedDatabase = QDir(stagingRoot).filePath(QStringLiteral("quizapp.sqlite"));
    if (targetReady) {
        targetReady = streamCopy(stagedDatabase, databasePath, &result.error);
    }
    if (targetReady) {
        targetReady = validateDatabase(databasePath, manifest, &result.error);
    }
    if (!targetReady) {
        for (const QString &path : createdBlobs) {
            QFile::remove(path);
        }
        if (!backupPath.isEmpty()) {
            QString restoreError;
            if (!streamCopy(backupPath, databasePath, &restoreError)) {
                result.error += QStringLiteral("；恢复原数据库失败：%1").arg(restoreError);
            }
        } else {
            QFile::remove(databasePath);
        }
        QFile::remove(databasePath + QStringLiteral("-wal"));
        QFile::remove(databasePath + QStringLiteral("-shm"));
        cleanupStaging();
        result.status = DefaultBankBundleBootstrapStatus::Failed;
        return result;
    }

    QFile::remove(databasePath + QStringLiteral("-wal"));
    QFile::remove(databasePath + QStringLiteral("-shm"));
    cleanupStaging();
    completedWork = totalWork;
    reportProgress(QStringLiteral("题库准备完成"));
    result.status = DefaultBankBundleBootstrapStatus::Installed;
    return result;
}

} // namespace quizapp::services
