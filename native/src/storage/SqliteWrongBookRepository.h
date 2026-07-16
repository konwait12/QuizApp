#pragma once

#include "repositories/IWrongBookRepository.h"

#include <QSqlDatabase>

namespace quizapp::storage {

class SqliteWrongBookRepository final : public repositories::IWrongBookRepository {
public:
    explicit SqliteWrongBookRepository(QSqlDatabase database);

    bool upsert(
        const domain::WrongBookEntry &entry,
        QString *error = nullptr) override;
    bool remove(const QUuid &questionId, QString *error = nullptr) override;
    bool contains(
        const QUuid &questionId,
        bool *contained,
        QString *error = nullptr) const override;
    QSet<QUuid> listQuestionIdsByPathPrefix(
        const QStringList &pathPrefix,
        QString *error = nullptr) const override;

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
