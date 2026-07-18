CREATE TABLE IF NOT EXISTS library_node_order (
    parent_path_json TEXT NOT NULL,
    child_title TEXT NOT NULL,
    sort_order INTEGER NOT NULL CHECK(sort_order >= 0),
    updated_at TEXT NOT NULL,
    PRIMARY KEY(parent_path_json, child_title),
    UNIQUE(parent_path_json, sort_order)
);

CREATE INDEX IF NOT EXISTS idx_library_node_order_parent
ON library_node_order(parent_path_json, sort_order);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(7, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
