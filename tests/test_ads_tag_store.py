"""Integration tests for AdsTagStore against the real filesystem (NTFS)."""

import os
import shutil
import tempfile
import unittest
from pathlib import Path

from src.Core.tag_color import TagColor
from src.Storage.ads_tag_store import AdsTagStore


@unittest.skipUnless(
    AdsTagStore().is_supported(Path(tempfile.gettempdir())),
    "requires an NTFS volume",
)
class AdsTagStoreTests(unittest.TestCase):
    def setUp(self):
        self.store = AdsTagStore()
        self.base = Path(tempfile.mkdtemp(prefix="ct_ads_test_"))

    def tearDown(self):
        shutil.rmtree(self.base, ignore_errors=True)

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

    def test_remove_tag_on_untagged_is_noop(self):
        f = self._file()
        self.store.remove_tag(f)  # must not raise
        self.assertIs(self.store.get_tag(f), TagColor.NONE)

    def test_untagged_returns_none(self):
        f = self._file()
        self.assertIs(self.store.get_tag(f), TagColor.NONE)

    def test_empty_stream_treated_as_untagged(self):
        f = self._file()
        Path(f"{f}:ColorTag").write_bytes(b"")
        self.assertIs(self.store.get_tag(f), TagColor.NONE)

    def test_unknown_value_treated_as_untagged(self):
        f = self._file()
        Path(f"{f}:ColorTag").write_bytes(b"magenta")
        self.assertIs(self.store.get_tag(f), TagColor.NONE)

    def test_utf16_value_treated_as_untagged(self):
        f = self._file()
        Path(f"{f}:ColorTag").write_bytes("red".encode("utf-16"))
        self.assertIs(self.store.get_tag(f), TagColor.NONE)

    def test_trailing_newline_is_stripped(self):
        f = self._file()
        Path(f"{f}:ColorTag").write_bytes(b"yellow\n")
        self.assertIs(self.store.get_tag(f), TagColor.YELLOW)

    def test_read_only_file_read_works_write_raises(self):
        f = self._file()
        self.store.set_tag(f, TagColor.GREEN)
        os.chmod(f, 0o444)  # read-only attribute
        try:
            self.assertIs(self.store.get_tag(f), TagColor.GREEN)
            with self.assertRaises(PermissionError):
                self.store.set_tag(f, TagColor.RED)
            with self.assertRaises(PermissionError):
                self.store.remove_tag(f)
        finally:
            os.chmod(f, 0o666)

    def test_tag_changes_preserve_precise_modification_time(self):
        f = self._file()
        timestamp = 1_728_000_000_123_456_700  # NTFS 100 ns precision
        os.utime(f, ns=(timestamp, timestamp))
        before = f.stat().st_mtime_ns
        self.store.set_tag(f, TagColor.BLUE)
        self.assertEqual(f.stat().st_mtime_ns, before)
        self.store.remove_tag(f)
        self.assertEqual(f.stat().st_mtime_ns, before)

    def test_long_path(self):
        long_dir = self.base / ("d" * 60)
        long_dir.mkdir()
        f = long_dir / ("n" * 200 + ".txt")
        f.write_text("x", encoding="utf-8")
        self.assertGreater(len(str(f)), 260)
        self.store.set_tag(f, TagColor.PURPLE)
        self.assertIs(self.store.get_tag(f), TagColor.PURPLE)

    def test_unicode_path(self):
        f = self._file("тег-файл-🎨.txt")
        self.store.set_tag(f, TagColor.ORANGE)
        self.assertIs(self.store.get_tag(f), TagColor.ORANGE)

    def test_is_supported_true_on_ntfs(self):
        self.assertTrue(self.store.is_supported(self.base))

    def test_set_none_raises(self):
        f = self._file()
        with self.assertRaises(ValueError):
            self.store.set_tag(f, TagColor.NONE)

    def test_all_seven_colors_round_trip(self):
        f = self._file()
        for color in TagColor:
            if color is TagColor.NONE:
                continue
            self.store.set_tag(f, color)
            self.assertIs(self.store.get_tag(f), color)


if __name__ == "__main__":
    unittest.main()
