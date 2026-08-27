#!/usr/bin/env python3
"""Print a small TexasSolver-oriented repository map."""

from __future__ import annotations

import argparse
import re
from collections import Counter
from pathlib import Path

AREAS = ("src", "include", "tests", "examples", "scripts", "cmake", "docs")
SKIP = {".git", ".vs", ".idea", "__pycache__", "Testing", "build", "artifacts",
        "external", "vcpkg_installed", "generated", "Generated", "coverage"}
SOURCE_EXTS = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".py"}
CLASS_RE = re.compile(r"\b(?:class|struct)\s+([A-Za-z_]\w*)")


def files_under(root: Path, area: str) -> list[Path]:
    base = root / area
    if not base.is_dir():
        return []
    return [p for p in base.rglob("*") if p.is_file()
            and not any(part in SKIP for part in p.relative_to(root).parts)]


def show(label: str, values: list[str], limit: int = 10) -> None:
    if values:
        print(f"{label}: " + ", ".join(values[:limit]) + (" ..." if len(values) > limit else ""))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path.cwd())
    args = parser.parse_args()
    root = args.root.resolve()
    groups = {area: files_under(root, area) for area in AREAS}
    tracked = [p for paths in groups.values() for p in paths]
    rel = lambda p: p.relative_to(root).as_posix()
    cpp = {".cpp", ".cc", ".cxx"}
    headers = {".h", ".hpp"}

    print(f"# TexasSolver | {root}")
    print(f"files={len(tracked)} cpp={sum(p.suffix.lower() in cpp for p in tracked)} "
          f"headers={sum(p.suffix.lower() in headers for p in tracked)} "
          f"py={sum(p.suffix.lower() == '.py' for p in tracked)} "
          f"tests={sum(p.name.startswith('test_') for p in groups['tests'])}")
    print("areas:")
    for area in AREAS:
        paths = groups[area]
        if not paths:
            continue
        base = root / area
        dirs = Counter((p.relative_to(base).parts[0] if len(p.relative_to(base).parts) > 1 else ".") for p in paths)
        print(f"  {area}={len(paths)} " + ", ".join(f"{name}:{count}" for name, count in dirs.most_common(12)))

    configs = [rel(p) for p in [root / "CMakeLists.txt", root / "vcpkg.json", root / "README.md", root / "AGENTS.md"] if p.is_file()]
    configs += [rel(p) for p in groups["cmake"] if p.name == "CMakeLists.txt"]
    show("configs", sorted(configs), 12)
    show("entrypoints", sorted(rel(p) for p in groups["examples"] if p.suffix == ".cpp"), 12)
    show("test_areas", sorted({p.relative_to(root / "tests").parts[0] for p in groups["tests"] if p.relative_to(root / "tests").parts}), 8)

    symbols = []
    for p in groups["include"]:
        if p.suffix.lower() not in headers:
            continue
        try:
            symbols += [f"{name}@{rel(p)}" for name in CLASS_RE.findall(p.read_text(encoding="utf-8", errors="ignore"))[:6]]
        except OSError:
            pass
    show("public_types", sorted(set(symbols)), 16)
    show("recent", [rel(p) for p in sorted(tracked, key=lambda p: p.stat().st_mtime, reverse=True)[:8]], 8)
    show("largest", [f"{p.stat().st_size // 1024}K:{rel(p)}" for p in sorted(tracked, key=lambda p: p.stat().st_size, reverse=True)
                      if p.suffix.lower() in SOURCE_EXTS][:8], 8)
    print("ignored: " + ", ".join(sorted(SKIP)))
    print("stack: C++17, CMake, vcpkg; domains=core, games, solver, preflop, util")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
