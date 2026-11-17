#!/usr/bin/env python3
"""Speak text aloud with espeak-ng, keeping it a SEPARATE process (GPL isolation).

Usage:  python3 seesay_speak.py "text to say" [en|cmn]
The app never links espeak-ng as a library; it only runs this command line.

Writes a wav file then plays it. Tries the available players in order so it
works whether ALSA or PipeWire owns the sound card.
"""
from __future__ import annotations

import os
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


def build_and_play(text: str, voice: str) -> bool:
    text = (text or "").strip()
    if not text:
        return False

    # 1) espeak-ng synthesizes the wav. -s sets speed (words/min); a bit slower
    #    is easier for blind users to follow.
    cmd = ["espeak-ng", "-v", voice, "-s", "150", "-w", WAV_PATH, text]
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
