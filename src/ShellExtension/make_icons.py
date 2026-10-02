"""Convert every PNG in ``icons/`` to a same-name modern ICO file.

The PNG payload is embedded losslessly in a single-image ICO container. This
keeps the exact RGBA pixels supplied by the designer while producing resources
that llvm-rc and ``IExplorerCommand::GetIcon`` can load.

Run from anywhere:

    python src/ShellExtension/make_icons.py
"""

from __future__ import annotations

import struct
from pathlib import Path


OUT_DIR = Path(__file__).resolve().parent / "icons"


def _png_as_ico(png: bytes) -> bytes:
    """Wrap one PNG losslessly in a single-image ICO container."""
    if png[:8] != b"\x89PNG\r\n\x1a\n" or png[12:16] != b"IHDR":
        raise ValueError("not a PNG with an IHDR header")

    width, height = struct.unpack(">II", png[16:24])
    if not (1 <= width <= 256 and 1 <= height <= 256):
        raise ValueError(f"unsupported icon dimensions: {width}x{height}")

    encoded_width = 0 if width == 256 else width
    encoded_height = 0 if height == 256 else height
    header = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack(
        "<BBBBHHII",
        encoded_width,
        encoded_height,
        0,
        0,
        1,
        32,
        len(png),
        22,
    )
    return header + entry + png


def main() -> None:
    png_paths = sorted(OUT_DIR.glob("*.png"))
    if not png_paths:
        raise RuntimeError(f"no PNG icons found in {OUT_DIR}")

    for source in png_paths:
        target = source.with_suffix(".ico")
        target.write_bytes(_png_as_ico(source.read_bytes()))
        print(f"{source.name} -> {target.name}")

    print(f"Converted {len(png_paths)} icons in {OUT_DIR}")


if __name__ == "__main__":
    main()
