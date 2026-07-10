"""Build GitHub Release assets for per-bank downloads.

New release-bank rule:
- physical source files may live under data/<subject>/<chapter>.json;
- legacy flat data/*.json remains supported;
- release manifest contains metadata only;
- each bank is uploaded as a separate quizapp-bank-xxx.json asset.
"""
from __future__ import annotations

import json
import re
import shutil
from datetime import datetime
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = PROJECT_ROOT / "data"
OUT_DIR = PROJECT_ROOT / "output" / "release-banks"


def normalize_path(value: object) -> list[str]:
    if isinstance(value, list):
        return [str(item).strip() for item in value if str(item).strip()]
    return [part.strip() for part in str(value or "").split("/") if part.strip()]


def parse_bank_name(name: str) -> tuple[str, str]:
    clean = re.sub(r"\s*\(\d+题\)\s*", "", str(name or "")).strip()
    parts = [part.strip() for part in re.split(r"\s*[-－—]\s*", clean) if part.strip()]
    if len(parts) >= 2:
        return parts[0], "-".join(parts[1:])
    return clean or "未分类", "综合练习"


def safe_asset_part(value: object) -> str:
    text = str(value or "").strip()
    text = re.sub(r"[\\/:*?\"<>|\s]+", "-", text)
    text = re.sub(r"-+", "-", text).strip("-")
    return text or "bank"


def load_bank(path: Path) -> dict:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"{path} is not a question bank object")
    questions = payload.get("questions") if isinstance(payload.get("questions"), list) else []
    subject, chapter = parse_bank_name(payload.get("name") or path.stem)
    subject = payload.get("subject") or payload.get("科目") or subject
    chapter = payload.get("chapter") or payload.get("章节") or chapter
    logical_path = normalize_path(payload.get("path") or payload.get("路径") or [subject, chapter])
    if not logical_path:
        logical_path = [subject, chapter]
    payload["subject"] = logical_path[0] if logical_path else subject
    payload["chapter"] = logical_path[-1] if logical_path else chapter
    payload["path"] = logical_path
    payload["name"] = payload.get("name") or f"{payload['subject']}-{payload['chapter']}"
    return payload


def main() -> None:
    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    entries: list[dict] = []
    source_files = sorted(DATA_DIR.rglob("*.json"), key=lambda item: str(item.relative_to(DATA_DIR)).lower())
    for index, source in enumerate(source_files, start=1):
        bank = load_bank(source)
        asset_name = "quizapp-bank-{index:03d}-{subject}-{chapter}.json".format(
            index=index,
            subject=safe_asset_part(bank.get("subject")),
            chapter=safe_asset_part(bank.get("chapter")),
        )
        target = OUT_DIR / asset_name
        bank["releaseSource"] = asset_name
        target.write_text(json.dumps(bank, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        entries.append(
            {
                "file": asset_name,
                "name": bank.get("name"),
                "subject": bank.get("subject"),
                "chapter": bank.get("chapter"),
                "path": bank.get("path"),
                "questionCount": len(bank.get("questions") or []),
            }
        )

    manifest = {
        "schemaVersion": 2,
        "generatedAt": datetime.now().isoformat(timespec="seconds"),
        "banks": entries,
    }
    (OUT_DIR / "quizapp-bank-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({"outDir": str(OUT_DIR), "banks": len(entries)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
