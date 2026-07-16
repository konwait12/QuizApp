# QuizApp Native

This directory is the new Qt 6.9.3 / C++17 implementation. The current web
application remains the migration and compatibility client while native modules
are completed and verified.

## Boundaries

- `domain/`: immutable learning concepts and stable identifiers.
- `services/`: use cases such as practice, review, notebook launch and backup.
- `repositories/`: persistence contracts. UI code may not access SQLite directly.
- `storage/`: SQLite migrations and repository implementations.
- `handwriting/`: the adapter around pinned SpeedyNote source.
- `ui/`: adaptive Qt Widgets screens. It must not own learning data.

The first native milestone is Android tablet. Windows reuses the same domain,
storage and handwriting code. There is no account or cloud-sync dependency.

## Current native scope

- Install and browse public Xiaoyi postgraduate banks by subject, collection,
  chapter and section without flattening every JSON file into the home list.
- Start sequential, random, memorize, answer-table and wrong-book sessions from
  any directory level. Session progress remains isolated by scope and mode.
- Open a grouped question overview, jump to any question, and keep explicit
  correct, wrong, answered and unanswered states.
- Add a wrong answer to the wrong book only after explicit confirmation and
  remove it without coupling membership to practice resets.
- Browse large answer tables through a model-backed virtualized table, then
  open question detail or question-bound handwriting without losing position.
- Render question and built-in explanation images from the content-addressed
  Blob store.
- Add questions to review explicitly, work through a due queue, reveal answers,
  rate Again/Hard/Good/Easy, and persist FSRS state plus immutable review logs.
- Track foreground time for practice, review and handwriting, aggregate it by
  local calendar day, and display 7/30/90-day interactive study trends.
- Keep the existing system/light/dark appearance intact while offering an
  isolated Endfield-inspired advanced theme built only from original QuizApp
  widgets and design tokens.

## Build prerequisites

- Qt 6.9.3 with `Core`, `Gui`, `Sql` and `Test` for the current core target.
- CMake 3.24 or newer.
- Rust with the `x86_64-pc-windows-gnu` and `aarch64-linux-android` targets.
  The build pins the scheduler crate through
  `third_party/fsrs-bridge/Cargo.lock`; set `QUIZAPP_CARGO` when Cargo is not
  on `PATH`.
- Windows builds use MinGW 13.1 from an ASCII-only path. Set
  `QUIZAPP_MINGW_HOME` when it is not installed under the configured Qt root.
- Android builds use SDK platform 35, NDK 27.2 and arm64-v8a. Set
  `ANDROID_SDK_ROOT`; `ANDROID_NDK_ROOT` is optional when the SDK has one NDK.
- Set `QUIZAPP_QT_ROOT` only when Qt is not installed under
  `native/.toolchains/Qt`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-native.ps1 `
  -Target Windows -Configuration Debug

powershell -ExecutionPolicy Bypass -File scripts/build-native.ps1 `
  -Target Android -Configuration Debug
```

The script maps a non-ASCII project path to a temporary drive for MinGW/Ninja,
keeps Cargo outputs in an ASCII-only cache derived from the toolchain root,
validates the pinned SpeedyNote source contract during CMake configuration, and
runs Windows Qt Test unless `-SkipTests` is supplied. Android builds package an
arm64 debug APK at `output/native-build/QuizApp-native-debug-arm64-v8a.apk`.
