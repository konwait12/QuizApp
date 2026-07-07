"""Import AutoQuiz JSON exports as the bundled MaoGai banks for QuizApp."""
from __future__ import annotations

import argparse
import json
import re
import shutil
from datetime import datetime
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = PROJECT_ROOT.parents[1]
DEFAULT_SOURCE_DIR = WORKSPACE_ROOT / "浏览器下载" / "新建文件夹"
DATA_DIR = PROJECT_ROOT / "data"
OUTPUT_DIR = PROJECT_ROOT / "output"

SUBJECT = "毛概"


def clean_question(text: object) -> str:
    value = str(text or "")
    value = value.replace("\u200b", "").replace("\ufeff", "").strip()
    value = re.sub(r"^\s*[（(]\s*(单选题|多选题|判断题)\s*[)）]\s*", "", value)
    value = re.sub(r"^\s*【\s*(单选题|多选题|判断题)\s*】\s*", "", value)
    value = re.sub(r"\s+", " ", value)
    return value.strip()


def clean_option(text: object) -> str:
    value = str(text or "").replace("\u200b", "").replace("\ufeff", "").strip()
    value = re.sub(r"^\s*[A-Ha-h](?:[.．、]|\s+)\s*", "", value)
    value = re.sub(r"\s+", " ", value)
    return value.strip()


def normalize_type(value: object) -> str:
    raw = str(value or "").strip().lower()
    if raw in {"multi", "multiple", "多选", "多选题"}:
        return "多选"
    if raw in {"bool", "tf", "truefalse", "判断", "判断题"}:
        return "判断"
    return "单选"


def normalize_answer(value: object, question_type: str, options: list[str]) -> str:
    raw = str(value or "").strip()
    raw = re.sub(r"^(正确答案|参考答案|答案)\s*[:：]?\s*", "", raw)
    upper = raw.upper()

    if question_type == "判断":
        if raw in {"对", "正确", "是", "√", "✓"} or upper in {"A", "TRUE", "T", "YES", "Y", "1"}:
            return "A"
        if raw in {"错", "错误", "否", "×", "✗"} or upper in {"B", "FALSE", "F", "NO", "N", "0"}:
            return "B"
        return ""

    letters = sorted({c for c in upper if "A" <= c <= "H" and ord(c) - 64 <= len(options)})
    return "".join(letters)


def normalize_chapter(path: Path, payload: dict) -> str:
    raw = str(payload.get("chapter") or payload.get("name") or path.stem)
    raw = re.sub(r"_?\d{8}_\d{6}$", "", raw).replace("_", " ").strip()
    match = re.search(r"(导论|第[一二三四五六七八九十0-9]+章(?:\s+[^_]+)?)", raw)
    return match.group(1).strip() if match else raw or "综合练习"


def normalize_questions(payload: dict) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for raw in payload.get("questions") or []:
        if not isinstance(raw, dict):
            continue

        question_type = normalize_type(raw.get("type") or raw.get("question_type"))
        options = [clean_option(item.get("text") if isinstance(item, dict) else item) for item in raw.get("options") or []]
        options = [item for item in options if item]
        if question_type == "判断":
            options = ["对", "错"]

        answer = normalize_answer(raw.get("ans") or raw.get("answer") or raw.get("correct") or raw.get("rawAns"), question_type, options)
        question = clean_question(raw.get("q") or raw.get("question") or raw.get("stem"))
        if not question or not answer:
            continue

        result.append({
            "id": f"q{len(result) + 1}",
            "type": question_type,
            "q": question,
            "options": options,
            "ans": answer,
            "exp": str(raw.get("exp") or raw.get("explanation") or "").strip(),
        })
    return result


def validate_questions(questions: list[dict[str, object]]) -> dict[str, int]:
    issues = {
        "emptyQuestion": 0,
        "emptyAnswer": 0,
        "tooFewOptions": 0,
        "answerOutOfRange": 0,
        "joinedOptions": 0,
    }
    for question in questions:
        qtype = str(question.get("type") or "")
        text = str(question.get("q") or "")
        answer = str(question.get("ans") or "").upper()
        options = question.get("options") if isinstance(question.get("options"), list) else []
        if not text:
            issues["emptyQuestion"] += 1
        if not answer:
            issues["emptyAnswer"] += 1
        if qtype in {"单选", "多选"} and len(options) < 2:
            issues["tooFewOptions"] += 1
        if qtype in {"单选", "多选"} and any(ord(c) - 64 > len(options) for c in answer if "A" <= c <= "H"):
            issues["answerOutOfRange"] += 1
        if any(re.search(r"(^|\n|\s)[A-H][.．、].+(\n|\s)[B-H][.．、]", str(option)) for option in options):
            issues["joinedOptions"] += 1
    return {key: value for key, value in issues.items() if value}


def backup_existing() -> Path | None:
    existing = sorted(DATA_DIR.glob(f"{SUBJECT}-*.json"))
    if not existing:
        return None
    backup_dir = OUTPUT_DIR / f"maogai-backup-{datetime.now().strftime('%Y%m%d-%H%M%S')}"
    backup_dir.mkdir(parents=True, exist_ok=True)
    for path in existing:
        shutil.copy2(path, backup_dir / path.name)
    return backup_dir


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", nargs="?", type=Path, default=DEFAULT_SOURCE_DIR)
    args = parser.parse_args()

    source_dir = args.source.resolve()
    if not source_dir.exists():
        raise FileNotFoundError(f"Source directory not found: {source_dir}")

    files = sorted(source_dir.glob("*.json"), key=lambda item: item.name)
    if not files:
        raise RuntimeError(f"No JSON exports found in {source_dir}")

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    backup_dir = backup_existing()

    for old_file in DATA_DIR.glob(f"{SUBJECT}-*.json"):
        old_file.unlink()

    total = 0
    for path in files:
        payload = json.loads(path.read_text(encoding="utf-8-sig"))
        if not isinstance(payload, dict):
            continue
        chapter = normalize_chapter(path, payload)
        questions = normalize_questions(payload)
        output = {
            "name": f"{SUBJECT}-{chapter}",
            "subject": SUBJECT,
            "chapter": chapter,
            "source": "AutoQuiz",
            "sourceFile": path.name,
            "questions": questions,
        }
        output_file = DATA_DIR / f"{SUBJECT}-{chapter}.json"
        output_file.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        issues = validate_questions(questions)
        print(json.dumps({"file": output_file.name, "count": len(questions), "issues": issues}, ensure_ascii=False))
        total += len(questions)

    print(json.dumps({"backup": str(backup_dir) if backup_dir else "", "importedFiles": len(files), "total": total}, ensure_ascii=False))


if __name__ == "__main__":
    main()
