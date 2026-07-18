CREATE TABLE IF NOT EXISTS managed_bank_overrides (
    overriding_bank_id TEXT NOT NULL REFERENCES banks(id) ON DELETE CASCADE,
    overridden_bank_id TEXT NOT NULL UNIQUE REFERENCES banks(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL,
    PRIMARY KEY(overriding_bank_id, overridden_bank_id),
    CHECK(overriding_bank_id <> overridden_bank_id)
);

CREATE INDEX IF NOT EXISTS idx_managed_bank_overrides_owner
ON managed_bank_overrides(overriding_bank_id, created_at);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(9, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
