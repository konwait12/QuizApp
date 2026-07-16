#pragma once

#include "domain/WrongBookEntry.h"

#include <QSet>
#include <QStringList>

namespace quizapp::repositories {

class IWrongBookRepository {
public:
    virtual ~IWrongBookRepository() = default;

    virtual bool upsert(
        const domain::WrongBookEntry &entry,
        QString *error = nullptr) = 0;
    virtual bool remove(const QUuid &questionId, QString *error = nullptr) = 0;
    virtual bool contains(
        const QUuid &questionId,
        bool *contained,
        QString *error = nullptr) const = 0;
    virtual QSet<QUuid> listQuestionIdsByPathPrefix(
        const QStringList &pathPrefix,
        QString *error = nullptr) const = 0;
};

} // namespace quizapp::repositories
