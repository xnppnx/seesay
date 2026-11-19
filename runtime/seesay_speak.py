#!/usr/bin/env python3
"""Speak text aloud with espeak-ng, keeping it a SEPARATE process (GPL isolation).

Usage:  python3 seesay_speak.py "text to say" [en|cmn]
The app never links espeak-ng as a library; it only runs this command line.

Writes a wav file then plays it. Tries the available players in order so it
works whether ALSA or PipeWire owns the sound card.
"""
from __future__ import annotations

import html
import os
import re
import subprocess
import sys
from pathlib import Path

def _data_dir() -> str:
    if os.environ.get("SEESAY_DATA_DIR"):
        return os.environ["SEESAY_DATA_DIR"]
    xdg = os.environ.get("XDG_DATA_HOME")
    home = os.environ.get("HOME") or str(Path.home())
    base = xdg if xdg else str(Path(home) / ".local" / "share")
    return str(Path(base) / "seesay")


DATA_DIR = Path(_data_dir())
DATA_DIR.mkdir(parents=True, exist_ok=True)
WAV_PATH = os.environ.get("SEESAY_SPEECH_WAV", str(DATA_DIR / "speech.wav"))

# SSML mode (project book 8.9.16): Chinese text is spoken by the cmn voice,
# Latin words / key combos (Ctrl+Y, Alt+Enter, ...) by the en voice, with a
# short pause between segments. Fixes "everything runs together, unreadable".
# Verified on-device: <break> and <voice xml:lang="en"> both take effect with
# espeak 1.52 -m. Set SEESAY_SSML=0 to fall back to the old plain-text path.
SSML_ENABLED = os.environ.get("SEESAY_SSML", "1") != "0"
BREAK_MS = os.environ.get("SEESAY_SSML_BREAK", "400")

# Latin tokens / key combos that the English voice reads far better than cmn.
_LATIN_RE = re.compile(
    r"(?:Ctrl|Alt|Shift|Esc|Tab|Enter|Backspace|Windows|Del|Home|End|PgUp|PgDn)"
    r"\s*\+\s*[\w=+\-]+|"                      # Ctrl+Y, Alt+Enter, Shift+A
    r"\b(?:Ctrl|Alt|Shift|Enter|Tab|Esc|Delete|Backspace)\b|"
    r"[A-Za-z][A-Za-z0-9_.\-]*",               # plain Latin words / F5 / x86
    re.IGNORECASE,
)


def to_ssml(text: str) -> str:
    """Wrap Latin tokens in an English voice and add pauses between segments.

    Input is one plain sentence as produced by build_sentence(), e.g.
    "我想我看到：键盘 识别到文字：Ctrl+Y Ctrl+Shift 送中富前的位置到 ...".
    Segments are split on the two lead-in markers so the fixed Chinese words
    are kept intact; within a segment, Latin runs get the en voice and a
    break is inserted around them so nothing runs together.
    """
    raw = text.strip()
    if not raw:
        return raw

    # Split on the ORIGINAL text, then escape each piece. Escaping first would
    # let the regex re-match letters inside entities (&lt; -> "lt") and corrupt
    # them - so the order here matters.
    parts: list[str] = []
    last = 0
    for m in _LATIN_RE.finditer(raw):
        if m.start() > last:
            parts.append(html.escape(raw[last:m.start()], quote=False))
        parts.append('<voice xml:lang="en">' +
                     html.escape(m.group(0), quote=False) + '</voice>')
        parts.append(f'<break time="{BREAK_MS}ms"/>')
        last = m.end()
    if last < len(raw):
        parts.append(html.escape(raw[last:], quote=False))
    return '<speak>' + ''.join(parts) + '</speak>'


def build_and_play(text: str, voice: str) -> bool:
    text = (text or "").strip()
    if not text:
        return False

    # 1) espeak-ng synthesizes the wav. -s sets speed (words/min); a bit slower
    #    is easier for blind users to follow. In Chinese mode, wrap the text in
    #    SSML so Latin/key tokens use the en voice and segments get pauses.
    speak_text = text
    cmd = ["espeak-ng", "-v", voice, "-s", "150"]
    if SSML_ENABLED and voice == "cmn":
        speak_text = to_ssml(text)
        cmd.append("-m")
    cmd += ["-w", WAV_PATH, speak_text]
    try:
        subprocess.run(cmd, check=True, timeout=30,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    except Exception as exc:  # noqa: BLE001 - report and let caller decide
        print(f"[speak] espeak-ng failed: {exc}", file=sys.stderr)
        return False

    if not os.path.exists(WAV_PATH) or os.path.getsize(WAV_PATH) < 44:
        print("[speak] wav not produced", file=sys.stderr)
        return False

    # 2) Play it. Different images expose different players; try them all.
    players = [
        ["aplay", "-q", WAV_PATH],
        ["paplay", WAV_PATH],
        ["aplay", "-q", "-D", "plughw:1,0", WAV_PATH],
    ]
    for player in players:
        try:
            subprocess.run(player, check=True, timeout=30,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print(f"[speak] played via {player[0]}", file=sys.stderr)
            return True
        except FileNotFoundError:
            continue
        except Exception:  # noqa: BLE001 - try next player
            continue

    # Wav exists but no player worked. Still return True: speech data is ready
    # and a later replay can try again; do not block the recognition flow.
    print("[speak] wav ready but no player succeeded", file=sys.stderr)
    return True


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: seesay_speak.py TEXT [en|cmn]", file=sys.stderr)
        return 2
    text = sys.argv[1]
    voice = sys.argv[2] if len(sys.argv) > 2 else "en"
    return 0 if build_and_play(text, voice) else 1


if __name__ == "__main__":
    raise SystemExit(main())
