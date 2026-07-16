#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

namespace quizapp::domain {

enum class QuestionType {
    Single,
    Multiple,
    Boolean,
    Subjective,
};

struct BuiltinExplanation {
    QString text;
    QStringList imageBlobIds;
    QString videoUrl;
    QString provider;
    QString sourceId;
};

struct Question {
    QUuid id;
    QString bankId;
    QString sourceProvider;
    QString sourceId;
    QStringList path;
    QuestionType type = QuestionType::Single;
    QString prompt;
    QStringList options;
    QString correctAnswer;
    int sourceOrder = 0;
    QStringList questionImageBlobIds;
    BuiltinExplanation builtinExplanation;
    QHash<QString, QString> blobRelativePaths;
    QByteArray contentHash;
    QDateTime updatedAt;
};

} // namespace quizapp::domain
