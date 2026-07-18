#include "services/LocalBackupService.h"

#include "platform/SecureSecretStore.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace quizapp::services {
namespace {

const QByteArray kMagic = QByteArrayLiteral("QUIZBACKUPV2\r\n");
constexpr qint64 kChunkSize = 1024 * 1024;
constexpr quint64 kMaximumManifestSize = 32 * 1024 * 1024;
constexpr qsizetype kMaximumEntryCount = 100000;

struct SourceEntry {
    BackupEntryInfo info;
    QString sourcePath;
    QByteArray memoryData;
};

struct ParsedArchive {
    QJsonObject manifest;
    QVector<BackupEntryInfo> entries;
    qint64 payloadOffset = 0;
    QString error;
};

struct RestoreAction {
    QString target;
    QString backup;
    bool existed = false;
};

void clearError(QString *error)
{
    if (error) {
        error->clear();
    }
}

bool setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool readExact(QIODevice &source, qint64 size, QByteArray *data)
{
    if (!data || size < 0 || size > std::numeric_limits<qsizetype>::max()) {
        return false;
    }
    data->clear();
    data->reserve(static_cast<qsizetype>(size));
    while (data->size() < size) {
        const qint64 remaining = size - data->size();
        const QByteArray chunk = source.read(remaining);
        if (chunk.isEmpty()) {
            return false;
        }
        data->append(chunk);
    }
    return true;
}

bool writeAll(QIODevice &destination, const QByteArray &data)
{
    qint64 written = 0;
    while (written < data.size()) {
        const qint64 count = destination.write(
            data.constData() + written, data.size() - written);
        if (count <= 0) {
            return false;
        }
        written += count;
    }
    return true;
}

bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(u'\\')) {
        return false;
    }
    const QString clean = QDir::cleanPath(path);
    return clean == path && clean != QStringLiteral(".")
        && !clean.startsWith(QStringLiteral("../"))
        && !clean.contains(QStringLiteral("/../"));
}

QString normalizedAbsolute(const QString &path)
{
    return QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
}

bool pathInside(const QString &path, const QString &root)
{
    if (path.isEmpty() || root.isEmpty()) {
        return false;
    }
    const QString normalizedPath = normalizedAbsolute(path);
    QString normalizedRoot = normalizedAbsolute(root);
    if (!normalizedRoot.endsWith(u'/')) {
        normalizedRoot.append(u'/');
    }
    return normalizedPath == normalizedRoot.left(normalizedRoot.size() - 1)
        || normalizedPath.startsWith(normalizedRoot, Qt::CaseInsensitive);
}

QJsonObject variantJson(const QVariant &value)
{
    QJsonObject result;
    const int type = value.metaType().id();
    if (type == QMetaType::Bool) {
        result.insert(QStringLiteral("type"), QStringLiteral("bool"));
        result.insert(QStringLiteral("value"), value.toBool());
    } else if (type == QMetaType::Int || type == QMetaType::UInt
               || type == QMetaType::LongLong || type == QMetaType::ULongLong) {
        result.insert(QStringLiteral("type"), QStringLiteral("integer"));
        result.insert(QStringLiteral("value"), QString::number(value.toLongLong()));
    } else if (type == QMetaType::Double || type == QMetaType::Float) {
        result.insert(QStringLiteral("type"), QStringLiteral("number"));
        result.insert(QStringLiteral("value"), value.toDouble());
    } else if (type == QMetaType::QStringList) {
        result.insert(QStringLiteral("type"), QStringLiteral("string-list"));
        QJsonArray values;
        for (const QString &entry : value.toStringList()) {
            values.append(entry);
        }
        result.insert(QStringLiteral("value"), values);
    } else if (type == QMetaType::QDateTime) {
        result.insert(QStringLiteral("type"), QStringLiteral("datetime"));
        result.insert(
            QStringLiteral("value"), value.toDateTime().toUTC().toString(Qt::ISODateWithMs));
    } else if (type == QMetaType::QByteArray) {
        result.insert(QStringLiteral("type"), QStringLiteral("bytes"));
        result.insert(QStringLiteral("value"),
                      QString::fromLatin1(value.toByteArray().toBase64()));
    } else {
        result.insert(QStringLiteral("type"), QStringLiteral("string"));
        result.insert(QStringLiteral("value"), value.toString());
    }
    return result;
}

