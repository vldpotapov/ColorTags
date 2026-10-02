"""Render the application icon from assets/app-icon.svg.

The SVG carries filter effects (inner shadows), which the usual SVG libraries
drop, so it is rasterised with a browser engine through Playwright. Every size
is rendered from the vector rather than downsampled from one large bitmap: at
16 and 20 pixels the difference is the whole legibility of the shape.

Produces, next to the sources that consume them:

    src/VisualNative/icons/app.ico        the window, the tray and the installer
    src/ShellExtension/assets/Square44x44Logo.png   the MSIX package logos
    src/ShellExtension/assets/Square150x150Logo.png

Run it only when the SVG changes; the results are committed, so a normal build
needs neither Python nor a browser:

    pip install playwright && playwright install chromium
    python tools/make_app_icon.py
"""

from __future__ import annotations

import struct
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
SVG = ROOT / "assets" / "app-icon.svg"
ICO_PATH = ROOT / "src" / "VisualNative" / "icons" / "app.ico"
ASSETS = ROOT / "src" / "ShellExtension" / "assets"

# The sizes Windows actually asks for: list view, details, the title bar, the
# tray, the Start menu, Alt+Tab and the 256 one the file dialog uses.
ICON_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
PACKAGE_SIZES = {44: "Square44x44Logo.png", 150: "Square150x150Logo.png"}

PAGE = """<!doctype html><html><head><meta charset="utf-8"><style>
html,body{margin:0;padding:0;background:transparent}
svg{display:block;width:%dpx;height:%dpx}
</style></head><body>%s</body></html>"""


def render(svg_text: str, sizes: list[int], into: Path) -> dict[int, bytes]:
    into.mkdir(parents=True, exist_ok=True)
    rendered: dict[int, bytes] = {}
    with sync_playwright() as play:
        browser = play.chromium.launch()
        for size in sizes:
            page = browser.new_page(viewport={"width": size, "height": size})
            page.set_content(PAGE % (size, size, svg_text))
            target = into / f"app-{size}.png"
            page.screenshot(path=str(target), omit_background=True)
            rendered[size] = target.read_bytes()
            page.close()
        browser.close()
    return rendered


def pack_ico(images: dict[int, bytes]) -> bytes:
    """One ICO holding each rendered PNG as its own entry.

    Windows Vista and later read PNG-compressed entries at any size, and the
    menu icons in this project already ship that way.
    """
    sizes = sorted(images)
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries, payload = b"", b""
    for size in sizes:
        data = images[size]
        encoded = 0 if size == 256 else size
        entries += struct.pack("<BBBBHHII", encoded, encoded, 0, 0, 1, 32,
                               len(data), offset)
        payload += data
        offset += len(data)
    return header + entries + payload


def main() -> None:
    svg_text = SVG.read_text(encoding="utf-8")
    scratch = ROOT / "temp" / "icon-render"

    icons = render(svg_text, ICON_SIZES, scratch)
    ICO_PATH.parent.mkdir(parents=True, exist_ok=True)
    ICO_PATH.write_bytes(pack_ico(icons))
    print(f"{ICO_PATH.relative_to(ROOT)}  ({len(ICON_SIZES)} sizes)")

    package = render(svg_text, list(PACKAGE_SIZES), scratch)
    ASSETS.mkdir(parents=True, exist_ok=True)
    for size, name in PACKAGE_SIZES.items():
        (ASSETS / name).write_bytes(package[size])
        print(f"{(ASSETS / name).relative_to(ROOT)}")


if __name__ == "__main__":
    main()
