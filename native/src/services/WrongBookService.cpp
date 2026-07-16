#include "services/WrongBookService.h"

namespace quizapp::services {

WrongBookService::WrongBookService(repositories::IWrongBookRepository &repository)
    : repository_(repository)
{
}

bool WrongBookService::setMembership(
    const QUuid &questionId,
    bool included,
    QString *error)
{
    if (!included) {
        return repository_.remove(questionId, error);
    }
    domain::WrongBookEntry entry;
    entry.questionId = questionId;
    return repository_.upsert(entry, error);
}

QSet<QUuid> WrongBookService::questionIds(
    const QStringList &pathPrefix,
    QString *error) const
{
    return repository_.listQuestionIdsByPathPrefix(pathPrefix, error);
}

} // namespace quizapp::services
