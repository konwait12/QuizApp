#include "storage/SqliteQuestionRepository.h"
#include "domain/QuestionOrdering.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

#include <array>
#include <utility>

namespace quizapp::storage {
namespace {

void clearError(QString *error)
{
    if (error) {
        error->clear();
    }
}

bool fail(QSqlDatabase database, const QString &message, QString *error)
{
    database.rollback();
    if (error) {
        *error = message;
    }
    return false;
}

QString uuidText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

QString nonNull(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString pathJson(const QStringList &path)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(path)).toJson(QJsonDocument::Compact));
}

QString explanationJson(const domain::BuiltinExplanation &explanation)
{
    QJsonObject object;
    object.insert(QStringLiteral("provider"), explanation.provider);
    object.insert(QStringLiteral("sourceId"), explanation.sourceId);
    object.insert(QStringLiteral("text"), explanation.text);
    object.insert(QStringLiteral("videoUrl"), explanation.videoUrl);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

domain::BuiltinExplanation parseExplanation(const QString &value)
{
    domain::BuiltinExplanation explanation;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        explanation.text = value;
        return explanation;
    }
    const QJsonObject object = document.object();
    explanation.provider = object.value(QStringLiteral("provider")).toString();
    explanation.sourceId = object.value(QStringLiteral("sourceId")).toString();
    explanation.text = object.value(QStringLiteral("text")).toString();
    explanation.videoUrl = object.value(QStringLiteral("videoUrl")).toString();
    return explanation;
}

QStringList parsePath(const QString &value)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        return {};
    }
    QStringList path;
    for (const QJsonValue &entry : document.array()) {
        path.append(entry.toString());
    }
    return path;
}

bool execPrepared(QSqlQuery &query, QString *error)
{
    if (query.exec()) {
        return true;
    }
    if (error) {
        *error = query.lastError().text();
    }
    return false;
}

domain::Question questionFromQuery(const QSqlQuery &query)
{
    domain::Question question;
    question.id = QUuid(query.value(0).toString());
    question.bankId = query.value(1).toString();
    question.sourceProvider = query.value(2).toString();
    question.sourceId = query.value(3).toString();
    question.path = parsePath(query.value(4).toString());
    question.type = static_cast<domain::QuestionType>(query.value(5).toInt());
    question.prompt = query.value(6).toString();
    question.correctAnswer = query.value(7).toString();
    question.builtinExplanation = parseExplanation(query.value(8).toString());
    question.contentHash = query.value(9).toByteArray();
    question.sourceOrder = query.value(10).toInt();
    question.updatedAt = QDateTime::fromString(query.value(11).toString(), Qt::ISODateWithMs);
    return question;
}

const QString kQuestionColumns = QStringLiteral(
    "q.id, q.bank_id, q.source_provider, q.source_id, q.path_json, q.type, "
    "q.prompt, q.correct_answer, q.builtin_explanation, q.content_hash, "
    "q.source_order, q.updated_at");

bool loadRelatedData(
    QSqlDatabase database,
    QVector<domain::Question> *questions,
    const QString &whereClause,
    const QVariantList &whereValues,
    QString *error)
{
    QHash<QString, qsizetype> indexes;
    for (qsizetype index = 0; index < questions->size(); ++index) {
        indexes.insert(uuidText(questions->at(index).id), index);
    }
    if (indexes.isEmpty()) {
        return true;
    }

    QSqlQuery options(database);
    options.prepare(QStringLiteral(
        "SELECT qo.question_id, qo.text FROM question_options qo "
        "JOIN questions q ON q.id = qo.question_id WHERE %1 "
        "ORDER BY q.source_order, qo.sort_order").arg(whereClause));
    for (const QVariant &whereValue : whereValues) {
        options.addBindValue(whereValue);
    }
    if (!execPrepared(options, error)) {
        return false;
    }
    while (options.next()) {
        const auto found = indexes.constFind(options.value(0).toString());
        if (found != indexes.cend()) {
            (*questions)[found.value()].options.append(options.value(1).toString());
        }
    }

    QSqlQuery blobs(database);
    blobs.prepare(QStringLiteral(
        "SELECT qb.question_id, qb.blob_id, qb.role, b.relative_path FROM question_blobs qb "
        "JOIN questions q ON q.id = qb.question_id "
        "JOIN blobs b ON b.id = qb.blob_id WHERE %1 "
        "ORDER BY q.source_order, qb.role, qb.sort_order").arg(whereClause));
    for (const QVariant &whereValue : whereValues) {
        blobs.addBindValue(whereValue);
    }
    if (!execPrepared(blobs, error)) {
        return false;
    }
    while (blobs.next()) {
        const auto found = indexes.constFind(blobs.value(0).toString());
        if (found == indexes.cend()) {
            continue;
        }
        const QString blobId = blobs.value(1).toString();
        (*questions)[found.value()].blobRelativePaths.insert(
            blobId, blobs.value(3).toString());
        if (blobs.value(2).toString() == QStringLiteral("question")) {
            (*questions)[found.value()].questionImageBlobIds.append(blobId);
        } else {
            (*questions)[found.value()].builtinExplanation.imageBlobIds.append(
                blobId);
        }
    }
    return true;
}

