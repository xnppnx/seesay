#!/usr/bin/env python3
"""Read visible text from a photo with RapidOCR (Chinese + English).

Own process; exits to release memory before/after the vision engine.
"""
from __future__ import annotations

import json
import os
import sys

os.environ["OMP_NUM_THREADS"] = os.environ.get("OMP_NUM_THREADS", "2")

try:
    from rapidocr_onnxruntime import RapidOCR
except Exception as exc:  # noqa: BLE001
    print(json.dumps({"ok": False, "error": f"rapidocr import failed: {exc}"}))
    raise SystemExit(1)

try:
    MIN_CONFIDENCE = float(os.environ.get("SEESAY_OCR_MIN_CONFIDENCE", "0.5"))
except ValueError:
    MIN_CONFIDENCE = 0.5


def main() -> int:
    if len(sys.argv) < 2:
        print(json.dumps({"ok": False, "error": "usage: read_text.py IMAGE"}))
        return 2
    image = sys.argv[1]
    if not os.path.exists(image):
        print(json.dumps({"ok": False, "error": "image not found"}))
        return 1

    try:
        engine = RapidOCR()
        result, _elapse = engine(image)
    except Exception as exc:  # noqa: BLE001
        print(json.dumps({"ok": False, "error": f"rapidocr failed: {exc}"}))
        return 1

    lines: list[str] = []
    if result:
        for item in result:
            # item = [box, text, score]
            try:
                text = str(item[1]).strip()
                score = float(item[2])
            except (IndexError, TypeError, ValueError):
                continue
            if text and score >= MIN_CONFIDENCE:
                lines.append(text)

    print(json.dumps({"ok": True, "lines": lines}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
