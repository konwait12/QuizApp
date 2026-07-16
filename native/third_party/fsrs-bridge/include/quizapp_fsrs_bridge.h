#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QuizAppFsrsMemoryState {
    float stability;
    float difficulty;
} QuizAppFsrsMemoryState;

typedef struct QuizAppFsrsItemState {
    QuizAppFsrsMemoryState memory;
    float interval_days;
} QuizAppFsrsItemState;

typedef struct QuizAppFsrsNextStates {
    QuizAppFsrsItemState again;
    QuizAppFsrsItemState hard;
    QuizAppFsrsItemState good;
    QuizAppFsrsItemState easy;
} QuizAppFsrsNextStates;

enum QuizAppFsrsStatus {
    QUIZAPP_FSRS_OK = 0,
    QUIZAPP_FSRS_INVALID_ARGUMENT = 1,
    QUIZAPP_FSRS_SCHEDULER_ERROR = 2
};

int32_t quizapp_fsrs_next_states(
    bool has_memory_state,
    QuizAppFsrsMemoryState memory_state,
    float desired_retention,
    uint32_t elapsed_days,
    QuizAppFsrsNextStates *output);

const char *quizapp_fsrs_scheduler_version(void);

#ifdef __cplusplus
}
#endif

