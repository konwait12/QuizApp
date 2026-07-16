import json
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "resources" / "schema" / "001_initial.sql"
BACKUP_SCHEMA_PATH = ROOT / "resources" / "contracts" / "quizbackup-v2.schema.json"

REQUIRED_TABLES = {
    "ai_records",
    "banks",
    "blobs",
    "exam_answers",
    "exam_sessions",
    "notebook_links",
    "notebooks",
    "practice_answers",
    "practice_sessions",
    "question_blobs",
    "question_options",
    "questions",
    "review_cards",
    "schema_migrations",
    "settings",
    "study_events",
    "subjects",
    "wrong_book",
}


def verify_sql_schema() -> None:
    sql = SCHEMA_PATH.read_text(encoding="utf-8")
    with sqlite3.connect(":memory:") as database:
        database.executescript(sql)
        tables = {
            row[0]
            for row in database.execute(
                "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
            )
        }
        missing = REQUIRED_TABLES - tables
        if missing:
            raise AssertionError(f"missing tables: {sorted(missing)}")
        version = database.execute(
            "SELECT MAX(version) FROM schema_migrations"
        ).fetchone()[0]
        if version != 1:
            raise AssertionError(f"unexpected schema version: {version}")

        migration = (ROOT / "resources" / "schema" / "002_question_paths.sql").read_text(
            encoding="utf-8"
        )
        database.executescript(migration)
        columns = {
            row[1] for row in database.execute("PRAGMA table_info(questions)")
        }
        if not {"path_json", "active"}.issubset(columns):
            raise AssertionError("question path migration is incomplete")
        version = database.execute(
            "SELECT MAX(version) FROM schema_migrations"
        ).fetchone()[0]
        if version != 2:
            raise AssertionError(f"unexpected migrated schema version: {version}")

        review_migration = (
            ROOT / "resources" / "schema" / "003_review_history.sql"
        ).read_text(encoding="utf-8")
        database.executescript(review_migration)
        review_columns = {
            row[1] for row in database.execute("PRAGMA table_info(review_cards)")
        }
        expected_review_columns = {
            "desired_retention",
            "stability",
            "difficulty",
            "last_review_at",
            "scheduled_days",
            "review_count",
            "lapse_count",
            "scheduler_version",
            "created_at",
        }
        if not expected_review_columns.issubset(review_columns):
            raise AssertionError("review-card migration is incomplete")
        tables = {
            row[0]
            for row in database.execute(
                "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
            )
        }
        if "review_logs" not in tables:
            raise AssertionError("review history table is missing")
        version = database.execute(
            "SELECT MAX(version) FROM schema_migrations"
        ).fetchone()[0]
        if version != 3:
            raise AssertionError(f"unexpected migrated schema version: {version}")


def verify_backup_schema() -> None:
    schema = json.loads(BACKUP_SCHEMA_PATH.read_text(encoding="utf-8"))
    if schema.get("properties", {}).get("schemaVersion", {}).get("const") != 2:
        raise AssertionError("backup schema version must be 2")
    required = set(schema.get("required", []))
    expected = {"format", "schemaVersion", "appVersion", "createdAt", "database", "entries", "totals"}
    if required != expected:
        raise AssertionError(f"unexpected manifest fields: {sorted(required)}")
    if schema.get("properties", {}).get("includesSecrets", {}).get("const") is not False:
        raise AssertionError("v2 default manifest must exclude secrets")


if __name__ == "__main__":
    verify_sql_schema()
    verify_backup_schema()
    print(json.dumps({"sqliteSchema": True, "backupManifest": True}, indent=2))
