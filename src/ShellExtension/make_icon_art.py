"""Draw the menu icons from the shared palette.

One filled disk per tag, plus a "-check" variant carrying the tick the context
menu shows on a tag that is already applied. The colours are read out of
src/Shared/ColorTagsConfig.h so this script cannot drift from what the overlay
and the settings window draw: that header stays the only place a colour lives.

Everything is drawn eight times larger and resampled down, because Pillow's
draw primitives are not antialiased either.

    python src/ShellExtension/make_icon_art.py

Then run make_icons.py to wrap the PNGs as ICO resources.
"""

from __future__ import annotations

import re
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 32
SCALE = 8
OUT_DIR = Path(__file__).resolve().parent / "icons"
HEADER = Path(__file__).resolve().parents[1] / "Shared" / "ColorTagsConfig.h"

TAG_PATTERN = re.compile(
    r'\{L"(?P<id>[a-z]+)",\s*RGB\(\s*(?P<r>\d+),\s*(?P<g>\d+),\s*(?P<b>\d+)\s*\)'
)


def read_palette(header: Path) -> list[tuple[str, tuple[int, int, int]]]:
    text = header.read_text(encoding="utf-8")
    tags = [
        (match.group("id"),
         (int(match.group("r")), int(match.group("g")), int(match.group("b"))))
        for match in TAG_PATTERN.finditer(text)
    ]
    if not tags:
        raise RuntimeError(f"no RGB(...) tag entries found in {header}")
    return tags


def relative_luminance(color: tuple[int, int, int]) -> float:
    """WCAG relative luminance — the same test the badge text uses."""

    def channel(value: int) -> float:
        srgb = value / 255
        return srgb / 12.92 if srgb <= 0.04045 else ((srgb + 0.055) / 1.055) ** 2.4

    red, green, blue = (channel(part) for part in color)
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue


def contrast_color(fill: tuple[int, int, int]) -> tuple[int, int, int]:
    """Whichever of dark or white has the better WCAG contrast ratio.

    A plain luminance threshold picks white on mid-tone fills like purple and
    gray, where dark is in fact the more legible of the two.
    """
    luminance = relative_luminance(fill)
    dark = (luminance + 0.05) / (relative_luminance((28, 28, 28)) + 0.05)
    light = 1.05 / (luminance + 0.05)
    return (28, 28, 28) if dark >= light else (255, 255, 255)


def darker(color: tuple[int, int, int], factor: float = 0.78) -> tuple[int, int, int]:
    return tuple(max(0, min(255, round(part * factor))) for part in color)


def draw_icon(fill: tuple[int, int, int], with_check: bool) -> Image.Image:
    canvas = SIZE * SCALE
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    inset = SCALE // 2
    draw.ellipse(
        (inset, inset, canvas - inset - 1, canvas - inset - 1),
        fill=fill + (255,),
        outline=darker(fill) + (255,),
        width=SCALE,
    )

    if with_check:
        mark = contrast_color(fill) + (255,)
        points = [(9.0, 16.4), (14.0, 21.4), (23.0, 11.0)]
        draw.line(
            [(x * SCALE, y * SCALE) for x, y in points],
            fill=mark,
            width=int(3.2 * SCALE),
            joint="curve",
        )
        # Round the two ends: PIL's joint option only rounds the corner.
        radius = 1.6 * SCALE
        for x, y in (points[0], points[-1]):
            draw.ellipse(
                (x * SCALE - radius, y * SCALE - radius,
                 x * SCALE + radius, y * SCALE + radius),
                fill=mark,
            )

    return image.resize((SIZE, SIZE), Image.LANCZOS)


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, color in read_palette(HEADER):
        for suffix, with_check in (("", False), ("-check", True)):
            target = OUT_DIR / f"{name}{suffix}.png"
            draw_icon(color, with_check).save(target)
            print(f"{target.name}  {color}")


if __name__ == "__main__":
    main()