QStringList firstQuestionPathForBank(QSqlDatabase database, const QString &bankId)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT path_json FROM questions WHERE bank_id=? AND active=1 "
        "ORDER BY source_order LIMIT 1"));
    query.addBindValue(bankId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return parsePath(query.value(0).toString());
}

bool validatePackage(const domain::BankImportPackage &package, QString *error)
{
    if (package.bank.id.trimmed().isEmpty()
        || package.bank.subjectId.trimmed().isEmpty()
        || package.bank.title.trimmed().isEmpty()
        || package.bank.contentHash.size() != 32) {
        if (error) {
            *error = QStringLiteral("Bank metadata is incomplete or has an invalid content hash");
        }
        return false;
    }
    QSet<QString> subjectIds;
    for (const domain::SubjectNode &subject : package.subjects) {
        if (subject.id.trimmed().isEmpty() || subject.title.trimmed().isEmpty()
            || subjectIds.contains(subject.id)) {
            if (error) {
                *error = QStringLiteral("Subject hierarchy contains an invalid or duplicate node");
            }
            return false;
        }
        if (!subject.parentId.isEmpty() && !subjectIds.contains(subject.parentId)) {
            if (error) {
                *error = QStringLiteral("Subject nodes must be ordered from parent to child");
            }
            return false;
        }
        subjectIds.insert(subject.id);
    }
    if (!subjectIds.contains(package.bank.subjectId)) {
        if (error) {
            *error = QStringLiteral("Bank subject is not present in the import package");
        }
        return false;
    }

    const QRegularExpression blobIdPattern(QStringLiteral("^[0-9a-f]{64}$"));
    QSet<QString> blobIds;
    QSet<QString> blobPaths;
    for (const domain::BlobAsset &blob : package.blobs) {
        if (!blobIdPattern.match(blob.id).hasMatch()
            || !blob.mediaType.startsWith(QStringLiteral("image/"))
            || blob.byteSize <= 0 || blob.relativePath.trimmed().isEmpty()
            || blobIds.contains(blob.id) || blobPaths.contains(blob.relativePath)) {
            if (error) {
                *error = QStringLiteral("Blob metadata contains an invalid or duplicate asset");
            }
            return false;
        }
        blobIds.insert(blob.id);
        blobPaths.insert(blob.relativePath);
    }

    QSet<QUuid> questionIds;
    QSet<int> sourceOrders;
    for (const domain::Question &question : package.bank.questions) {
        if (question.id.isNull() || question.prompt.trimmed().isEmpty()
            || question.contentHash.size() != 32 || question.sourceOrder < 0
            || (!question.bankId.isEmpty() && question.bankId != package.bank.id)
            || static_cast<int>(question.type) < static_cast<int>(domain::QuestionType::Single)
            || static_cast<int>(question.type) > static_cast<int>(domain::QuestionType::Subjective)
            || questionIds.contains(question.id) || sourceOrders.contains(question.sourceOrder)
            || question.options.size() > 26) {
            if (error) {
                *error = QStringLiteral("Question data contains an invalid identity, order, or content hash");
            }
            return false;
        }
        questionIds.insert(question.id);
        sourceOrders.insert(question.sourceOrder);
    }
    return true;
}

} // namespace

