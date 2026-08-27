#!/usr/bin/env python3
"""Print a compact source dependency/import map."""

from __future__ import annotations

import argparse
import re
from collections import Counter, defaultdict
from pathlib import Path

AREAS = ("include", "src", "tests", "examples", "scripts")
SKIP = {".git", "build", "external", "artifacts", "vcpkg_installed", "generated", "Generated", "__pycache__"}
CPP_INCLUDE = re.compile(r"^\s*#\s*include\s*([<\"])([^>\"]+)[>\"]")
PY_IMPORT = re.compile(r"^\s*(?:from\s+([\w.]+)\s+import|import\s+([\w.]+))")


def files(root: Path) -> list[Path]:
    return [p for area in AREAS if (root / area).is_dir() for p in (root / area).rglob("*")
            if p.is_file() and p.suffix.lower() in {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx", ".py"}
            and not any(part in SKIP for part in p.relative_to(root).parts)]


def module(path: str) -> str:
    parts = Path(path).parts
    return "/".join(parts[:2]) if len(parts) > 1 else parts[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--reverse", metavar="NAME", help="show files/modules that import NAME")
    parser.add_argument("--modules", action="store_true", help="aggregate dependencies by top-level folder")
    parser.add_argument("--missing", action="store_true", help="show unresolved quoted includes")
    parser.add_argument("--top", type=int, default=12, metavar="N", help="limit map entries (default: 12)")
    args = parser.parse_args()
    root = args.root.resolve()
    source_files = files(root)
    known = {p.name for p in source_files} | {p.relative_to(root).as_posix() for p in source_files}
    edges: list[tuple[str, str, int, str, bool]] = []
    for path in source_files:
        source = path.relative_to(root).as_posix()
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue
        for line_no, line in enumerate(lines, 1):
            match = CPP_INCLUDE.match(line)
            if match:
                target, quoted = match.group(2), match.group(1) == '"'
                resolved = target in known or Path(target).name in known
                edges.append((source, target, line_no, "internal" if quoted and resolved else "external", quoted and not resolved))
                continue
            if path.suffix == ".py":
                match = PY_IMPORT.match(line)
                if match:
                    target = match.group(1) or match.group(2)
                    edges.append((source, target, line_no, "internal" if target.split(".")[0] in {"src", "scripts"} else "external", False))

    print(f"# TexasSolver includes | files={len(source_files)} edges={len(edges)}")
    if args.modules:
        grouped = Counter((module(source), module(target)) for source, target, *_ in edges)
        print("modules:")
        for (source, target), count in grouped.most_common(args.top):
            print(f"  {source} -> {target} ({count})")
    else:
        print("dependencies:")
        for source, target, line, kind, _ in edges[:args.top * 4]:
            print(f"  {source}:{line} -> {target} [{kind}]")

    frequent = Counter(target for _, target, *_ in edges)
    print("frequent:")
    for target, count in frequent.most_common(args.top):
        print(f"  {count}x {target}")
    if args.reverse:
        print(f"reverse: {args.reverse}")
        for source, target, line, *_ in edges:
            if args.reverse.lower() in target.lower():
                print(f"  {source}:{line}")
    if args.missing:
        print("missing:")
        for source, target, line, _, missing in edges:
            if missing:
                print(f"  {source}:{line} -> {target}")
    adjacency: dict[str, set[str]] = defaultdict(set)
    for source, target, *_ in edges:
        target_path = next((p for p in source_files if p.name == Path(target).name), None)
        if target_path:
            adjacency[source].add(target_path.relative_to(root).as_posix())
    cycles = []
    def visit(node: str, path: list[str], active: set[str]) -> None:
        if node in active:
            cycles.append(" -> ".join(path[path.index(node):] + [node]))
            return
        if node in path:
            return
        for child in adjacency.get(node, ()):
            visit(child, path + [node], active | {node})
    for node in adjacency:
        visit(node, [], set())
    if cycles:
        print("cycles:")
        for cycle in list(dict.fromkeys(cycles))[:args.top]:
            print(f"  {cycle}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
