"""Tests for TagService over the in-memory store.

Covers the roadmap's Phase 1 test list: file tagging, folder tagging,
replacing a tag, removing a tag, invalid tag values, missing paths,
Unicode paths, long paths, read-only items.
"""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from src.Core.tag_color import TagColor
from src.Core.tag_service import TagService
from src.Core.tag_validation import TagValidationError
from tests.in_memory_tag_store import InMemoryTagStore


class TagServiceTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.service = TagService(InMemoryTagStore())

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _make_file(self, name: str = "test.txt") -> Path:
        p = self.root / name
        p.write_text("content", encoding="utf-8")
        return p

    def _make_folder(self, name: str = "folder") -> Path:
        p = self.root / name
        p.mkdir()
        return p

    # --- file tagging -------------------------------------------------

    def test_set_and_get_file_tag(self) -> None:
        f = self._make_file()
        self.service.set_tag(f, TagColor.RED)
        self.assertIs(self.service.get_tag(f), TagColor.RED)

    def test_untagged_file_returns_none(self) -> None:
        f = self._make_file()
        self.assertIs(self.service.get_tag(f), TagColor.NONE)

    # --- folder tagging ------------------------------------------------

    def test_set_and_get_folder_tag(self) -> None:
        d = self._make_folder()
        self.service.set_tag(d, TagColor.GREEN)
        self.assertIs(self.service.get_tag(d), TagColor.GREEN)

    # --- replacing a tag ------------------------------------------------

    def test_replace_tag(self) -> None:
        f = self._make_file()
        self.service.set_tag(f, TagColor.RED)
        self.service.set_tag(f, TagColor.BLUE)
        self.assertIs(self.service.get_tag(f), TagColor.BLUE)

    # --- removing a tag -------------------------------------------------

    def test_remove_tag(self) -> None:
        f = self._make_file()
        self.service.set_tag(f, TagColor.PURPLE)
        self.service.remove_tag(f)
        self.assertIs(self.service.get_tag(f), TagColor.NONE)

    def test_remove_tag_on_untagged_is_noop(self) -> None:
        f = self._make_file()
        self.service.remove_tag(f)  # must not raise
        self.assertIs(self.service.get_tag(f), TagColor.NONE)

    # --- invalid tag values ---------------------------------------------

    def test_set_invalid_tag_raises(self) -> None:
        f = self._make_file()
        with self.assertRaises(TagValidationError):
            self.service.set_tag(f, "red")  # type: ignore[arg-type]
        with self.assertRaises(TagValidationError):
            self.service.set_tag(f, TagColor.NONE)

    # --- missing paths ---------------------------------------------------

    def test_missing_path_raises(self) -> None:
        missing = self.root / "nope.txt"
        with self.assertRaises(TagValidationError):
            self.service.get_tag(missing)
        with self.assertRaises(TagValidationError):
            self.service.set_tag(missing, TagColor.RED)
        with self.assertRaises(TagValidationError):
            self.service.remove_tag(missing)

    # --- Unicode paths ---------------------------------------------------

    def test_unicode_path(self) -> None:
        f = self._make_file("тест_файл_🎨.txt")
        self.service.set_tag(f, TagColor.YELLOW)
        self.assertIs(self.service.get_tag(f), TagColor.YELLOW)
        self.service.remove_tag(f)
        self.assertIs(self.service.get_tag(f), TagColor.NONE)

    def test_cyrillic_folder_path(self) -> None:
        d = self._make_folder("папка_с_тегами")
        self.service.set_tag(d, TagColor.ORANGE)
        self.assertIs(self.service.get_tag(d), TagColor.ORANGE)

    # --- long paths ------------------------------------------------------

    def test_long_path(self) -> None:
        # Build a nested chain that exceeds the classic 260-char MAX_PATH.
        parts = []
        remaining = 300
        while remaining > 0:
            chunk = "d" * min(40, remaining)
            parts.append(chunk)
            remaining -= len(chunk) + 1
        deep = self.root.joinpath(*parts)
        deep.mkdir(parents=True)
        f = deep / ("f" * 100 + ".txt")
        f.write_text("x", encoding="utf-8")
        self.assertGreater(len(str(f)), 260)
        self.service.set_tag(f, TagColor.GRAY)
        self.assertIs(self.service.get_tag(f), TagColor.GRAY)
        self.service.remove_tag(f)
        self.assertIs(self.service.get_tag(f), TagColor.NONE)

    # --- read-only items --------------------------------------------------

    def test_read_only_file(self) -> None:
        f = self._make_file("readonly.txt")
        os.chmod(f, 0o444)
        try:
            # The in-memory store does not touch the file, so tagging works;
            # real read-only semantics belong to the storage backend (Phase 2).
            self.service.set_tag(f, TagColor.BLUE)
            self.assertIs(self.service.get_tag(f), TagColor.BLUE)
        finally:
            os.chmod(f, 0o644)

    # --- support and enumeration ------------------------------------------

    def test_is_tag_supported(self) -> None:
        f = self._make_file()
        d = self._make_folder()
        self.assertTrue(self.service.is_tag_supported(f))
        self.assertTrue(self.service.is_tag_supported(d))

    def test_get_all_supported_tags(self) -> None:
        tags = self.service.get_all_supported_tags()
        self.assertEqual(len(tags), 7)
        self.assertEqual(
            [t.id for t in tags],
            ["red", "orange", "yellow", "green", "blue", "purple", "gray"],
        )

    # --- store wiring ------------------------------------------------------

    def test_service_requires_itagstore(self) -> None:
        with self.assertRaises(TypeError):
            TagService(object())  # type: ignore[arg-type]

    def test_store_is_exposed(self) -> None:
        store = InMemoryTagStore()
        service = TagService(store)
        self.assertIs(service.store, store)


if __name__ == "__main__":
    unittest.main()