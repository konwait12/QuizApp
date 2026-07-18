#include "services/LegacyMigrationService.h"

#include <QCryptographicHash>
#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <array>
#include <utility>

namespace quizapp::services {
namespace {

constexpr qsizetype kMaximumPackageBytes = 512LL * 1024 * 1024;
const QRegularExpression kDateKey(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));

const QSet<QString> kQuestionStateKeys{
    QStringLiteral("quizapp_wrong_book"),
    QStringLiteral("quizapp_saved_session"),
    QStringLiteral("quizapp_saved_progress"),
    QStringLiteral("quizapp_view_progress"),
    QStringLiteral("quizapp_answer_state"),
};

bool fail(const QString &message, QString *error)
{
    if (error) {
        *error = message;
    }
    return false;
}

QString compactJson(const QJsonValue &value)
{
    if (value.isObject()) {
        return QString::fromUtf8(
            QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(
            QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    const QByteArray wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}

QString normalizedQuestionText(const QString &value)
{
    return value.simplified().toCaseFolded();
}

QByteArray questionSignature(const QString &prompt, const QStringList &options)
{
    QJsonArray normalizedOptions;
    for (const QString &option : options) {
        normalizedOptions.append(normalizedQuestionText(option));
    }
    const QJsonObject canonical{
        {QStringLiteral("prompt"), normalizedQuestionText(prompt)},
        {QStringLiteral("options"), normalizedOptions},
    };
    return QCryptographicHash::hash(
        QJsonDocument(canonical).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256);
}

QString optionText(const QJsonValue &value)
{
    if (value.isString()) return value.toString();
    const QJsonObject object = value.toObject();
    for (const QString &key : {QStringLiteral("text"), QStringLiteral("label"),
                              QStringLiteral("value")}) {
        const QString text = object.value(key).toString();
        if (!text.isEmpty()) return text;
    }
    return {};
}

void collectBankQuestionSignatures(
    const QJsonValue &value,
    QHash<QString, QByteArray> *signatures)
{
    QJsonArray banks;
    if (value.isArray()) {
        banks = value.toArray();
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("questions")).isArray()) {
            banks.append(object);
        } else {
            for (const QJsonValue &entry : object) {
                if (entry.isObject()) banks.append(entry);
            }
        }
    }
    for (const QJsonValue &entry : banks) {
        const QJsonObject bank = entry.toObject();
        QString bankId = bank.value(QStringLiteral("id")).toVariant().toString().trimmed();
        if (bankId.isEmpty()) bankId = bank.value(QStringLiteral("name")).toString().trimmed();
        if (bankId.isEmpty()) continue;
        const QJsonArray questions = bank.value(QStringLiteral("questions")).toArray();
        for (int index = 0; index < questions.size(); ++index) {
            const QJsonObject question = questions.at(index).toObject();
            QString prompt = question.value(QStringLiteral("q")).toString();
            if (prompt.isEmpty()) prompt = question.value(QStringLiteral("prompt")).toString();
            if (prompt.isEmpty()) prompt = question.value(QStringLiteral("question")).toString();
            QStringList options;
            for (const QJsonValue &option : question.value(QStringLiteral("options")).toArray()) {
                options.append(optionText(option));
            }
            if (!prompt.trimmed().isEmpty()) {
                signatures->insert(
                    QStringLiteral("%1:%2").arg(bankId).arg(index),
                    questionSignature(prompt, options));
            }
        }
    }
}

QHash<QString, QByteArray> collectLegacyQuestionSignatures(
    const QHash<QString, QJsonValue> &storage,
    const QJsonObject &indexedDatabases)
{
    QHash<QString, QByteArray> signatures;
    collectBankQuestionSignatures(
        storage.value(QStringLiteral("quizapp_banks")), &signatures);
    for (const QJsonValue &databaseValue : indexedDatabases) {
        const QJsonObject database = databaseValue.toObject();
        const QJsonObject stores = database.value(QStringLiteral("stores")).isObject()
            ? database.value(QStringLiteral("stores")).toObject() : database;
        const QJsonArray banks = stores.value(QStringLiteral("large_banks")).toArray();
        collectBankQuestionSignatures(banks, &signatures);
    }
    return signatures;
}

QJsonValue parseCompactValue(const QString &value)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(
        (QStringLiteral("[") + value + QStringLiteral("]")).toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()
        || document.array().size() != 1) return {};
    return document.array().at(0);
}

QJsonValue parsedStorageValue(const QString &value)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        return value;
    }
    if (document.isObject()) return document.object();
    if (document.isArray()) return document.array();
    return value;
}

