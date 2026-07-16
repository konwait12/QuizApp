#pragma once

#include "domain/ReviewCard.h"

#include <QSet>
#include <optional>

namespace quizapp::repositories {

class IReviewRepository {
public:
    virtual ~IReviewRepository() = default;

    virtual bool upsert(
        const domain::ReviewCard &card,
        QString *error = nullptr) = 0;
    virtual bool saveReview(
        const domain::ReviewCard &card,
        const domain::ReviewLog &log,
        QString *error = nullptr) = 0;
    virtual bool remove(const QUuid &questionId, QString *error = nullptr) = 0;
    virtual std::optional<domain::ReviewCard> find(
        const QUuid &questionId,
        QString *error = nullptr) const = 0;
    virtual QVector<domain::ReviewCard> listDue(
        const QDateTime &now,
        int limit,
        QString *error = nullptr) const = 0;
    virtual QVector<domain::ReviewLog> history(
        const QUuid &questionId,
        int limit,
        QString *error = nullptr) const = 0;
    virtual QSet<QUuid> listQuestionIdsByPathPrefix(
        const QStringList &pathPrefix,
        QString *error = nullptr) const = 0;
    virtual domain::ReviewStats stats(
        const QDateTime &now,
        QString *error = nullptr) const = 0;
};

} // namespace quizapp::repositories
