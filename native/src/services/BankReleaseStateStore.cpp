#include "services/BankReleaseStateStore.h"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>

namespace quizapp::services {
namespace {

constexpr int kStateSchemaVersion = 1;

void addField(QCryptographicHash *hash, const QByteArray &value)
{
    hash->addData(QByteArray::number(value.size()));
    hash->addData(QByteArrayView(":", 1));
    hash->addData(value);
    hash->addData(QByteArrayView("\n", 1));
}

QByteArray decodedDigest(const QJsonValue &value)
{
    const QByteArray digest = QByteArray::fromHex(value.toString().toLatin1());
    return digest.size() == 32 ? digest : QByteArray{};
}

} // namespace

QString BankReleaseStateStore::settingsKey()
{
    return QStringLiteral("updates/banks/stateV1");
}

QString BankReleaseStateStore::entryKey(const BankReleaseEntry &entry)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addField(&hash, entry.path.join(u'/').normalized(
        QString::NormalizationForm_KC).toUtf8());
    addField(&hash, entry.assetName.trimmed().toUtf8());
    return QString::fromLatin1(hash.result().toHex().left(24));
}

QByteArray BankReleaseStateStore::entryFingerprint(const BankReleaseEntry &entry)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addField(&hash, entryKey(entry).toUtf8());
    addField(&hash, entry.name.trimmed().toUtf8());
    addField(&hash, QByteArray::number(entry.questionCount));
    addField(&hash, QByteArray::number(entry.expectedByteSize));
    addField(&hash, entry.expectedSha256.toHex());
    addField(&hash, QByteArray::number(entry.assetApiId));
    addField(&hash, entry.assetUpdatedAt.toUtf8());
    addField(&hash, entry.downloadUrl.toUtf8());
    addField(&hash, QCryptographicHash::hash(
        entry.embeddedPayload, QCryptographicHash::Sha256).toHex());
    return hash.result();
}

QByteArray BankReleaseStateStore::catalogFingerprint(
    const BankReleaseCatalog &catalog)
{
    QVector<QPair<QString, QByteArray>> entries;
    entries.reserve(catalog.entries.size());
    for (const BankReleaseEntry &entry : catalog.entries) {
        entries.append({entryKey(entry), entryFingerprint(entry)});
    }
    std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
        return left.first < right.first;
    });
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addField(&hash, catalog.tagName.toUtf8());
    for (const auto &[key, fingerprint] : entries) {
        addField(&hash, key.toUtf8());
        addField(&hash, fingerprint.toHex());
    }
    return hash.result();
}

BankReleaseState BankReleaseStateStore::load(const QSettings &settings)
{
    BankReleaseState state;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        settings.value(settingsKey()).toByteArray(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return state;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != kStateSchemaVersion) {
        return state;
    }
    state.lastCheckedAt = QDateTime::fromString(
        root.value(QStringLiteral("lastCheckedAt")).toString(), Qt::ISODateWithMs);
    state.lastCheckedTag = root.value(QStringLiteral("lastCheckedTag")).toString();
    state.lastCatalogFingerprint = decodedDigest(
        root.value(QStringLiteral("lastCatalogFingerprint")));
    const QJsonObject installed = root.value(QStringLiteral("installedEntries")).toObject();
    for (auto iterator = installed.constBegin(); iterator != installed.constEnd(); ++iterator) {
        const QByteArray fingerprint = decodedDigest(iterator.value());
        if (!fingerprint.isEmpty()) {
            state.installedEntryFingerprints.insert(iterator.key(), fingerprint);
        }
    }
    return state;
}

QStringList BankReleaseStateStore::outdatedEntryIds(
    const BankReleaseCatalog &catalog,
    const BankReleaseState &state)
{
    QStringList ids;
    for (const BankReleaseEntry &entry : catalog.entries) {
        if (state.installedEntryFingerprints.value(entryKey(entry))
            != entryFingerprint(entry)) {
            ids.append(entry.id);
        }
    }
    return ids;
}

bool BankReleaseStateStore::recordCheck(
    QSettings &settings,
    const BankReleaseCatalog &catalog,
    const QDateTime &checkedAt,
    QString *error)
{
    BankReleaseState state = load(settings);
    state.lastCheckedAt = checkedAt.toUTC();
    state.lastCheckedTag = catalog.tagName;
    state.lastCatalogFingerprint = catalogFingerprint(catalog);
    return save(settings, state, error);
}

bool BankReleaseStateStore::recordInstall(
    QSettings &settings,
    const BankReleaseCatalog &catalog,
    const QVector<BankReleaseSelection> &selections,
    const QDateTime &installedAt,
    QString *error)
{
    BankReleaseState state = load(settings);
    state.lastCheckedAt = installedAt.toUTC();
    state.lastCheckedTag = catalog.tagName;
    state.lastCatalogFingerprint = catalogFingerprint(catalog);
    for (const BankReleaseSelection &selection : selections) {
        if (selection.conflictPolicy == BankReleaseConflictPolicy::Skip) {
            continue;
        }
        state.installedEntryFingerprints.insert(
            entryKey(selection.entry), entryFingerprint(selection.entry));
    }
    return save(settings, state, error);
}

bool BankReleaseStateStore::save(
    QSettings &settings,
    const BankReleaseState &state,
    QString *error)
{
    if (error) {
        error->clear();
    }
    QJsonObject installed;
    for (auto iterator = state.installedEntryFingerprints.constBegin();
         iterator != state.installedEntryFingerprints.constEnd(); ++iterator) {
        installed.insert(iterator.key(), QString::fromLatin1(iterator.value().toHex()));
    }
    const QJsonObject root{
        {QStringLiteral("schemaVersion"), kStateSchemaVersion},
        {QStringLiteral("lastCheckedAt"),
         state.lastCheckedAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("lastCheckedTag"), state.lastCheckedTag},
        {QStringLiteral("lastCatalogFingerprint"),
         QString::fromLatin1(state.lastCatalogFingerprint.toHex())},
        {QStringLiteral("installedEntries"), installed},
    };
    settings.setValue(settingsKey(), QJsonDocument(root).toJson(QJsonDocument::Compact));
    settings.sync();
    if (settings.status() == QSettings::NoError) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("题库更新状态保存失败");
    }
    return false;
}

} // namespace quizapp::services
