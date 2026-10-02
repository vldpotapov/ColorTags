"""Tests for path and tag validation."""

from __future__ import annotations

import unittest
from pathlib import Path

from src.Core.tag_color import TagColor
from src.Core.tag_validation import TagValidationError, validate_path, validate_tag


class ValidatePathTests(unittest.TestCase):
    def test_accepts_path_and_str(self) -> None:
        p = Path(".")
        self.assertIsInstance(validate_path(p), Path)
        self.assertIsInstance(validate_path("."), Path)

    def test_empty_string_raises(self) -> None:
        with self.assertRaises(TagValidationError):
            validate_path("")

    def test_whitespace_only_raises(self) -> None:
        with self.assertRaises(TagValidationError):
            validate_path("   ")

    def test_none_raises(self) -> None:
        with self.assertRaises(TagValidationError):
            validate_path(None)  # type: ignore[arg-type]

    def test_non_string_raises(self) -> None:
        with self.assertRaises(TagValidationError):
            validate_path(42)  # type: ignore[arg-type]

    def test_missing_path_raises(self) -> None:
        with self.assertRaises(TagValidationError):
            validate_path("Z:\\definitely\\missing\\path\\xyz")


class ValidateTagTests(unittest.TestCase):
    def test_valid_tags_pass(self) -> None:
        for color in TagColor.all_supported():
            validate_tag(color)  # must not raise

    def test_none_tag_raises(self) -> None:
        with self.assertRaises(TagValidationError):
            validate_tag(TagColor.NONE)

    def test_wrong_type_raises(self) -> None:
        for bad in ("red", 1, None, ["red"]):
            with self.assertRaises(TagValidationError):
                validate_tag(bad)  # type: ignore[arg-type]


if __name__ == "__main__":
    unittest.main()