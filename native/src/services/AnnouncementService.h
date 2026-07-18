#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

class QSettings;

namespace quizapp::services {

struct AnnouncementItem {
    QString id;
    QString title;
    QString date;
    QString bodyHtml;
    bool latest = false;
};

struct AnnouncementCatalog {
    QVector<AnnouncementItem> items;
    QByteArray fingerprint;
};

class AnnouncementService final {
public:
    static QStringList assetNames();
    static bool parseCatalog(
        const QByteArray &payload,
        AnnouncementCatalog *catalog,
        QString *error = nullptr);
    static bool announcementAssetUrl(
        const QByteArray &releaseJson,
        QString *downloadUrl,
        QString *error = nullptr);
    static QByteArray fingerprint(const QVector<AnnouncementItem> &items);
};

struct AnnouncementState {
    AnnouncementCatalog cachedCatalog;
    QStringList readIds;
    QDateTime lastCheckedAt;
    QByteArray suppressedCatalogFingerprint;
};

class AnnouncementStateStore final {
public:
    static AnnouncementState load(const QSettings &settings);
    static QStringList unreadIds(const AnnouncementState &state);
    static bool saveCatalog(
        QSettings &settings,
        const AnnouncementCatalog &catalog,
        const QDateTime &checkedAt = QDateTime::currentDateTimeUtc(),
        QString *error = nullptr);
    static bool markAllRead(
        QSettings &settings,
        const AnnouncementCatalog &catalog,
        bool suppressCurrentCatalog,
        QString *error = nullptr);
};

} // namespace quizapp::services
