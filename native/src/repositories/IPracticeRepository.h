#pragma once

#include "domain/PracticeSession.h"

#include <optional>

namespace quizapp::repositories {

class IPracticeRepository {
public:
    virtual ~IPracticeRepository() = default;

    virtual bool save(
        const domain::PracticeSession &session,
        QString *error = nullptr) = 0;
    virtual std::optional<domain::PracticeSession> load(
        const QUuid &sessionId,
        QString *error = nullptr) const = 0;
    virtual std::optional<domain::PracticeSession> latest(
        const QString &scopeId,
        domain::PracticeMode mode,
        QString *error = nullptr) const = 0;
    virtual bool remove(
        const QUuid &sessionId,
        QString *error = nullptr) = 0;
};

} // namespace quizapp::repositories