QJsonValue redactSecrets(const QJsonValue &value, int *redacted)
{
    if (value.isArray()) {
        QJsonArray result;
        for (const QJsonValue &entry : value.toArray()) {
            result.append(redactSecrets(entry, redacted));
        }
        return result;
    }
    if (!value.isObject()) {
        return value;
    }
    static const QSet<QString> secretNames{
        QStringLiteral("apikey"),
        QStringLiteral("api_key"),
        QStringLiteral("key"),
        QStringLiteral("token"),
        QStringLiteral("authorization"),
        QStringLiteral("secret"),
    };
    QJsonObject result;
    const QJsonObject source = value.toObject();
    for (auto iterator = source.constBegin(); iterator != source.constEnd(); ++iterator) {
        const QString normalized = iterator.key().trimmed().toLower();
        if (secretNames.contains(normalized)) {
            ++(*redacted);
            continue;
        }
        result.insert(iterator.key(), redactSecrets(iterator.value(), redacted));
    }
    return result;
}

bool exec(QSqlQuery &query, QString *error)
{
    if (query.exec()) {
        return true;
    }
    return fail(query.lastError().text(), error);
}

bool migrationExists(QSqlDatabase database, const QByteArray &hash, bool *exists, QString *error)
{
    *exists = false;
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT 1 FROM legacy_migrations WHERE source_hash=?"));
    query.addBindValue(hash);
    if (!exec(query, error)) {
        return false;
    }
    *exists = query.next();
    return true;
}

bool insertRecord(
    QSqlDatabase database,
    const QString &migrationId,
    const QString &category,
    const QString &recordKey,
    const QJsonValue &payload,
    QString *error)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO legacy_records(migration_id, category, record_key, payload_json) "
        "VALUES(?, ?, ?, ?)"));
    query.addBindValue(migrationId);
    query.addBindValue(category);
    query.addBindValue(recordKey);
    query.addBindValue(compactJson(payload));
    return exec(query, error);
}

bool insertSetting(
    QSqlDatabase database,
    const QString &key,
    const QJsonValue &value,
    const QString &timestamp,
    QString *error)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO settings(key, value_json, updated_at) VALUES(?, ?, ?) "
        "ON CONFLICT(key) DO NOTHING"));
    query.addBindValue(key);
    query.addBindValue(compactJson(value));
    query.addBindValue(timestamp);
    return exec(query, error);
}

QString stableRecordKey(const QJsonValue &value, int index)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toVariant().toString().trimmed();
        if (!id.isEmpty()) return id;
        const QString key = object.value(QStringLiteral("key")).toVariant().toString().trimmed();
        if (!key.isEmpty()) return key;
    }
    return QString::number(index);
}

int insertDerivedRecords(
    QSqlDatabase database,
    const QString &migrationId,
    const QString &storageKey,
    const QJsonValue &value,
    QString *error)
{
    if (!kQuestionStateKeys.contains(storageKey) || !value.isObject()) return 0;
    const QJsonObject object = value.toObject();
    int inserted = 0;
    if (storageKey == QStringLiteral("quizapp_wrong_book")) {
        for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
            if (!insertRecord(
                    database, migrationId, QStringLiteral("question:wrong-book"),
                    iterator.key(), iterator.value(), error)) return -1;
            ++inserted;
        }
        return inserted;
    }
    if (storageKey == QStringLiteral("quizapp_answer_state")) {
        for (const QString &mode : {QStringLiteral("sequence"), QStringLiteral("random")}) {
            const QJsonObject answers = object.value(mode).toObject();
            for (auto iterator = answers.constBegin(); iterator != answers.constEnd(); ++iterator) {
                if (!insertRecord(
                        database, migrationId,
                        QStringLiteral("question:answer:%1").arg(mode),
                        iterator.key(), iterator.value(), error)) return -1;
                ++inserted;
            }
        }
        return inserted;
    }
    if (storageKey == QStringLiteral("quizapp_saved_progress")) {
        for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
            if (!insertRecord(
                    database, migrationId, QStringLiteral("practice:session"),
                    iterator.key(), iterator.value(), error)) return -1;
        }
    } else if (storageKey == QStringLiteral("quizapp_saved_session")) {
        const QString key = object.value(QStringLiteral("scopeKey")).toString(
            QStringLiteral("latest"));
        if (!insertRecord(
                database, migrationId, QStringLiteral("practice:latest"),
                key, value, error)) return -1;
    }
    return 0;
}

