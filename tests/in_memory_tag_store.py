"""In-memory ITagStore used by unit tests.

Not a production backend — it exists so the core can be tested without
touching the filesystem. Real stores arrive in Phase 2.
"""

from __future__ import annotations

from pathlib import Path

from src.Core.itag_store import ITagStore
from src.Core.tag_color import TagColor


class InMemoryTagStore(ITagStore):
    """Keeps tag assignments in a dict keyed by the string path."""

    def __init__(self) -> None:
        self._tags: dict[str, TagColor] = {}

    def get_tag(self, path: Path) -> TagColor:
        return self._tags.get(str(path), TagColor.NONE)

    def set_tag(self, path: Path, tag: TagColor) -> None:
        self._tags[str(path)] = tag

    def remove_tag(self, path: Path) -> None:
        self._tags.pop(str(path), None)

    def is_supported(self, path: Path) -> bool:
        return True