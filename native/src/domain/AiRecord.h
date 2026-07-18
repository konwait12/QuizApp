#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUuid>

namespace quizapp::domain {

struct AiRecord {
    QUuid id;
    QString recordType;
    QString sourceId;
    QString model;
    QString content;
    QByteArray sourceHash;
    QDateTime createdAt;
};

} // namespace quizapp::domain
