# Native data contracts

## Stable question identity

Questions with a trusted source use `provider + sourceId` as their UUIDv5
input. Other questions use normalized path, prompt and options. Content changes
produce a new content hash without changing a trusted source identity. This
allows release updates to migrate progress and notes without array indexes.

## Storage ownership

- SQLite owns structured learning state and searchable metadata.
- `blobs/<sha256>` owns immutable images, PDFs and attachments.
- `notebooks/*.snbx` owns SpeedyNote documents.
- API keys are stored through the platform secure-storage adapter and are not
  rows in the main database.

Repository implementations are the only code allowed to read or write these
locations. UI widgets consume services and models.

## Bank replacement

Bank import is one SQLite transaction. Existing active questions in the bank
are first moved to unique negative source-order slots and marked inactive. The
incoming stable IDs are then upserted and reactivated. Questions omitted by an
update remain in SQLite with `active = 0`, so practice answers, review history
and notebook foreign keys are not destroyed. Catalog and practice queries only
return active questions. A missing referenced blob or any other write failure
rolls back the complete bank replacement.

Schema migration 2 adds the canonical `path_json` and `active` columns. Native
migrations are idempotent and are applied in version order from embedded Qt
resources.

## Review and study history

Schema migration 3 preserves the original `review_cards.fsrs_state_json`
column, adds typed FSRS 6.6 state and scheduler metadata, and introduces the
append-only `review_logs` table. A rating updates the card and writes its log in
one transaction. Removing a review card explicitly removes its review logs;
resetting practice progress does not affect either table.

Foreground learning is stored as short `study_events` chunks with UTC start
times and durations. Aggregation splits chunks at local calendar-day boundaries
so week and month views remain correct across midnight and time-zone offsets.

## Backup v2

`.quizbackup` is a ZIP container. Every file is listed in `manifest.json` with
its SHA-256 and byte size. Restore validates the manifest and all entries before
opening a transaction. The current database and files remain untouched until
validation succeeds; failed replacement restores the pre-import snapshot.

The default backup includes imported banks, progress, wrong-book records,
review cards, exams, statistics, settings, AI history, notebooks, PDFs and
images. It excludes API keys, OCR language models, release caches and logs.
