"""Validation helpers shared by the tag core."""

from __future__ import annotations

from pathlib import Path

from .tag_color import TagColor


class TagValidationError(ValueError):
    """Raised when a path or tag fails validation."""


def validate_path(path: str | Path) -> Path:
    """Normalize and validate a filesystem path.

    Raises ``TagValidationError`` for empty paths, non-string values,
    or paths that do not exist.
    """
    if isinstance(path, Path):
        p = path
    elif isinstance(path, str) and path.strip():
        p = Path(path)
    else:
        raise TagValidationError("path must be a non-empty string or Path")
    if not p.exists():
        raise TagValidationError(f"path does not exist: {p}")
    return p


def validate_tag(tag: TagColor) -> None:
    """Validate a tag value.

    Raises ``TagValidationError`` for non-``TagColor`` values and for
    ``TagColor.NONE`` (use ``remove_tag`` to clear a tag).
    """
    if not isinstance(tag, TagColor):
        raise TagValidationError(
            f"tag must be a TagColor, got {type(tag).__name__}"
        )
    if tag is TagColor.NONE:
        raise TagValidationError(
            "TagColor.NONE is not a settable tag; use remove_tag"
        )