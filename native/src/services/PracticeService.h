#pragma once

#include "domain/PracticeSession.h"

#include <optional>

namespace quizapp::services {

class PracticeService final {
public:
    domain::PracticeSession start(
        const QString &scopeId,
        domain::PracticeMode mode,
        const QVector<QUuid> &questionIds,
        quint64 randomSeed = 0) const;

    bool selectAnswer(domain::PracticeSession &session, const QString &answer) const;
    bool toggleDraftOption(domain::PracticeSession &session, QChar option) const;
    bool submitDraft(domain::PracticeSession &session) const;
    bool toggleReveal(domain::PracticeSession &session) const;
    bool move(domain::PracticeSession &session, qsizetype targetIndex) const;

    std::optional<QUuid> currentQuestionId(const domain::PracticeSession &session) const;
    domain::NotebookLaunchContext notebookContext(const domain::PracticeSession &session) const;
};

} // namespace quizapp::services
