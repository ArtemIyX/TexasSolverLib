#!/usr/bin/env python3
"""Print a compact summary of recent Git and working-tree changes."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def git(root: Path, *args: str) -> str:
    result = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    return result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--commits", type=int, default=8, help="number of commits (default: 8)")
    parser.add_argument("--files", type=int, default=20, help="maximum changed files (default: 20)")
    args = parser.parse_args()
    root = args.root.resolve()

    branch = git(root, "branch", "--show-current") or "(detached HEAD)"
    status = git(root, "status", "--short")
    log = git(root, "log", f"-{args.commits}", "--date=short", "--pretty=format:%h %ad %s")
    changed = git(root, "log", f"-{args.commits}", "--name-only", "--pretty=format:")
    files = list(dict.fromkeys(line for line in changed.splitlines() if line.strip()))

    print(f"# Recent changes | {root}")
    print(f"branch: {branch}")
    print(f"working_tree: {'clean' if not status else 'changed'}")
    if status:
        print("status:")
        for line in status.splitlines()[:args.files]:
            print(f"  {line}")
    print("commits:")
    print("  " + (log.replace("\n", "\n  ") if log else "none"))
    print(f"recent_files: {len(files)}")
    for path in files[:args.files]:
        print(f"  {path}")
    if len(files) > args.files:
        print(f"  ... {len(files) - args.files} more")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
