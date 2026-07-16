#include "domain/QuestionIdentity.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace quizapp::domain {
namespace {

const QUuid kQuestionNamespace(QStringLiteral("0fddc8b1-cc77-5d81-a514-9b0b995ce06b"));

QString normalized(const QString &value)
{
    return value.simplified().normalized(QString::NormalizationForm_KC);
}

QJsonArray normalizedArray(const QStringList &values)
{
    QJsonArray output;
    for (const QString &value : values) {
        output.append(normalized(value));
    }
    return output;
}

} // namespace

QUuid QuestionIdentity::create(
    const QString &provider,
    const QString &sourceId,
    const QStringList &path,
    const QString &prompt,
    const QStringList &options)
{
    QByteArray identity;
    if (!provider.trimmed().isEmpty() && !sourceId.trimmed().isEmpty()) {
        identity = normalized(provider).toUtf8() + ':' + normalized(sourceId).toUtf8();
    } else {
        identity = canonicalBytes(path, prompt, options);
    }
    return QUuid::createUuidV5(kQuestionNamespace, identity);
}

QByteArray QuestionIdentity::contentHash(
    const QStringList &path,
    const QString &prompt,
    const QStringList &options)
{
    return QCryptographicHash::hash(canonicalBytes(path, prompt, options), QCryptographicHash::Sha256);
}

QByteArray QuestionIdentity::contentHash(const Question &question)
{
    QJsonObject canonical;
    canonical.insert(QStringLiteral("answer"), normalized(question.correctAnswer));
    canonical.insert(QStringLiteral("explanation"), normalized(question.builtinExplanation.text));
    canonical.insert(QStringLiteral("explanationImages"),
        normalizedArray(question.builtinExplanation.imageBlobIds));
    canonical.insert(QStringLiteral("explanationVideo"),
        normalized(question.builtinExplanation.videoUrl));
    canonical.insert(QStringLiteral("options"), normalizedArray(question.options));
    canonical.insert(QStringLiteral("path"), normalizedArray(question.path));
    canonical.insert(QStringLiteral("prompt"), normalized(question.prompt));
    canonical.insert(QStringLiteral("questionImages"),
        normalizedArray(question.questionImageBlobIds));
    canonical.insert(QStringLiteral("type"), static_cast<int>(question.type));
    return QCryptographicHash::hash(
        QJsonDocument(canonical).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256);
}

QByteArray QuestionIdentity::canonicalBytes(
    const QStringList &path,
    const QString &prompt,
    const QStringList &options)
{
    QJsonObject canonical;
    canonical.insert(QStringLiteral("options"), normalizedArray(options));
    canonical.insert(QStringLiteral("path"), normalizedArray(path));
    canonical.insert(QStringLiteral("prompt"), normalized(prompt));
    return QJsonDocument(canonical).toJson(QJsonDocument::Compact);
}

} // namespace quizapp::domain
