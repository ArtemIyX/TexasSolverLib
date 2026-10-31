#!/usr/bin/env python3
"""Summarize custom CTest failures and failed-test summaries."""

from __future__ import annotations

import argparse
import re
from collections import OrderedDict
from pathlib import Path

FAIL = re.compile(
    r"^\s*\[(?:\*\*)?FAIL(?:\*\*)?\]\s*"
    r"(?P<test>[^:]+):\s*(?P<detail>.*)$",
    re.IGNORECASE,
)
LOCATION = re.compile(
    r"^.*?\.(?:c|cc|cpp|cxx|h|hpp|py):(?P<line>\d+)\s*(?P<message>.*)$",
    re.IGNORECASE,
)
FAILED_HEADER = re.compile(r"^\s*The following tests FAILED:\s*$", re.IGNORECASE)
FAILED_TEST = re.compile(r"^\s*\d+\s*-\s*(?P<test>.+?)\s+\((?P<status>[^)]+)\)\s*$")


def parse_failures(text: str) -> list[dict[str, object]]:
    records: OrderedDict[tuple[str, str], dict[str, object]] = OrderedDict()
    failed_tests: OrderedDict[str, str] = OrderedDict()
    in_failed_list = False

    for raw in text.splitlines():
        match = FAIL.match(raw)
        if match:
            test = match.group("test").strip()
            detail = re.sub(r"\s+", " ", match.group("detail").strip())
            location = LOCATION.match(detail)
            line = location.group("line") if location else None
            message = location.group("message").strip() if location else detail
            message = re.sub(r"\s+", " ", message)
            key = (test, message)
            if key not in records:
                records[key] = {
                    "test": test,
                    "line": line,
                    "message": message,
                    "count": 0,
                }
            records[key]["count"] = int(records[key]["count"]) + 1

        if FAILED_HEADER.match(raw):
            in_failed_list = True
            continue
        if in_failed_list:
            failed_match = FAILED_TEST.match(raw)
            if failed_match:
                failed_tests[failed_match.group("test").strip()] = failed_match.group("status").strip()
            elif raw.strip():
                in_failed_list = False

    detailed_tests = {str(record["test"]) for record in records.values()}
    for test, status in failed_tests.items():
        if test not in detailed_tests:
            records[(test, f"CTest reported {status}")] = {
                "test": test,
                "line": None,
                "message": f"CTest reported {status}",
                "count": 1,
            }
    return list(records.values())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=Path, default=Path("tmp/ctest_debug.log"),
                        help="CTest log (default: tmp/ctest_debug.log)")
    parser.add_argument("-o", "--output", type=Path, default=Path("tmp/ctest_errors.txt"),
                        help="summary file (default: tmp/ctest_errors.txt)")
    args = parser.parse_args()
    input_error: str | None = None
    if not args.input.is_file():
        records = []
        input_error = "input: NOT FOUND"
    else:
        try:
            records = parse_failures(args.input.read_text(encoding="utf-8", errors="replace"))
        except OSError as error:
            records = []
            input_error = f"input: READ FAILED ({error})"

    output = [f"# CTest failure scan: {args.input}", f"unique_failures={len(records)}"]
    if input_error:
        output.append(input_error)
    if records:
        output.append("\nFAILURES")
        for number, record in enumerate(records, 1):
            count = f" [{record['count']}x]" if int(record["count"]) > 1 else ""
            line = f" (line {record['line']})" if record["line"] else ""
            output.append(f"{number}. {record['test']}{count}{line}: {record['message']}")
    result = "SCAN FAILED" if input_error else "FAILURES FOUND" if records else "NO FAILURES DETECTED"
    output.append("\nresult: " + result)
    text = "\n".join(output) + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")
    print(f"output: {args.output}")
    print("result: " + result)
    return 2 if input_error else 1 if records else 0


if __name__ == "__main__":
    raise SystemExit(main())
