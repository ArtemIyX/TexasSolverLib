#!/usr/bin/env python3
"""Summarize custom CTest [FAIL] lines without redundant source paths."""

from __future__ import annotations

import argparse
import re
from collections import OrderedDict
from pathlib import Path

FAIL = re.compile(
    r"^\s*\[FAIL\]\s*(?P<test>[^:]+):\s*"
    r"(?P<file>.+?\.(?:cpp|cc|cxx|h|hpp|py)):(?P<line>\d+)\s*(?P<message>.*)$"
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=Path, default=Path("tmp/ctest_debug.log"),
                        help="CTest log (default: tmp/ctest_debug.log)")
    parser.add_argument("-o", "--output", type=Path, default=Path("tmp/ctest_errors.txt"),
                        help="summary file (default: tmp/ctest_errors.txt)")
    args = parser.parse_args()
    records: OrderedDict[tuple[str, str], dict[str, object]] = OrderedDict()
    if args.input.is_file():
        for raw in args.input.read_text(encoding="utf-8", errors="replace").splitlines():
            match = FAIL.match(raw)
            if not match:
                continue
            test = match.group("test").strip()
            message = re.sub(r"\s+", " ", match.group("message").strip())
            key = (test, message)
            if key not in records:
                records[key] = {"test": test, "line": match.group("line"), "message": message, "count": 0}
            records[key]["count"] = int(records[key]["count"]) + 1

    output = [f"# CTest failure scan: {args.input}", f"unique_failures={len(records)}"]
    if not args.input.is_file():
        output.append("input: NOT FOUND")
    if records:
        output.append("\nFAILURES")
        for number, record in enumerate(records.values(), 1):
            count = f" [{record['count']}x]" if int(record["count"]) > 1 else ""
            output.append(f"{number}. {record['test']}{count} (line {record['line']}): {record['message']}")
    output.append("\nresult: " + ("FAILURES FOUND" if records else "NO FAILURES DETECTED"))
    text = "\n".join(output) + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")
    print(f"output: {args.output}")
    print("result: " + ("FAILURES FOUND" if records else "NO FAILURES DETECTED"))
    return 1 if records else 0


if __name__ == "__main__":
    raise SystemExit(main())
