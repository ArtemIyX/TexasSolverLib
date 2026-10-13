#!/usr/bin/env python3
"""Summarize one C++ file without dumping implementation bodies."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

INCLUDE = re.compile(r"^\s*#\s*include\s*([<\"])([^>\"]+)[>\"]")
NAMESPACE = re.compile(r"^\s*namespace(?:\s+([\w:]+))?")
TYPE = re.compile(r"^\s*(class|struct|enum(?:\s+class)?)\s+([A-Za-z_]\w*)(?:\s*:\s*([^\{]+))?")
FUNCTION = re.compile(r"^\s*(?:(?:static|inline|virtual|constexpr|consteval|explicit|friend|extern|const|noexcept|override|final)\s+)*"
                      r"(?:[\w:<>,*&~]+\s+)+([~A-Za-z_]\w*(?:::\w+)*)\s*\([^;{}]*\)\s*(?:const\b|noexcept\b|override\b|final\b|->[^;{]+)?\s*(\{|;)")
MEMBER = re.compile(r"^\s*(?:static\s+|mutable\s+|constexpr\s+|const\s+|inline\s+)*[\w:<>,*&]+\s+([A-Za-z_]\w*)\s*(?:[=({]|;)")
TODO = re.compile(r"\b(TODO|FIXME|HACK|XXX)\b\s*:?[ \t]*(.*)", re.I)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path)
    parser.add_argument("--members", action="store_true", help="show likely class/struct data members")
    parser.add_argument("--signatures", action="store_true", help="show function declaration lines")
    parser.add_argument("--top", type=int, default=20, metavar="N", help="limit functions and TODOs (default: 20)")
    args = parser.parse_args()
    path = args.file.resolve()
    if not path.is_file():
        parser.error(f"file not found: {path}")
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    includes, namespaces, types, functions, members, todos = [], [], [], [], [], []
    brace_depth = 0
    type_stack: list[tuple[str, int]] = []
    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        match = INCLUDE.match(line)
        if match:
            includes.append((number, match.group(2), "internal" if match.group(1) == '"' else "external"))
        match = NAMESPACE.match(line)
        if match:
            namespaces.append((number, match.group(1) or "anonymous"))
        match = TYPE.match(line)
        if match:
            base = re.sub(r"\s+", " ", match.group(3).strip()) if match.group(3) else ""
            types.append((number, match.group(1), match.group(2), base))
            type_stack.append((match.group(2), brace_depth + line.count("{")))
        match = FUNCTION.match(line)
        if match and match.group(1) not in {"if", "for", "while", "switch", "catch"}:
            functions.append({"line": number, "name": match.group(1).split("::")[-1], "signature": stripped,
                              "depth": brace_depth, "definition": match.group(2) == "{"})
        if args.members and type_stack and brace_depth > type_stack[-1][1] - 1:
            match = MEMBER.match(line)
            if match and "(" not in line and not stripped.startswith(("using ", "typedef ")):
                members.append((number, type_stack[-1][0], match.group(1)))
        match = TODO.search(line)
        if match:
            todos.append((number, match.group(1).upper(), match.group(2).strip()))
        brace_depth += line.count("{") - line.count("}")
        while type_stack and brace_depth < type_stack[-1][1]:
            type_stack.pop()

    print(f"# C++ file | {path} | lines={len(lines)} chars={path.stat().st_size}")
    print(f"includes={len(includes)} types={len(types)} functions={len(functions)} todos={len(todos)}")
    if includes:
        print("includes: " + ", ".join(f"{name} [{kind}]@{line}" for line, name, kind in includes))
    if namespaces:
        print("namespaces: " + ", ".join(f"{name}@{line}" for line, name in namespaces))
    if types:
        print("types:")
        for line, kind, name, base in types:
            print(f"  {kind} {name}" + (f" : {base}" if base else "") + f" @{line}")
    if functions:
        print("functions:")
        for index, function in enumerate(functions[:max(0, args.top)]):
            suffix = f" {function['signature']}" if args.signatures else ""
            end_line = functions[index + 1]["line"] - 1 if index + 1 < len(functions) else len(lines)
            print(f"  {function['name']} @{function['line']} depth={function['depth']} "
                  f"size~{max(1, end_line - function['line'] + 1)}L "
                  f"{'definition' if function['definition'] else 'declaration'}{suffix}")
    if args.members and members:
        print("members:")
        for line, owner, name in members[:max(0, args.top * 2)]:
            print(f"  {owner}.{name} @{line}")
    if todos:
        print("todos:")
        for line, kind, text in todos[:max(0, args.top)]:
            print(f"  {kind}@{line}: {text}")
    print("structure: " + " -> ".join(f"{kind} {name}" for _, kind, name, _ in types) if types else "structure: (no named types)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
