"""colortag — CLI prototype (Phase 3).

Usage (from the project root):

    python -m src.Cli.colortag set        <path> <color>
    python -m src.Cli.colortag set-many   <color> <path> [<path> ...]
    python -m src.Cli.colortag get        <path>
    python -m src.Cli.colortag remove     <path>
    python -m src.Cli.colortag remove-many <path> [<path> ...]
    python -m src.Cli.colortag list
    python -m src.Cli.colortag inspect    <path>

Exit codes: 0 success, 1 runtime error, 2 usage error.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from ..Core.tag_color import TagColor
from ..Core.tag_service import TagService
from ..Core.tag_validation import TagValidationError
from ..Storage.ads_tag_store import AdsTagStore
from ..Storage.composite_tag_store import CompositeTagStore
from ..Storage.sqlite_tag_store import SqliteTagStore


def _build_service() -> TagService:
    return TagService(CompositeTagStore(AdsTagStore(), SqliteTagStore()))


def _parse_color(text: str) -> TagColor:
    """Accept a stable id or display name, case-insensitive."""
    lowered = text.strip().lower()
    for color in TagColor.all_supported():
        if color.id == lowered or color.display_name.lower() == lowered:
            return color
    raise TagValidationError(
        f"unknown color: {text!r} (expected one of "
        + ", ".join(c.id for c in TagColor.all_supported())
        + ")"
    )


def _cmd_set(args: argparse.Namespace, service: TagService) -> int:
    color = _parse_color(args.color)
    service.set_tag(args.path, color)
    print(f"Tag: {color.id}")
    return 0


def _cmd_set_many(args: argparse.Namespace, service: TagService) -> int:
    color = _parse_color(args.color)
    for path in args.paths:
        service.set_tag(path, color)
    print(f"Tag: {color.id} ({len(args.paths)} paths)")
    return 0


def _cmd_get(args: argparse.Namespace, service: TagService) -> int:
    color = service.get_tag(args.path)
    print(f"Tag: {color.id}" if color is not TagColor.NONE else "No tag")
    return 0


def _cmd_remove(args: argparse.Namespace, service: TagService) -> int:
    service.remove_tag(args.path)
    print("Tag removed")
    return 0


def _cmd_remove_many(args: argparse.Namespace, service: TagService) -> int:
    for path in args.paths:
        service.remove_tag(path)
    print(f"Tag removed ({len(args.paths)} paths)")
    return 0


def _cmd_list(args: argparse.Namespace, service: TagService) -> int:
    for color in service.get_all_supported_tags():
        print(f"{color.id:<8} {color.display_name}")
    return 0


def _cmd_inspect(args: argparse.Namespace, service: TagService) -> int:
    color = service.get_tag(args.path)
    print(f"Tag: {color.id}" if color is not TagColor.NONE else "No tag")
    print(f"Supported: {service.is_tag_supported(args.path)}")
    print(f"Store: {type(service.store).__name__}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="colortag",
        description="Color tags for Windows Explorer (prototype).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_set = sub.add_parser("set", help="assign a color tag to a path")
    p_set.add_argument("path", type=Path, help="file or folder path")
    p_set.add_argument("color", help="color id or display name, e.g. red")
    p_set.set_defaults(func=_cmd_set)

    p_set_many = sub.add_parser(
        "set-many", help="assign a color tag to several paths"
    )
    p_set_many.add_argument("color", help="color id or display name, e.g. red")
    p_set_many.add_argument(
        "paths", type=Path, nargs="+", help="file or folder paths"
    )
    p_set_many.set_defaults(func=_cmd_set_many)

    p_get = sub.add_parser("get", help="show the tag of a path")
    p_get.add_argument("path", type=Path, help="file or folder path")
    p_get.set_defaults(func=_cmd_get)

    p_remove = sub.add_parser("remove", help="remove the tag from a path")
    p_remove.add_argument("path", type=Path, help="file or folder path")
    p_remove.set_defaults(func=_cmd_remove)

    p_remove_many = sub.add_parser(
        "remove-many", help="remove the tag from several paths"
    )
    p_remove_many.add_argument(
        "paths", type=Path, nargs="+", help="file or folder paths"
    )
    p_remove_many.set_defaults(func=_cmd_remove_many)

    p_list = sub.add_parser("list", help="list supported colors")
    p_list.set_defaults(func=_cmd_list)

    p_inspect = sub.add_parser("inspect", help="show tag and backend details")
    p_inspect.add_argument("path", type=Path, help="file or folder path")
    p_inspect.set_defaults(func=_cmd_inspect)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    service = _build_service()
    try:
        return args.func(args, service)
    except TagValidationError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except FileNotFoundError as e:
        print(f"error: path not found: {e.filename}", file=sys.stderr)
        return 1
    except PermissionError as e:
        print(f"error: permission denied: {e.filename}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())