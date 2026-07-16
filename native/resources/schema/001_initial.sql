PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS subjects (
    id TEXT PRIMARY KEY,
    parent_id TEXT REFERENCES subjects(id) ON DELETE CASCADE,
    title TEXT NOT NULL,
    icon TEXT NOT NULL DEFAULT '',
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS banks (
    id TEXT PRIMARY KEY,
    subject_id TEXT NOT NULL REFERENCES subjects(id) ON DELETE CASCADE,
    title TEXT NOT NULL,
    source_provider TEXT NOT NULL DEFAULT '',
    source_id TEXT NOT NULL DEFAULT '',
    schema_version INTEGER NOT NULL DEFAULT 2,
    content_hash BLOB NOT NULL,
    distribution_version TEXT NOT NULL DEFAULT '',
    installed_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    UNIQUE(source_provider, source_id)
);

CREATE TABLE IF NOT EXISTS questions (
    id TEXT PRIMARY KEY,
    bank_id TEXT NOT NULL REFERENCES banks(id) ON DELETE CASCADE,
    source_provider TEXT NOT NULL DEFAULT '',
    source_id TEXT NOT NULL DEFAULT '',
    type INTEGER NOT NULL CHECK(type BETWEEN 0 AND 3),
    prompt TEXT NOT NULL,
    correct_answer TEXT NOT NULL DEFAULT '',
    builtin_explanation TEXT NOT NULL DEFAULT '',
    content_hash BLOB NOT NULL,
    source_order INTEGER NOT NULL,
    updated_at TEXT NOT NULL,
    UNIQUE(bank_id, source_order)
);

CREATE TABLE IF NOT EXISTS question_options (
    question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,
    option_key TEXT NOT NULL,
    text TEXT NOT NULL,
    sort_order INTEGER NOT NULL,
    PRIMARY KEY(question_id, option_key)
);

CREATE TABLE IF NOT EXISTS blobs (
    id TEXT PRIMARY KEY,
    media_type TEXT NOT NULL,
    byte_size INTEGER NOT NULL CHECK(byte_size >= 0),
    relative_path TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL,
    last_verified_at TEXT
);

CREATE TABLE IF NOT EXISTS question_blobs (
    question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,
    blob_id TEXT NOT NULL REFERENCES blobs(id) ON DELETE RESTRICT,
    role TEXT NOT NULL CHECK(role IN ('question', 'explanation')),
    sort_order INTEGER NOT NULL,
    PRIMARY KEY(question_id, role, sort_order)
);

CREATE TABLE IF NOT EXISTS practice_sessions (
    id TEXT PRIMARY KEY,
    scope_id TEXT NOT NULL,
    mode INTEGER NOT NULL CHECK(mode BETWEEN 0 AND 5),
    current_index INTEGER NOT NULL DEFAULT 0,
    question_order_json TEXT NOT NULL,
    viewport_json TEXT NOT NULL DEFAULT '{}',
    is_complete INTEGER NOT NULL DEFAULT 0 CHECK(is_complete IN (0, 1)),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_practice_scope_mode_updated
ON practice_sessions(scope_id, mode, updated_at DESC);

CREATE TABLE IF NOT EXISTS practice_answers (
    session_id TEXT NOT NULL REFERENCES practice_sessions(id) ON DELETE CASCADE,
    question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,
    answer TEXT NOT NULL DEFAULT '',
    draft TEXT NOT NULL DEFAULT '',
    revealed INTEGER NOT NULL DEFAULT 0 CHECK(revealed IN (0, 1)),
    answered_at TEXT,
    PRIMARY KEY(session_id, question_id)
);

CREATE TABLE IF NOT EXISTS wrong_book (
    question_id TEXT PRIMARY KEY REFERENCES questions(id) ON DELETE CASCADE,
    reason_tags_json TEXT NOT NULL DEFAULT '[]',
    note TEXT NOT NULL DEFAULT '',
    added_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS review_cards (
    question_id TEXT PRIMARY KEY REFERENCES questions(id) ON DELETE CASCADE,
    fsrs_state_json TEXT NOT NULL,
    due_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_review_due ON review_cards(due_at);

CREATE TABLE IF NOT EXISTS exam_sessions (
    id TEXT PRIMARY KEY,
    scope_id TEXT NOT NULL,
    status INTEGER NOT NULL,
    duration_seconds INTEGER NOT NULL,
    elapsed_seconds INTEGER NOT NULL DEFAULT 0,
    question_order_json TEXT NOT NULL,
    score REAL,
    created_at TEXT NOT NULL,
    submitted_at TEXT
);

CREATE TABLE IF NOT EXISTS exam_answers (
    exam_id TEXT NOT NULL REFERENCES exam_sessions(id) ON DELETE CASCADE,
    question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,
    answer TEXT NOT NULL DEFAULT '',
    PRIMARY KEY(exam_id, question_id)
);

CREATE TABLE IF NOT EXISTS notebooks (
    id TEXT PRIMARY KEY,
    question_id TEXT REFERENCES questions(id) ON DELETE SET NULL,
    title TEXT NOT NULL,
    format_version INTEGER NOT NULL,
    relative_path TEXT NOT NULL UNIQUE,
    content_hash BLOB NOT NULL,
    completed INTEGER NOT NULL DEFAULT 0 CHECK(completed IN (0, 1)),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS notebook_links (
    notebook_id TEXT NOT NULL REFERENCES notebooks(id) ON DELETE CASCADE,
    target_type TEXT NOT NULL CHECK(target_type IN ('question', 'notebook', 'url')),
    target_id TEXT NOT NULL,
    label TEXT NOT NULL DEFAULT '',
    sort_order INTEGER NOT NULL,
    PRIMARY KEY(notebook_id, target_type, target_id)
);

CREATE TABLE IF NOT EXISTS study_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    activity TEXT NOT NULL,
    scope_id TEXT NOT NULL DEFAULT '',
    started_at TEXT NOT NULL,
    duration_seconds INTEGER NOT NULL CHECK(duration_seconds >= 0)
);

CREATE INDEX IF NOT EXISTS idx_study_events_started ON study_events(started_at);

CREATE TABLE IF NOT EXISTS ai_records (
    id TEXT PRIMARY KEY,
    record_type TEXT NOT NULL,
    source_id TEXT NOT NULL,
    model TEXT NOT NULL,
    content TEXT NOT NULL,
    source_hash BLOB NOT NULL,
    created_at TEXT NOT NULL,
    UNIQUE(record_type, source_id)
);

CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value_json TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE VIRTUAL TABLE IF NOT EXISTS question_search USING fts5(
    question_id UNINDEXED,
    prompt,
    options,
    builtin_explanation,
    path,
    tokenize='unicode61'
);

CREATE VIRTUAL TABLE IF NOT EXISTS notebook_search USING fts5(
    notebook_id UNINDEXED,
    title,
    body,
    tags,
    links,
    tokenize='unicode61'
);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES(1, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