std::optional<QVariant> parseVariant(const QJsonValue &value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QString type = object.value(QStringLiteral("type")).toString();
    const QJsonValue stored = object.value(QStringLiteral("value"));
    if (type == QStringLiteral("bool") && stored.isBool()) {
        return stored.toBool();
    }
    if (type == QStringLiteral("integer") && stored.isString()) {
        bool valid = false;
        const qlonglong number = stored.toString().toLongLong(&valid);
        return valid ? std::optional<QVariant>(number) : std::nullopt;
    }
    if (type == QStringLiteral("number") && stored.isDouble()) {
        return stored.toDouble();
    }
    if (type == QStringLiteral("string-list") && stored.isArray()) {
        QStringList values;
        for (const QJsonValue &entry : stored.toArray()) {
            if (!entry.isString()) {
                return std::nullopt;
            }
            values.append(entry.toString());
        }
        return values;
    }
    if (type == QStringLiteral("datetime") && stored.isString()) {
        const QDateTime time = QDateTime::fromString(stored.toString(), Qt::ISODateWithMs);
        return time.isValid() ? std::optional<QVariant>(time) : std::nullopt;
    }
    if (type == QStringLiteral("bytes") && stored.isString()) {
        return QByteArray::fromBase64(stored.toString().toLatin1());
    }
    if (type == QStringLiteral("string") && stored.isString()) {
        return stored.toString();
    }
    return std::nullopt;
}

QByteArray settingsPayload(QSettings &settings, bool includeSecrets, QString *error = nullptr)
{
    clearError(error);
    QJsonObject values;
    const QString protectedApiKey = platform::SecureSecretStore::settingsKey(
        QStringLiteral("ai/apiKey"));
    const QStringList keys = settings.allKeys();
    for (const QString &key : keys) {
        if (key == protectedApiKey) {
            continue;
        }
        if (!includeSecrets && LocalBackupService::isSensitiveSettingKey(key)) {
            continue;
        }
        values.insert(key, variantJson(settings.value(key)));
    }
    if (includeSecrets) {
        QString secretError;
        const QString apiKey = platform::SecureSecretStore::readSecret(
            QStringLiteral("ai/apiKey"), settings, &secretError);
        if (!secretError.isEmpty()) {
            setError(error, secretError);
            return {};
        }
        if (!apiKey.isEmpty()) {
            values.insert(QStringLiteral("ai/apiKey"), variantJson(apiKey));
        }
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("quizapp-settings-v1")},
        {QStringLiteral("values"), values},
    }).toJson(QJsonDocument::Compact);
}

bool applySettingsPayload(
    const QByteArray &payload,
    bool includesSecrets,
    QSettings &settings,
    QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return setError(error, QStringLiteral("备份中的设置数据无法解析"));
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString()
        != QStringLiteral("quizapp-settings-v1")
        || !root.value(QStringLiteral("values")).isObject()) {
        return setError(error, QStringLiteral("备份中的设置格式不受支持"));
    }
    QHash<QString, QVariant> preservedSecrets;
    QString secretError;
    const QString preservedApiKey = platform::SecureSecretStore::readSecret(
        QStringLiteral("ai/apiKey"), settings, &secretError);
    if (!secretError.isEmpty()) {
        return setError(error, secretError);
    }
    if (!includesSecrets) {
        for (const QString &key : settings.allKeys()) {
            if (LocalBackupService::isSensitiveSettingKey(key)) {
                preservedSecrets.insert(key, settings.value(key));
            }
        }
    }
    QHash<QString, QVariant> restored;
    std::optional<QString> restoredApiKey;
    const QJsonObject values = root.value(QStringLiteral("values")).toObject();
    for (auto iterator = values.begin(); iterator != values.end(); ++iterator) {
        if (!includesSecrets && LocalBackupService::isSensitiveSettingKey(iterator.key())) {
            continue;
        }
        const auto value = parseVariant(iterator.value());
        if (!value) {
            return setError(
                error, QStringLiteral("设置项格式无效：%1").arg(iterator.key()));
        }
        if (iterator.key() == QStringLiteral("ai/apiKey")) {
            restoredApiKey = value->toString();
            continue;
        }
        if (iterator.key() == platform::SecureSecretStore::settingsKey(
                QStringLiteral("ai/apiKey"))) {
            continue;
        }
        restored.insert(iterator.key(), *value);
    }
    settings.clear();
    for (auto iterator = restored.cbegin(); iterator != restored.cend(); ++iterator) {
        settings.setValue(iterator.key(), iterator.value());
    }
    for (auto iterator = preservedSecrets.cbegin(); iterator != preservedSecrets.cend(); ++iterator) {
        settings.setValue(iterator.key(), iterator.value());
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        return setError(error, QStringLiteral("恢复设置时写入失败"));
    }
    const QString apiKey = includesSecrets && restoredApiKey.has_value()
        ? *restoredApiKey : preservedApiKey;
    if (!platform::SecureSecretStore::writeSecret(
            QStringLiteral("ai/apiKey"), apiKey, settings, error)) {
        return false;
    }
    return true;
}

QByteArray hashFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法读取备份源文件：%1").arg(path));
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kChunkSize);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            setError(error, QStringLiteral("读取备份源文件失败：%1").arg(path));
            return {};
        }
        hash.addData(chunk);
    }
    return hash.result();
}

