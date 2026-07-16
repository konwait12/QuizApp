#include "services/PracticeService.h"

#include <QRandomGenerator>
#include <algorithm>

namespace quizapp::services {

domain::PracticeSession PracticeService::start(
    const QString &scopeId,
    domain::PracticeMode mode,
    const QVector<QUuid> &questionIds,
    quint64 randomSeed) const
{
    domain::PracticeSession session;
    session.id = QUuid::createUuid();
    session.scopeId = scopeId;
    session.mode = mode;
    session.questionOrder = questionIds;

    if (mode == domain::PracticeMode::Random && session.questionOrder.size() > 1) {
        QRandomGenerator generator(randomSeed == 0 ? QRandomGenerator::global()->generate64() : randomSeed);
        std::shuffle(session.questionOrder.begin(), session.questionOrder.end(), generator);
    }
    return session;
}

bool PracticeService::selectAnswer(domain::PracticeSession &session, const QString &answer) const
{
    const auto questionId = currentQuestionId(session);
    if (!questionId.has_value()) {
        return false;
    }
    const QString normalized = answer.trimmed().toUpper();
    if (session.answers.value(*questionId) == normalized) {
        session.answers.remove(*questionId);
    } else if (!normalized.isEmpty()) {
        session.answers.insert(*questionId, normalized);
    } else {
        session.answers.remove(*questionId);
    }
    session.drafts.remove(*questionId);
    session.revealedAnswers.remove(*questionId);
    session.dirty = true;
    return true;
}

bool PracticeService::toggleDraftOption(domain::PracticeSession &session, QChar option) const
{
    const auto questionId = currentQuestionId(session);
    const QChar normalized = option.toUpper();
    if (!questionId.has_value() || normalized < u'A' || normalized > u'H') {
        return false;
    }
    QString draft = session.drafts.value(*questionId, session.answers.value(*questionId));
    const qsizetype position = draft.indexOf(normalized);
    if (position >= 0) {
        draft.remove(position, 1);
    } else {
        draft.append(normalized);
    }
    std::sort(draft.begin(), draft.end());
    if (draft.isEmpty()) {
        session.drafts.remove(*questionId);
    } else {
        session.drafts.insert(*questionId, draft);
    }
    session.answers.remove(*questionId);
    session.revealedAnswers.remove(*questionId);
    session.dirty = true;
    return true;
}

bool PracticeService::submitDraft(domain::PracticeSession &session) const
{
    const auto questionId = currentQuestionId(session);
    if (!questionId.has_value() || !session.drafts.contains(*questionId)) {
        return false;
    }
    session.answers.insert(*questionId, session.drafts.take(*questionId));
    session.dirty = true;
    return true;
}

bool PracticeService::toggleReveal(domain::PracticeSession &session) const
{
    const auto questionId = currentQuestionId(session);
    if (!questionId.has_value()) {
        return false;
    }
    if (session.revealedAnswers.contains(*questionId)) {
        session.revealedAnswers.remove(*questionId);
    } else {
        session.revealedAnswers.insert(*questionId);
    }
    session.dirty = true;
    return true;
}

bool PracticeService::move(domain::PracticeSession &session, qsizetype targetIndex) const
{
    if (targetIndex < 0 || targetIndex >= session.questionOrder.size()) {
        return false;
    }
    session.currentIndex = targetIndex;
    session.dirty = true;
    return true;
}

std::optional<QUuid> PracticeService::currentQuestionId(const domain::PracticeSession &session) const
{
    if (session.currentIndex < 0 || session.currentIndex >= session.questionOrder.size()) {
        return std::nullopt;
    }
    return session.questionOrder.at(session.currentIndex);
}

domain::NotebookLaunchContext PracticeService::notebookContext(const domain::PracticeSession &session) const
{
    domain::NotebookLaunchContext context;
    context.sessionId = session.id;
    context.questionId = currentQuestionId(session).value_or(QUuid());
    context.questionIndex = session.currentIndex;
    context.practiceMode = session.mode;
    context.practiceViewport = session.viewport;
    return context;
}

} // namespace quizapp::services
