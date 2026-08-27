#!/usr/bin/env python3
"""Find large files without dumping the entire repository tree."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from collections import Counter
from pathlib import Path

DEFAULT_EXCLUDE = {".git", ".vs", ".idea", "build", "external", "artifacts",
                   "vcpkg_installed", "node_modules", "__pycache__", "Testing",
                   "generated", "Generated", "coverage", "dist", "out"}


def size_text(size: int) -> str:
    value = float(size)
    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024 or unit == "GiB":
            return f"{value:.1f}{unit}"
        value /= 1024
    return f"{size}B"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path.cwd())
    parser.add_argument("--min-size", type=float, default=100, metavar="MB",
                        help="minimum size in MB (default: 100)")
    parser.add_argument("--top", type=int, default=30, metavar="N", help="maximum files (default: 30)")
    parser.add_argument("--ext", action="append", help="only these extensions, e.g. --ext .cpp; repeatable")
    parser.add_argument("--dir", dest="directories", action="append",
                        help="scan only this relative directory; repeatable")
    parser.add_argument("--exclude", action="append", default=[],
                        help="exclude directory or path component; repeatable")
    parser.add_argument("--include-generated", action="store_true")
    parser.add_argument("--all", action="store_true", help="include files below the size threshold")
    parser.add_argument("--tracked", action="store_true", help="scan only Git-tracked files")
    parser.add_argument("--sort", choices=("size", "mtime", "path"), default="size")
    parser.add_argument("--json", action="store_true", dest="as_json", help="output JSON")
    parser.add_argument("--summary", action="store_true", help="show extension and directory totals")
    args = parser.parse_args()
    root = args.root.resolve()
    excluded = set(args.exclude) | (set() if args.include_generated else DEFAULT_EXCLUDE)
    extensions = {ext.lower() if ext.startswith(".") else f".{ext.lower()}" for ext in (args.ext or [])}

    candidates: list[Path]
    if args.tracked:
        raw = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True).stdout
        candidates = [root / item for item in raw.decode(errors="replace").split("\0") if item]
    else:
        bases = [root / item for item in args.directories] if args.directories else [root]
        candidates = [p for base in bases if base.exists() for p in base.rglob("*")]
    records = []
    for path in candidates:
        if not path.is_file():
            continue
        try:
            relative = path.relative_to(root)
            if any(part in excluded for part in relative.parts):
                continue
            if extensions and path.suffix.lower() not in extensions:
                continue
            stat = path.stat()
        except OSError:
            continue
        if not args.all and stat.st_size < args.min_size * 1024 * 1024:
            continue
        records.append({"path": relative.as_posix(), "bytes": stat.st_size,
                        "size": size_text(stat.st_size), "mtime": stat.st_mtime,
                        "extension": path.suffix.lower() or "[none]"})

    records.sort(key={"size": lambda r: -r["bytes"], "mtime": lambda r: -r["mtime"],
                      "path": lambda r: r["path"]}[args.sort])
    records = records[:max(0, args.top)]
    if args.as_json:
        print(json.dumps(records, indent=2))
        return 0
    print(f"# Large files | root={root} | found={len(records)} threshold={args.min_size:g}MB")
    for record in records:
        print(f"{record['size']:>9}  {record['path']}")
    if args.summary:
        by_ext = Counter(record["extension"] for record in records)
        by_dir = Counter(Path(record["path"]).parts[0] for record in records)
        print("extensions: " + ", ".join(f"{key}={value}" for key, value in by_ext.most_common()))
        print("directories: " + ", ".join(f"{key}={value}" for key, value in by_dir.most_common()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