bool excludedDataName(const QString &name)
{
    const QString lower = name.toLower();
    return lower == QStringLiteral("quizapp.sqlite")
        || lower == QStringLiteral("quizapp.sqlite-wal")
        || lower == QStringLiteral("quizapp.sqlite-shm")
        || lower == QStringLiteral("sharedstorage")
        || lower.startsWith(QStringLiteral(".backup-work-"))
        || lower == QStringLiteral(".backup-export-pending")
        || lower == QStringLiteral(".backup-import-pending")
        || lower.startsWith(QStringLiteral(".notebook-delete-"))
        || lower.startsWith(QStringLiteral(".restore-"))
        || lower == QStringLiteral(".quizapp-restore-pending");
}

bool collectFiles(
    const QString &root,
    const QString &prefix,
    const std::function<bool(const QString &)> &includeTopLevel,
    QVector<SourceEntry> *entries,
    QString *error)
{
    if (root.isEmpty() || !QDir(root).exists()) {
        return true;
    }
    QDirIterator iterator(
        root,
        QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    const QDir base(root);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        QString relative = base.relativeFilePath(absolute).replace(u'\\', u'/');
        const QString top = relative.section(u'/', 0, 0);
        if (!includeTopLevel(top)) {
            continue;
        }
        const QString logical = prefix + u'/' + relative;
        if (!safeRelativePath(logical)) {
            return setError(error, QStringLiteral("备份源路径不安全：%1").arg(relative));
        }
        SourceEntry entry;
        entry.info.path = logical;
        entry.info.size = info.size();
        entry.sourcePath = absolute;
        entries->append(entry);
        if (entries->size() > kMaximumEntryCount) {
            return setError(error, QStringLiteral("备份文件数量超过安全上限"));
        }
    }
    return true;
}

QJsonObject databaseCounts(QSqlDatabase database)
{
    const std::array<std::pair<QString, QString>, 8> queries{{
        {QStringLiteral("banks"), QStringLiteral("SELECT COUNT(*) FROM banks WHERE active=1")},
        {QStringLiteral("questions"), QStringLiteral(
            "SELECT COUNT(*) FROM questions q JOIN banks b ON b.id=q.bank_id WHERE b.active=1")},
        {QStringLiteral("practiceSessions"), QStringLiteral("SELECT COUNT(*) FROM practice_sessions")},
        {QStringLiteral("wrongBook"), QStringLiteral("SELECT COUNT(*) FROM wrong_book")},
        {QStringLiteral("reviewCards"), QStringLiteral("SELECT COUNT(*) FROM review_cards")},
        {QStringLiteral("examSessions"), QStringLiteral("SELECT COUNT(*) FROM exam_sessions")},
        {QStringLiteral("notebooks"), QStringLiteral("SELECT COUNT(*) FROM notebooks")},
        {QStringLiteral("studyEvents"), QStringLiteral("SELECT COUNT(*) FROM study_events")},
    }};
    QJsonObject counts;
    for (const auto &[name, statement] : queries) {
        QSqlQuery query(database);
        if (query.exec(statement) && query.next()) {
            counts.insert(name, static_cast<double>(query.value(0).toLongLong()));
        } else {
            counts.insert(name, 0);
        }
    }
    return counts;
}

bool snapshotDatabase(
    QSqlDatabase database,
    const QString &targetPath,
    QString *error)
{
    QFile::remove(targetPath);
    QSqlQuery checkpoint(database);
    checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(FULL)"));
    checkpoint.finish();
    QString escaped = QDir::fromNativeSeparators(targetPath);
    escaped.replace(u'\'', QStringLiteral("''"));
    QSqlQuery snapshot(database);
    if (!snapshot.exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped))) {
        return setError(
            error, QStringLiteral("创建数据库一致性快照失败：%1")
                       .arg(snapshot.lastError().text()));
    }
    return QFileInfo(targetPath).isFile();
}

QJsonObject entryJson(const BackupEntryInfo &entry)
{
    return {
        {QStringLiteral("path"), entry.path},
        {QStringLiteral("size"), static_cast<double>(entry.size)},
        {QStringLiteral("sha256"), QString::fromLatin1(entry.sha256.toHex())},
    };
}