SqliteQuestionRepository::SqliteQuestionRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool SqliteQuestionRepository::replaceBank(
    const domain::BankImportPackage &package,
    QString *error)
{
    clearError(error);
    if (!database_.isOpen()) {
        if (error) {
            *error = QStringLiteral("Database is not open");
        }
        return false;
    }
    if (!validatePackage(package, error)) {
        return false;
    }
    if (!database_.transaction()) {
        if (error) {
            *error = database_.lastError().text();
        }
        return false;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const domain::BlobAsset &blob : package.blobs) {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "INSERT INTO blobs(id, media_type, byte_size, relative_path, created_at, last_verified_at) "
            "VALUES(?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET "
            "media_type=excluded.media_type, byte_size=excluded.byte_size, "
            "relative_path=excluded.relative_path, last_verified_at=excluded.last_verified_at"));
        query.addBindValue(blob.id);
        query.addBindValue(blob.mediaType);
        query.addBindValue(blob.byteSize);
        query.addBindValue(blob.relativePath);
        query.addBindValue(blob.createdAt.isValid()
            ? blob.createdAt.toUTC().toString(Qt::ISODateWithMs) : now);
        query.addBindValue(now);
        QString queryError;
        if (!execPrepared(query, &queryError)) {
            return fail(database_, queryError, error);
        }
    }
    for (const domain::SubjectNode &subject : package.subjects) {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "INSERT INTO subjects(id, parent_id, title, icon, sort_order, created_at, updated_at) "
            "VALUES(?, ?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET "
            "parent_id=excluded.parent_id, title=excluded.title, icon=excluded.icon, "
            "sort_order=excluded.sort_order, updated_at=excluded.updated_at"));
        query.addBindValue(subject.id);
        query.addBindValue(subject.parentId.isEmpty() ? QVariant() : QVariant(subject.parentId));
        query.addBindValue(subject.title);
        query.addBindValue(nonNull(subject.icon));
        query.addBindValue(subject.sortOrder);
        query.addBindValue(now);
        query.addBindValue(now);
        QString queryError;
        if (!execPrepared(query, &queryError)) {
            return fail(database_, queryError, error);
        }
    }

    const QString provider = package.bank.sourceProvider.trimmed().isEmpty()
        ? QStringLiteral("local")
        : package.bank.sourceProvider.trimmed();
    const QString sourceId = package.bank.sourceId.trimmed().isEmpty()
        ? package.bank.id
        : package.bank.sourceId.trimmed();
    QSqlQuery bank(database_);
    bank.prepare(QStringLiteral(
        "INSERT INTO banks(id, subject_id, title, source_provider, source_id, schema_version, "
        "content_hash, distribution_version, installed_at, updated_at) "
        "VALUES(?, ?, ?, ?, ?, 2, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET "
        "subject_id=excluded.subject_id, title=excluded.title, "
        "source_provider=excluded.source_provider, source_id=excluded.source_id, "
        "schema_version=excluded.schema_version, content_hash=excluded.content_hash, "
        "distribution_version=excluded.distribution_version, updated_at=excluded.updated_at"));
    bank.addBindValue(package.bank.id);
    bank.addBindValue(package.bank.subjectId);
    bank.addBindValue(package.bank.title);
    bank.addBindValue(provider);
    bank.addBindValue(sourceId);
    bank.addBindValue(package.bank.contentHash);
    bank.addBindValue(nonNull(package.bank.distributionVersion));
    bank.addBindValue(now);
    bank.addBindValue(now);
    QString queryError;
    if (!execPrepared(bank, &queryError)) {
        return fail(database_, queryError, error);
    }

    QSqlQuery deactivate(database_);
    deactivate.prepare(QStringLiteral(
        "UPDATE questions SET active=0, source_order=-1000000000-rowid WHERE bank_id=? AND active=1"));
    deactivate.addBindValue(package.bank.id);
    if (!execPrepared(deactivate, &queryError)) {
        return fail(database_, queryError, error);
    }
    QSqlQuery clearSearch(database_);
    clearSearch.prepare(QStringLiteral(
        "DELETE FROM question_search WHERE question_id IN "
        "(SELECT id FROM questions WHERE bank_id=?)"));
    clearSearch.addBindValue(package.bank.id);
    if (!execPrepared(clearSearch, &queryError)) {
        return fail(database_, queryError, error);
    }

    for (const domain::Question &question : package.bank.questions) {
        const QString questionId = uuidText(question.id);
        const QString updatedAt = question.updatedAt.isValid()
            ? question.updatedAt.toUTC().toString(Qt::ISODateWithMs)
            : now;
        QSqlQuery upsert(database_);
        upsert.prepare(QStringLiteral(
            "INSERT INTO questions(id, bank_id, source_provider, source_id, type, prompt, "
            "correct_answer, builtin_explanation, content_hash, source_order, updated_at, "
            "path_json, active) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1) "
            "ON CONFLICT(id) DO UPDATE SET bank_id=excluded.bank_id, "
            "source_provider=excluded.source_provider, source_id=excluded.source_id, "
            "type=excluded.type, prompt=excluded.prompt, correct_answer=excluded.correct_answer, "
            "builtin_explanation=excluded.builtin_explanation, content_hash=excluded.content_hash, "
            "source_order=excluded.source_order, updated_at=excluded.updated_at, "
            "path_json=excluded.path_json, active=1"));
        upsert.addBindValue(questionId);
        upsert.addBindValue(package.bank.id);
        upsert.addBindValue(nonNull(question.sourceProvider));
        upsert.addBindValue(nonNull(question.sourceId));
        upsert.addBindValue(static_cast<int>(question.type));
        upsert.addBindValue(question.prompt);
        upsert.addBindValue(nonNull(question.correctAnswer));
        upsert.addBindValue(explanationJson(question.builtinExplanation));
        upsert.addBindValue(question.contentHash);
        upsert.addBindValue(question.sourceOrder);
        upsert.addBindValue(updatedAt);
        upsert.addBindValue(pathJson(question.path));
        if (!execPrepared(upsert, &queryError)) {
            return fail(database_, queryError, error);
        }

        QSqlQuery deleteOptions(database_);
        deleteOptions.prepare(QStringLiteral("DELETE FROM question_options WHERE question_id=?"));
        deleteOptions.addBindValue(questionId);
        if (!execPrepared(deleteOptions, &queryError)) {
            return fail(database_, queryError, error);
        }
        for (qsizetype index = 0; index < question.options.size(); ++index) {
            QSqlQuery option(database_);
            option.prepare(QStringLiteral(
                "INSERT INTO question_options(question_id, option_key, text, sort_order) "
                "VALUES(?, ?, ?, ?)"));
            option.addBindValue(questionId);
            option.addBindValue(QString(
                QChar(static_cast<char16_t>(u'A' + index))));
            option.addBindValue(nonNull(question.options.at(index)));
            option.addBindValue(index);
            if (!execPrepared(option, &queryError)) {
                return fail(database_, queryError, error);
            }
        }

        QSqlQuery deleteBlobs(database_);
        deleteBlobs.prepare(QStringLiteral("DELETE FROM question_blobs WHERE question_id=?"));
        deleteBlobs.addBindValue(questionId);
        if (!execPrepared(deleteBlobs, &queryError)) {
            return fail(database_, queryError, error);
        }
        const std::array<std::pair<QString, QStringList>, 2> blobGroups{{
            {QStringLiteral("question"), question.questionImageBlobIds},
            {QStringLiteral("explanation"), question.builtinExplanation.imageBlobIds},
        }};
        for (const auto &[role, blobIds] : blobGroups) {
            for (qsizetype index = 0; index < blobIds.size(); ++index) {
                QSqlQuery blob(database_);
                blob.prepare(QStringLiteral(
                    "INSERT INTO question_blobs(question_id, blob_id, role, sort_order) "
                    "VALUES(?, ?, ?, ?)"));
                blob.addBindValue(questionId);
                blob.addBindValue(blobIds.at(index));
                blob.addBindValue(role);
                blob.addBindValue(index);
                if (!execPrepared(blob, &queryError)) {
                    return fail(database_, queryError, error);
                }
            }
        }

        QSqlQuery search(database_);
        search.prepare(QStringLiteral(
            "INSERT INTO question_search(question_id, prompt, options, builtin_explanation, path) "
            "VALUES(?, ?, ?, ?, ?)"));
        search.addBindValue(questionId);
        search.addBindValue(question.prompt);
        search.addBindValue(question.options.join(u' '));
        search.addBindValue(question.builtinExplanation.text);
        search.addBindValue(question.path.join(QStringLiteral(" / ")));
        if (!execPrepared(search, &queryError)) {
            return fail(database_, queryError, error);
        }
    }

    if (!database_.commit()) {
        return fail(database_, database_.lastError().text(), error);
    }
    return true;
}

