#pragma once

#include "domain/ExamSession.h"

namespace quizapp::services {

class ExamService final {
public:
    domain::ExamSession start(
        const QString &scopeId,
        const QString &title,
        const QVector<domain::Question> &questions,
        qsizetype requestedCount,
        int durationSeconds,
        quint64 randomSeed = 0) const;

    bool selectAnswer(
        domain::ExamSession &session,
        const domain::Question &question,
        QChar option) const;
    bool move(domain::ExamSession &session, qsizetype targetIndex) const;
    bool setPaused(domain::ExamSession &session, bool paused) const;
    bool advanceTimer(domain::ExamSession &session, int elapsedSeconds) const;
    bool submit(
        domain::ExamSession &session,
        const QHash<QUuid, domain::Question> &questions,
        bool timedOut = false) const;

    static bool isObjective(const domain::Question &question);
    static QString normalizeAnswer(QString answer);
};

} // namespace quizapp::services
