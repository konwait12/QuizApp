#pragma once

#include "repositories/IStudyRepository.h"

#include <QSqlDatabase>

namespace quizapp::storage {

class SqliteStudyRepository final : public repositories::IStudyRepository {
public:
    explicit SqliteStudyRepository(QSqlDatabase database);

    bool append(
        const domain::StudyEvent &event,
        QString *error = nullptr) override;
    QVector<domain::StudyEvent> listStartedBetween(
        const QDateTime &from,
        const QDateTime &until,
        QString *error = nullptr) const override;

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage

