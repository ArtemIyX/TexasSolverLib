#!/usr/bin/env python3
"""Print a compact map of targets and properties from CMake files."""

from __future__ import annotations

import argparse
import re
from collections import defaultdict
from pathlib import Path

COMMANDS = {
    "target_sources": "sources", "target_link_libraries": "links",
    "target_include_directories": "includes", "target_compile_definitions": "definitions",
    "target_compile_options": "options",
}
TARGET = re.compile(r"\b(add_(?:library|executable|custom_target))\s*\(\s*([\w.-]+)([^)]*)\)", re.I | re.S)
PROP = re.compile(r"\b(target_(?:sources|link_libraries|include_directories|compile_definitions|compile_options))\s*\(\s*([\w.-]+)([^)]*)\)", re.I | re.S)
TEST = re.compile(r"\badd_test\s*\(\s*(?:NAME\s+)?([\w.-]+)([^)]*)\)", re.I | re.S)
TOKENS = re.compile(r"(?:\$<[^>]+>|[^\s()]+)")


def clean(value: str) -> list[str]:
    return [token for token in TOKENS.findall(value) if token.upper() not in {"PUBLIC", "PRIVATE", "INTERFACE", "BEFORE", "AFTER"}]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--target", help="only show this target")
    parser.add_argument("--tests", action="store_true", help="show only test targets")
    parser.add_argument("--all-files", action="store_true", help="include CMake files below external/build directories")
    args = parser.parse_args()
    root = args.root.resolve()
    skip = set() if args.all_files else {".git", "build", "external", "vcpkg_installed", "artifacts"}
    cmake_files = [p for p in root.rglob("CMakeLists.txt") if not any(part in skip for part in p.relative_to(root).parts)]
    targets: dict[str, dict[str, set[str] | str]] = defaultdict(lambda: {"kind": "", "sources": set(), "links": set(), "includes": set(), "definitions": set(), "options": set()})
    tests: dict[str, str] = {}
    for path in cmake_files:
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for match in TARGET.finditer(text):
            targets[match.group(2)]["kind"] = match.group(1).removeprefix("add_")
        for match in PROP.finditer(text):
            target, property_name = match.group(2), COMMANDS[match.group(1).lower()]
            targets[target][property_name].update(clean(match.group(3)))
        for match in TEST.finditer(text):
            tests[match.group(1)] = " ".join(clean(match.group(2)))

    selected = sorted(targets)
    if args.target:
        selected = [name for name in selected if name == args.target or args.target.lower() in name.lower()]
    if args.tests:
        selected = [name for name in selected if name in tests or "test" in name.lower()]
    print(f"# CMake map | files={len(cmake_files)} targets={len(selected)} tests={len(tests)}")
    for name in selected:
        target = targets[name]
        print(f"\n{name} [{target['kind']}]")
        for label in ("sources", "links", "includes", "definitions", "options"):
            values = sorted(target[label])
            if values:
                print(f"  {label}: " + ", ".join(values))
    if tests and not args.target:
        print("\ntests:")
        for name, command in sorted(tests.items()):
            print(f"  {name}" + (f": {command}" if command else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
