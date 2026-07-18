#include "services/AnnouncementService.h"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

#include <algorithm>

namespace quizapp::services {
namespace {

const QString kCacheKey = QStringLiteral("updates/announcements/cacheV1");
const QString kReadIdsKey = QStringLiteral("updates/announcements/readIds");
const QString kLastCheckedKey = QStringLiteral("updates/announcements/lastCheckedAt");
const QString kSuppressedFingerprintKey =
    QStringLiteral("updates/announcements/suppressedFingerprint");

bool fail(const QString &message, QString *error)
{
    if (error) {
        *error = message;
    }
    return false;
}

QString sanitizedHtml(QString html)
{
    static const QRegularExpression scriptTags(
        QStringLiteral(R"(<\s*(script|style|iframe|object|embed)\b[^>]*>[\s\S]*?<\s*/\s*\1\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression standaloneTags(
        QStringLiteral(R"(<\s*(script|style|iframe|object|embed)\b[^>]*/?\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression quotedEvents(
        QStringLiteral(R"(\s+on[a-z]+\s*=\s*(["']).*?\1)"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression unquotedEvents(
        QStringLiteral(R"(\s+on[a-z]+\s*=\s*[^\s>]+)"),
        QRegularExpression::CaseInsensitiveOption);
    html.remove(scriptTags);
    html.remove(standaloneTags);
    html.remove(quotedEvents);
    html.remove(unquotedEvents);
    html.replace(
        QRegularExpression(QStringLiteral("javascript:"),
                           QRegularExpression::CaseInsensitiveOption),
        QString());
    return html.trimmed();
}

QString fallbackId(const QString &date, const QString &title)
{
    return QStringLiteral("announcement-%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(
            (date + u'\n' + title).toUtf8(), QCryptographicHash::Sha256)
            .toHex().left(20)));
}

QJsonObject itemObject(const AnnouncementItem &item)
{
    return {
        {QStringLiteral("id"), item.id},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("date"), item.date},
        {QStringLiteral("body"), item.bodyHtml},
        {QStringLiteral("latest"), item.latest},
    };
}

bool saveSettings(QSettings &settings, QString *error)
{
    settings.sync();
    if (settings.status() == QSettings::NoError) {
        if (error) {
            error->clear();
        }
        return true;
    }
    return fail(QStringLiteral("公告状态保存失败"), error);
}

} // namespace

QStringList AnnouncementService::assetNames()
{
    return {
        QStringLiteral("quizapp-announcements.json"),
        QStringLiteral("QuizApp-announcements.json"),
        QStringLiteral("announcements.json"),
    };
}

bool AnnouncementService::parseCatalog(
    const QByteArray &payload,
    AnnouncementCatalog *catalog,
    QString *error)
{
    if (error) {
        error->clear();
    }
    if (!catalog) {
        return fail(QStringLiteral("公告目录目标无效"), error);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || (!document.isArray() && !document.isObject())) {
        return fail(QStringLiteral("公告文件格式无效"), error);
    }
    const QJsonArray array = document.isArray()
        ? document.array()
        : document.object().value(QStringLiteral("announcements")).toArray();
    QVector<AnnouncementItem> items;
    QSet<QString> ids;
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            continue;
        }
        const QJsonObject object = array.at(index).toObject();
        AnnouncementItem item;
        item.title = object.value(QStringLiteral("title")).toString(
            object.value(QStringLiteral("name")).toString()).trimmed();
        item.date = object.value(QStringLiteral("date")).toString(
            object.value(QStringLiteral("publishedAt")).toString(
                object.value(QStringLiteral("updatedAt")).toString())).trimmed();
        item.id = object.value(QStringLiteral("id")).toString().trimmed();
        if (item.id.isEmpty()) {
            item.id = fallbackId(item.date, item.title);
        }
        QString body = object.value(QStringLiteral("bodyHtml")).toString(
            object.value(QStringLiteral("html")).toString(
                object.value(QStringLiteral("body")).toString()));
        if (body.trimmed().isEmpty()) {
            const QString text = object.value(QStringLiteral("text")).toString().trimmed();
            if (!text.isEmpty()) {
                body = QStringLiteral("<p>%1</p>").arg(text.toHtmlEscaped());
            }
        }
        item.bodyHtml = sanitizedHtml(body);
        item.latest = object.value(QStringLiteral("latest")).toBool(false);
        if (item.id.isEmpty() || item.title.isEmpty() || item.bodyHtml.isEmpty()
            || ids.contains(item.id)) {
            continue;
        }
        ids.insert(item.id);
        items.append(item);
    }
    if (items.isEmpty()) {
        return fail(QStringLiteral("公告文件没有可显示内容"), error);
    }
    if (std::none_of(items.cbegin(), items.cend(), [](const AnnouncementItem &item) {
            return item.latest;
        })) {
        items[0].latest = true;
    }
    catalog->items = items;
    catalog->fingerprint = fingerprint(items);
    return true;
}

