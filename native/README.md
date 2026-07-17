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

The immutable Web-to-native acceptance inventory is maintained in
[`docs/legacy-feature-migration-matrix.md`](docs/legacy-feature-migration-matrix.md).
`v1.0.18` features and themes are a minimum compatibility baseline, not an
optional backlog.

## Current native scope

- Install and browse the 27 Postgraduate Exam Bank Bundle by subject, collection,
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
- Keep system/light/dark appearance independent from all six `v1.0.18` color
  themes, expose a global 0-18 px component-radius token, and offer Endfield as
  an isolated seventh advanced theme built only from original QuizApp widgets
  and design tokens.

## Shared storage and bank hierarchy

On Android, the native client uses this visible shared-storage layout:

```text
/storage/emulated/0/QuizApp/
├── QuestionBanks/
├── Backups/
├── Exports/
├── Notes/
└── RecycleBin/
```

Android 11 and newer require the user to grant "all files access" before the
app can continuously monitor this root-level folder. The permission is suited
to sideloaded builds; Play Store publication would require a different storage
policy. Older Android versions use the bounded legacy storage permissions.

Desktop builds derive the same five-folder layout from the application data
root. No personal absolute path is compiled into the application.

Every JSON file under `QuestionBanks` is a managed bank source. Its relative
folder path plus filename determines the in-app hierarchy:

```text
QuestionBanks/毛概/第一章/单选题.json
              └科目 └章节 └题目分区
```

Additional folders create additional nested levels. A JSON file placed directly
under `QuestionBanks` becomes a first-level library item. The directory path is
authoritative even when the JSON contains older `subject`, `chapter` or `path`
fields.

The app scans on startup and when returning to the foreground. It stores file
size, modification time and SHA-256, imports only new or changed files, and
reactivates unchanged files that reappear. Removing a source file hides its bank
without immediately deleting practice, wrong-book or review data. The in-app
file tree shows the source folders and synchronization state.

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
  -Target Android -Configuration Debug `
  -DefaultBankBundleDir <validated-bundle-directory>
```

The script maps a non-ASCII project path to a temporary drive for MinGW/Ninja,
keeps Cargo outputs in an ASCII-only cache derived from the toolchain root,
validates the pinned SpeedyNote source contract during CMake configuration, and
runs Windows Qt Test unless `-SkipTests` is supplied. Android builds package an
arm64 debug APK at `output/native-build/QuizApp-native-debug-arm64-v8a.apk`.
`-DefaultBankBundleDir` is optional for developer builds. Release builds pass
the generated `27考研题库包` directory explicitly; its SQLite database and media
remain ignored build inputs rather than committed source files.
