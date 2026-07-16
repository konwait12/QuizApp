#pragma once

#include "repositories/IWrongBookRepository.h"

namespace quizapp::services {

class WrongBookService final {
public:
    explicit WrongBookService(repositories::IWrongBookRepository &repository);

    bool setMembership(
        const QUuid &questionId,
        bool included,
        QString *error = nullptr);
    QSet<QUuid> questionIds(
        const QStringList &pathPrefix,
        QString *error = nullptr) const;

private:
    repositories::IWrongBookRepository &repository_;
};

} // namespace quizapp::services
