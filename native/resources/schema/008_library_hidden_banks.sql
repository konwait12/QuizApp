CREATE TABLE IF NOT EXISTS library_hidden_banks (
    bank_id TEXT PRIMARY KEY REFERENCES banks(id) ON DELETE CASCADE,
    path_json TEXT NOT NULL,
    hidden_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_library_hidden_banks_path
ON library_hidden_banks(path_json, hidden_at);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(8, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