std::optional<domain::Question> SqliteQuestionRepository::findById(
    const QUuid &id,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM questions q JOIN banks b ON b.id=q.bank_id "
        "WHERE q.id=? AND q.active=1 AND b.active=1")
        .arg(kQuestionColumns));
    query.addBindValue(uuidText(id));
    if (!execPrepared(query, error) || !query.next()) {
        return std::nullopt;
    }
    QVector<domain::Question> questions{questionFromQuery(query)};
    if (!loadRelatedData(
            database_, &questions, QStringLiteral("q.id=?"), {uuidText(id)}, error)) {
        return std::nullopt;
    }
    return questions.constFirst();
}

QVector<domain::InstalledBankSummary> SqliteQuestionRepository::listInstalledBanks(
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT b.id, b.title, COUNT(q.id) FROM banks b "
        "LEFT JOIN questions q ON q.bank_id=b.id AND q.active=1 "
        "WHERE b.active=1 GROUP BY b.id, b.title ORDER BY b.updated_at DESC, b.title"));
    if (!execPrepared(query, error)) {
        return {};
    }

    QVector<domain::InstalledBankSummary> banks;
    while (query.next()) {
        domain::InstalledBankSummary bank;
        bank.id = query.value(0).toString();
        bank.title = query.value(1).toString();
        bank.questionCount = query.value(2).toLongLong();
        bank.path = firstQuestionPathForBank(database_, bank.id);
        if (bank.path.isEmpty()) {
            bank.path = {bank.title};
        }
        banks.append(bank);
    }
    return banks;
}

