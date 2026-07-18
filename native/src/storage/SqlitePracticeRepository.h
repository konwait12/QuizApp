#pragma once

#include "repositories/IPracticeRepository.h"

#include <QSqlDatabase>
#include <QVector>

namespace quizapp::storage {

class SqlitePracticeRepository final : public repositories::IPracticeRepository {
public:
    explicit SqlitePracticeRepository(QSqlDatabase database);

    bool save(
        const domain::PracticeSession &session,
        QString *error = nullptr) override;
    std::optional<domain::PracticeSession> load(
        const QUuid &sessionId,
        QString *error = nullptr) const override;
    std::optional<domain::PracticeSession> latest(
        const QString &scopeId,
        domain::PracticeMode mode,
        QString *error = nullptr) const override;
    std::optional<domain::PracticeSession> latestAcrossModes(
        const QString &scopeId,
        const QVector<domain::PracticeMode> &modes,
        QString *error = nullptr) const;
    std::optional<domain::PracticeSession> latestIncompleteAcrossScopes(
        const QVector<domain::PracticeMode> &modes,
        QString *error = nullptr) const;
    bool remove(
        const QUuid &sessionId,
        QString *error = nullptr) override;
    bool removeScopeMode(
        const QString &scopeId,
        domain::PracticeMode mode,
        QString *error = nullptr);

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
