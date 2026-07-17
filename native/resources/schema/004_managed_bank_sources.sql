ALTER TABLE banks
ADD COLUMN active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0, 1));

CREATE TABLE IF NOT EXISTS bank_sources (
    source_key TEXT PRIMARY KEY,
    managed_root TEXT NOT NULL,
    relative_path TEXT NOT NULL,
    bank_id TEXT REFERENCES banks(id) ON DELETE SET NULL,
    file_size INTEGER NOT NULL DEFAULT 0 CHECK(file_size >= 0),
    modified_msecs INTEGER NOT NULL DEFAULT 0,
    sha256 BLOB NOT NULL DEFAULT X'',
    available INTEGER NOT NULL DEFAULT 1 CHECK(available IN (0, 1)),
    last_error TEXT NOT NULL DEFAULT '',
    last_synced_at TEXT NOT NULL,
    UNIQUE(managed_root, relative_path)
);

CREATE INDEX IF NOT EXISTS idx_bank_sources_root_available
ON bank_sources(managed_root, available, relative_path);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(4, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
