#pragma once

#include <QtTypes>

namespace quizapp::domain {

struct LibraryStats {
    qsizetype bankCount = 0;
    qsizetype questionCount = 0;
    qsizetype blobCount = 0;
};

} // namespace quizapp::domain
