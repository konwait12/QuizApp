#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QUuid>
#include <QVector>

namespace quizapp::domain {

enum class PracticeMode {
    Sequential,
    Random,
    Memorize,
    AnswerLookup,
    WrongBook,
    Review,
};

struct PracticeSession {
    QUuid id;
    QString scopeId;
    PracticeMode mode = PracticeMode::Sequential;
    QVector<QUuid> questionOrder;
    qsizetype currentIndex = 0;
    QHash<QUuid, QString> answers;
    QHash<QUuid, QString> drafts;
    QSet<QUuid> revealedAnswers;
    QJsonObject viewport;
    bool dirty = false;
    bool complete = false;
};

struct NotebookLaunchContext {
    QUuid sessionId;
    QUuid questionId;
    qsizetype questionIndex = 0;
    PracticeMode practiceMode = PracticeMode::Sequential;
    QJsonObject practiceViewport;
};

} // namespace quizapp::domain

Q_DECLARE_METATYPE(quizapp::domain::NotebookLaunchContext)