ParsedArchive parseArchiveHeader(QFile &file)
{
    ParsedArchive result;
    if (!file.isOpen() || !file.isReadable()) {
        result.error = QStringLiteral("无法读取备份文件");
        return result;
    }
    QByteArray magic;
    if (!readExact(file, kMagic.size(), &magic) || magic != kMagic) {
        result.error = QStringLiteral("这不是 QuizApp v2 完整备份文件");
        return result;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::BigEndian);
    quint64 manifestSize = 0;
    stream >> manifestSize;
    if (stream.status() != QDataStream::Ok || manifestSize == 0
        || manifestSize > kMaximumManifestSize
        || manifestSize > static_cast<quint64>(file.size() - file.pos())) {
        result.error = QStringLiteral("备份清单长度无效");
        return result;
    }
    QByteArray manifestBytes;
    if (!readExact(file, static_cast<qint64>(manifestSize), &manifestBytes)) {
        result.error = QStringLiteral("备份清单被截断");
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("备份清单无法解析");
        return result;
    }
    result.manifest = document.object();
    if (result.manifest.value(QStringLiteral("format")).toString()
            != QStringLiteral("quizapp-local-backup")
        || result.manifest.value(QStringLiteral("schemaVersion")).toInt()
            != LocalBackupService::kSchemaVersion
        || !result.manifest.value(QStringLiteral("entries")).isArray()) {
        result.error = QStringLiteral("备份格式或版本不受支持");
        return result;
    }
    QSet<QString> paths;
    qint64 totalSize = 0;
    const QJsonArray entries = result.manifest.value(QStringLiteral("entries")).toArray();
    if (entries.isEmpty() || entries.size() > kMaximumEntryCount) {
        result.error = QStringLiteral("备份条目数量无效");
        return result;
    }
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        BackupEntryInfo entry;
        entry.path = object.value(QStringLiteral("path")).toString();
        entry.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble(-1));
        entry.sha256 = QByteArray::fromHex(
            object.value(QStringLiteral("sha256")).toString().toLatin1());
        if (!safeRelativePath(entry.path) || paths.contains(entry.path)
            || entry.size < 0 || entry.sha256.size() != 32
            || totalSize > std::numeric_limits<qint64>::max() - entry.size) {
            result.error = QStringLiteral("备份条目清单无效");
            return result;
        }
        paths.insert(entry.path);
        totalSize += entry.size;
        result.entries.append(entry);
    }
    result.payloadOffset = file.pos();
    if (totalSize != file.size() - result.payloadOffset) {
        result.error = QStringLiteral("备份载荷大小与清单不一致");
    }
    return result;
}

bool copyFileContents(QFile &source, QIODevice &destination, qint64 expectedSize)
{
    qint64 copied = 0;
    while (copied < expectedSize) {
        const qint64 wanted = std::min(kChunkSize, expectedSize - copied);
        QByteArray chunk;
        if (!readExact(source, wanted, &chunk) || !writeAll(destination, chunk)) {
            return false;
        }
        copied += chunk.size();
    }
    return true;
}

bool removePath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        return true;
    }
    if (info.isDir() && !info.isSymLink()) {
        return QDir(path).removeRecursively();
    }
    return QFile::remove(path);
}

bool copyPath(const QString &sourcePath, const QString &targetPath, QString *error)
{
    const QFileInfo source(sourcePath);
    if (source.isSymLink()) {
        return setError(error, QStringLiteral("恢复数据中不允许符号链接"));
    }
    if (source.isFile()) {
        if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
            return setError(error, QStringLiteral("无法创建恢复目标目录"));
        }
        QSaveFile target(targetPath);
        QFile input(sourcePath);
        if (!input.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)
            || !copyFileContents(input, target, source.size()) || !target.commit()) {
            return setError(error, QStringLiteral("无法写入恢复文件：%1").arg(targetPath));
        }
        return true;
    }
    if (!source.isDir()) {
        return setError(error, QStringLiteral("恢复源项目不存在：%1").arg(sourcePath));
    }
    if (!QDir().mkpath(targetPath)) {
        return setError(error, QStringLiteral("无法创建恢复目录：%1").arg(targetPath));
    }
    QDirIterator iterator(
        sourcePath,
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks
            | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    const QDir sourceRoot(sourcePath);
    while (iterator.hasNext()) {
        const QString sourceEntry = iterator.next();
        const QFileInfo entryInfo = iterator.fileInfo();
        const QString relative = sourceRoot.relativeFilePath(sourceEntry);
        const QString targetEntry = QDir(targetPath).filePath(relative);
        if (entryInfo.isDir()) {
            if (!QDir().mkpath(targetEntry)) {
                return setError(error, QStringLiteral("无法创建恢复子目录"));
            }
        } else if (!copyPath(sourceEntry, targetEntry, error)) {
            return false;
        }
    }
    return true;
}

QStringList topLevelNames(const QString &root, bool dataRoot)
{
    QStringList names;
    QDir directory(root);
    for (const QFileInfo &entry : directory.entryInfoList(
             QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot
                 | QDir::Hidden | QDir::System,
             QDir::Name)) {
        if (entry.isSymLink()) {
            continue;
        }
        if (dataRoot && excludedDataName(entry.fileName())) {
            continue;
        }
        names.append(entry.fileName());
    }
    return names;
}

