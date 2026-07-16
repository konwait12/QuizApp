#pragma once

#include "repositories/IQuestionRepository.h"

#include <QSqlDatabase>

namespace quizapp::storage {

class SqliteQuestionRepository final : public repositories::IQuestionRepository {
public:
    explicit SqliteQuestionRepository(QSqlDatabase database);

    bool replaceBank(
        const domain::BankImportPackage &package,
        QString *error = nullptr) override;
    std::optional<domain::Question> findById(
        const QUuid &id,
        QString *error = nullptr) const override;
    QVector<domain::InstalledBankSummary> listInstalledBanks(
        QString *error = nullptr) const override;
    QVector<domain::Question> listByBankId(
        const QString &bankId,
        QString *error = nullptr) const override;
    QVector<domain::Question> listByPath(
        const QStringList &path,
        QString *error = nullptr) const override;
    QVector<domain::Question> listByPathPrefix(
        const QStringList &pathPrefix,
        QString *error = nullptr) const override;
    qsizetype countByPath(
        const QStringList &path,
        QString *error = nullptr) const override;

private:
    QSqlDatabase database_;
};

} // namespace quizapp::storage
