#include "services/ExamService.h"

#include <QRandomGenerator>

#include <algorithm>

namespace quizapp::services {

bool ExamService::isObjective(const domain::Question &question)
{
    return question.type != domain::QuestionType::Subjective
        && !question.id.isNull()
        && !question.options.isEmpty()
        && !normalizeAnswer(question.correctAnswer).isEmpty();
}

QString ExamService::normalizeAnswer(QString answer)
{
    answer = answer.trimmed().toUpper();
    answer.remove(u' ');
    answer.remove(u',');
    answer.remove(u'，');
    QVector<QChar> letters;
    for (const QChar character : std::as_const(answer)) {
        if (character >= u'A' && character <= u'Z'
            && !letters.contains(character)) {
            letters.append(character);
        }
    }
    std::sort(letters.begin(), letters.end());
    QString normalized;
    normalized.reserve(letters.size());
    for (const QChar character : std::as_const(letters)) {
        normalized.append(character);
    }
    return normalized;
}

domain::ExamSession ExamService::start(
    const QString &scopeId,
    const QString &title,
    const QVector<domain::Question> &questions,
    qsizetype requestedCount,
    int durationSeconds,
    quint64 randomSeed) const
{
    QVector<QUuid> candidates;
    for (const domain::Question &question : questions) {
        if (isObjective(question) && !candidates.contains(question.id)) {
            candidates.append(question.id);
        }
    }
    QRandomGenerator generator(
        randomSeed == 0 ? QRandomGenerator::global()->generate64() : randomSeed);
    std::shuffle(candidates.begin(), candidates.end(), generator);
    if (requestedCount > 0 && candidates.size() > requestedCount) {
        candidates.resize(requestedCount);
    }

    domain::ExamSession session;
    session.id = QUuid::createUuid();
    session.scopeId = scopeId.trimmed().isEmpty() ? QStringLiteral("all") : scopeId;
    session.title = title.trimmed().isEmpty() ? QStringLiteral("综合模拟考试") : title;
    session.durationSeconds = std::max(60, durationSeconds);
    session.remainingSeconds = session.durationSeconds;
    session.questionOrder = candidates;
    session.createdAt = QDateTime::currentDateTimeUtc();
    return session;
}

bool ExamService::selectAnswer(
    domain::ExamSession &session,
    const domain::Question &question,
    QChar option) const
{
    if (session.status != domain::ExamStatus::Active || session.paused
        || !session.questionOrder.contains(question.id)) {
        return false;
    }
    option = option.toUpper();
    const int optionIndex = option.unicode() - u'A';
    if (optionIndex < 0 || optionIndex >= question.options.size()) {
        return false;
    }
    QString answer = normalizeAnswer(session.answers.value(question.id));
    if (question.type == domain::QuestionType::Multiple) {
        const qsizetype index = answer.indexOf(option);
        if (index >= 0) {
            answer.remove(index, 1);
        } else {
            answer.append(option);
        }
        answer = normalizeAnswer(answer);
    } else {
        answer = answer == QString(option) ? QString() : QString(option);
    }
    if (answer.isEmpty()) {
        session.answers.remove(question.id);
    } else {
        session.answers.insert(question.id, answer);
    }
    return true;
}

bool ExamService::move(domain::ExamSession &session, qsizetype targetIndex) const
{
    if (session.status != domain::ExamStatus::Active || session.paused
        || targetIndex < 0 || targetIndex >= session.questionOrder.size()) {
        return false;
    }
    session.currentIndex = targetIndex;
    return true;
}

bool ExamService::setPaused(domain::ExamSession &session, bool paused) const
{
    if (session.status != domain::ExamStatus::Active) {
        return false;
    }
    session.paused = paused;
    return true;
}

bool ExamService::advanceTimer(domain::ExamSession &session, int elapsedSeconds) const
{
    if (session.status != domain::ExamStatus::Active || session.paused
        || elapsedSeconds <= 0) {
        return false;
    }
    session.remainingSeconds = std::max(0, session.remainingSeconds - elapsedSeconds);
    return true;
}

bool ExamService::submit(
    domain::ExamSession &session,
    const QHash<QUuid, domain::Question> &questions,
    bool timedOut) const
{
    if (session.status != domain::ExamStatus::Active
        || session.questionOrder.isEmpty()) {
        return false;
    }
    QVector<domain::ExamResultItem> items;
    items.reserve(session.questionOrder.size());
    int correct = 0;
    int unanswered = 0;
    for (const QUuid &questionId : std::as_const(session.questionOrder)) {
        const auto found = questions.constFind(questionId);
        if (found == questions.cend() || !isObjective(*found)) {
            return false;
        }
        domain::ExamResultItem item;
        item.questionId = questionId;
        item.questionSnapshot = *found;
        item.answer = normalizeAnswer(session.answers.value(questionId));
        item.unanswered = item.answer.isEmpty();
        item.correct = !item.unanswered
            && item.answer == normalizeAnswer(found->correctAnswer);
        correct += item.correct ? 1 : 0;
        unanswered += item.unanswered ? 1 : 0;
        items.append(item);
    }
    session.resultItems = items;
    session.correctCount = correct;
    session.unansweredCount = unanswered;
    session.wrongCount = session.questionOrder.size() - correct - unanswered;
    session.score = qRound(100.0 * correct / session.questionOrder.size());
    session.timedOut = timedOut;
    session.paused = false;
    session.status = domain::ExamStatus::Submitted;
    session.submittedAt = QDateTime::currentDateTimeUtc();
    return true;
}

} // namespace quizapp::services
