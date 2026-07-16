#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace quizapp::domain {

struct WrongBookEntry {
    QUuid questionId;
    QStringList reasonTags;
    QString note;
    QDateTime addedAt;
    QDateTime updatedAt;
};

} // namespace quizapp::domain
