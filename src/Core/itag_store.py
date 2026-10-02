"""ITagStore — storage abstraction for tag persistence.

The core never hardcodes a single storage method. Concrete stores:

- ``AdsTagStore`` (NTFS alternate data streams) — Phase 2
- ``SqliteTagStore`` (SQLite database) — Phase 2
- ``PropertySystemTagStore`` (Windows Property System) — Phase 2
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path

from .tag_color import TagColor


class ITagStore(ABC):
    """Persists tag assignments for filesystem items.

    Implementations must be safe to call from any thread and must not
    block for long (Explorer's UI thread constraint applies to shell
    integration, not to the core itself).
    """

    @abstractmethod
    def get_tag(self, path: Path) -> TagColor:
        """Return the tag for *path*, or ``TagColor.NONE`` when untagged."""

    @abstractmethod
    def set_tag(self, path: Path, tag: TagColor) -> None:
        """Assign *tag* to *path*. *tag* must not be ``TagColor.NONE``."""

    @abstractmethod
    def remove_tag(self, path: Path) -> None:
        """Remove any tag from *path*."""

    @abstractmethod
    def is_supported(self, path: Path) -> bool:
        """Whether this store can persist tags for *path*."""