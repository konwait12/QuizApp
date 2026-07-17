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
| System light/dark appearance | `uiConfig.theme`, `applyUiConfig()` | Partial | System change listener, per-page screenshot suite, persisted migration from Web settings. |
| Forest green default theme | `uiConfig.palette = forest` | Partial | Exact token mapping and compatibility migration. |
| Palette presets | `PALETTE_PRESETS`, palette cards | Partial | Six `v1.0.18` presets now have native tokens, preview, persistence and tests; legacy setting migration and backup/export remain. |
| Custom primary color | `uiConfig.primary` | Not started | Color picker, contrast validation, persistence and backup. |
| Global component corner radius | comfortable rounded `v1.0.18` surfaces | Partial | Native 0-18 px design token, live preview, persistence and screenshots exist; backup/export and every remaining module must adopt the token. |
| Reduce motion | `uiConfig.reduceMotion` | Partial | Apply to every transition and handwriting animation; accessibility test. |
| Subject/chapter icon choices | legacy icon settings and previews | Not started | Native icon model, preview, hierarchy persistence and backup. |
| Optional Endfield advanced theme | no legacy equivalent; new native theme | Partial | Complete page coverage, phone/tablet screenshots, no proprietary assets, theme export metadata. Existing themes must remain unchanged. |
| Phone/tablet navigation | responsive Web layouts | Partial | Android rotation, split-screen and process-restoration device tests. |
| Android system back | WebView history handling | Partial | Deep route matrix, handwriting return and process-restoration device tests. |

## Question Banks and Library

| Legacy capability | Legacy evidence | Native status | Missing before completion |
| --- | --- | --- | --- |
| Bundled banks visible on first launch | `data/` and legacy bundled loaders | Not started | Package default banks, first-run install, count/hash verification and update policy. |
| Arbitrary subject/chapter nesting | editable path model | Partial | Shared-folder hierarchy is now authoritative for imported files and covered by tests; rename/move identity reconciliation remains. |
| Import JSON at every hierarchy level | edit mode and file input | Foundation | Android root folder, SAF fallback, validation report, duplicate/update behavior and UI. |
| In-app file manager | legacy storage/recycle surfaces | Partial | Native folder/file tree, source path, sync status, refresh, Android permission and external-open actions exist; create/move/delete/recycle actions remain. |
| Automatic directory scan | legacy load/import lifecycle | Partial | Startup/resume scan, SHA-256 metadata, changed/new/missing reconciliation and cancellation exist; Android lifecycle device tests and explicit retry/report UI remain. |
| Bank quality validation | import diagnostics and repair logic | Partial | Full-library report for type, answer, duplicate, media and quarantined records. |
| Bank release distribution | release bank manifest and chooser | Not started | Independent manifest, per-subject selection, download verification, conflict policy and rollback. |
| Editable hierarchy | edit mode create/rename/order | Not started | Native model, long-press ordering, recycle workflow, persistence and tests. |
| Recycle bin | `quizapp_recycle_bin` | Not started | Separate shared folder, restore/permanent delete, metadata and backup behavior. |

## Practice and Learning

| Legacy capability | Legacy evidence | Native status | Missing before completion |
| --- | --- | --- | --- |
| Sequential practice | sequential session flow | Partial | Legacy migration, all question types, device tests and export. |
| Random practice | shuffled order and separate progress | Partial | Configurable merge policy, reset/reshuffle settings, migration and tests. |
| Memorize mode | answer-first practice | Partial | Settings, completion statistics, migration and documentation. |
| Answer table | grouped answer lookup | Partial | Last-position setting, search/filter, migration and export. |
| Explicit wrong-book membership | add/remove confirmation flow | Partial | Reason tags/notes UI, recycle semantics, export and migration. |
| Question overview | grouped type sections and status colors | Partial | Full type grouping, large-bank performance and tablet device tests. |
| Manual save, autosave and reset | save/reset/autosave settings | Foundation | User-configurable semantics, crash recovery, migration and exhaustive mode tests. |
| Saved-progress widget | configurable phone/tablet widget | Not started | Native home component, sizing, hide/help state and migration. |
| Correct/incorrect answer feedback | option state and animation | Partial | Reduced-motion behavior, accessibility semantics and device screenshots. |
| Question-bound handwriting entry | practice handwriting route | Partial | Entry from every practice/exam/review surface and complete viewport restoration tests. |
| FSRS review | FSRS queue/history | Partial | Full rating history UI, configurable parameters, backup/migration and documentation. |
| Mock exam | setup, timer, pause, result/history | Not started | Complete native module and all completion gates. |

## Notes, Search and Content Tools

| Legacy capability | Legacy evidence | Native status | Missing before completion |
| --- | --- | --- | --- |
| Question-bound SpeedyNote document | handwriting practice workspace | Partial | Full tool set, Android pen/gesture device evidence, export and recovery. |
| Free notebooks | notebook launcher/library | Not started | Independent notebook model and complete lifecycle. |
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
| BYOK AI provider configuration | DeepSeek/OpenAI-compatible presets | Not started | Secure key storage, provider/model discovery, validation and backup exclusion. |
| AI question analysis | structured question prompt and records | Not started | Native record model, stale-source detection, cancellation/errors and export. |
| AI learning assistant | local threads and attachments | Not started | Native threads, attachment boundary, persistence and deletion. |
| Study time tracking | foreground activity timer | Partial | All activity coverage, suspend/resume tests and backup. |
| 7/30/90-day charts | study chart ranges | Partial | Month archive controls, interaction/accessibility and device screenshots. |
| Wrong-reason analysis | tags, notes and optional AI summary | Foundation | Native UI and repository for tags/notes; AI remains physically separate. |
| Full local backup `.quizbackup v2` | legacy local backup | Not started | Streaming archive, preview, hashes, rollback, cross-device restore and tests. |
| Export | JSON/PDF/PNG and notebook export | Foundation | Unified export service, destination UI, errors and tests. |
| Announcements | local/remote announcement feed | Not started | Independent manifest, unread state, automatic silent check and archive. |
| Application updates | GitHub Releases latest flow | Not started | Native Android download/install, progress, fallback and overwrite-data test. |
| Settings organization | grouped legacy settings | Foundation | All migrated settings, save feedback, reset/export and tablet layout. |

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
