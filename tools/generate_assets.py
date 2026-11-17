#!/usr/bin/env python3
"""Generate SeeSay assets: the 256x256 camera+speaker icon and 3 store screenshots.

Icon design (candidate A, chosen by the user):
  - A camera on top (seeing) and a speaker with sound waves below (speaking),
    stacked top-to-bottom so nothing overflows the rounded tile.
  - Deep-blue tile, white camera, cyan lens and waves: high contrast, legible
    at small sizes, matching accessibility-app conventions.

Screenshots are 320x170 placeholders mirroring the project book UI sketch.
Replace with real on-device captures after Phase 1 hardware validation.

Only needs Pillow. Run from anywhere:  py -3 tools/generate_assets.py
"""
from __future__ import annotations

from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ICON_PATH = ROOT / "assets" / "images" / "seesay.png"
SHOT_DIR = ROOT / "screenshot"

S = 256
SS = 4
DIM = S * SS

WHITE = (245, 249, 255)
CYAN = (72, 207, 232)
CYAN_SOFT = (120, 224, 240)
GREEN = (78, 222, 150)
AMBER = (247, 197, 72)
DARK_TEXT = (232, 240, 252)


def sc(v):
    return int(round(v * SS))


def box(x0, y0, x1, y1):
    return [sc(x0), sc(y0), sc(x1), sc(y1)]


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def tile_image(top, bottom, radius=54):
    img = Image.new("RGB", (DIM, DIM))
    px = img.load()
    for y in range(DIM):
        t = y / (DIM - 1)
        c = lerp(top, bottom, t)
        for x in range(DIM):
            px[x, y] = c
    mask = Image.new("L", (DIM, DIM), 0)
    ImageDraw.Draw(mask).rounded_rectangle(box(14, 14, 242, 242), radius=sc(radius), fill=255)
    return img, mask


def camera(d, ink, accent, cx=120):
    d.rounded_rectangle(box(cx - 54, 62, cx + 54, 128), radius=sc(14), fill=ink)
    d.rounded_rectangle(box(cx - 30, 54, cx + 16, 66), radius=sc(5), fill=ink)
    d.ellipse(box(cx - 31, 78, cx + 23, 132), fill=accent)
    d.ellipse(box(cx - 20, 89, cx + 12, 121), fill=(16, 24, 44))
    d.ellipse(box(cx - 14, 92, cx - 4, 102), fill=(255, 255, 255))
    d.ellipse(box(cx + 36, 74, cx + 46, 84), fill=accent)


def speaker(d, ink, accent, cy=172, mouth=140, radii=(20, 34, 48)):
    d.rounded_rectangle(box(84, cy - 15, 104, cy + 15), radius=sc(4), fill=ink)
    d.polygon(
        [(sc(104), sc(cy - 15)), (sc(132), sc(cy - 30)),
         (sc(132), sc(cy + 30)), (sc(104), sc(cy + 15))],
        fill=ink,
    )
    for i, r in enumerate(radii):
        d.arc(box(mouth - r, cy - r, mouth + r, cy + r), start=-52, end=52,
              fill=accent, width=sc(6 - i))


def draw_icon() -> None:
    img, mask = tile_image((30, 44, 82), (14, 22, 46))
    d = ImageDraw.Draw(img)
    camera(d, WHITE, CYAN)
    speaker(d, WHITE, CYAN)
    rgba = Image.new("RGBA", (DIM, DIM), (0, 0, 0, 0))
    rgba.paste(img, (0, 0))
    rgba.putalpha(mask)
    ICON_PATH.parent.mkdir(parents=True, exist_ok=True)
    rgba.resize((S, S), Image.LANCZOS).save(ICON_PATH)
    print(f"icon: {ICON_PATH} ({S}x{S})")


def base_screen(title: str, status: str, status_color: tuple[int, int, int]):
    W, H = 320, 170
    img = Image.new("RGB", (W, H), (16, 20, 30))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W, 26], fill=(26, 32, 48))
    d.ellipse([8, 9, 18, 19], fill=status_color)
    d.text((24, 8), status, fill=DARK_TEXT)
    tw = d.textlength(title)
    d.text((W - tw - 8, 8), title, fill=CYAN_SOFT)
    d.line([0, 26, W, 26], fill=(54, 66, 92))
    d.rectangle([0, H - 30, W, H], fill=(26, 32, 48))
    d.line([0, H - 30, W, H - 30], fill=(54, 66, 92))
    d.text((8, H - 21), "SPACE shoot   OK replay   fn+H help", fill=(176, 190, 214))
    return img, d


def draw_screens() -> None:
    SHOT_DIR.mkdir(parents=True, exist_ok=True)

    img, d = base_screen("SeeSay", "READY", GREEN)
    d.text((14, 52), "I can see:", fill=DARK_TEXT)
    d.text((28, 74), "Press SPACE to", fill=(150, 165, 190))
    d.text((28, 92), "take a photo.", fill=(150, 165, 190))
    d.text((14, 122), "SeeSay ready.", fill=CYAN_SOFT)
    img.save(SHOT_DIR / "seesay-main.png")

    img, d = base_screen("SeeSay", "DONE", GREEN)
    d.text((14, 48), "I can see a desk, a keyboard,", fill=DARK_TEXT)
    d.text((14, 66), "and a laptop.", fill=DARK_TEXT)
    d.text((14, 92), "Text reads: Coffee twenty eight.", fill=CYAN_SOFT)
    d.text((14, 122), "Press OK to replay.", fill=(150, 165, 190))
    img.save(SHOT_DIR / "seesay-result.png")

    img, d = base_screen("SeeSay", "HELP", AMBER)
    d.text((14, 44), "SPACE   take photo & speak", fill=DARK_TEXT)
    d.text((14, 62), "OK / ESC  replay result", fill=DARK_TEXT)
    d.text((14, 80), "fn+H     toggle this help", fill=DARK_TEXT)
    d.text((14, 98), "fn+S/D/A volume", fill=DARK_TEXT)
    d.text((14, 116), "hold ESC go HOME (system)", fill=(150, 165, 190))
    img.save(SHOT_DIR / "seesay-keys.png")

    print(f"screenshots: {SHOT_DIR} (3 x 320x170)")


if __name__ == "__main__":
    draw_icon()
    draw_screens()
