# Legacy Web to Native Migration Matrix

This document is the acceptance baseline for the Qt migration. The legacy Web
application remains a reference and migration source, not a second product
implementation.

## Status Rules

| Status | Meaning |
| --- | --- |
| `Not started` | No native domain model or complete user flow exists. |
| `Foundation` | Part of the model or UI exists, but the feature is not independently usable. |
| `Partial` | The main flow works, but one or more completion gates are missing. |
| `Complete` | Model, full flow, persistence, failure states, import/export, automated tests and user documentation are all present. |

A feature must not be marked `Complete` from a visible button, a screenshot, a
passing build, or compatibility code alone.

`v1.0.18` is the minimum compatibility baseline. Every feature and theme that
exists in that release must remain available after the native migration. A
native replacement may improve its implementation, but may not silently remove
the capability or its persisted user data.

## Themes and Shared UI

| Legacy capability | Legacy evidence | Native status | Missing before completion |
| --- | --- | --- | --- |
| System light/dark appearance | `uiConfig.theme`, `applyUiConfig()` | Partial | Legacy setting import now maps the saved mode without overwriting native edits; system change listener and per-page screenshot suite remain. |
| Forest green default theme | `uiConfig.palette = forest` | Partial | Native first-install default and legacy setting mapping exist; full page token/screenshot parity remains. |
| Palette presets | `PALETTE_PRESETS`, palette cards | Partial | Six `v1.0.18` presets have native tokens, preview, persistence, tests, legacy setting mapping and local backup coverage; remaining page/device screenshot parity remains. |
| Custom primary color | `uiConfig.primary` | Partial | Native color swatch, live preview, background/readability contrast validation, legacy setting migration, persistence, backup and UI screenshots exist; reset/device acceptance remains. |
| Global component corner radius | comfortable rounded `v1.0.18` surfaces | Partial | Native 0-18 px design token, live preview, persistence, backup and screenshots exist; every remaining module must adopt the token. |
| Reduce motion | `uiConfig.reduceMotion` | Partial | Apply to every transition and handwriting animation; accessibility test. |
| Subject/chapter icon choices | legacy icon settings and previews | Partial | Native Emoji choices now follow the detected database/shared-folder nesting depth, preserve the two legacy keys, persist every level, render by actual card depth and participate in local backup; Android device acceptance remains. |
| Optional Endfield advanced theme | no legacy equivalent; new native theme | Partial | Complete page coverage, phone/tablet screenshots, no proprietary assets, theme export metadata. Existing themes must remain unchanged. |
| Phone/tablet navigation | responsive Web layouts | Partial | Android rotation, split-screen and process-restoration device tests. |
| Android system back | WebView history handling | Partial | Deep route matrix, handwriting return and process-restoration device tests. |

## Question Banks and Library

| Legacy capability | Legacy evidence | Native status | Missing before completion |
| --- | --- | --- | --- |
| Bundled banks visible on first launch | `data/` and legacy bundled loaders | Partial | `27考研题库包` is packaged with count/provider/media verification, visible first-run progress, empty-database rollback protection and a full 870/72/1936 desktop bootstrap test; real-device first launch and update policy remain. |
| Arbitrary subject/chapter nesting | editable path model | Partial | Shared-folder hierarchy is authoritative; dynamic per-depth icons, tested rename/move operations and SHA-256 relocation reconciliation preserve stable source-defined question IDs across repeated path changes. Content-signature reconciliation for questions without source IDs remains. |
| Import JSON at every hierarchy level | edit mode and file input | Foundation | Android root folder, SAF fallback, validation report, duplicate/update behavior and UI. |
| In-app file manager | legacy storage/recycle surfaces | Partial | Native tree, status, refresh, permission, external-open, folder creation, JSON import, rename, move, recycle, restore and permanent delete exist; Android device acceptance remains. |
| Automatic directory scan | legacy load/import lifecycle | Partial | Startup/resume scan, SHA-256 metadata, changed/new/missing reconciliation and cancellation exist; Android lifecycle device tests and explicit retry/report UI remain. |
| Bank quality validation | import diagnostics and repair logic | Partial | Full-library report for type, answer, duplicate, media and quarantined records. |
| Bank release distribution | release bank manifest and chooser | Partial | Native latest-Release catalog, subject/chapter selection, size/SHA-256 validation, overwrite/keep-both conflicts, whole-batch rollback, per-bank installed fingerprints and silent 12-hour automatic checks are covered; real Android network/download/device acceptance remains. |
| Editable hierarchy | edit mode create/rename/order | Partial | Native shared-storage editor creates, renames, moves and recycles files/folders; the library has scoped edit mode deletion plus long-press drag ordering persisted by parent path. Shared cards move real files to the recycle bin, while bundled cards are only hidden locally and can be restored. Android pen/touch drag acceptance remains. |
| Recycle bin | `quizapp_recycle_bin` | Partial | Separate shared folder, direct-card recycle, bundled-bank local hiding, restore and permanent shared-file delete flows exist; richer metadata, backup behavior and Android device acceptance remain. |

