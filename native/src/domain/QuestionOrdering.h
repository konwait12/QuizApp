#pragma once

#include "domain/Question.h"

#include <QString>

namespace quizapp::domain {

bool naturalLibraryTitleLess(const QString &left, const QString &right);
bool sourceQuestionLess(const Question &left, const Question &right);

} // namespace quizapp::domain
