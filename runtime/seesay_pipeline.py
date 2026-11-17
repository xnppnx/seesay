#!/usr/bin/env python3
"""SeeSay voice-loop pipeline (the 'conductor').

Full flow on one SPACE press:
  speak "Taking photo"  ->  rpicam-jpeg  ->  speak "Analyzing"
  -> describe scene/objects (own process)  -> read text (own process)
  -> build one plain sentence  -> speak it  -> write result JSON for the UI.

Each heavy engine runs in a separate short-lived process so its memory is
freed before the next loads (device has ~132 MB free RAM). Nothing here talks
to the network.

Outputs:
  /tmp/seesay_result.json   final structured result (read by the C++ app)
  /tmp/seesay_status.txt    current stage (READY/SHOOTING/ANALYZING/...)
  /tmp/seesay_last_speech.txt  last spoken sentence (for replay)
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

RUNTIME_DIR = Path(__file__).resolve().parent
PHOTO_PATH = os.environ.get("SEESAY_PHOTO", "/tmp/seesay_capture.jpg")
RESULT_PATH = os.environ.get("SEESAY_RESULT", "/tmp/seesay_result.json")
STATUS_PATH = os.environ.get("SEESAY_STATUS", "/tmp/seesay_status.txt")
SPEECH_PATH = os.environ.get("SEESAY_LAST_SPEECH", "/tmp/seesay_last_speech.txt")
PY = sys.executable or "python3"


def set_status(value: str) -> None:
    try:
        Path(STATUS_PATH).write_text(value, encoding="utf-8")
    except OSError:
        pass


def speak(text: str, voice: str = "en") -> None:
    """Synthesize AND play one line. Blocking on purpose: audio must finish."""
    try:
        Path(SPEECH_PATH).write_text(text, encoding="utf-8")
    except OSError:
        pass
    try:
        subprocess.run([PY, str(RUNTIME_DIR / "seesay_speak.py"), text, voice],
                       timeout=60, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    except Exception as exc:  # noqa: BLE001
        print(f"[pipeline] speak failed: {exc}", file=sys.stderr)


def replay_last() -> int:
    try:
        text = Path(SPEECH_PATH).read_text(encoding="utf-8").strip()
    except OSError:
        text = ""
    if not text:
        speak("No announcement to repeat.", "en")
        return 1
    voice = "cmn" if has_cjk(text) else "en"
    speak(text, voice)
    return 0


def write_result(payload: dict) -> None:
    try:
        Path(RESULT_PATH).write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
    except OSError as exc:
        print(f"[pipeline] result write failed: {exc}", file=sys.stderr)


def run_script(name: str, *args: str) -> dict:
    try:
        proc = subprocess.run([PY, str(RUNTIME_DIR / name), *args],
                              timeout=90, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True)
    except Exception as exc:  # noqa: BLE001
        return {"ok": False, "error": str(exc)}
    out = proc.stdout.strip()
    # The engine prints exactly one JSON line; ignore any stray log lines.
    for line in reversed(out.splitlines()):
        line = line.strip()
        if line.startswith("{"):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
    return {"ok": False, "error": proc.stderr.strip()[:200] or "no JSON from engine"}


def capture() -> tuple[bool, str]:
    try:
        if Path(PHOTO_PATH).exists():
            os.remove(PHOTO_PATH)
    except OSError:
        pass
    cmd = ["rpicam-jpeg", "-n", "-t", "1200", "--width", "1640",
           "--height", "1232", "-q", "85", "-o", PHOTO_PATH]
    try:
        proc = subprocess.run(cmd, timeout=30, stdout=subprocess.DEVNULL,
                              stderr=subprocess.PIPE, text=True)
    except FileNotFoundError:
        return False, "rpicam-jpeg not found"
    except Exception as exc:  # noqa: BLE001
        return False, str(exc)
    if proc.returncode != 0 or not Path(PHOTO_PATH).exists():
        return False, proc.stderr.strip()[:200] or "camera failed"
    return True, ""


def has_cjk(text: str) -> bool:
    return any("\u4e00" <= ch <= "\u9fff" for ch in text)


def article(word: str) -> str:
    return "an" if word[:1].lower() in "aeiou" else "a"


def join_objects(labels: list[str]) -> str:
    items = [f"{article(w)} {w}" for w in labels]
    if len(items) == 1:
        return items[0]
    if len(items) == 2:
        return f"{items[0]} and {items[1]}"
    return ", ".join(items[:-1]) + ", and " + items[-1]


def build_sentence(objects: list[dict], text_lines: list[str]) -> tuple[str, str, bool]:
    """Return (speech, display, unsure)."""
    labels = [o["label"] for o in objects[:5]]  # cap to keep it short
    text_blob = " ".join(text_lines).strip()
    unsure = not labels and not text_blob

    if unsure:
        speech = "I am not sure. Please try again."
        display = "I am not sure.\nPlease try again."
        return speech, display, True

    parts: list[str] = []
    if labels:
        parts.append("I can see " + join_objects(labels) + ".")
    if text_blob:
        parts.append("Text reads: " + text_blob + ".")

    speech = " ".join(parts)
    display = ("I can see " + ", ".join(labels) + "\n" if labels else "")
    display += ("Text: " + text_blob if text_blob else "")
    return speech, display.strip(), False


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--replay":
        return replay_last()

    set_status("SHOOTING")
    speak("Taking photo.")
    ok, detail = capture()
    if not ok:
        set_status("ERROR")
        speech = "Camera failed. Please try again."
        write_result({"ok": False, "error": "camera", "speech": speech,
                      "display": "Camera failed.", "detail": detail})
        speak(speech)
        return 1

    set_status("ANALYZING")
    speak("Analyzing the scene.")

    described = run_script("seesay_describe.py", PHOTO_PATH)
    objects = described.get("objects", []) if described.get("ok") else []
    if not isinstance(objects, list):
        objects = []

    read = run_script("seesay_readtext.py", PHOTO_PATH)
    lines = read.get("lines", []) if read.get("ok") else []
    if not isinstance(lines, list):
        lines = []

    speech, display, unsure = build_sentence(objects, [str(x) for x in lines])
    if unsure:
        set_status("UNSURE")
    else:
        set_status("REPORTING")

    write_result({"ok": True, "unsure": unsure, "speech": speech,
                  "display": display, "objects": objects, "text": lines})
    voice = "cmn" if has_cjk(speech) else "en"
    speak(speech, voice)

    if not unsure:
        set_status("DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