bool resolveQuestionRecords(
    QSqlDatabase database,
    const QString &migrationId,
    const QHash<QString, QByteArray> &legacySignatures,
    QHash<QString, QString> *resolvedQuestionKeys,
    int *resolvedCount,
    QString *error)
{
    QHash<QString, QString> legacyToNative;
    QSqlQuery questions(database);
    if (!questions.exec(QStringLiteral(
            "SELECT q.id, b.id, b.source_id, q.source_order FROM questions q "
            "JOIN banks b ON b.id=q.bank_id"))) {
        return fail(questions.lastError().text(), error);
    }
    while (questions.next()) {
        const QString questionId = questions.value(0).toString();
        const QString bankId = questions.value(1).toString();
        const QString sourceId = questions.value(2).toString();
        const QString index = QString::number(questions.value(3).toInt());
        legacyToNative.insert(bankId + u':' + index, questionId);
        if (!sourceId.isEmpty()) legacyToNative.insert(sourceId + u':' + index, questionId);
    }

    QHash<QByteArray, QStringList> nativeBySignature;
    QSqlQuery nativeQuestions(database);
    if (!nativeQuestions.exec(QStringLiteral("SELECT id, prompt FROM questions WHERE active=1"))) {
        return fail(nativeQuestions.lastError().text(), error);
    }
    while (nativeQuestions.next()) {
        const QString questionId = nativeQuestions.value(0).toString();
        QSqlQuery options(database);
        options.prepare(QStringLiteral(
            "SELECT text FROM question_options WHERE question_id=? ORDER BY sort_order"));
        options.addBindValue(questionId);
        if (!exec(options, error)) return false;
        QStringList texts;
        while (options.next()) texts.append(options.value(0).toString());
        nativeBySignature[questionSignature(nativeQuestions.value(1).toString(), texts)]
            .append(questionId);
    }
    for (auto iterator = legacySignatures.constBegin();
         iterator != legacySignatures.constEnd(); ++iterator) {
        if (legacyToNative.contains(iterator.key())) continue;
        const QStringList candidates = nativeBySignature.value(iterator.value());
        if (candidates.size() == 1) legacyToNative.insert(iterator.key(), candidates.first());
    }
    if (resolvedQuestionKeys) *resolvedQuestionKeys = legacyToNative;

    QSqlQuery records(database);
    records.prepare(QStringLiteral(
        "SELECT category, record_key, payload_json FROM legacy_records "
        "WHERE migration_id=? AND category LIKE 'question:%'"));
    records.addBindValue(migrationId);
    if (!exec(records, error)) return false;
    struct Resolution {
        QString category;
        QString legacyKey;
        QString questionId;
        QString payload;
    };
    QVector<Resolution> resolutions;
    while (records.next()) {
        const QString legacyKey = records.value(1).toString();
        const QString questionId = legacyToNative.value(legacyKey);
        if (!questionId.isEmpty()) {
            resolutions.append({
                records.value(0).toString(), legacyKey, questionId,
                records.value(2).toString()});
        }
    }
    records.finish();

    for (const Resolution &resolution : resolutions) {
        QSqlQuery update(database);
        update.prepare(QStringLiteral(
            "UPDATE legacy_records SET resolved_id=?, resolution_status='resolved' "
            "WHERE migration_id=? AND category=? AND record_key=?"));
        update.addBindValue(resolution.questionId);
        update.addBindValue(migrationId);
        update.addBindValue(resolution.category);
        update.addBindValue(resolution.legacyKey);
        if (!exec(update, error)) return false;
        if (resolution.category == QStringLiteral("question:wrong-book")) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(
                resolution.payload.toUtf8(), &parseError);
            const QJsonObject payload = document.object();
            const QJsonArray tags = payload.value(QStringLiteral("reasonTags")).toArray();
            const QString note = payload.value(QStringLiteral("reasonNote")).toString();
            const qint64 updatedMillis = payload.value(QStringLiteral("updatedAt")).toInteger();
            const QString timestamp = updatedMillis > 0
                ? QDateTime::fromMSecsSinceEpoch(updatedMillis).toUTC().toString(Qt::ISODateWithMs)
                : QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            QSqlQuery wrong(database);
            wrong.prepare(QStringLiteral(
                "INSERT INTO wrong_book(question_id, reason_tags_json, note, added_at, updated_at) "
                "VALUES(?, ?, ?, ?, ?) ON CONFLICT(question_id) DO NOTHING"));
            wrong.addBindValue(resolution.questionId);
            wrong.addBindValue(QString::fromUtf8(
                QJsonDocument(tags).toJson(QJsonDocument::Compact)));
            wrong.addBindValue(note);
            wrong.addBindValue(timestamp);
            wrong.addBindValue(timestamp);
            if (!exec(wrong, error)) return false;
        } else if (resolution.category.startsWith(QStringLiteral("question:answer:"))) {
            const QJsonValue answerValue = parseCompactValue(resolution.payload);
            const QString answer = answerValue.toString().trimmed().toUpper();
            if (!answer.isEmpty()) {
                const int mode = resolution.category.endsWith(QStringLiteral("random")) ? 1 : 0;
                QSqlQuery answerState(database);
                answerState.prepare(QStringLiteral(
                    "INSERT INTO question_answer_state(question_id, mode, answer, updated_at, "
                    "legacy_migration_id) VALUES(?, ?, ?, ?, ?) "
                    "ON CONFLICT(question_id, mode) DO NOTHING"));
                answerState.addBindValue(resolution.questionId);
                answerState.addBindValue(mode);
                answerState.addBindValue(answer);
                answerState.addBindValue(
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                answerState.addBindValue(migrationId);
                if (!exec(answerState, error)) return false;
            }
        }
        ++(*resolvedCount);
    }
    return true;
}

