#pragma once

#include "domain/StudyEvent.h"

namespace quizapp::repositories {

class IStudyRepository {
public:
    virtual ~IStudyRepository() = default;

    virtual bool append(
        const domain::StudyEvent &event,
        QString *error = nullptr) = 0;
    virtual QVector<domain::StudyEvent> listStartedBetween(
        const QDateTime &from,
        const QDateTime &until,
        QString *error = nullptr) const = 0;
};

} // namespace quizapp::repositories