QVector<domain::Question> SqliteQuestionRepository::listByBankId(
    const QString &bankId,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM questions q JOIN banks b ON b.id=q.bank_id "
        "WHERE q.bank_id=? AND q.active=1 AND b.active=1 "
        "ORDER BY q.source_order").arg(kQuestionColumns));
    query.addBindValue(bankId);
    if (!execPrepared(query, error)) {
        return {};
    }
    QVector<domain::Question> questions;
    while (query.next()) {
        questions.append(questionFromQuery(query));
    }
    if (!loadRelatedData(
            database_, &questions, QStringLiteral("q.bank_id=? AND q.active=1"),
            {bankId}, error)) {
        return {};
    }
    return questions;
}

QVector<domain::Question> SqliteQuestionRepository::listByPath(
    const QStringList &path,
    QString *error) const
{
    clearError(error);
    const QString serializedPath = pathJson(path);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM questions q JOIN banks b ON b.id=q.bank_id "
        "WHERE q.path_json=? AND q.active=1 AND b.active=1 "
        "ORDER BY q.source_order").arg(kQuestionColumns));
    query.addBindValue(serializedPath);
    if (!execPrepared(query, error)) {
        return {};
    }
    QVector<domain::Question> questions;
    while (query.next()) {
        questions.append(questionFromQuery(query));
    }
    if (!loadRelatedData(
            database_, &questions, QStringLiteral("q.path_json=? AND q.active=1"),
            {serializedPath}, error)) {
        return {};
    }
    return questions;
}

QVector<domain::Question> SqliteQuestionRepository::listByPathPrefix(
    const QStringList &pathPrefix,
    QString *error) const
{
    clearError(error);
    if (pathPrefix.isEmpty()) {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "SELECT %1 FROM questions q JOIN banks b ON b.id=q.bank_id "
            "WHERE q.active=1 AND b.active=1 "
            "ORDER BY q.path_json, q.source_order").arg(kQuestionColumns));
        if (!execPrepared(query, error)) {
            return {};
        }
        QVector<domain::Question> questions;
        while (query.next()) {
            questions.append(questionFromQuery(query));
        }
        if (!loadRelatedData(
                database_, &questions, QStringLiteral("q.active=1"), {}, error)) {
            return {};
        }
        std::stable_sort(questions.begin(), questions.end(), domain::sourceQuestionLess);
        return questions;
    }

    const QString serializedPath = pathJson(pathPrefix);
    const QString descendantPrefix = serializedPath.left(serializedPath.size() - 1)
        + QStringLiteral(",");
    const QString whereClause = QStringLiteral(
        "(q.path_json=? OR substr(q.path_json, 1, ?)=?) AND q.active=1 "
        "AND EXISTS(SELECT 1 FROM banks b WHERE b.id=q.bank_id AND b.active=1)");
    const QVariantList values{
        serializedPath,
        descendantPrefix.size(),
        descendantPrefix,
    };
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM questions q WHERE %2 "
        "ORDER BY q.path_json, q.source_order").arg(kQuestionColumns, whereClause));
    for (const QVariant &value : values) {
        query.addBindValue(value);
    }
    if (!execPrepared(query, error)) {
        return {};
    }
    QVector<domain::Question> questions;
    while (query.next()) {
        questions.append(questionFromQuery(query));
    }
    if (!loadRelatedData(database_, &questions, whereClause, values, error)) {
        return {};
    }
    std::stable_sort(questions.begin(), questions.end(), domain::sourceQuestionLess);
    return questions;
}

qsizetype SqliteQuestionRepository::countByPath(
    const QStringList &path,
    QString *error) const
{
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM questions q JOIN banks b ON b.id=q.bank_id "
        "WHERE q.path_json=? AND q.active=1 AND b.active=1"));
    query.addBindValue(pathJson(path));
    if (!execPrepared(query, error) || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

} // namespace quizapp::storage
