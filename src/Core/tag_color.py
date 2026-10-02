"""TagColor — the stable, platform-independent tag model.

Maps to the roadmap's ``enum class TagColor``:

    None, Red, Orange, Yellow, Green, Blue, Purple, Gray

The internal identifiers are stable and must never change: they are the
values persisted to storage (Phase 8 allows renaming only the display
labels, never the ids).
"""

from __future__ import annotations

from enum import Enum

# Module-level so it is not captured as an enum member (Python 3.11+).
_DISPLAY_NAMES: dict[str, str] = {
    "none": "None",
    "red": "Red",
    "orange": "Orange",
    "yellow": "Yellow",
    "green": "Green",
    "blue": "Blue",
    "purple": "Purple",
    "gray": "Gray",
}


class TagColor(Enum):
    """The seven tag colors plus the "no tag" state."""

    NONE = "none"
    RED = "red"
    ORANGE = "orange"
    YELLOW = "yellow"
    GREEN = "green"
    BLUE = "blue"
    PURPLE = "purple"
    GRAY = "gray"

    @property
    def id(self) -> str:
        """Stable storage identifier, e.g. ``"red"``."""
        return self.value

    @property
    def display_name(self) -> str:
        """Human-readable label, e.g. ``"Red"``."""
        return _DISPLAY_NAMES[self.value]

    @classmethod
    def from_id(cls, value: str) -> "TagColor":
        """Parse a stable id; raises ``ValueError`` for unknown ids."""
        try:
            return cls(value)
        except ValueError:
            raise ValueError(f"unknown tag id: {value!r}") from None

    @classmethod
    def all_supported(cls) -> tuple["TagColor", ...]:
        """All taggable colors, excluding ``NONE``."""
        return tuple(c for c in cls if c is not cls.NONE)