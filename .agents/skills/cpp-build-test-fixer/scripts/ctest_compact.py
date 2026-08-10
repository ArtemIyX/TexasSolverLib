#!/usr/bin/env python3
"""Run or parse CTest JUnit results and print only failure diagnostics."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import xml.etree.ElementTree as element_tree
from pathlib import Path
from typing import Iterable


SOURCE_LOCATION = re.compile(
    r"^(?P<file>.+?\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inl|py|cs|rs))"
    r"(?:\((?P<msvc_line>\d+)(?:,\d+)?\)|:(?P<unix_line>\d+)(?::\d+)?)"
    r"\s*:?[ \t]*(?P<reason>.*)$",
    re.IGNORECASE,
)
HARNESS_FAILURE = re.compile(
    r"^\[FAIL\]\s*(?P<name>.*?)(?:\s*:\s*|\s+-\s+)(?P<details>.+)$",
    re.IGNORECASE,
)
ERROR_WORD = re.compile(
    r"\b(error|failed|failure|assert|expected|exception|abort|crash|"
    r"segmentation|access violation)\b",
    re.IGNORECASE,
)
SUMMARY_NOISE = re.compile(
    r"^(?:\[PASS\]|\[\s*(?:RUN|OK)\s*\]|[-=]+|Running \d+ tests?|"
    r"\d+% tests passed|The following tests FAILED|\d+/\d+ Test|\s*Start \d+:)",
    re.IGNORECASE,
)


def local_name(element: element_tree.Element) -> str:
    return element.tag.rsplit("}", 1)[-1]


def compact(text: str, limit: int) -> str:
    normalized = re.sub(r"\s+", " ", text).strip()
    return normalized if len(normalized) <= limit else f"{normalized[:limit - 3]}..."


def meaningful_lines(parts: Iterable[str]) -> list[str]:
    lines: list[str] = []
    for part in parts:
        for line in part.splitlines():
            value = line.strip()
            if value and not SUMMARY_NOISE.match(value):
                lines.append(value)
    return lines


def parse_location(text: str) -> tuple[str | None, str]:
    match = SOURCE_LOCATION.match(text)
    if not match:
        return None, text.strip()
    line = match.group("msvc_line") or match.group("unix_line")
    reason = re.sub(r"^(?:fatal\s+)?(?:error|failure)\s*:\s*", "", match.group("reason"), flags=re.I)
    return f"{Path(match.group('file')).name}:{line}", reason.strip() or "test assertion failed"


def testcase_parts(testcase: element_tree.Element) -> list[str]:
    parts: list[str] = []
    for child in testcase:
        if local_name(child) not in {"failure", "error", "system-out", "system-err"}:
            continue
        message = child.attrib.get("message", "").strip()
        if message:
            parts.append(message)
        if child.text and child.text.strip():
            parts.append(child.text)
    return parts


def is_failed(testcase: element_tree.Element) -> bool:
    status = testcase.attrib.get("status", "").lower()
    if status in {"failed", "error", "notrun", "timeout"}:
        return True
    return any(local_name(child) in {"failure", "error"} for child in testcase)


def records_for_case(testcase: element_tree.Element, limit: int) -> list[tuple[str, str | None, str]]:
    target = testcase.attrib.get("classname", "CTest")
    default_name = testcase.attrib.get("name", "unknown test")
    lines = meaningful_lines(testcase_parts(testcase))
    records: list[tuple[str, str | None, str]] = []

    for line in lines:
        match = HARNESS_FAILURE.match(line)
        if not match:
            continue
        name = match.group("name").strip() or default_name
        location, reason = parse_location(match.group("details").strip())
        records.append((f"{target}::{name}", location, compact(reason, limit)))

    if records:
        return records

    for line in lines:
        location, reason = parse_location(line)
        if location:
            return [(f"{target}::{default_name}", location, compact(reason, limit))]

    for line in reversed(lines):
        if ERROR_WORD.search(line):
            return [(f"{target}::{default_name}", None, compact(line, limit))]

    return [(f"{target}::{default_name}", None, "test process returned a non-zero exit code")]


def print_records(records: list[tuple[str, str | None, str]]) -> None:
    seen: set[tuple[str, str | None, str]] = set()
    for record in records:
        if record in seen:
            continue
        seen.add(record)
        name, location, reason = record
        if location:
            print(f"[FAIL] {name} : {location} - {reason}")
        else:
            print(f"[FAIL] {name} - {reason}")


def parse_junit(path: Path, limit: int) -> int:
    try:
        root = element_tree.parse(path).getroot()
    except (OSError, element_tree.ParseError) as error:
        print(f"[FAIL] CTest runner - cannot parse JUnit result: {compact(str(error), limit)}")
        return 1

    failed_cases = [
        testcase
        for testcase in root.iter()
        if local_name(testcase) == "testcase" and is_failed(testcase)
    ]
    if not failed_cases:
        print("ALL TESTS PASSED")
        return 0

    records: list[tuple[str, str | None, str]] = []
    for testcase in failed_cases:
        records.extend(records_for_case(testcase, limit))
    print_records(records)
    return 1


def last_diagnostic(output: str, limit: int) -> str:
    lines = meaningful_lines([output])
    for line in reversed(lines):
        if ERROR_WORD.search(line):
            return compact(line, limit)
    return compact(lines[-1], limit) if lines else "CTest did not produce JUnit output"


def run_ctest(build_dir: Path, configuration: str, junit_file: Path, limit: int) -> int:
    if not build_dir.is_dir():
        print(f"[FAIL] CTest runner - build directory does not exist: {build_dir}")
        return 1

    junit_file.unlink(missing_ok=True)
    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "-C",
        configuration,
        "-Q",
        "--no-tests=error",
        "--output-junit",
        str(junit_file),
    ]
    try:
        completed = subprocess.run(command, text=True, capture_output=True, check=False)
    except OSError as error:
        print(f"[FAIL] CTest runner - {compact(str(error), limit)}")
        return 1

    output = f"{completed.stdout}\n{completed.stderr}"
    if not junit_file.is_file():
        print(f"[FAIL] CTest - {last_diagnostic(output, limit)}")
        return completed.returncode if completed.returncode != 0 else 1

    parsed_exit = parse_junit(junit_file, limit)
    if parsed_exit == 0 and completed.returncode != 0:
        print(f"[FAIL] CTest - exited with code {completed.returncode} without failed test records")
        return completed.returncode
    return parsed_exit if parsed_exit != 0 else completed.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--max-reason-length", type=int, default=300)
    parser.add_argument(
        "--junit-file",
        help="Parse an existing JUnit file without invoking CTest.",
    )
    args = parser.parse_args()
    if args.max_reason_length < 20:
        parser.error("--max-reason-length must be at least 20")

    if args.junit_file:
        return parse_junit(Path(args.junit_file), args.max_reason_length)

    build_dir = Path(args.build_dir)
    return run_ctest(
        build_dir,
        args.configuration,
        build_dir / "ctest-compact-results.xml",
        args.max_reason_length,
    )


if __name__ == "__main__":
    sys.exit(main())
