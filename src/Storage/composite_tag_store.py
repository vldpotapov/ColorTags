"""CompositeTagStore — routes each path to the best available backend.

Primary: ``AdsTagStore`` (NTFS). Fallback: ``SqliteTagStore`` (everything
else — FAT32/exFAT, network shares, cloud-virtual drives). Routing is per
path via ``is_supported``, so one service instance covers all volumes.
"""

from __future__ import annotations

from pathlib import Path

from ..Core.itag_store import ITagStore
from ..Core.tag_color import TagColor


class CompositeTagStore(ITagStore):
    """Delegates each operation to the primary or fallback store per path."""

    def __init__(self, primary: ITagStore, fallback: ITagStore) -> None:
        if not isinstance(primary, ITagStore) or not isinstance(fallback, ITagStore):
            raise TypeError("primary and fallback must implement ITagStore")
        self._primary = primary
        self._fallback = fallback

    def _store_for(self, path: Path) -> ITagStore:
        if self._primary.is_supported(path):
            return self._primary
        return self._fallback

    def get_tag(self, path: Path) -> TagColor:
        return self._store_for(path).get_tag(path)

    def set_tag(self, path: Path, tag: TagColor) -> None:
        self._store_for(path).set_tag(path, tag)

    def remove_tag(self, path: Path) -> None:
        self._store_for(path).remove_tag(path)

    def is_supported(self, path: Path) -> bool:
        return self._primary.is_supported(path) or self._fallback.is_supported(path)