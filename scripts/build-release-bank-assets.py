"""Build GitHub Release assets for per-bank downloads.

New release-bank rule:
- physical source files may live under data/<subject>/<chapter>.json;
- legacy flat data/*.json remains supported;
- release manifest contains metadata only;
- each bank is uploaded as a separate quizapp-bank-xxx.json asset.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
from datetime import datetime
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = PROJECT_ROOT / "data"
OUT_DIR = PROJECT_ROOT / "output" / "release-banks"

SEGMENT_ORDER = {
    "考研数学": 0,
    "毛概": 10,
    "习思": 20,
    "基础篇": 0,
    "强化篇": 1,
    "数学一": 0,
    "数学二": 1,
    "数学三": 2,
    "高等数学": 0,
    "线性代数": 1,
    "概率论与数理统计": 2,
}


def chinese_ordinal_to_number(token: object) -> int | None:
    text = str(token or "").strip()
    if not text:
        return None
    if text.isdigit():
        return int(text)
    digits = {"零": 0, "〇": 0, "一": 1, "二": 2, "两": 2, "三": 3, "四": 4, "五": 5, "六": 6, "七": 7, "八": 8, "九": 9}
    if text in digits:
        return digits[text]
    if text == "十":
        return 10
    if text.startswith("十"):
        return 10 + digits.get(text[1:], 0)
    if text.endswith("十"):
        return digits.get(text[:-1], 0) * 10
    if "十" in text:
        left, right = text.split("十", 1)
        return digits.get(left, 1) * 10 + digits.get(right, 0)
    return None


def segment_sort_key(segment: object) -> tuple:
    text = str(segment or "").strip()
    if text in SEGMENT_ORDER:
        return (0, SEGMENT_ORDER[text], text)
    match = re.match(r"^第\s*([0-9零〇一二两三四五六七八九十百]+)\s*[章节讲]", text)
    if match:
        number = chinese_ordinal_to_number(match.group(1))
        return (1, number if number is not None else 5000, text)
    chunks = tuple((0, int(part)) if part.isdigit() else (1, part) for part in re.split(r"(\d+)", text) if part)
    return (9, chunks, text)


def bank_file_sort_key(path: Path) -> tuple:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
        logical_path = normalize_path(payload.get("path") or payload.get("路径") or [])
    except Exception:
        logical_path = []
    if not logical_path:
        relative = path.relative_to(DATA_DIR).with_suffix("")
        logical_path = [part for part in relative.parts if part]
    return tuple(segment_sort_key(part) for part in logical_path)


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


def source_bank_files() -> list[Path]:
    return sorted(DATA_DIR.rglob("*.json"), key=bank_file_sort_key)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build per-bank GitHub Release assets.")
    parser.add_argument("--dry-run", action="store_true", help="Validate sources without copying large bank files.")
    args = parser.parse_args()
    source_files = source_bank_files()
    if args.dry_run:
        print(json.dumps({
            "banks": len(source_files),
            "bytes": sum(source.stat().st_size for source in source_files),
            "restrictedBanksIncluded": False,
        }, ensure_ascii=False))
        return

    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    entries: list[dict] = []
    for index, source in enumerate(source_files, start=1):
        bank = load_bank(source)
        # Keep Release asset names ASCII-only. GitHub/CLI may normalize non-ASCII
        # filenames, while the manifest must match assets exactly.
        asset_name = f"quizapp-bank-{index:03d}.json"
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