## Practice and Learning

| Legacy capability | Legacy evidence | Native status | Missing before completion |
| --- | --- | --- | --- |
| Sequential practice | sequential session flow | Partial | Shared per-question answer state now synchronizes global and nested scopes, remains separate from random mode and resolves direct legacy question keys; all question types, device tests and export remain. |
| Random practice | shuffled order and separate progress | Partial | Shared per-question answer state is isolated from sequential mode and covered for large banks; configurable merge policy, reset/reshuffle settings, remaining legacy key resolution and device tests remain. |
| Memorize mode | answer-first practice | Partial | Settings, completion statistics, migration and documentation. |
| Answer table | grouped answer lookup | Partial | Last-position setting, search/filter, migration and export. |
| Explicit wrong-book membership | add/remove confirmation flow | Partial | Reason tags/notes UI, recycle semantics, export and migration. |
| Question overview | grouped type sections and status colors | Partial | Full type grouping, large-bank performance and tablet device tests. |
| Manual save, autosave and reset | save/reset/autosave settings | Partial | Manual save, exit-autosave setting, mode-aware persistence and reset cleanup have UI/core coverage; crash recovery, remaining legacy migration cases and exhaustive device-mode tests remain. |
| Saved-progress widget | configurable phone/tablet widget | Partial | Native home card restores the latest incomplete saved session, keeps the legacy show/help keys, supports 300-430 px phone and 440-720 px tablet sizing, and has repository/UI/screenshot/backup coverage; Android device acceptance remains. |
| Correct/incorrect answer feedback | option state and animation | Partial | Reduced-motion behavior, accessibility semantics and device screenshots. |
| Question-bound handwriting entry | practice handwriting route | Partial | Entry from every practice/exam/review surface and complete viewport restoration tests. |
| FSRS review | FSRS queue/history | Partial | Full rating history UI, configurable parameters, legacy migration and documentation. Local backup covers the repository. |
| Mock exam | setup, timer, pause, result/history | Partial | Native subject/all-bank setup, objective filtering, random sampling, pause/resume timer, 10-second checkpoint plus action saves, question overview, handwriting return, unified grading, last-30 history, deletion-independent text snapshots and local backup are covered by tests. Android lifecycle/timer device tests, result media rendering, legacy migration and accessibility remain. |

## Notes, Search and Content Tools

| Legacy capability | Legacy evidence | Native status | Missing before completion |
| --- | --- | --- | --- |
| Question-bound SpeedyNote document | handwriting practice workspace | Partial | Full tool set, Android pen/gesture device evidence, export and recovery. |
| Free notebooks | notebook launcher/library | Partial | Native home entry and notebook library now create/open/rename/recycle/restore/permanently delete independent SpeedyNote bundles. SQLite metadata, interrupted migration repair, content hashes, viewport persistence, backup inclusion, phone/tablet layouts and core/UI tests exist. Android pen/device acceptance, export/share, search/tags and process-kill recovery remain. |
| Multi-page notes | page strip, add/reorder/bookmark | Partial | Reorder, templates, orientation, thumbnails, bookmarks and tests. |
| Infinite canvas | free canvas mode | Not started | Viewport model, persistence, export and performance tests. |
| Pen/highlighter/eraser/lasso/shapes/text/image | legacy handwriting tools | Foundation | Remaining native tools, object actions, input mapping and tests. |
| PDF import/annotation/export | PDF notebook module | Foundation | Qt PDF backend, relink, search index invalidation, export and failure recovery. |
| OCR | OCR settings and local pipeline | Not started | Tesseract pipeline, model management, confidence UI and tests. |
| Full-text search | question/notebook search | Foundation | Native UI, index invalidation, highlighting, filters and performance gate. |
| Bookmarks and links | page bookmarks and notebook links | Not started | Native repositories, navigation, import/export and tests. |
| Knowledge graph | knowledge graph module | Not started | Native relationship model, graph UI, persistence and export. |

## AI, Statistics, Backup and Distribution