bool replaceTarget(
    const QString &staged,
    const QString &target,
    const QString &backup,
    QVector<RestoreAction> *actions,
    QString *error)
{
    RestoreAction action;
    action.target = target;
    action.backup = backup;
    action.existed = QFileInfo::exists(target);
    if (action.existed) {
        if (!copyPath(target, backup, error)) {
            return false;
        }
        if (!removePath(target)) {
            return setError(error, QStringLiteral("无法暂存当前数据：%1").arg(target));
        }
    }
    actions->append(action);
    if (!staged.isEmpty() && QFileInfo::exists(staged)
        && !copyPath(staged, target, error)) {
        return false;
    }
    return true;
}

void rollbackActions(const QVector<RestoreAction> &actions)
{
    for (auto iterator = actions.crbegin(); iterator != actions.crend(); ++iterator) {
        removePath(iterator->target);
        if (iterator->existed && QFileInfo::exists(iterator->backup)) {
            QString ignored;
            copyPath(iterator->backup, iterator->target, &ignored);
        }
    }
}

} // namespace

QString LocalBackupService::suggestedFileName(const QDateTime &time)
{
    return QStringLiteral("QuizApp-backup-%1.quizbackup")
        .arg(time.toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

bool LocalBackupService::isSensitiveSettingKey(const QString &key)
{
    QString normalized = key.toLower();
    normalized.remove(u'-');
    normalized.remove(u'_');
    return normalized.contains(QStringLiteral("apikey"))
        || normalized.contains(QStringLiteral("accesstoken"))
        || normalized.contains(QStringLiteral("bearertoken"))
        || normalized.contains(QStringLiteral("authorization"));
}

bool LocalBackupService::create(
    const QString &destinationPath,
    QSqlDatabase database,
    const QString &databasePath,
    const QString &dataRoot,
    const QString &sharedRoot,
    QSettings &settings,
    bool includeSecrets,
    const QString &appVersion,
    const QString &buildCommit,
    BackupProgress progress,
    QString *error) const
{
    clearError(error);
    if (!database.isOpen() || destinationPath.isEmpty() || databasePath.isEmpty()
        || dataRoot.isEmpty() || !pathInside(databasePath, dataRoot)) {
        return setError(error, QStringLiteral("备份源或目标无效"));
    }
    QTemporaryDir workDirectory(
        QDir(dataRoot).filePath(QStringLiteral(".backup-work-XXXXXX")));
    if (!workDirectory.isValid()) {
        return setError(error, QStringLiteral("无法创建备份临时目录"));
    }
    if (progress) {
        progress(QStringLiteral("database"), 0, 1);
    }
    const QString databaseSnapshot = QDir(workDirectory.path()).filePath(
        QStringLiteral("quizapp.sqlite"));
    if (!snapshotDatabase(database, databaseSnapshot, error)) {
        return false;
    }

    QVector<SourceEntry> entries;
    SourceEntry settingsEntry;
    settingsEntry.info.path = QStringLiteral("settings/settings.json");
    settingsEntry.memoryData = settingsPayload(settings, includeSecrets, error);
    if (settingsEntry.memoryData.isEmpty()) {
        return false;
    }
    settingsEntry.info.size = settingsEntry.memoryData.size();
    entries.append(settingsEntry);
    SourceEntry databaseEntry;
    databaseEntry.info.path = QStringLiteral("database/quizapp.sqlite");
    databaseEntry.sourcePath = databaseSnapshot;
    databaseEntry.info.size = QFileInfo(databaseSnapshot).size();
    entries.append(databaseEntry);

    if (!collectFiles(
            dataRoot,
            QStringLiteral("data"),
            [](const QString &top) { return !excludedDataName(top); },
            &entries,
            error)) {
        return false;
    }
    const QStringList sharedGroups{
        QStringLiteral("QuestionBanks"),
        QStringLiteral("Notes"),
        QStringLiteral("RecycleBin"),
    };
    const bool includesSharedStorage = !sharedRoot.isEmpty() && QDir(sharedRoot).exists();
    if (includesSharedStorage) {
        if (!collectFiles(
                sharedRoot,
                QStringLiteral("shared"),
                [&sharedGroups](const QString &top) { return sharedGroups.contains(top); },
                &entries,
                error)) {
            return false;
        }
    }
    std::sort(entries.begin(), entries.end(), [](const SourceEntry &left, const SourceEntry &right) {
        return left.info.path < right.info.path;
    });
    QSet<QString> logicalPaths;
    qint64 totalBytes = 0;
    for (SourceEntry &entry : entries) {
        if (logicalPaths.contains(entry.info.path)) {
            return setError(error, QStringLiteral("备份条目发生重复"));
        }
        logicalPaths.insert(entry.info.path);
        entry.info.sha256 = entry.sourcePath.isEmpty()
            ? QCryptographicHash::hash(entry.memoryData, QCryptographicHash::Sha256)
            : hashFile(entry.sourcePath, error);
        if (entry.info.sha256.size() != 32) {
            return false;
        }
        totalBytes += entry.info.size;
    }

    QJsonArray entryArray;
    QSet<QString> dataRoots;
    for (const SourceEntry &entry : std::as_const(entries)) {
        entryArray.append(entryJson(entry.info));
        if (entry.info.path.startsWith(QStringLiteral("data/"))) {
            dataRoots.insert(entry.info.path.section(u'/', 1, 1));
        }
    }
    QJsonArray dataRootArray;
    QStringList sortedDataRoots(dataRoots.begin(), dataRoots.end());
    sortedDataRoots.sort(Qt::CaseInsensitive);
    for (const QString &name : std::as_const(sortedDataRoots)) {
        dataRootArray.append(name);
    }
    QJsonObject counts = databaseCounts(database);
    counts.insert(QStringLiteral("settings"), settings.allKeys().size());
    counts.insert(QStringLiteral("files"), entries.size() - 2);
    const QJsonObject manifest{
        {QStringLiteral("format"), QStringLiteral("quizapp-local-backup")},
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("appVersion"), appVersion},
        {QStringLiteral("buildCommit"), buildCommit},
        {QStringLiteral("platform"), QSysInfo::prettyProductName()},
        {QStringLiteral("includesSecrets"), includeSecrets},
        {QStringLiteral("includesSharedStorage"), includesSharedStorage},
        {QStringLiteral("payloadBytes"), static_cast<double>(totalBytes)},
        {QStringLiteral("counts"), counts},
        {QStringLiteral("dataRoots"), dataRootArray},
        {QStringLiteral("entries"), entryArray},
    };
    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QSaveFile output(destinationPath);
    if (!QDir().mkpath(QFileInfo(destinationPath).absolutePath())
        || !output.open(QIODevice::WriteOnly)
        || !writeAll(output, kMagic)) {
        return setError(error, QStringLiteral("无法创建备份文件"));
    }
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint64>(manifestBytes.size());
    if (stream.status() != QDataStream::Ok
        || !writeAll(output, manifestBytes)) {
        return setError(error, QStringLiteral("无法写入备份清单"));
    }
    qint64 completed = 0;
    for (const SourceEntry &entry : std::as_const(entries)) {
        if (entry.sourcePath.isEmpty()) {
            if (!writeAll(output, entry.memoryData)) {
                return setError(error, QStringLiteral("写入备份数据失败"));
            }
            completed += entry.memoryData.size();
        } else {
            QFile input(entry.sourcePath);
            if (!input.open(QIODevice::ReadOnly)
                || !copyFileContents(input, output, entry.info.size)) {
                return setError(error, QStringLiteral("写入备份文件失败：%1").arg(entry.info.path));
            }
            completed += entry.info.size;
        }
        if (progress) {
            progress(QStringLiteral("archive"), completed, totalBytes);
        }
    }
    if (!output.commit()) {
        return setError(error, QStringLiteral("无法提交完整备份文件"));
    }
    if (progress) {
        progress(QStringLiteral("complete"), totalBytes, totalBytes);
    }
    return true;
}

BackupInspection LocalBackupService::inspect(
    const QString &archivePath,
    bool verifyPayload,
    BackupProgress progress) const
{
    BackupInspection inspection;
    QFile file(archivePath);
    if (!file.open(QIODevice::ReadOnly)) {
        inspection.error = QStringLiteral("无法打开备份文件");
        return inspection;
    }
    const ParsedArchive parsed = parseArchiveHeader(file);
    if (!parsed.error.isEmpty()) {
        inspection.error = parsed.error;
        return inspection;
    }
    inspection.schemaVersion = parsed.manifest.value(QStringLiteral("schemaVersion")).toInt();
    inspection.appVersion = parsed.manifest.value(QStringLiteral("appVersion")).toString();
    inspection.buildCommit = parsed.manifest.value(QStringLiteral("buildCommit")).toString();
    inspection.platform = parsed.manifest.value(QStringLiteral("platform")).toString();
    inspection.createdAt = QDateTime::fromString(
        parsed.manifest.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
    inspection.includesSecrets = parsed.manifest.value(QStringLiteral("includesSecrets")).toBool();
    inspection.counts = parsed.manifest.value(QStringLiteral("counts")).toObject();
    inspection.entries = parsed.entries;
    for (const BackupEntryInfo &entry : parsed.entries) {
        inspection.payloadBytes += entry.size;
    }
    if (!verifyPayload) {
        inspection.valid = true;
        return inspection;
    }
    qint64 completed = 0;
    for (const BackupEntryInfo &entry : parsed.entries) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 remaining = entry.size;
        while (remaining > 0) {
            const qint64 wanted = std::min(kChunkSize, remaining);
            QByteArray chunk;
            if (!readExact(file, wanted, &chunk)) {
                inspection.error = QStringLiteral("备份载荷被截断");
                return inspection;
            }
            hash.addData(chunk);
            remaining -= chunk.size();
            completed += chunk.size();
            if (progress) {
                progress(QStringLiteral("verify"), completed, inspection.payloadBytes);
            }
        }
        if (hash.result() != entry.sha256) {
            inspection.error = QStringLiteral("备份校验失败：%1").arg(entry.path);
            return inspection;
        }
    }
    inspection.valid = file.pos() == file.size();
    if (!inspection.valid) {
        inspection.error = QStringLiteral("备份文件包含未声明的数据");
    }
    return inspection;
}

bool LocalBackupService::stageRestore(
    const QString &archivePath,
    const QString &dataRoot,
    BackupProgress progress,
    QString *error) const
{
    clearError(error);
    QFile file(archivePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return setError(error, QStringLiteral("无法打开备份文件"));
    }
    const ParsedArchive parsed = parseArchiveHeader(file);
    if (!parsed.error.isEmpty()) {
        return setError(error, parsed.error);
    }
    const QString pendingRoot = QDir(dataRoot).filePath(QStringLiteral(".quizapp-restore-pending"));
    if (!pathInside(pendingRoot, dataRoot)) {
        return setError(error, QStringLiteral("恢复暂存目录无效"));
    }
    removePath(pendingRoot);
    const QString payloadRoot = QDir(pendingRoot).filePath(QStringLiteral("payload"));
    if (!QDir().mkpath(payloadRoot)) {
        return setError(error, QStringLiteral("无法创建恢复暂存目录"));
    }
    qint64 total = 0;
    for (const BackupEntryInfo &entry : parsed.entries) {
        total += entry.size;
    }
    qint64 completed = 0;
    for (const BackupEntryInfo &entry : parsed.entries) {
        const QString destination = QDir(payloadRoot).filePath(entry.path);
        if (!pathInside(destination, payloadRoot)
            || !QDir().mkpath(QFileInfo(destination).absolutePath())) {
            removePath(pendingRoot);
            return setError(error, QStringLiteral("备份条目路径无效"));
        }
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly)) {
            removePath(pendingRoot);
            return setError(error, QStringLiteral("无法暂存恢复条目"));
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 remaining = entry.size;
        while (remaining > 0) {
            const qint64 wanted = std::min(kChunkSize, remaining);
            QByteArray chunk;
            if (!readExact(file, wanted, &chunk) || !writeAll(output, chunk)) {
                removePath(pendingRoot);
                return setError(error, QStringLiteral("恢复载荷被截断"));
            }
            hash.addData(chunk);
            remaining -= chunk.size();
            completed += chunk.size();
            if (progress) {
                progress(QStringLiteral("stage"), completed, total);
            }
        }
        if (hash.result() != entry.sha256 || !output.commit()) {
            removePath(pendingRoot);
            return setError(error, QStringLiteral("恢复条目校验失败：%1").arg(entry.path));
        }
    }
    QSaveFile marker(QDir(pendingRoot).filePath(QStringLiteral("ready.json")));
    if (!marker.open(QIODevice::WriteOnly)) {
        removePath(pendingRoot);
        return setError(error, QStringLiteral("无法创建恢复标记"));
    }
    const QByteArray markerData = QJsonDocument(parsed.manifest).toJson(QJsonDocument::Compact);
    if (!writeAll(marker, markerData) || !marker.commit()) {
        removePath(pendingRoot);
        return setError(error, QStringLiteral("无法提交恢复标记"));
    }
    return true;
}

