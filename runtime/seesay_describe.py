#!/usr/bin/env python3
"""Describe a photo with Cactus Mirage: scene + objects, English.

Runs as its OWN process and exits, so Mirage's memory is released before the
OCR engine loads (the device has ~132 MB free RAM).

Offline is enforced: nothing may reach HuggingFace at runtime.
Weights/engine paths are supplied by the launcher via CACTUS_* env vars.
"""
from __future__ import annotations

import json
import os
import sys

# Hard offline guarantees (project book 8.6).
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"
os.environ["CACTUS_TELEMETRY"] = "0"

try:
    from cactus import Mirage
except Exception as exc:  # noqa: BLE001
    print(json.dumps({"ok": False, "error": f"cactus import failed: {exc}"}))
    raise SystemExit(1)

# Only announce objects we are reasonably sure about (project book 8.4).
try:
    MIN_CONFIDENCE = float(os.environ.get("SEESAY_MIN_CONFIDENCE", "0.5"))
except ValueError:
    MIN_CONFIDENCE = 0.5


def clean_label(value) -> str:
    if isinstance(value, str):
        return value.strip()
    if isinstance(value, dict):
        for key in ("label", "name", "text", "class"):
            if key in value:
                return str(value[key]).strip()
    return ""


def confidence_of(value) -> float:
    if isinstance(value, dict):
        for key in ("confidence", "score", "prob", "probability"):
            if key in value:
                try:
                    return float(value[key])
                except (TypeError, ValueError):
                    return 0.0
    return 1.0  # no score reported = treat as a plain label, decide in caller


def extract_objects(result) -> list[dict]:
    """Mirage returns different shapes across versions; handle the common ones."""
    raw = None
    if isinstance(result, dict):
        for key in ("objects", "object", "detections"):
            if key in result:
                raw = result[key]
                break
    elif hasattr(result, "objects"):
        raw = getattr(result, "objects")

    if raw is None:
        return []
    if isinstance(raw, dict):
        raw = list(raw.items())

    objects: list[dict] = []
    if isinstance(raw, (list, tuple)):
        for item in raw:
            if isinstance(item, dict):
                label = clean_label(item)
                conf = confidence_of(item)
            elif isinstance(item, (list, tuple)) and len(item) >= 2:
                label = clean_label(item[0])
                try:
                    conf = float(item[1])
                except (TypeError, ValueError):
                    conf = 1.0
            else:
                label = clean_label(item)
                conf = 1.0
            if label:
                objects.append({"label": label, "confidence": round(conf, 3)})
    return objects


def extract_scene(result) -> str:
    scene = None
    if isinstance(result, dict):
        scene = result.get("scene")
    elif hasattr(result, "scene"):
        scene = getattr(result, "scene")
    if not scene:
        return ""
    if isinstance(scene, str):
        return scene.strip()
    if isinstance(scene, dict):
        # e.g. {"setting": "desk", "lighting": "dim"} -> "desk"
        for key in ("setting", "place", "location", "scene", "description"):
            if scene.get(key):
                return str(scene[key]).strip()
    return ""


def main() -> int:
    if len(sys.argv) < 2:
        print(json.dumps({"ok": False, "error": "usage: describe_scene.py IMAGE"}))
        return 2
    image = sys.argv[1]
    if not os.path.exists(image):
        print(json.dumps({"ok": False, "error": "image not found"}))
        return 1

    try:
        model = Mirage()
        result = model.describe(image, sections=["scene", "objects"])
    except Exception as exc:  # noqa: BLE001
        print(json.dumps({"ok": False, "error": f"mirage failed: {exc}"}))
        return 1

    objects = extract_objects(result)
    kept = [o for o in objects if o["confidence"] >= MIN_CONFIDENCE]
    kept.sort(key=lambda o: o["confidence"], reverse=True)
    payload = {
        "ok": True,
        "scene": extract_scene(result),
        "objects": kept,
        "min_confidence": MIN_CONFIDENCE,
    }
    print(json.dumps(payload, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
