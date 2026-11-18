#!/usr/bin/env python3
"""SeeSay voice-loop pipeline (the 'conductor').

Full flow on one SPACE press:
  speak "Taking photo"  ->  rpicam-jpeg  ->  speak "Analyzing"
  -> describe scene/objects (own process)  -> read text (own process)
  -> build one plain sentence  -> speak it  -> write result JSON for the UI.

Each heavy engine runs in a separate short-lived process so its memory is
freed before the next loads (device has ~132 MB free RAM). Nothing here talks
to the network.

Outputs (all under the app data dir: $XDG_DATA_HOME/seesay or
$HOME/.local/share/seesay, per the CardputerZero guideline):
  result.json      final structured result (read by the C++ app)
  status.txt       current stage (READY/SHOOTING/ANALYZING/...)
  last_speech.txt  last spoken sentence (for replay)
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

RUNTIME_DIR = Path(__file__).resolve().parent

# Stage-1: prefer the C++ ncnn OCR subprocess (10x faster) when present.
# Set SEESAY_OCR_CPP=0 to force the legacy Python RapidOCR fallback.
OCR_CPP_BIN = os.environ.get("SEESAY_OCR_CPP_BIN", "/usr/share/seesay/bin/seesay_ocr")
OCR_USE_CPP = os.environ.get("SEESAY_OCR_CPP", "1") != "0"

# Stage-2: prefer the C++ ncnn NanoDet subprocess (~40x faster) when present.
# Set SEESAY_DETECT_CPP=0 to force the legacy Python Cactus Mirage fallback.
DETECT_CPP_BIN = os.environ.get("SEESAY_DETECT_CPP_BIN", "/usr/share/seesay/bin/seesay_detect")
DETECT_USE_CPP = os.environ.get("SEESAY_DETECT_CPP", "1") != "0"

# All runtime files live under the app's own data directory, per the
# CardputerZero guideline ("configuration files: $HOME/.config/YourApp/").
# XDG_DATA_HOME/seesay, falling back to $HOME/.local/share/seesay.
def _data_dir() -> Path:
    if os.environ.get("SEESAY_DATA_DIR"):
        return Path(os.environ["SEESAY_DATA_DIR"])
    xdg = os.environ.get("XDG_DATA_HOME")
    home = os.environ.get("HOME") or str(Path.home())
    base = Path(xdg) if xdg else Path(home) / ".local" / "share"
    return base / "seesay"


DATA_DIR = _data_dir()
DATA_DIR.mkdir(parents=True, exist_ok=True)

PHOTO_PATH = os.environ.get("SEESAY_PHOTO", str(DATA_DIR / "capture.jpg"))
# Engines read the DOWNSCALED image, never the raw 1640x1232 capture. On this
# device a full-size photo made RapidOCR exhaust RAM and crash the machine.
SMALL_PATH = os.environ.get("SEESAY_SMALL_PHOTO", str(DATA_DIR / "capture_small.jpg"))
# On-screen thumbnail: PNG because the device LVGL has PNG decoding enabled
# (JPEG decoding is off). Kept small (~120px wide) so it never crowds the text.
THUMB_PATH = os.environ.get("SEESAY_THUMB", str(DATA_DIR / "thumb.png"))
THUMB_MAX_WIDTH = int(os.environ.get("SEESAY_THUMB_WIDTH", "120"))
# Stage-1: LVGL caches decoded images by source path; writing the same thumb.png
# repeatedly shows a STALE photo. Alternate between two filenames each capture
# and publish the active one via a tiny pointer file that the UI reads.
THUMB_A = str(DATA_DIR / "thumb_0.png")
THUMB_B = str(DATA_DIR / "thumb_1.png")
THUMB_SEQ = str(DATA_DIR / "thumb_seq.txt")
THUMB_ACTIVE = str(DATA_DIR / "thumb_active.txt")
SMALL_MAX_WIDTH = int(os.environ.get("SEESAY_SMALL_WIDTH", "800"))
RESULT_PATH = os.environ.get("SEESAY_RESULT", str(DATA_DIR / "result.json"))
STATUS_PATH = os.environ.get("SEESAY_STATUS", str(DATA_DIR / "status.txt"))
SPEECH_PATH = os.environ.get("SEESAY_LAST_SPEECH", str(DATA_DIR / "last_speech.txt"))

# Stage-3: fixed prompts use pre-recorded WAVs played asynchronously (0 delay,
# no espeak cold start, no blocking of the recognition flow). Dynamic result
# sentences still go through espeak-ng as a separate process (GPL isolation).
# Set SEESAY_AUDIO_PREREC=0 to force all speech through espeak (fallback).
AUDIO_DIR = os.environ.get("SEESAY_AUDIO_DIR", "/usr/share/seesay/audio")
PREREC_ENABLED = os.environ.get("SEESAY_AUDIO_PREREC", "1") != "0"
# Exact fixed phrases (project book 4.1) -> bundled WAV filename.
PREREC_MAP = {
    "SeeSay ready. Press the button to take a photo.": "ready.wav",
    "Taking photo.": "taking_photo.wav",
    "Analyzing the scene.": "analyzing.wav",
    "I am not sure. Please try again.": "not_sure.wav",
    "Camera failed. Please try again.": "camera_failed.wav",
    "Goodbye.": "goodbye.wav",
}
PY = sys.executable or "python3"


def set_status(value: str) -> None:
    try:
        Path(STATUS_PATH).write_text(value, encoding="utf-8")
    except OSError:
        pass


def speak(text: str, voice: str = "en") -> None:
    """Play one line. Fixed prompts use pre-recorded WAV (async, 0 delay);
    dynamic sentences use espeak-ng (blocking, so the announcement finishes).
    """
    try:
        Path(SPEECH_PATH).write_text(text, encoding="utf-8")
    except OSError:
        pass

    # Stage-3 fast path: fixed prompt with a bundled WAV -> play asynchronously.
    # Do NOT wait: recognition must continue while the prompt is being spoken.
    wav_name = PREREC_MAP.get(text)
    if PREREC_ENABLED and wav_name:
        wav_path = Path(AUDIO_DIR) / wav_name
        if wav_path.exists():
            try:
                subprocess.Popen(["aplay", "-q", str(wav_path)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return  # fire-and-forget: no espeak, no blocking
            except Exception as exc:  # noqa: BLE001 - fall through to espeak
                print(f"[pipeline] prerec play failed, using espeak: {exc}", file=sys.stderr)
        # WAV missing -> fall back to espeak so the user still hears the prompt.

    # Default path: espeak-ng synthesizes + plays (separate process, blocking).
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
                              timeout=45, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
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


def run_ocr(image: str) -> dict:
    """Recognize text, preferring the C++ ncnn subprocess over Python RapidOCR.

    Both engines print one JSON line with the same shape, so callers do not
    change. If the C++ binary is missing/fails, fall back to the Python script
    so a bad deploy can never leave the device without OCR.
    """
    if OCR_USE_CPP and Path(OCR_CPP_BIN).exists():
        try:
            proc = subprocess.run([OCR_CPP_BIN, image], timeout=45,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True)
            out = proc.stdout.strip()
            for line in reversed(out.splitlines()):
                line = line.strip()
                if line.startswith("{"):
                    try:
                        result = json.loads(line)
                        if result.get("ok"):
                            return result
                    except json.JSONDecodeError:
                        continue
            print(f"[pipeline] C++ OCR failed, falling back: "
                  f"{(proc.stderr or out).strip()[:160]}", file=sys.stderr)
        except Exception as exc:  # noqa: BLE001
            print(f"[pipeline] C++ OCR error, falling back: {exc}", file=sys.stderr)
    return run_script("seesay_readtext.py", image)


def run_detect(image: str) -> dict:
    """Detect objects, preferring the C++ ncnn NanoDet subprocess over Mirage.

    Output shape matches seesay_describe.py ({"ok":..., "objects":[...]}), so
    build_sentence / speech / UI need no changes. If the C++ binary is missing
    or fails, fall back to the Python Mirage script so a bad deploy can never
    leave the device without object detection.
    """
    if DETECT_USE_CPP and Path(DETECT_CPP_BIN).exists():
        try:
            proc = subprocess.run([DETECT_CPP_BIN, image], timeout=45,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True)
            out = proc.stdout.strip()
            for line in reversed(out.splitlines()):
                line = line.strip()
                if line.startswith("{"):
                    try:
                        result = json.loads(line)
                        if result.get("ok"):
                            return result
                    except json.JSONDecodeError:
                        continue
            print(f"[pipeline] C++ detect failed, falling back: "
                  f"{(proc.stderr or out).strip()[:160]}", file=sys.stderr)
        except Exception as exc:  # noqa: BLE001
            print(f"[pipeline] C++ detect error, falling back: {exc}", file=sys.stderr)
    return run_script("seesay_describe.py", image)


def capture() -> tuple[bool, str]:
    try:
        if Path(PHOTO_PATH).exists():
            os.remove(PHOTO_PATH)
    except OSError:
        pass
    # The CSI camera is mounted upside down on this device: raw frames come
    # out rotated 180 degrees. Flip both axes at capture time so the photo,
    # the on-screen thumbnail, AND the Mirage/OCR engines all see an upright
    # image (verified on-device: --hflip --vflip corrects the orientation).
    cmd = ["rpicam-jpeg", "-n", "-t", "100", "--width", "1640",
           "--height", "1232", "-q", "85", "--hflip", "--vflip",
           "-o", PHOTO_PATH]
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


def downscale() -> bool:
    """Shrink the raw capture so OCR cannot blow up RAM on this small device.

    Also writes a small PNG thumbnail for the on-screen image widget (project
    book 5.2). The two files are derived from one decode to stay cheap.
    """
    try:
        from PIL import Image
        with Image.open(PHOTO_PATH) as im:
            im = im.convert("RGB")
            if im.width > SMALL_MAX_WIDTH:
                new_h = round(im.height * SMALL_MAX_WIDTH / im.width)
                im = im.resize((SMALL_MAX_WIDTH, new_h), Image.LANCZOS)
            im.save(SMALL_PATH, "JPEG", quality=85)

            # Thumbnail: resize into a fixed-width frame, saved as PNG.
            if im.width > THUMB_MAX_WIDTH:
                th = round(im.height * THUMB_MAX_WIDTH / im.width)
                thumb = im.resize((THUMB_MAX_WIDTH, th), Image.LANCZOS)
            else:
                thumb = im.copy()
            # Stage-1: alternate filenames so LVGL's path-keyed image cache
            # cannot serve a stale decoded thumbnail on consecutive shots.
            try:
                seq = int(Path(THUMB_SEQ).read_text(encoding="utf-8").strip())
            except (OSError, ValueError):
                seq = 0
            seq += 1
            active = THUMB_A if seq % 2 == 1 else THUMB_B
            thumb.save(active, "PNG", optimize=True)
            # Publish the active basename (no path) for the UI to load.
            Path(THUMB_SEQ).write_text(str(seq), encoding="utf-8")
            Path(THUMB_ACTIVE).write_text(Path(active).name, encoding="utf-8")
        return Path(SMALL_PATH).exists()
    except Exception as exc:  # noqa: BLE001
        print(f"[pipeline] downscale failed: {exc}", file=sys.stderr)
        return False


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
    """Return (speech, display, unsure).

    Project book 4.2 rule 3: hedge low-confidence results. Objects at >=0.7
    are stated firmly ("I can see a desk"); objects between the 0.5 filter
    and 0.7 get a softer lead-in ("I think I see a cup") so we never mislead
    a blind user with false certainty.
    """
    items = objects[:5]  # cap to keep it short
    labels = [o["label"] for o in items]
    text_blob = " ".join(text_lines).strip()
    unsure = not labels and not text_blob

    if unsure:
        speech = "I am not sure. Please try again."
        display = "I am not sure.\nPlease try again."
        return speech, display, True

    all_confident = all(float(o.get("confidence", 1.0)) >= 0.7 for o in items)
    chinese_text = bool(text_blob) and has_cjk(text_blob)

    # When the visible text is Chinese, speak the WHOLE sentence in Chinese
    # (project book 4.1/4.2): mixing an English "I can see..." into a Chinese
    # voice sounds wrong. Object labels stay as-is (Mirage outputs English),
    # but the connecting words and punctuation are Chinese.
    parts: list[str] = []
    if labels:
        if chinese_text:
            lead = "我看到：" if all_confident else "我想我看到："
            parts.append(lead + "、".join(labels))
        else:
            lead = "I can see " if all_confident else "I think I see "
            parts.append(lead + join_objects(labels) + ".")
    if text_blob:
        if chinese_text:
            parts.append("识别到文字：" + text_blob + "。")
        else:
            parts.append("Text reads: " + text_blob + ".")

    speech = " ".join(parts)
    display = (("我看到：" if chinese_text else
                ("I can see " if all_confident else "I think I see ")) +
               ", ".join(labels) + "\n" if labels else "")
    if text_blob:
        display += ("文字：" if chinese_text else "Text: ") + text_blob
    return speech, display.strip(), False


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--replay":
        return replay_last()
    if len(sys.argv) > 1 and sys.argv[1] == "--say":
        # Speak one fixed line only (startup greeting / goodbye).
        text = sys.argv[2] if len(sys.argv) > 2 else ""
        if text:
            speak(text, "cmn" if has_cjk(text) else "en")
        return 0

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

    # Instant feedback: build the thumbnail FIRST and flip the UI to ANALYZING
    # so the just-taken photo lands on screen with minimal delay. Recognition
    # voice and the engines run after; the user already sees the photo was
    # captured. (project book 7.1 "即时反馈优先")
    downscaled_ok = downscale()
    analysis_image = SMALL_PATH if downscaled_ok else PHOTO_PATH
    set_status("ANALYZING")

    speak("Analyzing the scene.")

    described = run_detect(analysis_image)
    objects = described.get("objects", []) if described.get("ok") else []
    if not isinstance(objects, list):
        objects = []

    read = run_ocr(analysis_image)
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
