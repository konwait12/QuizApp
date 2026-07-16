#pragma once

#include "domain/PracticeSession.h"

#include <QString>

namespace quizapp::handwriting {

struct NotebookOpenResult {
    bool opened = false;
    QString documentId;
    QString error;
};

class ISpeedyNoteAdapter {
public:
    virtual ~ISpeedyNoteAdapter() = default;

    virtual NotebookOpenResult openQuestionNotebook(
        const domain::NotebookLaunchContext &context) = 0;
    virtual void saveActiveNotebook() = 0;
    virtual domain::NotebookLaunchContext closeAndRestorePractice() = 0;
};

} // namespace quizapp::handwriting