bool LocalBackupService::hasPendingRestore(const QString &dataRoot) const
{
    return QFileInfo(QDir(dataRoot).filePath(
        QStringLiteral(".quizapp-restore-pending/ready.json"))).isFile();
}

bool LocalBackupService::applyPendingRestore(
    const QString &dataRoot,
    const QString &databasePath,
    const QString &sharedRoot,
    QSettings &settings,
    QString *error) const
{
    clearError(error);
    const QString pendingRoot = QDir(dataRoot).filePath(QStringLiteral(".quizapp-restore-pending"));
    const QString markerPath = QDir(pendingRoot).filePath(QStringLiteral("ready.json"));
    if (!QFileInfo(markerPath).isFile()) {
        return true;
    }
    QFile marker(markerPath);
    if (!marker.open(QIODevice::ReadOnly)) {
        return setError(error, QStringLiteral("无法读取待恢复清单"));
    }
    const QByteArray markerData = marker.readAll();
    marker.close();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(markerData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return setError(error, QStringLiteral("待恢复清单已损坏"));
    }
    const QJsonObject manifest = document.object();
    if (manifest.value(QStringLiteral("format")).toString()
            != QStringLiteral("quizapp-local-backup")
        || manifest.value(QStringLiteral("schemaVersion")).toInt() != kSchemaVersion) {
        return setError(error, QStringLiteral("待恢复版本不受支持"));
    }
    const QString payloadRoot = QDir(pendingRoot).filePath(QStringLiteral("payload"));
    const QString stagedDatabase = QDir(payloadRoot).filePath(
        QStringLiteral("database/quizapp.sqlite"));
    const QString stagedSettings = QDir(payloadRoot).filePath(
        QStringLiteral("settings/settings.json"));
    if (!QFileInfo(stagedDatabase).isFile() || !QFileInfo(stagedSettings).isFile()) {
        return setError(error, QStringLiteral("待恢复数据不完整"));
    }
    const bool includesSharedStorage = manifest.value(
        QStringLiteral("includesSharedStorage")).toBool();
    if (includesSharedStorage && sharedRoot.isEmpty()) {
        return setError(error, QStringLiteral("共享存储尚未授权，无法应用完整恢复"));
    }

    const QString rollbackRoot = QDir(dataRoot).filePath(QStringLiteral(".restore-rollback"));
    removePath(rollbackRoot);
    if (!QDir().mkpath(rollbackRoot)) {
        return setError(error, QStringLiteral("无法创建恢复回滚目录"));
    }
    const QByteArray settingsBefore = settingsPayload(settings, true, error);
    if (settingsBefore.isEmpty()) {
        return false;
    }
    QVector<RestoreAction> actions;
    QString operationError;

    const QString databaseBackup = QDir(rollbackRoot).filePath(QStringLiteral("database/quizapp.sqlite"));
    if (!replaceTarget(stagedDatabase, databasePath, databaseBackup, &actions, &operationError)) {
        rollbackActions(actions);
        removePath(rollbackRoot);
        return setError(error, QStringLiteral("恢复失败，原数据已回滚：%1").arg(operationError));
    }
    for (const QString &suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        const QString target = databasePath + suffix;
        if (QFileInfo::exists(target)
            && !replaceTarget({}, target,
                QDir(rollbackRoot).filePath(QStringLiteral("database/quizapp.sqlite") + suffix),
                &actions, &operationError)) {
            rollbackActions(actions);
            removePath(rollbackRoot);
            return setError(error, QStringLiteral("恢复失败，原数据已回滚：%1").arg(operationError));
        }
    }

    QStringList dataNames = topLevelNames(dataRoot, true);
    const QString stagedDataRoot = QDir(payloadRoot).filePath(QStringLiteral("data"));
    dataNames.append(topLevelNames(stagedDataRoot, false));
    dataNames.removeDuplicates();
    for (const QString &name : std::as_const(dataNames)) {
        if (excludedDataName(name)) {
            continue;
        }
        const QString staged = QDir(stagedDataRoot).filePath(name);
        const QString target = QDir(dataRoot).filePath(name);
        const QString backup = QDir(rollbackRoot).filePath(QStringLiteral("data/") + name);
        if (!replaceTarget(QFileInfo::exists(staged) ? staged : QString(),
                           target, backup, &actions, &operationError)) {
            rollbackActions(actions);
            applySettingsPayload(settingsBefore, true, settings, nullptr);
            removePath(rollbackRoot);
            return setError(error, QStringLiteral("恢复失败，原数据已回滚：%1").arg(operationError));
        }
    }

    if (includesSharedStorage) {
        const QStringList sharedNames{
            QStringLiteral("QuestionBanks"),
            QStringLiteral("Notes"),
            QStringLiteral("RecycleBin"),
        };
        for (const QString &name : sharedNames) {
            const QString staged = QDir(payloadRoot).filePath(QStringLiteral("shared/") + name);
            const QString target = QDir(sharedRoot).filePath(name);
            const QString backup = QDir(rollbackRoot).filePath(QStringLiteral("shared/") + name);
            if (!replaceTarget(QFileInfo::exists(staged) ? staged : QString(),
                               target, backup, &actions, &operationError)) {
                rollbackActions(actions);
                applySettingsPayload(settingsBefore, true, settings, nullptr);
                removePath(rollbackRoot);
                return setError(error, QStringLiteral("恢复失败，原数据已回滚：%1").arg(operationError));
            }
        }
    }

    QFile restoredSettings(stagedSettings);
    if (!restoredSettings.open(QIODevice::ReadOnly)) {
        rollbackActions(actions);
        applySettingsPayload(settingsBefore, true, settings, nullptr);
        removePath(rollbackRoot);
        return setError(error, QStringLiteral("恢复失败，原数据已回滚：无法读取设置数据"));
    }
    const QByteArray restoredSettingsData = restoredSettings.readAll();
    restoredSettings.close();
    if (!applySettingsPayload(
            restoredSettingsData,
            manifest.value(QStringLiteral("includesSecrets")).toBool(),
            settings,
            &operationError)) {
        rollbackActions(actions);
        applySettingsPayload(settingsBefore, true, settings, nullptr);
        removePath(rollbackRoot);
        return setError(error, QStringLiteral("恢复失败，原数据已回滚：%1").arg(operationError));
    }
    removePath(rollbackRoot);
    QFile::remove(markerPath);
    removePath(pendingRoot);
    return true;
}

} // namespace quizapp::services
