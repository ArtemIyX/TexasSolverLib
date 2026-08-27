#!/usr/bin/env python3
"""Reduce build, test, or runtime logs to unique actionable failures."""

from __future__ import annotations

import argparse
import re
from collections import Counter
from pathlib import Path

LOCATION = re.compile(r"(?P<file>[A-Za-z0-9_./\\-]+\.(?:cpp|cc|cxx|h|hpp|py|cmake))(?::(?P<line>\d+))?(?::(?P<col>\d+))?")
TEST = re.compile(r"(?:FAILED|Failure|FAIL)\s*(?:Test|test)?\s*[:=]?\s*([A-Za-z0-9_./:-]+)", re.I)
ERROR = re.compile(r"\b(?:error|fatal error|exception|assert(?:ion)? failed|segmentation fault|access violation|undefined reference|FAILED|FAIL)\b", re.I)
WARNING = re.compile(r"\bwarning\b", re.I)
NOISE = re.compile(r"^(?:\s*(?:Build (?:started|finished)|Done Building|Target .* up-to-date|\d+>\s*(?:Checking|Building|Linking)|\s*\[={2,}.*))", re.I)
PROJECT_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT_TEXT = str(PROJECT_ROOT).replace("/", "\\")
VCXPROJ_PATH = re.compile(r"\[[^\]\r\n]*[\\/]([^\\/\]]+\.vcxproj)\]", re.I)


def normalize(text: str) -> str:
    text = re.sub(r"\b\d+(?:\.\d+)?(?:ms|s)\b", "<time>", text)
    text = re.sub(r"0x[0-9a-f]+", "<addr>", text, flags=re.I)
    text = re.sub(r"\s+", " ", text).strip()
    return text


def display_line(line: str, location: str) -> str:
    """Make compiler output paths concise and avoid printing the source twice."""
    line = VCXPROJ_PATH.sub(r"[\1]", line)
    line = line.replace(PROJECT_ROOT_TEXT, "")
    line = line.replace(PROJECT_ROOT_TEXT.replace("\\", "/"), "")
    if location:
        relative_location = location.replace(PROJECT_ROOT_TEXT, "")
        relative_location = relative_location.replace(PROJECT_ROOT_TEXT.replace("\\", "/"), "")
        line = re.sub(rf"^\s*{re.escape(relative_location)}\s+", "", line)
    line = re.sub(r"^\s*(?:[A-Za-z]:)?[\\/]Users[\\/][^\s]+\.(?:cpp|cc|cxx|h|hpp)\s+",
                  "", line, count=1, flags=re.I)
    return line.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, nargs="?", default=Path("tmp/build_debug.log"),
                        help="log file to scan (default: tmp/build_debug.log)")
    parser.add_argument("-o", "--output", type=Path, default=Path("tmp/build_errors.txt"),
                        help="report path (default: tmp/build_errors.txt)")
    parser.add_argument("--warnings", action="store_true", help="include warnings after failures")
    parser.add_argument("--last", type=int, metavar="N", help="show only the last N relevant unique messages")
    parser.add_argument("--context", type=int, default=1, metavar="N", help="nearby log lines per issue (default: 1)")
    args = parser.parse_args()
    input_exists = args.input.is_file()
    lines = (args.input.read_text(encoding="utf-8", errors="replace").splitlines()
             if input_exists else [])
    records: dict[str, dict] = {}
    order: list[str] = []
    for index, line in enumerate(lines):
        if NOISE.match(line) or (not ERROR.search(line) and not (args.warnings and WARNING.search(line))):
            continue
        kind = "warning" if WARNING.search(line) and not ERROR.search(line) else "failure"
        match = LOCATION.search(line)
        location = f"{match.group('file')}:{match.group('line')}" if match and match.group("line") else ""
        location = location.replace(PROJECT_ROOT_TEXT, "")
        location = location.replace(PROJECT_ROOT_TEXT.replace("\\", "/"), "")
        test = TEST.search(line)
        key = normalize(re.sub(r"\s+", " ", line))
        if key not in records:
            records[key] = {"kind": kind, "line": display_line(line, location), "location": location,
                            "test": test.group(1) if test else "", "count": 0,
                            "index": index, "context": []}
            order.append(key)
        record = records[key]
        record["count"] += 1
        if len(record["context"]) < args.context:
            record["context"].append(display_line(line, location))
    failures = [records[key] for key in order if records[key]["kind"] == "failure"]
    warnings = [records[key] for key in order if records[key]["kind"] == "warning"]
    failures.sort(key=lambda record: record["index"])
    if args.last:
        failures = failures[-args.last:]
    output: list[str] = [f"# Log scan: {args.input}"]
    if not input_exists:
        output.append("input: NOT FOUND (nothing to scan)")
    output.append(f"relevant={len(failures)} unique_failures={len(failures)}")
    for title, records_to_print in (("FAILURES", failures), ("WARNINGS", warnings if args.warnings else [])):
        if not records_to_print:
            continue
        output.append(f"\n## {title}")
        for number, record in enumerate(records_to_print, 1):
            prefix = f"{number}. [{record['count']}x]"
            if record["location"]:
                prefix += f" {record['location']}"
            if record["test"]:
                prefix += f" test={record['test']}"
            output.append(f"{prefix} {record['line']}")
            for context in record["context"][:args.context]:
                if context != record["line"].strip():
                    output.append(f"   | {context}")
    output.append("\nresult: " + ("FAILURES FOUND" if failures else "NO FAILURES DETECTED"))
    text = "\n".join(output) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
