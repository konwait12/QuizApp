#include "services/ReviewService.h"

#include <algorithm>

namespace quizapp::services {
namespace {

int ratingIndex(domain::ReviewRating rating)
{
    return static_cast<int>(rating) - 1;
}

int elapsedDays(const domain::ReviewCard &card, const QDateTime &now)
{
    return card.lastReviewAt.isValid()
        ? static_cast<int>(std::max<qint64>(0, card.lastReviewAt.daysTo(now)))
        : 0;
}

} // namespace

ReviewService::ReviewService(repositories::IReviewRepository &repository)
    : repository_(repository)
{
}

bool ReviewService::add(const QUuid &questionId, const QDateTime &now, QString *error)
{
    if (error) {
        error->clear();
    }
    if (questionId.isNull() || !now.isValid()) {
        if (error) {
            *error = QStringLiteral("Invalid question or review time");
        }
        return false;
    }
    QString localError;
    QString *resultError = error ? error : &localError;
    if (repository_.find(questionId, resultError).has_value()) {
        return resultError->isEmpty();
    }
    if (!resultError->isEmpty()) {
        return false;
    }
    domain::ReviewCard card;
    card.questionId = questionId;
    card.dueAt = now.toUTC();
    card.createdAt = now.toUTC();
    card.updatedAt = now.toUTC();
    card.schedulerVersion = scheduler_.version();
    return repository_.upsert(card, resultError);
}

bool ReviewService::remove(const QUuid &questionId, QString *error)
{
    return repository_.remove(questionId, error);
}

bool ReviewService::contains(
    const QUuid &questionId,
    bool *contained,
    QString *error) const
{
    if (error) {
        error->clear();
    }
    if (!contained) {
        if (error) {
            *error = QStringLiteral("Review membership output pointer is null");
        }
        return false;
    }
    QString localError;
    QString *resultError = error ? error : &localError;
    *contained = repository_.find(questionId, resultError).has_value();
    return resultError->isEmpty();
}

QVector<domain::ReviewCard> ReviewService::due(
    const QDateTime &now,
    int limit,
    QString *error) const
{
    return repository_.listDue(now, limit, error);
}

QSet<QUuid> ReviewService::questionIds(
    const QStringList &pathPrefix,
    QString *error) const
{
    return repository_.listQuestionIdsByPathPrefix(pathPrefix, error);
}

domain::ReviewStats ReviewService::stats(const QDateTime &now, QString *error) const
{
    return repository_.stats(now, error);
}

std::optional<std::array<domain::ReviewPreview, 4>> ReviewService::preview(
    const QUuid &questionId,
    const QDateTime &now,
    QString *error) const
{
    const auto card = repository_.find(questionId, error);
    if (!card.has_value()) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("Review card does not exist");
        }
        return std::nullopt;
    }
    return scheduler_.preview(*card, now, error);
}

std::optional<domain::ReviewCard> ReviewService::rate(
    const QUuid &questionId,
    domain::ReviewRating rating,
    const QDateTime &now,
    QString *error)
{
    const int index = ratingIndex(rating);
    if (index < 0 || index >= 4) {
        if (error) {
            *error = QStringLiteral("Invalid review rating");
        }
        return std::nullopt;
    }
    const auto current = repository_.find(questionId, error);
    if (!current.has_value()) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("Review card does not exist");
        }
        return std::nullopt;
    }
    const auto previews = scheduler_.preview(*current, now, error);
    if (!previews.has_value()) {
        return std::nullopt;
    }
    const domain::ReviewPreview chosen = previews->at(static_cast<size_t>(index));
    domain::ReviewCard updated = *current;
    updated.hasMemoryState = true;
    updated.memory = chosen.memory;
    updated.dueAt = chosen.dueAt;
    updated.lastReviewAt = now.toUTC();
    updated.scheduledDays = chosen.scheduledDays;
    ++updated.reviewCount;
    if (rating == domain::ReviewRating::Again) {
        ++updated.lapseCount;
    }
    updated.schedulerVersion = scheduler_.version();
    updated.updatedAt = now.toUTC();

    domain::ReviewLog log;
    log.questionId = questionId;
    log.rating = rating;
    log.reviewedAt = now.toUTC();
    log.dueBefore = current->dueAt;
    log.dueAfter = updated.dueAt;
    log.elapsedDays = elapsedDays(*current, now);
    log.scheduledDays = updated.scheduledDays;
    log.hadMemoryState = current->hasMemoryState;
    log.memoryBefore = current->memory;
    log.memoryAfter = updated.memory;
    log.schedulerVersion = updated.schedulerVersion;
    if (!repository_.saveReview(updated, log, error)) {
        return std::nullopt;
    }
    return updated;
}

} // namespace quizapp::services
