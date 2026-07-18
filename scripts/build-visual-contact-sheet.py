from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main() -> None:
    parser = argparse.ArgumentParser(description="Build contact sheets for QuizApp Playwright screenshots.")
    parser.add_argument("--since", default="1970-01-01T00:00:00")
    parser.add_argument("--root", default="output/playwright")
    args = parser.parse_args()

    root = Path(args.root)
    cutoff = datetime.fromisoformat(args.since).timestamp()
    files = sorted(
        path for path in root.glob("*.png")
        if path.stat().st_mtime >= cutoff and not path.name.startswith("contact-sheet-")
    )
    font = ImageFont.load_default()
    pixel_issues: list[tuple[str, str, int]] = []

    for path in files:
        with Image.open(path) as source:
            sample = source.convert("RGB").resize((64, 64))
            colors = sample.getcolors(4096) or []
            if len(colors) < 8:
                pixel_issues.append((path.name, "low-color", len(colors)))

    sheet_count = (len(files) + 15) // 16
    for sheet_index in range(sheet_count):
        chunk = files[sheet_index * 16:(sheet_index + 1) * 16]
        canvas = Image.new("RGB", (1280, 900), "#202124")
        draw = ImageDraw.Draw(canvas)
        for index, path in enumerate(chunk):
            with Image.open(path) as source:
                image = source.convert("RGB")
                image.thumbnail((300, 190))
            x = (index % 4) * 320 + 10
            y = (index // 4) * 225 + 22
            canvas.paste(image, (x + (300 - image.width) // 2, y))
            draw.text((x, y - 16), path.name[:48], fill="white", font=font)
        canvas.save(root / f"contact-sheet-{sheet_index + 1}.png")

    print({"files": len(files), "sheets": sheet_count, "pixelIssues": pixel_issues})


if __name__ == "__main__":
    main()
