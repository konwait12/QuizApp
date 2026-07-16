#pragma once

#include "domain/Question.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace quizapp::domain {

class QuestionIdentity final {
public:
    static QUuid create(
        const QString &provider,
        const QString &sourceId,
        const QStringList &path,
        const QString &prompt,
        const QStringList &options);

    static QByteArray contentHash(
        const QStringList &path,
        const QString &prompt,
        const QStringList &options);
    static QByteArray contentHash(const Question &question);

private:
    static QByteArray canonicalBytes(
        const QStringList &path,
        const QString &prompt,
        const QStringList &options);
};

} // namespace quizapp::domain
