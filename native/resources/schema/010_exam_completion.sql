ALTER TABLE exam_sessions ADD COLUMN title TEXT NOT NULL DEFAULT '';
ALTER TABLE exam_sessions ADD COLUMN current_index INTEGER NOT NULL DEFAULT 0;
ALTER TABLE exam_sessions ADD COLUMN remaining_seconds INTEGER NOT NULL DEFAULT 0;
ALTER TABLE exam_sessions ADD COLUMN paused INTEGER NOT NULL DEFAULT 0 CHECK(paused IN (0, 1));
ALTER TABLE exam_sessions ADD COLUMN correct_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE exam_sessions ADD COLUMN wrong_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE exam_sessions ADD COLUMN unanswered_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE exam_sessions ADD COLUMN timed_out INTEGER NOT NULL DEFAULT 0 CHECK(timed_out IN (0, 1));
ALTER TABLE exam_sessions ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';

CREATE TABLE IF NOT EXISTS exam_result_items (
    exam_id TEXT NOT NULL REFERENCES exam_sessions(id) ON DELETE CASCADE,
    question_id TEXT NOT NULL,
    sort_order INTEGER NOT NULL,
    answer TEXT NOT NULL DEFAULT '',
    correct INTEGER NOT NULL CHECK(correct IN (0, 1)),
    unanswered INTEGER NOT NULL CHECK(unanswered IN (0, 1)),
    question_json TEXT NOT NULL,
    PRIMARY KEY(exam_id, sort_order)
);

CREATE INDEX IF NOT EXISTS idx_exam_sessions_status_updated
ON exam_sessions(status, updated_at DESC);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(10, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
