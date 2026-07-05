"""Import XiSi quiz exports from Workspace/browser downloads into data JSON files."""
from __future__ import annotations

import json
import re
from pathlib import Path

from docx import Document


PROJECT_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = PROJECT_ROOT.parents[1]
DOWNLOADS_DIR = WORKSPACE_ROOT / "浏览器下载"
DATA_DIR = PROJECT_ROOT / "data"
SUBJECT = "习思"

CHAPTERS = [
    ("导论", "导论"),
    ("第一章", "第一章 新时代坚持和发展中国特色社会主义"),
    ("第二章", "第二章 以中国式现代化全面推进中华民族伟大复兴"),
    ("第三章", "第三章 坚持党的全面领导"),
    ("第四章", "第四章 坚持以人民为中心"),
    ("第五章", "第五章 全面深化改革开放"),
    ("第六章", "第六章 推动高质量发展"),
    ("第七章", "第七章 社会主义现代化建设的教育、科技、人才战略"),
    ("第八章", "第八章 发展全过程人民民主"),
    ("第九章", "第九章 全面依法治国"),
    ("第十章", "第十章 建设社会主义文化强国"),
    ("第十一章", "第十一章 以保障和改善民生为重点加强社会建设"),
    ("第十二章", "第十二章 建设社会主义生态文明"),
    ("第十三章", "第十三章 维护和塑造国家安全"),
    ("第十四章", "第十四章 建设巩固国防和强大人民军队"),
    ("第十五章", "第十五章 坚持“一国两制”和推进祖国完全统一"),
    ("第十六章", "第十六章 中国特色大国外交和推动构建人类命运共同体"),
    ("第十七章", "第十七章 全面从严治党"),
]


def export_index(path: Path) -> int:
    if path.name == "quiz_export.docx":
        return 0
    match = re.search(r"\((\d+)\)", path.name)
    if not match:
        raise ValueError(f"Unexpected export filename: {path.name}")
    return int(match.group(1))


def clean_text(text: str) -> str:
    return (
        str(text or "")
        .replace("\u200b", "")
        .replace("\ufeff", "")
        .replace("\r", "\n")
        .strip()
    )


def clean_question(text: str) -> str:
    return re.sub(r"[─━]{5,}", "", clean_text(text)).strip()


def split_options(text: str) -> list[str]:
    text = clean_text(text)
    options: list[str] = []
    pattern = re.compile(
        r"(?:^|\n)\s*([A-H])(?:[.．、]|\s+)\s*([\s\S]*?)(?=\n\s*[A-H](?:[.．、]|\s+)\s*|$)"
    )
    for match in pattern.finditer(text):
        value = clean_question(match.group(2))
        if value:
            options.append(value)
    return options


def parse_type(title: str) -> str:
    if "多选" in title:
        return "多选"
    if "判断" in title:
        return "判断"
    return "单选"


def normalize_answer(answer: str, question_type: str) -> str:
    value = clean_text(answer)
    upper = value.upper()
    if question_type == "判断":
        if value in {"对", "正确", "A", "√", "✓"} or upper in {"TRUE", "T", "YES"}:
            return "A"
        if value in {"错", "错误", "B", "×", "✗"} or upper in {"FALSE", "F", "NO"}:
            return "B"
    letters = "".join(sorted(re.findall(r"[A-H]", upper)))
    if letters:
        return letters
    if value in {"对", "正确", "√", "✓"}:
        return "A"
    if value in {"错", "错误", "×", "✗"}:
        return "B"
    return value


def parse_docx(path: Path) -> list[dict[str, object]]:
    document = Document(str(path))
    questions: list[dict[str, object]] = []
    current: dict[str, object] | None = None

    def flush() -> None:
        nonlocal current
        if current and current.get("q") and current.get("ans"):
            questions.append(current)
        current = None

    for paragraph in document.paragraphs:
        text = clean_text(paragraph.text)
        if not text:
            continue

        style = paragraph.style.name if paragraph.style else ""
        if "Heading 3" in style or "标题 3" in style:
            flush()
            current = {"q": "", "options": [], "ans": "", "type": parse_type(text), "exp": ""}
            continue

        if current is None:
            continue

        if "正确答案" in text:
            raw_answer = re.split(r"[:：]", text, maxsplit=1)[-1].strip()
            current["rawAns"] = raw_answer
            current["ans"] = normalize_answer(raw_answer, str(current["type"]))
            continue

        if not current["q"]:
            if not ("共 " in text and "题" in text):
                current["q"] = clean_question(text)
            continue

        options = split_options(text)
        if options:
            current["options"] = options
        else:
            current["q"] = clean_question(f"{current['q']} {text}")

    flush()

    for idx, question in enumerate(questions, start=1):
        question["id"] = f"q{idx}"
        if question["type"] == "判断":
            question["options"] = ["对", "错"]

    return questions


def main() -> None:
    files = sorted(DOWNLOADS_DIR.glob("quiz_export*.docx"), key=export_index)
    if len(files) != len(CHAPTERS):
        raise RuntimeError(f"Expected {len(CHAPTERS)} XiSi docx files, found {len(files)} in {DOWNLOADS_DIR}")

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    for old_file in DATA_DIR.glob(f"{SUBJECT}-*.json"):
        old_file.unlink()

    total = 0
    for path, (short_title, chapter_title) in zip(files, CHAPTERS):
        questions = parse_docx(path)
        payload = {
            "name": f"{SUBJECT}-{chapter_title}",
            "subject": SUBJECT,
            "chapter": chapter_title,
            "sourceFile": path.name,
            "questions": questions,
        }
        output = DATA_DIR / f"{SUBJECT}-{short_title}.json"
        output.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
        total += len(questions)
        print(f"{output.name}: {len(questions)}")

    print(f"total: {total}")


if __name__ == "__main__":
    main()
