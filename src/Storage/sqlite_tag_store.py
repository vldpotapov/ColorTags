"""SqliteTagStore — SQLite fallback for volumes without ADS support.

Used for FAT32/exFAT volumes, network shares, and cloud-virtual drives
(Google Drive for Desktop reports FAT32). Identity strategy
(docs/STORAGE_RESEARCH.md §5):

- primary key: volume serial (``st_dev``) + file index (``st_ino``) —
  survives renames and moves on the same volume;
- a ``path`` column is kept for display and stale-row cleanup only;
- reads use file identity exclusively, so reusing a path never transfers a
  tag to a different file;
- stale rows are cleaned lazily when a path is re-tagged.

Database location: ``%LOCALAPPDATA%\\Colortags\\colortags.db`` (user scope,
no admin rights).

Thread safety: a fresh connection is opened per operation; no shared
mutable state. Safe to call from any thread.
"""

from __future__ import annotations

import os
import sqlite3
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

from ..Core.itag_store import ITagStore
from ..Core.tag_color import TagColor

_SCHEMA = """
CREATE TABLE IF NOT EXISTS tags (
    volume_serial TEXT    NOT NULL,
    file_index    INTEGER NOT NULL,
    path          TEXT    NOT NULL,
    tag           TEXT    NOT NULL,
    PRIMARY KEY (volume_serial, file_index)
);
CREATE INDEX IF NOT EXISTS idx_tags_path ON tags (path);
"""


def default_db_path() -> Path:
    """``%LOCALAPPDATA%\\Colortags\\colortags.db`` (created on demand)."""
    local = os.environ.get("LOCALAPPDATA")
    base = Path(local) if local else Path.home() / "AppData" / "Local"
    return base / "Colortags" / "colortags.db"


def _identity(path: Path) -> tuple[str, int]:
    """Volume serial + file index for *path* (Windows)."""
    st = path.stat()
    return str(st.st_dev), st.st_ino


class SqliteTagStore(ITagStore):
    """Persists tags in a local SQLite database keyed by file identity."""

    def __init__(self, db_path: Path | None = None) -> None:
        self._db_path = Path(db_path) if db_path is not None else default_db_path()
        self._db_path.parent.mkdir(parents=True, exist_ok=True)
        with self._connection() as conn:
            conn.executescript(_SCHEMA)

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self._db_path)
        conn.execute("PRAGMA journal_mode=WAL")
        return conn

    @contextmanager
    def _connection(self) -> Iterator[sqlite3.Connection]:
        """Open one transactional connection and always close it afterwards."""
        conn = self._connect()
        try:
            with conn:
                yield conn
        finally:
            conn.close()

    def get_tag(self, path: Path) -> TagColor:
        volume_serial, file_index = _identity(path)
        with self._connection() as conn:
            row = conn.execute(
                "SELECT tag FROM tags WHERE volume_serial = ? AND file_index = ?",
                (volume_serial, file_index),
            ).fetchone()
            if row is None:
                return TagColor.NONE
        try:
            return TagColor(row[0])
        except ValueError:
            return TagColor.NONE

    def set_tag(self, path: Path, tag: TagColor) -> None:
        if tag is TagColor.NONE:
            raise ValueError("TagColor.NONE cannot be assigned")
        volume_serial, file_index = _identity(path)
        with self._connection() as conn:
            # A path may now name a different file. Its old identity must not
            # transfer the tag to the replacement object.
            conn.execute(
                "DELETE FROM tags WHERE path = ? "
                "AND (volume_serial != ? OR file_index != ?)",
                (str(path), volume_serial, file_index),
            )
            conn.execute(
                "INSERT INTO tags (volume_serial, file_index, path, tag) "
                "VALUES (?, ?, ?, ?) "
                "ON CONFLICT(volume_serial, file_index) "
                "DO UPDATE SET path = excluded.path, tag = excluded.tag",
                (volume_serial, file_index, str(path), tag.id),
            )

    def remove_tag(self, path: Path) -> None:
        volume_serial, file_index = _identity(path)
        with self._connection() as conn:
            conn.execute(
                "DELETE FROM tags WHERE volume_serial = ? AND file_index = ?",
                (volume_serial, file_index),
            )

    def is_supported(self, path: Path) -> bool:
        # SQLite works on any writable volume; the router uses this store
        # only where the primary (ADS) is unsupported.
        return True
