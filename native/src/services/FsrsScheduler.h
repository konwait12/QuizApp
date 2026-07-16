#pragma once

#include "domain/ReviewCard.h"

#include <array>
#include <optional>

namespace quizapp::services {

class FsrsScheduler final {
public:
    std::optional<std::array<domain::ReviewPreview, 4>> preview(
        const domain::ReviewCard &card,
        const QDateTime &reviewedAt,
        QString *error = nullptr) const;

    QString version() const;
};

} // namespace quizapp::services