bool AnnouncementService::announcementAssetUrl(
    const QByteArray &releaseJson,
    QString *downloadUrl,
    QString *error)
{
    if (error) {
        error->clear();
    }
    if (!downloadUrl) {
        return fail(QStringLiteral("公告下载地址目标无效"), error);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(releaseJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("GitHub Release 响应格式无效"), error);
    }
    const QStringList names = assetNames();
    for (const QJsonValue &value : document.object().value(
             QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = value.toObject();
        if (!names.contains(asset.value(QStringLiteral("name")).toString())) {
            continue;
        }
        *downloadUrl = asset.value(QStringLiteral("browser_download_url")).toString(
            asset.value(QStringLiteral("url")).toString());
        if (!downloadUrl->isEmpty()) {
            return true;
        }
    }
    return fail(QStringLiteral("当前 Release 没有公告文件"), error);
}

QByteArray AnnouncementService::fingerprint(
    const QVector<AnnouncementItem> &items)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const AnnouncementItem &item : items) {
        hash.addData(QJsonDocument(itemObject(item)).toJson(QJsonDocument::Compact));
        hash.addData(QByteArrayView("\n", 1));
    }
    return hash.result();
}

AnnouncementState AnnouncementStateStore::load(const QSettings &settings)
{
    AnnouncementState state;
    const QByteArray cached = settings.value(kCacheKey).toByteArray();
    if (!cached.isEmpty()) {
        AnnouncementService::parseCatalog(cached, &state.cachedCatalog);
    }
    state.readIds = settings.value(kReadIdsKey).toStringList();
    state.lastCheckedAt = QDateTime::fromString(
        settings.value(kLastCheckedKey).toString(), Qt::ISODateWithMs);
    const QByteArray suppressed = QByteArray::fromHex(
        settings.value(kSuppressedFingerprintKey).toByteArray());
    if (suppressed.size() == 32) {
        state.suppressedCatalogFingerprint = suppressed;
    }
    return state;
}

QStringList AnnouncementStateStore::unreadIds(const AnnouncementState &state)
{
    const QSet<QString> read(state.readIds.cbegin(), state.readIds.cend());
    QStringList unread;
    for (const AnnouncementItem &item : state.cachedCatalog.items) {
        if (!read.contains(item.id)) {
            unread.append(item.id);
        }
    }
    return unread;
}

bool AnnouncementStateStore::saveCatalog(
    QSettings &settings,
    const AnnouncementCatalog &catalog,
    const QDateTime &checkedAt,
    QString *error)
{
    QJsonArray items;
    for (const AnnouncementItem &item : catalog.items) {
        items.append(itemObject(item));
    }
    settings.setValue(
        kCacheKey,
        QJsonDocument(QJsonObject{{QStringLiteral("announcements"), items}})
            .toJson(QJsonDocument::Compact));
    settings.setValue(kLastCheckedKey, checkedAt.toUTC().toString(Qt::ISODateWithMs));
    return saveSettings(settings, error);
}

bool AnnouncementStateStore::markAllRead(
    QSettings &settings,
    const AnnouncementCatalog &catalog,
    bool suppressCurrentCatalog,
    QString *error)
{
    QSet<QString> ids;
    const QStringList current = settings.value(kReadIdsKey).toStringList();
    for (const QString &id : current) {
        ids.insert(id);
    }
    for (const AnnouncementItem &item : catalog.items) {
        ids.insert(item.id);
    }
    QStringList ordered = ids.values();
    std::sort(ordered.begin(), ordered.end());
    settings.setValue(kReadIdsKey, ordered);
    if (suppressCurrentCatalog) {
        settings.setValue(
            kSuppressedFingerprintKey,
            QString::fromLatin1(catalog.fingerprint.toHex()));
    }
    return saveSettings(settings, error);
}

} // namespace quizapp::services
