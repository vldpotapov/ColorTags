"""Tests for the TagColor model."""

from __future__ import annotations

import unittest

from src.Core.tag_color import TagColor


class TagColorTests(unittest.TestCase):
    def test_exactly_seven_colors_plus_none(self) -> None:
        self.assertEqual(len(TagColor), 8)
        self.assertEqual(
            [c.id for c in TagColor],
            ["none", "red", "orange", "yellow", "green", "blue", "purple", "gray"],
        )

    def test_stable_ids(self) -> None:
        # The ids are the persisted values — they must never change.
        expected = {
            TagColor.NONE: "none",
            TagColor.RED: "red",
            TagColor.ORANGE: "orange",
            TagColor.YELLOW: "yellow",
            TagColor.GREEN: "green",
            TagColor.BLUE: "blue",
            TagColor.PURPLE: "purple",
            TagColor.GRAY: "gray",
        }
        for color, tag_id in expected.items():
            self.assertEqual(color.id, tag_id)

    def test_display_names(self) -> None:
        self.assertEqual(TagColor.RED.display_name, "Red")
        self.assertEqual(TagColor.GRAY.display_name, "Gray")
        self.assertEqual(TagColor.NONE.display_name, "None")

    def test_from_id_round_trip(self) -> None:
        for color in TagColor:
            self.assertIs(TagColor.from_id(color.id), color)

    def test_from_id_unknown_raises(self) -> None:
        for bad in ("", "crimson", "RED", "red ", "7", None):
            with self.assertRaises(ValueError):
                TagColor.from_id(bad)  # type: ignore[arg-type]

    def test_all_supported_excludes_none(self) -> None:
        supported = TagColor.all_supported()
        self.assertEqual(len(supported), 7)
        self.assertNotIn(TagColor.NONE, supported)
        self.assertIn(TagColor.RED, supported)
        self.assertIn(TagColor.GRAY, supported)


if __name__ == "__main__":
    unittest.main()