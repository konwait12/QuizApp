ALTER TABLE notebooks ADD COLUMN deleted_at TEXT;

CREATE INDEX IF NOT EXISTS idx_notebooks_free_updated
ON notebooks(question_id, deleted_at, updated_at DESC);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(11, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
