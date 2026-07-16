#include "services/FsrsScheduler.h"

#include "quizapp_fsrs_bridge.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace quizapp::services {
namespace {

int elapsedDays(const domain::ReviewCard &card, const QDateTime &reviewedAt)
{
    if (!card.lastReviewAt.isValid()) {
        return 0;
    }
    return static_cast<int>(std::max<qint64>(0, card.lastReviewAt.daysTo(reviewedAt)));
}

domain::ReviewPreview toPreview(
    domain::ReviewRating rating,
    const QuizAppFsrsItemState &state,
    const QDateTime &reviewedAt)
{
    domain::ReviewPreview preview;
    preview.rating = rating;
    preview.memory.stability = state.memory.stability;
    preview.memory.difficulty = state.memory.difficulty;
    preview.intervalDays = state.interval_days;
    preview.scheduledDays = std::max(1, qRound(state.interval_days));
    preview.dueAt = reviewedAt.addDays(preview.scheduledDays);
    return preview;
}

} // namespace

std::optional<std::array<domain::ReviewPreview, 4>> FsrsScheduler::preview(
    const domain::ReviewCard &card,
    const QDateTime &reviewedAt,
    QString *error) const
{
    if (error) {
        error->clear();
    }
    if (!reviewedAt.isValid() || card.desiredRetention <= 0.0
        || card.desiredRetention >= 1.0
        || (card.hasMemoryState && !card.memory.isValid())) {
        if (error) {
            *error = QStringLiteral("Invalid FSRS review state");
        }
        return std::nullopt;
    }

    QuizAppFsrsNextStates states{};
    const QuizAppFsrsMemoryState memory{
        static_cast<float>(card.memory.stability),
        static_cast<float>(card.memory.difficulty),
    };
    const int status = quizapp_fsrs_next_states(
        card.hasMemoryState,
        memory,
        static_cast<float>(card.desiredRetention),
        static_cast<uint32_t>(elapsedDays(card, reviewedAt)),
        &states);
    if (status != QUIZAPP_FSRS_OK) {
        if (error) {
            *error = QStringLiteral("FSRS scheduler failed with status %1").arg(status);
        }
        return std::nullopt;
    }

    return std::array<domain::ReviewPreview, 4>{
        toPreview(domain::ReviewRating::Again, states.again, reviewedAt),
        toPreview(domain::ReviewRating::Hard, states.hard, reviewedAt),
        toPreview(domain::ReviewRating::Good, states.good, reviewedAt),
        toPreview(domain::ReviewRating::Easy, states.easy, reviewedAt),
    };
}

QString FsrsScheduler::version() const
{
    return QString::fromLatin1(quizapp_fsrs_scheduler_version());
}

} // namespace quizapp::services

