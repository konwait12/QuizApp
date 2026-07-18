#include "services/NotebookService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace quizapp::services {
namespace {

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

} // namespace

NotebookService::NotebookService(QSqlDatabase database, QString dataRoot)
    : repository_(std::move(database))
    , dataRoot_(std::move(dataRoot))
{
}

std::optional<domain::NotebookRecord> NotebookService::createFree(
    const QString &title, QString *error) const
{
    if (error) error->clear();
    const QString cleanTitle = normalizedTitle(title);
    if (cleanTitle.isEmpty()) {
        fail(error, QStringLiteral("笔记标题不能为空"));
        return std::nullopt;
    }
    domain::NotebookRecord record;
    record.id = QUuid::createUuid();
    record.title = cleanTitle;
    record.relativePath = QStringLiteral("notes/free/%1.snb").arg(
        record.id.toString(QUuid::WithoutBraces));
    record.contentHash = QCryptographicHash::hash({}, QCryptographicHash::Sha256);
    record.createdAt = QDateTime::currentDateTimeUtc();
    record.updatedAt = record.createdAt;
    if (!repository_.create(record, error)) return std::nullopt;
    return record;
}

QVector<domain::NotebookRecord> NotebookService::listFree(
    bool recycled, QString *error) const
{
    return repository_.listFree(recycled, error);
}

bool NotebookService::rename(
    const QUuid &id, const QString &title, QString *error) const
{
    const QString cleanTitle = normalizedTitle(title);
    return !cleanTitle.isEmpty()
        ? repository_.rename(id, cleanTitle, error)
        : fail(error, QStringLiteral("笔记标题不能为空"));
}

bool NotebookService::recycle(const QUuid &id, QString *error) const
{
    return repository_.setRecycled(id, true, error);
}

bool NotebookService::restore(const QUuid &id, QString *error) const
{
    return repository_.setRecycled(id, false, error);
}

bool NotebookService::permanentlyDelete(const QUuid &id, QString *error) const
{
    const auto record = repository_.findById(id, error);
    if (!record || !record->deletedAt.has_value()) {
        return fail(error, QStringLiteral("只能彻底删除回收站中的自由笔记"));
    }
    const QString bundle = absoluteBundlePath(*record);
    if (!bundle.isEmpty() && QFileInfo::exists(bundle)) {
        const QString temporary = QDir(dataRoot_).filePath(
            QStringLiteral(".notebook-delete-%1").arg(id.toString(QUuid::WithoutBraces)));
        QDir(temporary).removeRecursively();
        if (!QDir().rename(bundle, temporary)) {
            return fail(error, QStringLiteral("无法暂存待删除的笔记文件"));
        }
        if (!repository_.remove(id, error)) {
            QDir().rename(temporary, bundle);
            return false;
        }
        if (!QDir(temporary).removeRecursively()) {
            return fail(error, QStringLiteral("笔记记录已删除，但临时文件清理失败"));
        }
        return true;
    }
    return repository_.remove(id, error);
}

bool NotebookService::markSaved(const QUuid &id, QString *error) const
{
    const auto record = repository_.findById(id, error);
    if (!record) return fail(error, QStringLiteral("自由笔记不存在"));
    QFile manifest(QDir(absoluteBundlePath(*record)).filePath(QStringLiteral("document.json")));
    QByteArray hash;
    if (manifest.open(QIODevice::ReadOnly)) {
        hash = QCryptographicHash::hash(manifest.readAll(), QCryptographicHash::Sha256);
    }
    return repository_.touch(id, hash, error);
}

QString NotebookService::absoluteBundlePath(const domain::NotebookRecord &record) const
{
    if (!safeRelativePath(record.relativePath)) return {};
    return QDir(dataRoot_).filePath(record.relativePath);
}

QString NotebookService::normalizedTitle(const QString &title) const
{
    return title.simplified().left(80);
}

bool NotebookService::safeRelativePath(const QString &path) const
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(u'\\')) return false;
    const QString clean = QDir::cleanPath(path);
    return clean == path && !clean.startsWith(QStringLiteral("../"))
        && !clean.contains(QStringLiteral("/../"));
}

} // namespace quizapp::services
