ALTER TABLE questions
ADD COLUMN path_json TEXT NOT NULL DEFAULT '[]';

ALTER TABLE questions
ADD COLUMN active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0, 1));

CREATE INDEX IF NOT EXISTS idx_questions_path_active_order
ON questions(path_json, active, source_order);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(2, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));

