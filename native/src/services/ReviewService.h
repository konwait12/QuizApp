#pragma once

#include "domain/ReviewCard.h"
#include "repositories/IReviewRepository.h"
#include "services/FsrsScheduler.h"

#include <array>
#include <optional>

namespace quizapp::services {

class ReviewService final {
public:
    explicit ReviewService(repositories::IReviewRepository &repository);

    bool add(
        const QUuid &questionId,
        const QDateTime &now = QDateTime::currentDateTimeUtc(),
        QString *error = nullptr);
    bool remove(const QUuid &questionId, QString *error = nullptr);
    bool contains(
        const QUuid &questionId,
        bool *contained,
        QString *error = nullptr) const;
    QVector<domain::ReviewCard> due(
        const QDateTime &now,
        int limit = 100,
        QString *error = nullptr) const;
    QSet<QUuid> questionIds(
        const QStringList &pathPrefix = {},
        QString *error = nullptr) const;
    domain::ReviewStats stats(
        const QDateTime &now,
        QString *error = nullptr) const;
    std::optional<std::array<domain::ReviewPreview, 4>> preview(
        const QUuid &questionId,
        const QDateTime &now,
        QString *error = nullptr) const;
    std::optional<domain::ReviewCard> rate(
        const QUuid &questionId,
        domain::ReviewRating rating,
        const QDateTime &now,
        QString *error = nullptr);

private:
    repositories::IReviewRepository &repository_;
    FsrsScheduler scheduler_;
};

} // namespace quizapp::services
