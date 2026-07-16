ALTER TABLE review_cards
ADD COLUMN desired_retention REAL NOT NULL DEFAULT 0.9;

ALTER TABLE review_cards
ADD COLUMN stability REAL;

ALTER TABLE review_cards
ADD COLUMN difficulty REAL;

ALTER TABLE review_cards
ADD COLUMN last_review_at TEXT;

ALTER TABLE review_cards
ADD COLUMN scheduled_days INTEGER NOT NULL DEFAULT 0;

ALTER TABLE review_cards
ADD COLUMN review_count INTEGER NOT NULL DEFAULT 0;

ALTER TABLE review_cards
ADD COLUMN lapse_count INTEGER NOT NULL DEFAULT 0;

ALTER TABLE review_cards
ADD COLUMN scheduler_version TEXT NOT NULL DEFAULT 'fsrs-rs/6.6.0';

ALTER TABLE review_cards
ADD COLUMN created_at TEXT;

UPDATE review_cards
SET created_at = COALESCE(created_at, updated_at);

CREATE TABLE IF NOT EXISTS review_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,
    rating INTEGER NOT NULL CHECK(rating BETWEEN 1 AND 4),
    reviewed_at TEXT NOT NULL,
    due_before TEXT,
    due_after TEXT NOT NULL,
    elapsed_days INTEGER NOT NULL CHECK(elapsed_days >= 0),
    scheduled_days INTEGER NOT NULL CHECK(scheduled_days >= 1),
    had_memory_state INTEGER NOT NULL CHECK(had_memory_state IN (0, 1)),
    stability_before REAL,
    difficulty_before REAL,
    stability_after REAL NOT NULL,
    difficulty_after REAL NOT NULL,
    scheduler_version TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_review_logs_question_reviewed
ON review_logs(question_id, reviewed_at DESC);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(3, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));

