"""TagService — the platform-independent tagging API.

Maps to the roadmap's required API:

    GetTag(path)              -> get_tag(path)
    SetTag(path, color)       -> set_tag(path, color)
    RemoveTag(path)           -> remove_tag(path)
    IsTagSupported(path)      -> is_tag_supported(path)
    GetAllSupportedTags()     -> get_all_supported_tags()

The service owns validation; stores own persistence.
"""

from __future__ import annotations

from pathlib import Path

from .itag_store import ITagStore
from .tag_color import TagColor
from .tag_validation import validate_path, validate_tag


class TagService:
    """High-level tag operations over an ``ITagStore``."""

    def __init__(self, store: ITagStore) -> None:
        if not isinstance(store, ITagStore):
            raise TypeError("store must implement ITagStore")
        self._store = store

    @property
    def store(self) -> ITagStore:
        """The underlying storage backend."""
        return self._store

    def get_tag(self, path: str | Path) -> TagColor:
        """Return the tag for *path*, or ``TagColor.NONE`` when untagged."""
        p = validate_path(path)
        return self._store.get_tag(p)

    def set_tag(self, path: str | Path, color: TagColor) -> None:
        """Assign *color* to *path* (replaces any existing tag)."""
        p = validate_path(path)
        validate_tag(color)
        self._store.set_tag(p, color)

    def remove_tag(self, path: str | Path) -> None:
        """Remove any tag from *path*."""
        p = validate_path(path)
        self._store.remove_tag(p)

    def is_tag_supported(self, path: str | Path) -> bool:
        """Whether the underlying store can tag *path*."""
        p = validate_path(path)
        return self._store.is_supported(p)

    @staticmethod
    def get_all_supported_tags() -> tuple[TagColor, ...]:
        """The seven taggable colors (``TagColor.NONE`` excluded)."""
        return TagColor.all_supported()