#!/usr/bin/env python3
"""Format C++ source files with clang-format."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


CPP_SUFFIXES = {".h", ".hpp", ".cpp"}
SKIPPED_DIRECTORIES = {
    ".git",
    ".idea",
    ".vs",
    "artifacts",
    "bin",
    "conan",
    "external",
    "lib",
    "libs",
    "out",
    "tmp",
    "vcpkg_installed",
    ".vcpkg",
    ".deps",
}
SKIPPED_DIRECTORY_PREFIXES = ("build-", "cmake-build-")


def should_skip_directory(name: str, *, skip_external: bool) -> bool:
    normalized = name.casefold()
    if normalized == "external" and not skip_external:
        return False
    return (
        normalized in SKIPPED_DIRECTORIES
        or normalized == "build"
        or normalized.startswith(SKIPPED_DIRECTORY_PREFIXES)
    )


def cpp_files(directory: Path, *, skip_external: bool) -> list[Path]:
    files: list[Path] = []
    for current, directories, filenames in os.walk(directory):
        directories[:] = sorted(
            name
            for name in directories
            if not should_skip_directory(name, skip_external=skip_external)
        )
        for filename in sorted(filenames):
            path = Path(current) / filename
            if path.suffix.casefold() in CPP_SUFFIXES:
                files.append(path)
    return sorted(files)


def format_files(files: list[Path]) -> int:
    if not files:
        print("No C++ files found to format.")
        return 0

    formatter = shutil.which("clang-format")
    if formatter is None:
        print("Error: clang-format was not found on PATH.", file=sys.stderr)
        return 1

    failures = 0
    for path in files:
        result = subprocess.run(
            [formatter, "-i", str(path)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        if result.returncode != 0:
            failures += 1
            detail = result.stderr.strip() or f"exit code {result.returncode}"
            print(f"Failed: {path}: {detail}", file=sys.stderr)
        else:
            print(f"Formatted: {path}")

    print(f"Formatted {len(files) - failures} of {len(files)} file(s).")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)

    file_mode = modes.add_parser("file", help="format one C++ file")
    file_mode.add_argument("path", type=Path, help="path to a .h, .hpp, or .cpp file")

    folder_mode = modes.add_parser("folder", help="format C++ files under a folder")
    folder_mode.add_argument("path", type=Path, help="folder to scan recursively")

    modes.add_parser("project", help="format C++ files in this repository")
    args = parser.parse_args()

    if args.mode == "file":
        path = args.path.resolve()
        if not path.is_file():
            parser.error(f"file does not exist: {path}")
        if path.suffix.casefold() not in CPP_SUFFIXES:
            parser.error(f"unsupported file type: {path.suffix or '(no extension)'}")
        return format_files([path])

    if args.mode == "folder":
        directory = args.path.resolve()
        if not directory.is_dir():
            parser.error(f"folder does not exist: {directory}")
        return format_files(cpp_files(directory, skip_external=False))

    project_root = Path(__file__).resolve().parents[1]
    return format_files(cpp_files(project_root, skip_external=True))


if __name__ == "__main__":
    raise SystemExit(main())
