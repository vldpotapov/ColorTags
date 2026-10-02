"""Tests for SqliteTagStore (fallback backend)."""

import shutil
import tempfile
import unittest
from pathlib import Path

from src.Core.tag_color import TagColor
from src.Storage.sqlite_tag_store import SqliteTagStore


class SqliteTagStoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="ct_sqlite_test_"))
        self.db = self.tmp / "test.db"
        self.store = SqliteTagStore(self.db)
        self.base = self.tmp / "items"
        self.base.mkdir()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _file(self, name="f.txt"):
        f = self.base / name
        f.write_text("content", encoding="utf-8")
        return f

    def test_round_trip_file(self):
        f = self._file()
        self.store.set_tag(f, TagColor.RED)
        self.assertIs(self.store.get_tag(f), TagColor.RED)

    def test_round_trip_folder(self):
        d = self.base / "folder"
        d.mkdir()
        self.store.set_tag(d, TagColor.BLUE)
        self.assertIs(self.store.get_tag(d), TagColor.BLUE)

    def test_replace_tag(self):
        f = self._file()
        self.store.set_tag(f, TagColor.RED)
        self.store.set_tag(f, TagColor.GREEN)
        self.assertIs(self.store.get_tag(f), TagColor.GREEN)

    def test_remove_tag(self):
        f = self._file()
        self.store.set_tag(f, TagColor.RED)
        self.store.remove_tag(f)
        self.assertIs(self.store.get_tag(f), TagColor.NONE)

    def test_untagged_returns_none(self):
        f = self._file()
        self.assertIs(self.store.get_tag(f), TagColor.NONE)

    def test_rename_recovery(self):
        """A same-volume rename preserves identity and therefore the tag."""
        f = self._file("orig.txt")
        self.store.set_tag(f, TagColor.PURPLE)
        renamed = self.base / "renamed.txt"
        f.rename(renamed)
        self.assertIs(self.store.get_tag(renamed), TagColor.PURPLE)

    def test_reusing_path_does_not_transfer_tag(self):
        original = self._file("reused.txt")
        self.store.set_tag(original, TagColor.RED)
        renamed = self.base / "original.txt"
        original.rename(renamed)
        replacement = self._file("reused.txt")

        self.assertIs(self.store.get_tag(replacement), TagColor.NONE)
        self.assertIs(self.store.get_tag(renamed), TagColor.RED)

    def test_removing_replacement_does_not_remove_original_tag(self):
        original = self._file("reused.txt")
        self.store.set_tag(original, TagColor.BLUE)
        renamed = self.base / "original.txt"
        original.rename(renamed)
        replacement = self._file("reused.txt")

        self.store.remove_tag(replacement)

        self.assertIs(self.store.get_tag(renamed), TagColor.BLUE)

    def test_retagging_reused_path_cleans_stale_row(self):
        original = self._file("reused.txt")
        self.store.set_tag(original, TagColor.RED)
        original.unlink()
        replacement = self._file("reused.txt")

        self.store.set_tag(replacement, TagColor.GREEN)

        self.assertIs(self.store.get_tag(replacement), TagColor.GREEN)

    def test_unicode_path(self):
        f = self._file("тег-файл-🎨.txt")
        self.store.set_tag(f, TagColor.ORANGE)
        self.assertIs(self.store.get_tag(f), TagColor.ORANGE)

    def test_is_supported_always_true(self):
        self.assertTrue(self.store.is_supported(self.base))

    def test_set_none_raises(self):
        f = self._file()
        with self.assertRaises(ValueError):
            self.store.set_tag(f, TagColor.NONE)

    def test_persists_across_instances(self):
        f = self._file()
        self.store.set_tag(f, TagColor.GRAY)
        other = SqliteTagStore(self.db)
        self.assertIs(other.get_tag(f), TagColor.GRAY)


if __name__ == "__main__":
    unittest.main()
