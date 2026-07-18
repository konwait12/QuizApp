#pragma once

#include "services/BankReleaseService.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

class QSettings;

namespace quizapp::services {

struct BankReleaseState {
    QDateTime lastCheckedAt;
    QString lastCheckedTag;
    QByteArray lastCatalogFingerprint;
    QHash<QString, QByteArray> installedEntryFingerprints;
};

class BankReleaseStateStore final {
public:
    static QString settingsKey();
    static QString entryKey(const BankReleaseEntry &entry);
    static QByteArray entryFingerprint(const BankReleaseEntry &entry);
    static QByteArray catalogFingerprint(const BankReleaseCatalog &catalog);
    static BankReleaseState load(const QSettings &settings);
    static QStringList outdatedEntryIds(
        const BankReleaseCatalog &catalog,
        const BankReleaseState &state);
    static bool recordCheck(
        QSettings &settings,
        const BankReleaseCatalog &catalog,
        const QDateTime &checkedAt = QDateTime::currentDateTimeUtc(),
        QString *error = nullptr);
    static bool recordInstall(
        QSettings &settings,
        const BankReleaseCatalog &catalog,
        const QVector<BankReleaseSelection> &selections,
        const QDateTime &installedAt = QDateTime::currentDateTimeUtc(),
        QString *error = nullptr);

private:
    static bool save(
        QSettings &settings,
        const BankReleaseState &state,
        QString *error);
};

} // namespace quizapp::services
