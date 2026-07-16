#pragma once

#include <QDateTime>
#include <QUuid>

namespace quizapp::domain {

enum class ReviewRating : int {
    Again = 1,
    Hard = 2,
    Good = 3,
    Easy = 4,
};

struct ReviewMemoryState {
    double stability = 0.0;
    double difficulty = 0.0;

    bool isValid() const
    {
        return stability > 0.0 && difficulty >= 1.0 && difficulty <= 10.0;
    }
};

struct ReviewCard {
    QUuid questionId;
    bool hasMemoryState = false;
    ReviewMemoryState memory;
    double desiredRetention = 0.9;
    QDateTime dueAt;
    QDateTime lastReviewAt;
    int scheduledDays = 0;
    int reviewCount = 0;
    int lapseCount = 0;
    QString schedulerVersion = QStringLiteral("fsrs-rs/6.6.0");
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct ReviewPreview {
    ReviewRating rating = ReviewRating::Good;
    ReviewMemoryState memory;
    double intervalDays = 0.0;
    int scheduledDays = 0;
    QDateTime dueAt;
};

struct ReviewLog {
    qint64 id = 0;
    QUuid questionId;
    ReviewRating rating = ReviewRating::Good;
    QDateTime reviewedAt;
    QDateTime dueBefore;
    QDateTime dueAfter;
    int elapsedDays = 0;
    int scheduledDays = 0;
    bool hadMemoryState = false;
    ReviewMemoryState memoryBefore;
    ReviewMemoryState memoryAfter;
    QString schedulerVersion;
};

struct ReviewStats {
    int total = 0;
    int due = 0;
    int fresh = 0;
    int learned = 0;
};

} // namespace quizapp::domain

