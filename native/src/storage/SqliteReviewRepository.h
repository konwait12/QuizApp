#pragma once

#include "repositories/IReviewRepository.h"

#include <QSqlDatabase>

namespace quizapp::storage {

class SqliteReviewRepository final : public repositories::IReviewRepository {
public:
    explicit SqliteReviewRepository(QSqlDatabase database);

    bool upsert(
        const domain::ReviewCard &card,
        QString *error = nullptr) override;
    bool saveReview(
        const domain::ReviewCard &card,
        const domain::ReviewLog &log,
        QString *error = nullptr) override;
    bool remove(const QUuid &questionId, QString *error = nullptr) override;
    std::optional<domain::ReviewCard> find(
        const QUuid &questionId,
        QString *error = nullptr) const override;
    QVector<domain::ReviewCard> listDue(
        const QDateTime &now,
        int limit,
        QString *error = nullptr) const override;
    QVector<domain::ReviewLog> history(
        const QUuid &questionId,
        int limit,
        QString *error = nullptr) const override;
    QSet<QUuid> listQuestionIdsByPathPrefix(
        const QStringList &pathPrefix,
        QString *error = nullptr) const override;
    domain::ReviewStats stats(
        const QDateTime &now,
        QString *error = nullptr) const override;

private:
    bool upsertWithoutTransaction(
        const domain::ReviewCard &card,
        QString *error) const;

    QSqlDatabase database_;
};

} // namespace quizapp::storage
