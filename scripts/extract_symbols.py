#!/usr/bin/env python3
"""Extract a compact C++ symbol index without printing source bodies."""

from __future__ import annotations

import argparse
import re
from collections import defaultdict
from pathlib import Path

SKIP = {".git", "build", "external", "artifacts", "vcpkg_installed", "generated", "Generated", "__pycache__"}
EXTS = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx"}
SYMBOL_RE = re.compile(
    r"^\s*(?:(?:template\s*<.*>\s*)?)(class|struct|enum(?:\s+class)?|namespace)\s+([A-Za-z_]\w*)"
    r"(?:\s*:\s*(?:public|protected|private)\s+([^\{]+))?"
)
FUNCTION_RE = re.compile(
    r"^\s*(?:(?:static|inline|virtual|constexpr|consteval|explicit|friend|extern|inline)\s+)*"
    r"(?:[A-Za-z_][\w:<>,*&\s]+\s+)?([~A-Za-z_]\w*(?:::\w+)*)\s*\([^;{}]*\)"
    r"\s*(?:const\s*)?(?:noexcept\b[^;{}]*)?(?:;|\{|=)"
)
CONTROL = {"if", "for", "while", "switch", "catch", "return", "sizeof"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--file", help="substring filter for file path")
    parser.add_argument("--module", help="substring filter for module/path, e.g. solver or src/games")
    parser.add_argument("--type", choices=("class", "struct", "enum", "namespace", "function"), action="append")
    parser.add_argument("--signatures", action="store_true", help="include function signatures")
    parser.add_argument("--inheritance", action="store_true", help="include base classes")
    parser.add_argument("--public", action="store_true", help="only index public include/ headers")
    parser.add_argument("--compact", action="store_true", help="print one record per line without file headings")
    parser.add_argument("--top", type=int, metavar="N", help="print at most N symbols")
    parser.add_argument("--area", choices=("include", "src"), action="append",
                        help="scan only this area; may be repeated")
    parser.add_argument("--sort", choices=("file", "name", "line"), default="file",
                        help="result ordering (default: file)")
    args = parser.parse_args()
    root = args.root.resolve()
    areas = args.area or ["include", "src"]
    paths = [p for area in areas if (root / area).is_dir()
             for p in (root / area).rglob("*") if p.is_file() and p.suffix.lower() in EXTS
             and not any(part in SKIP for part in p.relative_to(root).parts)]
    records: list[tuple[str, str, str, int, str, str]] = []
    for path in paths:
        rel = path.relative_to(root).as_posix()
        if args.file and args.file.lower() not in rel.lower():
            continue
        if args.module and args.module.lower() not in rel.lower():
            continue
        if args.public and not rel.startswith("include/"):
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue
        for number, line in enumerate(lines, 1):
            match = SYMBOL_RE.match(line)
            if match:
                kind = "enum" if match.group(1).startswith("enum") else match.group(1)
                bases = re.sub(r"\s+", " ", match.group(3).strip()) if match.group(3) else ""
                records.append((kind, match.group(2), rel, number, line.strip(), bases))
                continue
            match = FUNCTION_RE.match(line)
            if match:
                name = match.group(1)
                if name not in CONTROL:
                    records.append(("function", name.split("::")[-1], rel, number,
                                    re.sub(r"\s+", " ", line.strip()), ""))

    allowed = set(args.type or ("class", "struct", "enum", "namespace", "function"))
    records = [record for record in records if record[0] in allowed]
    sort_keys = {"file": lambda r: (r[2], r[3], r[0], r[1]),
                 "name": lambda r: (r[1].lower(), r[2], r[3]),
                 "line": lambda r: (r[3], r[2], r[1])}
    records.sort(key=sort_keys[args.sort])
    if args.top is not None:
        records = records[:max(0, args.top)]
    print(f"# TexasSolver symbols | root={root} | symbols={len(records)}")
    if args.compact:
        for kind, name, path, line, signature, bases in records:
            detail = f" {signature}" if args.signatures and kind == "function" else ""
            if args.inheritance and bases and kind in {"class", "struct"}:
                detail += f" : {bases}"
            print(f"{kind}\t{name}\t{path}:{line}{detail}")
        return 0
    current_file = None
    for kind, name, path, line, signature, bases in records:
        if path != current_file:
            print(f"\n{path}")
            current_file = path
        detail = f" {signature}" if args.signatures and kind == "function" else ""
        if args.inheritance and bases and kind in {"class", "struct"}:
            detail += f" : {bases}"
        print(f"  L{line} {kind:<9} {name}{detail}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
