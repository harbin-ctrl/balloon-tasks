#!/usr/bin/env python3
"""Generate Balloon Tasks! desktop icons from the licensed Baloo 2 font."""

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "assets"
FONT_PATH = ASSET_DIR / "fonts" / "Baloo2-Variable.ttf"
ICON_SIZES = (16, 32, 48, 64, 128, 256, 512)
MASTER_SIZE = 2048

BALLOON_BASE = (23, 105, 210)
BALLOON_HIGHLIGHT = (90, 182, 255)
TEXT_FILL = (255, 255, 255, 255)
TEXT_OUTLINE = (8, 43, 102, 255)


def clamp(value):
    return max(0.0, min(1.0, value))


def draw_balloon():
    """Preserve the original balloon geometry and shading in blue."""
    size = MASTER_SIZE
    cx, cy = size * 0.5, size * 0.44
    rx, ry = size * 0.34, size * 0.39
    pixels = bytearray(size * size * 4)

    for y in range(size):
        dy = (y + 0.5 - cy) / ry
        row = y * size * 4

        for x in range(size):
            dx = (x + 0.5 - cx) / rx
            distance = math.sqrt(dx * dx + dy * dy)
            if distance > 1.02:
                continue

            edge = clamp((1.02 - distance) * 30.0)
            light_distance = math.sqrt((dx + 0.35) ** 2 + (dy + 0.43) ** 2)
            light = clamp(1.0 - light_distance * 2.6)
            offset = row + x * 4

            for channel in range(3):
                base = BALLOON_BASE[channel]
                highlight = BALLOON_HIGHLIGHT[channel]
                pixels[offset + channel] = round(base + (highlight - base) * light)

            pixels[offset + 3] = round(edge * 255)

    return Image.frombytes("RGBA", (size, size), bytes(pixels))


def extra_bold(size):
    font = ImageFont.truetype(FONT_PATH, size)
    font.set_variation_by_name("ExtraBold")
    return font


def draw_center_text(image, text, center_y, stroke_frac):
    """Draw straight text spanning the icon's full width across the balloon's middle."""
    probe_size = 1000
    left, _, right, _ = extra_bold(probe_size).getbbox(
        text, stroke_width=round(probe_size * stroke_frac), anchor="mm")
    font_size = int(probe_size * MASTER_SIZE * 0.98 / (right - left))
    stroke_width = round(font_size * stroke_frac)
    font = extra_bold(font_size)
    left, top, right, bottom = font.getbbox(text, stroke_width=stroke_width, anchor="mm")
    ImageDraw.Draw(image).text(
        (MASTER_SIZE * 0.5 - (left + right) / 2, center_y - (top + bottom) / 2),
        text,
        font=font,
        fill=TEXT_FILL,
        stroke_width=stroke_width,
        stroke_fill=TEXT_OUTLINE,
        anchor="mm",
    )


def render(text):
    image = draw_balloon()
    draw_center_text(image, text, MASTER_SIZE * 0.44, 0.05)
    return image


def main():
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    source = render("BT!")

    for size in ICON_SIZES:
        icon = source.resize((size, size), Image.Resampling.LANCZOS, reducing_gap=3.0)
        icon.save(ASSET_DIR / f"icon_{size}x{size}.png", optimize=True)


if __name__ == "__main__":
    main()
