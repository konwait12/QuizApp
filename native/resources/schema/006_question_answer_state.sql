CREATE TABLE IF NOT EXISTS question_answer_state (
    question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,
    mode INTEGER NOT NULL CHECK(mode IN (0, 1)),
    answer TEXT NOT NULL DEFAULT '',
    updated_at TEXT NOT NULL,
    legacy_migration_id TEXT REFERENCES legacy_migrations(id) ON DELETE SET NULL,
    PRIMARY KEY(question_id, mode)
);

CREATE INDEX IF NOT EXISTS idx_question_answer_state_mode_updated
ON question_answer_state(mode, updated_at DESC);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(6, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
