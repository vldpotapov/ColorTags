"""AdsTagStore — NTFS alternate data stream persistence.

Stores the tag id in the ``:ColorTag`` stream of the filesystem object
(file or folder). Value format: stable tag id (``red|orange|yellow|green|
blue|purple|gray``), UTF-8, no BOM, no trailing newline.

Verified behavior (docs/STORAGE_RESEARCH.md, 2026-09-22):

- works for files and folders, no admin rights;
- survives rename/move and NTFS-to-NTFS copy;
- read-only files: reads work, writes raise ``PermissionError``;
- long paths (>260 chars) work;
- empty, UTF-16, or unknown stream content is treated as untagged.

Thread safety: every operation is a single open/read/write/delete on the
stream; no shared mutable state. Safe to call from any thread.
"""

from __future__ import annotations

import ctypes
import os
from pathlib import Path

from ..Core.itag_store import ITagStore
from ..Core.tag_color import TagColor

_STREAM_NAME = "ColorTag"
_KNOWN_IDS = frozenset(c.id for c in TagColor if c is not TagColor.NONE)


def _volume_root(path: Path) -> str:
    """Return the volume root (``C:\\`` or ``\\\\server\\share\\``) for *path*."""
    drive, _ = os.path.splitdrive(str(path))
    if not drive:
        # Relative path: resolve against the current directory.
        drive, _ = os.path.splitdrive(str(path.resolve()))
    if drive.endswith("\\"):
        return drive
    return drive + "\\"


def _volume_file_system(path: Path) -> str | None:
    """File system name of the volume containing *path*, or ``None`` on error."""
    root = _volume_root(path)
    fs_buf = ctypes.create_unicode_buffer(256)
    ok = ctypes.windll.kernel32.GetVolumeInformationW(
        ctypes.c_wchar_p(root),
        ctypes.create_unicode_buffer(256),
        256,
        None,
        None,
        None,
        fs_buf,
        256,
    )
    if not ok:
        return None
    return fs_buf.value


class AdsTagStore(ITagStore):
    """Persists tags in NTFS alternate data streams (``:ColorTag``)."""

    def get_tag(self, path: Path) -> TagColor:
        """Return the tag for *path*, or ``TagColor.NONE`` when untagged.

        Corrupt or foreign stream content (empty, non-UTF-8, unknown id)
        is treated as untagged rather than raising.
        """
        stream = f"{path}:{_STREAM_NAME}"
        try:
            with open(stream, "r", encoding="utf-8") as f:
                raw = f.read()
        except FileNotFoundError:
            return TagColor.NONE
        except OSError:
            # Unreadable stream (locked, permission) — treat as untagged.
            return TagColor.NONE
        except UnicodeDecodeError:
            # Foreign encoding (e.g. UTF-16 written by another tool).
            return TagColor.NONE

        value = raw.strip()
        if value not in _KNOWN_IDS:
            return TagColor.NONE
        return TagColor(value)

    def set_tag(self, path: Path, tag: TagColor) -> None:
        """Assign *tag* to *path*. *tag* must not be ``TagColor.NONE``.

        Raises ``PermissionError`` for read-only or locked items.

        The file's modification time is preserved: writing an ADS updates
        the file's LastWriteTime, which Explorer shows as "Date modified".
        """
        if tag is TagColor.NONE:
            raise ValueError("TagColor.NONE cannot be assigned")
        stream = f"{path}:{_STREAM_NAME}"
        st = None
        try:
            st = os.stat(path)
        except OSError:
            pass
        with open(stream, "w", encoding="utf-8", newline="") as f:
            f.write(tag.id)
        if st is not None:
            try:
                os.utime(path, (st.st_atime, st.st_mtime))
            except OSError:
                pass

    def remove_tag(self, path: Path) -> None:
        """Remove any tag from *path*. Missing stream is a no-op.

        The modification time is preserved for the same reason as in
        ``set_tag``: deleting a stream also counts as writing the item.
        """
        stream = f"{path}:{_STREAM_NAME}"
        try:
            st = os.stat(path)
        except OSError:
            st = None
        try:
            os.remove(stream)
        except FileNotFoundError:
            return
        if st is not None:
            try:
                os.utime(path, (st.st_atime, st.st_mtime))
            except OSError:
                pass

    def is_supported(self, path: Path) -> bool:
        """Whether the volume containing *path* supports ADS (NTFS)."""
        fs = _volume_file_system(path)
        return fs is not None and fs.upper() == "NTFS"