#include "services/BlobStore.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace quizapp::services {
namespace {

constexpr qsizetype kMaximumMediaBytes = 32 * 1024 * 1024;

struct DecodedMedia {
    QByteArray bytes;
    QString mediaType;
    QString extension;
};

void clearError(QString *error)
{
    if (error) {
        error->clear();
    }
}

bool fail(const QString &message, QString *error)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool decodeDataUri(const QString &source, DecodedMedia *decoded, QString *error)
{
    if (!source.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
        return fail(QStringLiteral("Only embedded data URI media is supported"), error);
    }
    const qsizetype comma = source.indexOf(u',');
    if (comma <= 5) {
        return fail(QStringLiteral("Media data URI header is invalid"), error);
    }

    const QStringList header = source.mid(5, comma - 5).split(u';', Qt::SkipEmptyParts);
    if (header.isEmpty() || !header.contains(QStringLiteral("base64"), Qt::CaseInsensitive)) {
        return fail(QStringLiteral("Media data URI must use Base64 encoding"), error);
    }
    const QString declaredType = header.constFirst().trimmed().toLower();
    const QHash<QString, QString> allowedTypes{
        {QStringLiteral("image/png"), QStringLiteral("png")},
        {QStringLiteral("image/jpeg"), QStringLiteral("jpg")},
        {QStringLiteral("image/gif"), QStringLiteral("gif")},
        {QStringLiteral("image/webp"), QStringLiteral("webp")},
    };
    if (!allowedTypes.contains(declaredType)) {
        return fail(QStringLiteral("Unsupported embedded media type: %1").arg(declaredType), error);
    }

    const QByteArray encoded = source.mid(comma + 1).toLatin1();
    const auto result = QByteArray::fromBase64Encoding(
        encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!result || result.decoded.isEmpty()) {
        return fail(QStringLiteral("Embedded media Base64 payload is invalid"), error);
    }
    if (result.decoded.size() > kMaximumMediaBytes) {
        return fail(QStringLiteral("Embedded media exceeds the 32 MiB item limit"), error);
    }

    QBuffer buffer;
    buffer.setData(result.decoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QByteArray detectedFormat = reader.format().toLower();
    const QHash<QByteArray, QString> detectedTypes{
        {QByteArrayLiteral("png"), QStringLiteral("image/png")},
        {QByteArrayLiteral("jpeg"), QStringLiteral("image/jpeg")},
        {QByteArrayLiteral("jpg"), QStringLiteral("image/jpeg")},
        {QByteArrayLiteral("gif"), QStringLiteral("image/gif")},
        {QByteArrayLiteral("webp"), QStringLiteral("image/webp")},
    };
    if (!detectedTypes.contains(detectedFormat)
        || detectedTypes.value(detectedFormat) != declaredType
        || !reader.canRead()) {
        return fail(QStringLiteral("Embedded media content does not match its declared type"), error);
    }

    decoded->bytes = result.decoded;
    decoded->mediaType = declaredType;
    decoded->extension = allowedTypes.value(declaredType);
    return true;
}

QString blobRelativePath(const QString &id, const QString &extension)
{
    return QStringLiteral("blobs/%1/%2/%3.%4")
        .arg(id.left(2), id.mid(2, 2), id, extension);
}

} // namespace

BlobStore::BlobStore(QString rootDirectory)
    : rootDirectory_(QDir::cleanPath(std::move(rootDirectory)))
{
}

QString BlobStore::rootDirectory() const
{
    return rootDirectory_;
}

QString BlobStore::absolutePath(const domain::BlobAsset &asset) const
{
    return QDir(rootDirectory_).filePath(asset.relativePath);
}

bool BlobStore::materialize(
    const QVector<domain::PendingMediaReference> &references,
    domain::BankImportPackage *package,
    QString *error) const
{
    clearError(error);
    if (!package || rootDirectory_.isEmpty()) {
        return fail(QStringLiteral("Blob store root and import package are required"), error);
    }
    if (!QDir().mkpath(rootDirectory_)) {
        return fail(QStringLiteral("Blob store root cannot be created"), error);
    }

    QHash<QUuid, qsizetype> questionIndexes;
    for (qsizetype index = 0; index < package->bank.questions.size(); ++index) {
        questionIndexes.insert(package->bank.questions.at(index).id, index);
    }

    QHash<QString, domain::BlobAsset> assets;
    QHash<QUuid, QVector<QPair<int, QString>>> questionAssignments;
    QHash<QUuid, QVector<QPair<int, QString>>> explanationAssignments;
    QStringList createdFiles;
    for (const domain::PendingMediaReference &reference : references) {
        if (!questionIndexes.contains(reference.questionId) || reference.sortOrder < 0) {
            for (const QString &path : createdFiles) {
                QFile::remove(path);
            }
            return fail(QStringLiteral("Media reference points to an unknown question or order"), error);
        }

        DecodedMedia decoded;
        QString decodeError;
        if (!decodeDataUri(reference.source, &decoded, &decodeError)) {
            for (const QString &path : createdFiles) {
                QFile::remove(path);
            }
            return fail(QStringLiteral("Question media could not be imported: %1").arg(decodeError), error);
        }

        const QString id = QString::fromLatin1(
            QCryptographicHash::hash(decoded.bytes, QCryptographicHash::Sha256).toHex());
        const QString relativePath = blobRelativePath(id, decoded.extension);
        const QString targetPath = QDir(rootDirectory_).filePath(relativePath);
        if (!QFileInfo::exists(targetPath)) {
            const QFileInfo targetInfo(targetPath);
            if (!QDir().mkpath(targetInfo.absolutePath())) {
                for (const QString &path : createdFiles) {
                    QFile::remove(path);
                }
                return fail(QStringLiteral("Blob directory cannot be created"), error);
            }
            QSaveFile file(targetPath);
            if (!file.open(QIODevice::WriteOnly)
                || file.write(decoded.bytes) != decoded.bytes.size()
                || !file.commit()) {
                for (const QString &path : createdFiles) {
                    QFile::remove(path);
                }
                return fail(QStringLiteral("Blob could not be written atomically"), error);
            }
            createdFiles.append(targetPath);
        }

        assets.insert(id, domain::BlobAsset{
            id,
            decoded.mediaType,
            decoded.bytes.size(),
            relativePath,
            QDateTime::currentDateTimeUtc(),
        });
        auto &assignments = reference.role == domain::ImportedMediaRole::Question
            ? questionAssignments[reference.questionId]
            : explanationAssignments[reference.questionId];
        assignments.append({reference.sortOrder, id});
    }

    auto sortedIds = [](QVector<QPair<int, QString>> values) {
        std::sort(values.begin(), values.end(), [](const auto &left, const auto &right) {
            return left.first < right.first;
        });
        QStringList ids;
        QSet<int> orders;
        for (const auto &[order, id] : values) {
            if (orders.contains(order)) {
                return QStringList();
            }
            orders.insert(order);
            ids.append(id);
        }
        return ids;
    };

    for (auto iterator = questionIndexes.cbegin(); iterator != questionIndexes.cend(); ++iterator) {
        domain::Question &question = package->bank.questions[iterator.value()];
        question.questionImageBlobIds = sortedIds(questionAssignments.value(iterator.key()));
        question.builtinExplanation.imageBlobIds = sortedIds(
            explanationAssignments.value(iterator.key()));
    }
    package->blobs = assets.values();
    std::sort(package->blobs.begin(), package->blobs.end(), [](const auto &left, const auto &right) {
        return left.id < right.id;
    });
    return true;
}

} // namespace quizapp::services