QString pathScopeId(const QStringList &path)
{
    const QByteArray serialized = QJsonDocument(QJsonArray::fromStringList(path))
                                      .toJson(QJsonDocument::Compact);
    return QStringLiteral("path:%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(serialized, QCryptographicHash::Sha256).toHex()));
}

QString legacyTimestamp(const QJsonObject &payload, const QString &fallback)
{
    const qint64 milliseconds = payload.value(QStringLiteral("savedAt")).toInteger();
    if (milliseconds > 0) {
        const QDateTime value = QDateTime::fromMSecsSinceEpoch(milliseconds, Qt::UTC);
        if (value.isValid()) return value.toString(Qt::ISODateWithMs);
    }
    return fallback;
}

bool materializePracticeSessions(
    QSqlDatabase database,
    const QString &migrationId,
    const QHash<QString, QString> &questionKeys,
    const QString &fallbackTimestamp,
    int *importedCount,
    QString *error)
{
    QSqlQuery records(database);
    records.prepare(QStringLiteral(
        "SELECT category, record_key, payload_json FROM legacy_records "
        "WHERE migration_id=? AND category IN ('practice:session', 'practice:latest') "
        "ORDER BY category, record_key"));
    records.addBindValue(migrationId);
    if (!exec(records, error)) return false;
    struct PendingSession { QString key; QJsonObject payload; };
    QVector<PendingSession> pending;
    QSet<QByteArray> payloadHashes;
    while (records.next()) {
        const QByteArray bytes = records.value(2).toString().toUtf8();
        const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        if (payloadHashes.contains(hash)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(bytes);
        if (!document.isObject()) continue;
        payloadHashes.insert(hash);
        pending.append({records.value(1).toString(), document.object()});
    }
    records.finish();

    for (const PendingSession &entry : pending) {
        const QJsonObject payload = entry.payload;
        QVector<QString> resolvedOrder;
        QSet<QString> seen;
        for (const QJsonValue &keyValue : payload.value(QStringLiteral("questionOrderKeys")).toArray()) {
            const QString questionId = questionKeys.value(keyValue.toString());
            if (!questionId.isEmpty() && !seen.contains(questionId)) {
                seen.insert(questionId);
                resolvedOrder.append(questionId);
            }
        }
        if (resolvedOrder.isEmpty()) continue;

        QString scopeId;
        const QJsonArray bankIds = payload.value(QStringLiteral("bankIds")).toArray();
        if (bankIds.size() == 1) {
            const QString candidate = bankIds.first().toString();
            QSqlQuery bank(database);
            bank.prepare(QStringLiteral("SELECT 1 FROM banks WHERE id=?"));
            bank.addBindValue(candidate);
            if (!exec(bank, error)) return false;
            if (bank.next()) scopeId = candidate;
        }
        if (scopeId.isEmpty()) {
            QSet<QString> resolvedBankIds;
            QSqlQuery bankForQuestion(database);
            bankForQuestion.prepare(QStringLiteral("SELECT bank_id FROM questions WHERE id=?"));
            for (const QString &questionId : std::as_const(resolvedOrder)) {
                bankForQuestion.bindValue(0, questionId);
                if (!exec(bankForQuestion, error)) return false;
                if (bankForQuestion.next()) resolvedBankIds.insert(bankForQuestion.value(0).toString());
                bankForQuestion.finish();
            }
            if (resolvedBankIds.size() == 1) scopeId = *resolvedBankIds.constBegin();
        }
        if (scopeId.isEmpty()) {
            QStringList path;
            for (const QJsonValue &part : payload.value(QStringLiteral("path")).toArray()) {
                if (!part.toString().trimmed().isEmpty()) path.append(part.toString().trimmed());
            }
            scopeId = pathScopeId(path);
        }

        const int mode = payload.value(QStringLiteral("shuffled")).toBool() ? 1 : 0;
        const QString currentQuestion = questionKeys.value(
            payload.value(QStringLiteral("currentQuestionKey")).toString());
        int currentIndex = resolvedOrder.indexOf(currentQuestion);
        if (currentIndex < 0) {
            currentIndex = std::clamp(
                payload.value(QStringLiteral("currentIndex")).toInt(),
                0, static_cast<int>(resolvedOrder.size() - 1));
        }
        QJsonArray orderJson;
        for (const QString &questionId : resolvedOrder) orderJson.append(questionId);
        const QString timestamp = legacyTimestamp(payload, fallbackTimestamp);
        const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject viewport{
            {QStringLiteral("legacyMigrationId"), migrationId},
            {QStringLiteral("legacyScopeKey"), payload.value(QStringLiteral("scopeKey"))},
            {QStringLiteral("displayMode"), payload.value(QStringLiteral("mode"))},
            {QStringLiteral("title"), payload.value(QStringLiteral("title"))},
        };
        QSqlQuery session(database);
        session.prepare(QStringLiteral(
            "INSERT INTO practice_sessions(id, scope_id, mode, current_index, "
            "question_order_json, viewport_json, is_complete, created_at, updated_at) "
            "VALUES(?, ?, ?, ?, ?, ?, 0, ?, ?)"));
        session.addBindValue(sessionId);
        session.addBindValue(scopeId);
        session.addBindValue(mode);
        session.addBindValue(currentIndex);
        session.addBindValue(QString::fromUtf8(
            QJsonDocument(orderJson).toJson(QJsonDocument::Compact)));
        session.addBindValue(QString::fromUtf8(
            QJsonDocument(viewport).toJson(QJsonDocument::Compact)));
        session.addBindValue(timestamp);
        session.addBindValue(timestamp);
        if (!exec(session, error)) return false;

        const QJsonObject answers = payload.value(QStringLiteral("answersByKey")).toObject();
        const QJsonObject drafts = payload.value(QStringLiteral("draftsByKey")).toObject();
        QHash<QString, QString> answersByQuestion;
        QHash<QString, QString> draftsByQuestion;
        for (auto iterator = answers.constBegin(); iterator != answers.constEnd(); ++iterator) {
            const QString questionId = questionKeys.value(iterator.key());
            if (!questionId.isEmpty()) answersByQuestion.insert(questionId, iterator.value().toString());
        }
        for (auto iterator = drafts.constBegin(); iterator != drafts.constEnd(); ++iterator) {
            const QString questionId = questionKeys.value(iterator.key());
            if (!questionId.isEmpty()) draftsByQuestion.insert(questionId, iterator.value().toString());
        }
        for (const QString &questionId : std::as_const(resolvedOrder)) {
            const QString answer = answersByQuestion.value(questionId).trimmed().toUpper();
            const QString draft = draftsByQuestion.value(questionId).trimmed().toUpper();
            if (answer.isEmpty() && draft.isEmpty()) continue;
            QSqlQuery answerQuery(database);
            answerQuery.prepare(QStringLiteral(
                "INSERT INTO practice_answers(session_id, question_id, answer, draft, "
                "revealed, answered_at) VALUES(?, ?, ?, ?, 0, ?)"));
            answerQuery.addBindValue(sessionId);
            answerQuery.addBindValue(questionId);
            answerQuery.addBindValue(answer);
            answerQuery.addBindValue(draft);
            answerQuery.addBindValue(answer.isEmpty() ? QVariant() : QVariant(timestamp));
            if (!exec(answerQuery, error)) return false;
        }
        ++(*importedCount);
    }
    return true;
}

bool importStudyStats(
    QSqlDatabase database,
    const QString &migrationId,
    const QJsonValue &value,
    int *importedDays,
    QString *error)
{
    if (!value.isObject()) {
        return true;
    }
    const QJsonObject stats = value.toObject();
    const QTimeZone zone = QTimeZone::systemTimeZone();
    for (auto iterator = stats.constBegin(); iterator != stats.constEnd(); ++iterator) {
        if (!kDateKey.match(iterator.key()).hasMatch()) continue;
        const QDate date = QDate::fromString(iterator.key(), Qt::ISODate);
        if (!date.isValid()) continue;
        const QJsonValue entry = iterator.value();
        const qint64 seconds = entry.isObject()
            ? entry.toObject().value(QStringLiteral("seconds")).toInteger()
            : entry.toInteger();
        if (seconds <= 0 || seconds > 24 * 60 * 60) continue;
        const QDateTime startedAt(date, QTime(0, 0), zone);
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "INSERT INTO study_events(activity, scope_id, started_at, duration_seconds, "
            "legacy_migration_id) VALUES('sequential', 'legacy:v1:daily-total', ?, ?, ?)"));
        query.addBindValue(startedAt.toUTC().toString(Qt::ISODateWithMs));
        query.addBindValue(seconds);
        query.addBindValue(migrationId);
        if (!exec(query, error)) return false;
        ++(*importedDays);
    }
    return true;
}

