use fsrs::{FSRS, ItemState, MemoryState, NextStates};
use std::ffi::c_char;

const OK: i32 = 0;
const INVALID_ARGUMENT: i32 = 1;
const SCHEDULER_ERROR: i32 = 2;
const VERSION: &[u8] = b"fsrs-rs/6.6.0\0";

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct QuizAppFsrsMemoryState {
    pub stability: f32,
    pub difficulty: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct QuizAppFsrsItemState {
    pub memory: QuizAppFsrsMemoryState,
    pub interval_days: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct QuizAppFsrsNextStates {
    pub again: QuizAppFsrsItemState,
    pub hard: QuizAppFsrsItemState,
    pub good: QuizAppFsrsItemState,
    pub easy: QuizAppFsrsItemState,
}

impl From<MemoryState> for QuizAppFsrsMemoryState {
    fn from(value: MemoryState) -> Self {
        Self {
            stability: value.stability,
            difficulty: value.difficulty,
        }
    }
}

impl From<QuizAppFsrsMemoryState> for MemoryState {
    fn from(value: QuizAppFsrsMemoryState) -> Self {
        Self {
            stability: value.stability,
            difficulty: value.difficulty,
        }
    }
}

impl From<ItemState> for QuizAppFsrsItemState {
    fn from(value: ItemState) -> Self {
        Self {
            memory: value.memory.into(),
            interval_days: value.interval,
        }
    }
}

impl From<NextStates> for QuizAppFsrsNextStates {
    fn from(value: NextStates) -> Self {
        Self {
            again: value.again.into(),
            hard: value.hard.into(),
            good: value.good.into(),
            easy: value.easy.into(),
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn quizapp_fsrs_next_states(
    has_memory_state: bool,
    memory_state: QuizAppFsrsMemoryState,
    desired_retention: f32,
    elapsed_days: u32,
    output: *mut QuizAppFsrsNextStates,
) -> i32 {
    if output.is_null()
        || !desired_retention.is_finite()
        || !(0.0..1.0).contains(&desired_retention)
        || (has_memory_state
            && (!memory_state.stability.is_finite()
                || memory_state.stability <= 0.0
                || !memory_state.difficulty.is_finite()
                || !(1.0..=10.0).contains(&memory_state.difficulty)))
    {
        return INVALID_ARGUMENT;
    }

    let previous = has_memory_state.then(|| memory_state.into());
    let Ok(states) = FSRS::default().next_states(previous, desired_retention, elapsed_days) else {
        return SCHEDULER_ERROR;
    };

    // SAFETY: output was checked for null and points to caller-owned writable storage.
    unsafe {
        output.write(states.into());
    }
    OK
}

#[unsafe(no_mangle)]
pub extern "C" fn quizapp_fsrs_scheduler_version() -> *const c_char {
    VERSION.as_ptr().cast()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn new_card_matches_official_scheduler() {
        let mut output = QuizAppFsrsNextStates::default();
        assert_eq!(
            quizapp_fsrs_next_states(
                false,
                QuizAppFsrsMemoryState::default(),
                0.9,
                0,
                &mut output,
            ),
            OK
        );
        assert!(output.good.interval_days.is_finite());
        assert!(output.good.interval_days >= output.hard.interval_days);
        assert!(output.easy.interval_days >= output.good.interval_days);
    }

    #[test]
    fn rejects_invalid_retention_without_writing() {
        let mut output = QuizAppFsrsNextStates::default();
        assert_eq!(
            quizapp_fsrs_next_states(
                false,
                QuizAppFsrsMemoryState::default(),
                1.0,
                0,
                &mut output,
            ),
            INVALID_ARGUMENT
        );
    }
}
