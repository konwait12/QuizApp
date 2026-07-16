#pragma once

#include "domain/Question.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

namespace quizapp::domain {

struct SubjectNode {
    QString id;
    QString parentId;
    QString title;
    QString icon;
    int sortOrder = 0;
};

struct QuestionBank {
    QString id;
    QString subjectId;
    QString title;
    QString sourceProvider;
    QString sourceId;
    QByteArray contentHash;
    QString distributionVersion;
    QVector<Question> questions;
};

struct InstalledBankSummary {
    QString id;
    QString title;
    QStringList path;
    qsizetype questionCount = 0;
};

struct BlobAsset {
    QString id;
    QString mediaType;
    qint64 byteSize = 0;
    QString relativePath;
    QDateTime createdAt;
};

struct BankImportPackage {
    QVector<SubjectNode> subjects;
    QuestionBank bank;
    QVector<BlobAsset> blobs;
};

} // namespace quizapp::domain