QJsonObject summaryJson(const LegacyMigrationSummary &summary)
{
    return {
        {QStringLiteral("localStorageRecords"), summary.localStorageRecords},
        {QStringLiteral("indexedDbRecords"), summary.indexedDbRecords},
        {QStringLiteral("studyDays"), summary.studyDays},
        {QStringLiteral("pendingQuestionRecords"), summary.pendingQuestionRecords},
        {QStringLiteral("resolvedQuestionRecords"), summary.resolvedQuestionRecords},
        {QStringLiteral("practiceSessions"), summary.practiceSessions},
        {QStringLiteral("notebookRecords"), summary.notebookRecords},
        {QStringLiteral("redactedSecrets"), summary.redactedSecrets},
    };
}

QVariant settingValue(const QJsonValue &value)
{
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) return value.toDouble();
    if (value.isString()) return value.toString();
    if (value.isArray() || value.isObject()) return compactJson(value);
    return {};
}

} // namespace

LegacyMigrationResult LegacyMigrationService::importPackage(
    const QByteArray &json,
    QSqlDatabase database,
    const ImportGate &gate) const
{
    LegacyMigrationResult result;
    if (!database.isOpen()) {
        result.status = LegacyMigrationStatus::Failed;
        result.error = QStringLiteral("数据库未打开");
        return result;
    }
    if (json.isEmpty() || json.size() > kMaximumPackageBytes) {
        result.status = LegacyMigrationStatus::InvalidPackage;
        result.error = QStringLiteral("旧版迁移包为空或超过 512 MiB 限制");
        return result;
    }

    result.sourceHash = QCryptographicHash::hash(json, QCryptographicHash::Sha256);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.status = LegacyMigrationStatus::InvalidPackage;
        result.error = QStringLiteral("旧版迁移包不是有效 JSON 对象");
        return result;
    }
    QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString()
            != QStringLiteral("quizapp-legacy-v1")
        || root.value(QStringLiteral("schemaVersion")).toInt() != 1
        || !root.value(QStringLiteral("localStorage")).isObject()) {
        result.status = LegacyMigrationStatus::InvalidPackage;
        result.error = QStringLiteral("旧版迁移包格式或版本不受支持");
        return result;
    }
    result.sourceVersion = root.value(QStringLiteral("sourceVersion")).toString().trimmed();
    result.exportedAt = QDateTime::fromString(
        root.value(QStringLiteral("exportedAt")).toString(), Qt::ISODateWithMs);
    if (result.sourceVersion.isEmpty() || !result.exportedAt.isValid()) {
        result.status = LegacyMigrationStatus::InvalidPackage;
        result.error = QStringLiteral("旧版迁移包缺少来源版本或导出时间");
        return result;
    }

    bool exists = false;
    if (!migrationExists(database, result.sourceHash, &exists, &result.error)) {
        result.status = LegacyMigrationStatus::Failed;
        return result;
    }
    if (exists) {
        result.status = LegacyMigrationStatus::AlreadyImported;
        return result;
    }

    QJsonObject storage = root.value(QStringLiteral("localStorage")).toObject();
    QJsonObject sanitizedStorage;
    QHash<QString, QJsonValue> parsedStorage;
    for (auto iterator = storage.constBegin(); iterator != storage.constEnd(); ++iterator) {
        if (!iterator.key().startsWith(QStringLiteral("quizapp_"))
            || !iterator.value().isString()) {
            continue;
        }
        QJsonValue parsed = parsedStorageValue(iterator.value().toString());
        if (iterator.key() == QStringLiteral("quizapp_ai_config")) {
            parsed = redactSecrets(parsed, &result.summary.redactedSecrets);
        }
        parsedStorage.insert(iterator.key(), parsed);
        sanitizedStorage.insert(iterator.key(), compactJson(parsed));
    }
    root.insert(QStringLiteral("localStorage"), sanitizedStorage);

    if (!database.transaction()) {
        result.status = LegacyMigrationStatus::Failed;
        result.error = database.lastError().text();
        return result;
    }
    auto rollback = [&database, &result](const QString &message) {
        database.rollback();
        result.status = LegacyMigrationStatus::Failed;
        result.error = message;
        return result;
    };
    if (gate && !gate(QStringLiteral("before-migration"))) {
        return rollback(QStringLiteral("迁移在写入前中断"));
    }

    result.migrationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString importedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSqlQuery migration(database);
    migration.prepare(QStringLiteral(
        "INSERT INTO legacy_migrations(id, source_version, source_hash, exported_at, status, "
        "sanitized_package_json, imported_at) VALUES(?, ?, ?, ?, 'staged', ?, ?)"));
    migration.addBindValue(result.migrationId);
    migration.addBindValue(result.sourceVersion);
    migration.addBindValue(result.sourceHash);
    migration.addBindValue(result.exportedAt.toUTC().toString(Qt::ISODateWithMs));
    migration.addBindValue(QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Compact)));
    migration.addBindValue(importedAt);
    if (!exec(migration, &result.error)) return rollback(result.error);

    QStringList storageKeys = parsedStorage.keys();
    storageKeys.sort(Qt::CaseSensitive);
    for (const QString &key : storageKeys) {
        const QJsonValue value = parsedStorage.value(key);
        if (!insertRecord(
                database, result.migrationId, QStringLiteral("localStorage"),
                key, value, &result.error)) {
            return rollback(result.error);
        }
        ++result.summary.localStorageRecords;
        const int derived = insertDerivedRecords(
            database, result.migrationId, key, value, &result.error);
        if (derived < 0) return rollback(result.error);
        result.summary.pendingQuestionRecords += derived;
        if (key == QStringLiteral("quizapp_ui_config")
            && !insertSetting(
                database, QStringLiteral("legacy/v1/uiConfig"), value,
                importedAt, &result.error)) {
            return rollback(result.error);
        }
    }

    const QJsonObject indexedDatabases = root.value(QStringLiteral("indexedDb")).toObject();
    const QHash<QString, QByteArray> legacyQuestionSignatures =
        collectLegacyQuestionSignatures(parsedStorage, indexedDatabases);
    QStringList databaseNames = indexedDatabases.keys();
    databaseNames.sort(Qt::CaseSensitive);
    for (const QString &databaseName : databaseNames) {
        const QJsonObject databaseObject = indexedDatabases.value(databaseName).toObject();
        const QJsonObject stores = databaseObject.value(QStringLiteral("stores")).isObject()
            ? databaseObject.value(QStringLiteral("stores")).toObject()
            : databaseObject;
        QStringList storeNames = stores.keys();
        storeNames.sort(Qt::CaseSensitive);
        for (const QString &storeName : storeNames) {
            const QJsonArray records = stores.value(storeName).toArray();
            QSet<QString> usedKeys;
            for (int index = 0; index < records.size(); ++index) {
                QString recordKey = stableRecordKey(records.at(index), index);
                while (usedKeys.contains(recordKey)) {
                    recordKey += QStringLiteral("#%1").arg(index);
                }
                usedKeys.insert(recordKey);
                if (!insertRecord(
                        database,
                        result.migrationId,
                        QStringLiteral("indexedDb:%1:%2").arg(databaseName, storeName),
                        recordKey,
                        records.at(index),
                        &result.error)) {
                    return rollback(result.error);
                }
                ++result.summary.indexedDbRecords;
            }
        }
    }

    if (!importStudyStats(
            database,
            result.migrationId,
            parsedStorage.value(QStringLiteral("quizapp_study_stats")),
            &result.summary.studyDays,
            &result.error)) {
        return rollback(result.error);
    }
    QHash<QString, QString> resolvedQuestionKeys;
    if (!resolveQuestionRecords(
            database,
            result.migrationId,
            legacyQuestionSignatures,
            &resolvedQuestionKeys,
            &result.summary.resolvedQuestionRecords,
            &result.error)) {
        return rollback(result.error);
    }
    result.summary.pendingQuestionRecords -= result.summary.resolvedQuestionRecords;
    if (!materializePracticeSessions(
            database,
            result.migrationId,
            resolvedQuestionKeys,
            importedAt,
            &result.summary.practiceSessions,
            &result.error)) {
        return rollback(result.error);
    }
    if (gate && !gate(QStringLiteral("before-commit"))) {
        return rollback(QStringLiteral("迁移在提交前中断"));
    }

    QSqlQuery finish(database);
    finish.prepare(QStringLiteral(
        "UPDATE legacy_migrations SET status='applied', summary_json=? WHERE id=?"));
    finish.addBindValue(QString::fromUtf8(
        QJsonDocument(summaryJson(result.summary)).toJson(QJsonDocument::Compact)));
    finish.addBindValue(result.migrationId);
    if (!exec(finish, &result.error)) return rollback(result.error);
    if (!database.commit()) {
        return rollback(database.lastError().text());
    }
    result.status = LegacyMigrationStatus::Imported;
    return result;
}

