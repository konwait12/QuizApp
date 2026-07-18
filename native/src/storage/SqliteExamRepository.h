#pragma once

#include "domain/ExamSession.h"

#include <QSqlDatabase>

#include <optional>

namespace quizapp::storage {

class SqliteExamRepository final {
public:
    explicit SqliteExamRepository(QSqlDatabase database);

    bool save(const domain::ExamSession &session, QString *error = nullptr);
    std::optional<domain::ExamSession> load(
        const QUuid &sessionId,
        QString *error = nullptr) const;
    std::optional<domain::ExamSession> latestActive(
        QString *error = nullptr) const;
    QVector<domain::ExamSession> history(
        qsizetype limit = 30,
        QString *error = nullptr) const;
    bool remove(const QUuid &sessionId, QString *error = nullptr);

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