| Legacy capability | Legacy evidence | Native status | Missing before completion |
| --- | --- | --- | --- |
| BYOK AI provider configuration | DeepSeek/OpenAI-compatible presets | Partial | Native DeepSeek/custom OpenAI-compatible settings, DPAPI on Windows, Android Keystore AES-GCM, plaintext legacy-key migration, HTTPS/localhost validation, model discovery, real connection test, generation controls and backup exclusion/explicit inclusion are covered by core/UI tests. Real Android Keystore and provider network acceptance, cancellation UX and integration with native AI consumers remain. |
| AI question analysis | structured question prompt and records | Partial | Native question-bound panel now sends the type, prompt, all options, structured answer, built-in explanation and optional images through the configured OpenAI-compatible endpoint. SQLite caching remains separate from built-in content and supports source-hash staleness, cancellation, retry and Markdown/JSON export with core/UI coverage. Real provider/Android networking, streamed responses, accessibility and phone/tablet device acceptance remain. |
| AI learning assistant | local threads and attachments | Not started | Native threads, attachment boundary, persistence and deletion. |
| Study time tracking | foreground activity timer | Partial | SQLite records participate in local backup; all activity coverage and suspend/resume device tests remain. |
| 7/30/90-day charts | study chart ranges | Partial | Month archive controls, interaction/accessibility and device screenshots. |
| Wrong-reason analysis | tags, notes and optional AI summary | Foundation | Native UI and repository for tags/notes; AI remains physically separate. |
| Full local backup `.quizbackup v2` | legacy local backup | Partial | Native streaming container covers a consistent SQLite snapshot, QSettings, notes/blobs and shared QuestionBanks/Notes/RecycleBin; per-entry SHA-256, preview, API-key exclusion, restart staging and transactional rollback have core/UI coverage. Android SAF open/create compiles into ARM64 APK. Real-device SAF round trip, interrupted-process recovery, cross-version/cross-device restore and large device performance remain. |
| Legacy WebView data migration | WebView localStorage and `quizapp_study_data` IndexedDB | Partial | Hidden local-only exporter now loads at the legacy `file:///android_asset/index.html` origin with networking blocked; binary-safe IndexedDB capture, API-key redaction, versioned SQLite staging, direct question-key resolution, idempotence and rollback tests exist. Content-signature resolution, saved-session/notebook materialization and real stable-package overwrite testing remain. |
| Export | JSON/PDF/PNG and notebook export | Foundation | Unified export service, destination UI, errors and tests. |
| Announcements | local/remote announcement feed | Partial | Native Release-asset catalog parsing, HTML sanitization, cached archive, per-item read state, unread badge, manual refresh and silent automatic checks are implemented and covered by core/UI tests. Real GitHub networking and Android phone/tablet interaction remain. |
| Application updates | GitHub Releases latest flow | Partial | Native latest-Release parsing, semantic/prerelease and same-version build comparison, platform asset selection, size/SHA-256 verification, real download progress, Release fallback and Android FileProvider installer handoff are implemented and compile for Windows/Android ARM64. Real GitHub download, Android unknown-source permission return, installer launch and signed overwrite-install data retention still require device acceptance. |
| Settings organization | grouped legacy settings | Partial | Supported `v1.0.18` settings map into absent native keys without overwriting native edits; remaining controls, custom-primary UI, reset/export and tablet layout remain. |

## Migration Order and Invariants

1. Shared storage and bank visibility come before additional feature surfaces.
2. Old Web data is never deleted before a verified transactional import.
3. Stable question UUIDs must survive bank updates and path moves whenever the
   source question identity is unchanged.
4. Built-in answer/explanation, user notes and AI records remain physically and
   semantically separate.
5. Android preview builds use a side-by-side package until legacy migration is
   verified. Stable package replacement is not allowed before that gate.
6. Every visible change requires current phone portrait and tablet landscape
   evidence; build or HTTP success alone is not visual acceptance.

## Immutable `v1.0.18` Baseline

The source of truth is Git tag `v1.0.18` at commit
`222670eaad49eb40821dbd555d8b100164699228`. This inventory is additive: later
legacy releases and new native features may add rows, but none of these rows may
be removed.

### Appearance Baseline

- Appearance modes: follow system, light and dark.
- Color themes: `classic` (清爽蓝), `forest` (森林绿), `ink` (墨黑灰),
  `sunset` (日落橙), `berry` (莓果红) and `cyan` (湖青色).
- Forest green light mode is the first-install default.
- Subject and chapter icon choices, custom primary color, phone/tablet saved
  progress width and current theme settings must migrate.
- Endfield is a new optional seventh advanced theme. It may not replace or
  modify the six legacy color themes.

### Behavior and Customization Baseline

- Exit autosave; separate or merged sequential/random progress; random
  reshuffle on reset.
- Remember last position independently for memorize and answer-table modes.
- Automatic application update checks, announcement checks and persistent
  study statistics.
- Tool panels default collapsed/expanded; saved-progress entry and first-use
  hint visibility.
- Flat or compact chapter actions; configurable chapter action buttons and
  default expansion.
- Configurable order and visibility for home metrics and tool cards; subject
  and nested library ordering.
- Editable hierarchy, recycle bin restore/permanent delete and storage paths.
- Sequential, random, memorize, answer table, explicit wrong-book membership,
  study charts, announcements, application updates and bank distribution.