bool LegacyMigrationService::applyUiSettings(
    QSqlDatabase database,
    QSettings &settings,
    int *appliedCount,
    QString *error) const
{
    if (appliedCount) *appliedCount = 0;
    if (error) error->clear();
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT value_json FROM settings WHERE key=?"));
    query.addBindValue(QStringLiteral("legacy/v1/uiConfig"));
    if (!exec(query, error)) return false;
    if (!query.next()) return true;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        query.value(0).toString().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("旧版界面设置记录格式无效"), error);
    }
    const QJsonObject legacy = document.object();
    const QSet<QString> themes{
        QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")};
    const QSet<QString> palettes{
        QStringLiteral("classic"), QStringLiteral("forest"), QStringLiteral("ink"),
        QStringLiteral("sunset"), QStringLiteral("berry"), QStringLiteral("cyan")};
    auto apply = [&settings, appliedCount](
                     const QString &nativeKey, const QJsonValue &value) {
        if (settings.contains(nativeKey) || value.isUndefined() || value.isNull()) return;
        const QVariant converted = settingValue(value);
        if (!converted.isValid()) return;
        settings.setValue(nativeKey, converted);
        if (appliedCount) ++(*appliedCount);
    };

    const QString theme = legacy.value(QStringLiteral("theme")).toString();
    if (themes.contains(theme)) apply(QStringLiteral("ui/theme"), theme);
    const QString palette = legacy.value(QStringLiteral("palette")).toString();
    if (palettes.contains(palette)) apply(QStringLiteral("ui/palette"), palette);
    const QString primary = legacy.value(QStringLiteral("primary")).toString();
    if (QRegularExpression(QStringLiteral("^#[0-9a-fA-F]{6}$")).match(primary).hasMatch()) {
        apply(QStringLiteral("ui/primary"), primary.toLower());
    }
    apply(QStringLiteral("ui/reduceMotion"), legacy.value(QStringLiteral("reduceMotion")));
    const QJsonValue radius = legacy.contains(QStringLiteral("componentRadius"))
        ? legacy.value(QStringLiteral("componentRadius"))
        : legacy.value(QStringLiteral("cornerRadius"));
    if (radius.isDouble()) {
        apply(QStringLiteral("ui/cornerRadius"), std::clamp(radius.toInt(), 0, 18));
    }

    const std::array<QPair<const char *, const char *>, 25> mappings{{
        {"autoSaveOnExit", "practice/autoSaveOnExit"},
        {"progressScopePolicy", "practice/progressScopePolicy"},
        {"randomReshuffleOnReset", "practice/randomReshuffleOnReset"},
        {"rememberReviewPosition", "view/rememberReviewPosition"},
        {"rememberAnswerLookupPosition", "view/rememberAnswerLookupPosition"},
        {"autoUpdateCheck", "distribution/autoUpdateCheck"},
        {"autoAnnouncementCheck", "distribution/autoAnnouncementCheck"},
        {"persistStudyStats", "study/persistStats"},
        {"studyChartRange", "study/chartRange"},
        {"toolPanelsDefaultExpanded", "home/toolPanelsDefaultExpanded"},
        {"showSavedProgressEntry", "home/showSavedProgressEntry"},
        {"showSavedProgressHint", "home/showSavedProgressHint"},
        {"savedProgressWidgetWidthPhone", "home/savedProgressWidthPhone"},
        {"savedProgressWidgetWidthTablet", "home/savedProgressWidthTablet"},
        {"iconStyle", "ui/iconStyle"},
        {"subjectIconStyle", "ui/subjectIconStyle"},
        {"chapterIconStyle", "ui/chapterIconStyle"},
        {"chapterActionLayout", "practice/chapterActionLayout"},
        {"chapterActionsDefaultExpanded", "practice/chapterActionsDefaultExpanded"},
        {"chapterActionButtons", "practice/chapterActionButtons"},
        {"toolCardOrder", "home/toolCardOrder"},
        {"hiddenToolCards", "home/hiddenToolCards"},
        {"homeMetricOrder", "home/metricOrder"},
        {"hiddenHomeMetrics", "home/hiddenMetrics"},
        {"subjectOrder", "library/subjectOrder"},
    }};
    for (const auto &[legacyKey, nativeKey] : mappings) {
        apply(QString::fromLatin1(nativeKey), legacy.value(QString::fromLatin1(legacyKey)));
    }
    apply(QStringLiteral("library/order"), legacy.value(QStringLiteral("libraryOrder")));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        return fail(QStringLiteral("旧版界面设置无法写入原生配置"), error);
    }
    return true;
}

} // namespace quizapp::services
