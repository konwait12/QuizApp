CREATE TABLE IF NOT EXISTS legacy_migrations (
    id TEXT PRIMARY KEY,
    source_version TEXT NOT NULL,
    source_hash BLOB NOT NULL UNIQUE,
    exported_at TEXT NOT NULL,
    status TEXT NOT NULL CHECK(status IN ('staged', 'applied', 'failed')),
    sanitized_package_json TEXT NOT NULL,
    summary_json TEXT NOT NULL DEFAULT '{}',
    imported_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS legacy_records (
    migration_id TEXT NOT NULL REFERENCES legacy_migrations(id) ON DELETE CASCADE,
    category TEXT NOT NULL,
    record_key TEXT NOT NULL,
    payload_json TEXT NOT NULL,
    resolved_id TEXT,
    resolution_status TEXT NOT NULL DEFAULT 'pending'
        CHECK(resolution_status IN ('pending', 'resolved', 'ignored', 'invalid')),
    PRIMARY KEY(migration_id, category, record_key)
);

CREATE INDEX IF NOT EXISTS idx_legacy_records_resolution
ON legacy_records(category, resolution_status, record_key);

ALTER TABLE study_events
ADD COLUMN legacy_migration_id TEXT REFERENCES legacy_migrations(id) ON DELETE CASCADE;

CREATE INDEX IF NOT EXISTS idx_study_events_legacy_migration
ON study_events(legacy_migration_id);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(5, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
