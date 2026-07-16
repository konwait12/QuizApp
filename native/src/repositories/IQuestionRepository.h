#pragma once

#include "domain/Question.h"
#include "domain/QuestionBank.h"

#include <optional>

namespace quizapp::repositories {

class IQuestionRepository {
public:
    virtual ~IQuestionRepository() = default;

    virtual bool replaceBank(
        const domain::BankImportPackage &package,
        QString *error = nullptr) = 0;
    virtual std::optional<domain::Question> findById(
        const QUuid &id,
        QString *error = nullptr) const = 0;
    virtual QVector<domain::InstalledBankSummary> listInstalledBanks(
        QString *error = nullptr) const = 0;
    virtual QVector<domain::Question> listByBankId(
        const QString &bankId,
        QString *error = nullptr) const = 0;
    virtual QVector<domain::Question> listByPath(
        const QStringList &path,
        QString *error = nullptr) const = 0;
    virtual QVector<domain::Question> listByPathPrefix(
        const QStringList &pathPrefix,
        QString *error = nullptr) const = 0;
    virtual qsizetype countByPath(
        const QStringList &path,
        QString *error = nullptr) const = 0;
};

} // namespace quizapp::repositories
