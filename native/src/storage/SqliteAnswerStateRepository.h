#pragma once

#include "domain/PracticeSession.h"

#include <QHash>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace quizapp::storage {

class SqliteAnswerStateRepository final {
public:
    explicit SqliteAnswerStateRepository(QSqlDatabase database);

    QHash<QUuid, QString> load(
        const QVector<QUuid> &questionIds,
        domain::PracticeMode mode,
        QString *error = nullptr) const;
    bool saveSessionAnswers(
        const domain::PracticeSession &session,
        QString *error = nullptr);
    bool clear(
        const QVector<QUuid> &questionIds,
        domain::PracticeMode mode,
        QString *error = nullptr);
    bool applyToSession(
        domain::PracticeSession *session,
        QString *error = nullptr) const;

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
