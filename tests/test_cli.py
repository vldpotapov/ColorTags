"""Integration tests for the colortag CLI (Phase 3)."""

import contextlib
import io
import shutil
import tempfile
import unittest
from pathlib import Path

from src.Cli.colortag import main


class CliTests(unittest.TestCase):
    def setUp(self):
        self.base = Path(tempfile.mkdtemp(prefix="ct_cli_test_"))

    def tearDown(self):
        shutil.rmtree(self.base, ignore_errors=True)

    def _file(self, name="f.txt"):
        f = self.base / name
        f.write_text("content", encoding="utf-8")
        return f

    def _run(self, *argv):
        out = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def test_set_get_round_trip(self):
        f = self._file()
        code, out, _ = self._run("set", str(f), "red")
        self.assertEqual(code, 0)
        self.assertIn("Tag: red", out)
        code, out, _ = self._run("get", str(f))
        self.assertEqual(code, 0)
        self.assertIn("Tag: red", out)

    def test_get_untagged(self):
        f = self._file()
        code, out, _ = self._run("get", str(f))
        self.assertEqual(code, 0)
        self.assertIn("No tag", out)

    def test_remove(self):
        f = self._file()
        self._run("set", str(f), "blue")
        code, out, _ = self._run("remove", str(f))
        self.assertEqual(code, 0)
        self.assertIn("Tag removed", out)
        code, out, _ = self._run("get", str(f))
        self.assertIn("No tag", out)

    def test_color_by_display_name_case_insensitive(self):
        f = self._file()
        code, _, _ = self._run("set", str(f), "Purple")
        self.assertEqual(code, 0)
        code, out, _ = self._run("get", str(f))
        self.assertIn("Tag: purple", out)

    def test_list(self):
        code, out, _ = self._run("list")
        self.assertEqual(code, 0)
        for color_id in ("red", "orange", "yellow", "green", "blue", "purple", "gray"):
            self.assertIn(color_id, out)

    def test_inspect(self):
        f = self._file()
        self._run("set", str(f), "green")
        code, out, _ = self._run("inspect", str(f))
        self.assertEqual(code, 0)
        self.assertIn("Tag: green", out)
        self.assertIn("Supported: True", out)

    def test_unknown_color_exit_1(self):
        f = self._file()
        code, _, err = self._run("set", str(f), "magenta")
        self.assertEqual(code, 1)
        self.assertIn("unknown color", err)

    def test_missing_path_exit_1(self):
        code, _, err = self._run("get", str(self.base / "nope.txt"))
        self.assertEqual(code, 1)
        self.assertIn("path does not exist", err)

    def test_unicode_path(self):
        f = self._file("тег-файл-🎨.txt")
        code, _, _ = self._run("set", str(f), "orange")
        self.assertEqual(code, 0)
        code, out, _ = self._run("get", str(f))
        self.assertIn("Tag: orange", out)

    def test_folder_tagging(self):
        d = self.base / "folder"
        d.mkdir()
        code, _, _ = self._run("set", str(d), "yellow")
        self.assertEqual(code, 0)
        code, out, _ = self._run("get", str(d))
        self.assertIn("Tag: yellow", out)

    def test_set_many(self):
        f1 = self._file("a.txt")
        f2 = self._file("b.txt")
        code, out, _ = self._run("set-many", "red", str(f1), str(f2))
        self.assertEqual(code, 0)
        self.assertIn("Tag: red (2 paths)", out)
        for f in (f1, f2):
            code, out, _ = self._run("get", str(f))
            self.assertIn("Tag: red", out)

    def test_set_many_unknown_color_exit_1(self):
        f = self._file()
        code, _, err = self._run("set-many", "magenta", str(f))
        self.assertEqual(code, 1)
        self.assertIn("unknown color", err)

    def test_set_many_missing_path_exit_1(self):
        code, _, err = self._run(
            "set-many", "red", str(self.base / "nope1.txt"), str(self.base / "nope2.txt")
        )
        self.assertEqual(code, 1)
        self.assertIn("path does not exist", err)

    def test_remove_many(self):
        f1 = self._file("a.txt")
        f2 = self._file("b.txt")
        self._run("set-many", "blue", str(f1), str(f2))
        code, out, _ = self._run("remove-many", str(f1), str(f2))
        self.assertEqual(code, 0)
        self.assertIn("Tag removed (2 paths)", out)
        for f in (f1, f2):
            code, out, _ = self._run("get", str(f))
            self.assertIn("No tag", out)

    def test_no_command_usage_error(self):
        with self.assertRaises(SystemExit) as ctx:
            self._run()
        self.assertEqual(ctx.exception.code, 2)


if __name__ == "__main__":
    unittest.main()