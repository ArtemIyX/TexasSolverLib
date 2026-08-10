#!/usr/bin/env python3
"""Run or parse CTest output and print only actionable test failures."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


SOURCE_LOCATION = re.compile(
    r"^(?P<file>.+?\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inl|py|cs|rs))"
    r"(?:\((?P<msvc_line>\d+)(?:,\d+)?\)|:(?P<unix_line>\d+)(?::\d+)?)"
    r"\s*:?[ \t]*(?P<reason>.*)$",
    re.IGNORECASE,
)
CTEST_PREFIX = re.compile(r"^\s*(?P<number>\d+):\s?(?P<text>.*)$")
CTEST_START = re.compile(r"^\s*Start\s+(?P<number>\d+):\s*(?P<target>\S+)")
CTEST_FAILED = re.compile(
    r"^\s*(?P<number>\d+)\s*-\s*(?P<target>\S+)\s+\(.*?(?:Failed|Timeout|Not Run).*?\)",
    re.IGNORECASE,
)
HARNESS_FAILURE = re.compile(
    r"^\[FAIL\]\s*(?P<name>.*?)(?:\s*:\s*|\s+-\s+)(?P<details>.+)$",
    re.IGNORECASE,
)
GTEST_RUN = re.compile(r"^\[\s*RUN\s*\]\s+(?P<name>.+)$")
GTEST_FAILED = re.compile(r"^\[\s*FAILED\s*\]\s+(?P<name>.+?)(?:\s+\(\d+\s*ms\))?$")
ERROR_WORD = re.compile(
    r"\b(error|failed|failure|assert|expected|exception|abort|crash|segmentation|access violation|timeout)\b",
    re.IGNORECASE,
)
NOISE = re.compile(
    r"^(?:\[\s*(?:OK|PASSED)\s*\]|[-=]+|Running \d+ tests?|\d+% tests passed|The following tests FAILED|\d+/\d+ Test|\s*Start \d+:)",
    re.IGNORECASE,
)


def compact(text: str, limit: int) -> str:
    text = re.sub(r"\s+", " ", text).strip()
    return text if len(text) <= limit else f"{text[:limit - 3]}..."


def parse_location(text: str) -> tuple[str | None, str]:
    match = SOURCE_LOCATION.match(text)
    if not match:
        return None, text.strip()
    line = match.group("msvc_line") or match.group("unix_line")
    reason = re.sub(r"^(?:fatal\s+)?(?:error|failure)\s*:\s*", "", match.group("reason"), flags=re.I).strip()
    return f"{Path(match.group('file')).name}:{line}", reason or "test assertion failed"


def split_contexts(output: str) -> tuple[dict[str, list[str]], dict[str, str], set[str]]:
    lines_by_number: dict[str, list[str]] = defaultdict(list)
    targets: dict[str, str] = {}
    failed_numbers: set[str] = set()
    current = "global"
    for raw_line in output.splitlines():
        start = CTEST_START.match(raw_line)
        if start:
            current = start.group("number")
            targets[current] = start.group("target")
            continue
        summary = CTEST_FAILED.match(raw_line)
        if summary:
            number = summary.group("number")
            targets[number] = summary.group("target")
            failed_numbers.add(number)
            continue
        prefixed = CTEST_PREFIX.match(raw_line)
        if prefixed:
            current = prefixed.group("number")
            line = prefixed.group("text").strip()
        else:
            line = raw_line.strip()
        if line:
            lines_by_number[current].append(line)
    return lines_by_number, targets, failed_numbers


def gtest_record(target: str, name: str, lines: list[str], failed_index: int, limit: int) -> tuple[str, str | None, str]:
    for index in range(failed_index - 1, max(-1, failed_index - 13), -1):
        location, reason = parse_location(lines[index])
        if not location:
            continue
        detail = [reason]
        for following in lines[index + 1:failed_index]:
            if following.startswith("[") or NOISE.match(following):
                continue
            detail.append(following)
            if len(detail) == 3:
                break
        return f"{target}::{name}", location, compact("; ".join(detail), limit)
    return f"{target}::{name}", None, "GoogleTest case failed"


def records_for_context(target: str, lines: list[str], limit: int) -> list[tuple[str, str | None, str]]:
    records: list[tuple[str, str | None, str]] = []
    current_gtest = "unknown GoogleTest case"
    for index, line in enumerate(lines):
        running = GTEST_RUN.match(line)
        if running:
            current_gtest = running.group("name").strip()
            continue
        harness = HARNESS_FAILURE.match(line)
        if harness:
            location, reason = parse_location(harness.group("details"))
            records.append((f"{target}::{harness.group('name').strip()}", location, compact(reason, limit)))
            continue
        gtest = GTEST_FAILED.match(line)
        if gtest:
            name = gtest.group("name").strip()
            if not name.endswith("test, listed below:"):
                records.append(gtest_record(target, name or current_gtest, lines, index, limit))
    return records


def fallback_record(target: str, lines: list[str], limit: int) -> tuple[str, str | None, str]:
    for line in reversed(lines):
        location, reason = parse_location(line)
        if location:
            return target, location, compact(reason, limit)
    for line in reversed(lines):
        if ERROR_WORD.search(line) and not NOISE.match(line):
            return target, None, compact(line, limit)
    return target, None, "test process returned a non-zero exit code"


def print_records(records: list[tuple[str, str | None, str]]) -> None:
    seen: set[tuple[str, str | None, str]] = set()
    for record in records:
        if record in seen:
            continue
        seen.add(record)
        name, location, reason = record
        print(f"[FAIL] {name} : {location} - {reason}" if location else f"[FAIL] {name} - {reason}")


def parse_output(output: str, limit: int, exit_code: int | None = None) -> int:
    contexts, targets, failed_numbers = split_contexts(output)
    records: list[tuple[str, str | None, str]] = []
    for number, lines in contexts.items():
        records.extend(records_for_context(targets.get(number, f"CTest-{number}"), lines, limit))
    for number in failed_numbers:
        target = targets[number]
        if not any(record[0].startswith(f"{target}::") or record[0] == target for record in records):
            records.append(fallback_record(target, contexts.get(number, []), limit))
    if records:
        print_records(records)
        return 1
    if failed_numbers or (exit_code is not None and exit_code != 0):
        print(f"[FAIL] CTest - {fallback_record('CTest', contexts.get('global', []), limit)[2]}")
        return exit_code if exit_code else 1
    print("ALL TESTS PASSED")
    return 0


def run_ctest(build_dir: Path, configuration: str, limit: int) -> int:
    if not build_dir.is_dir():
        print(f"[FAIL] CTest runner - build directory does not exist: {build_dir}")
        return 1
    command = ["ctest", "--test-dir", str(build_dir), "-C", configuration, "--output-on-failure", "--no-tests=error"]
    try:
        completed = subprocess.run(command, text=True, capture_output=True, check=False)
    except OSError as error:
        print(f"[FAIL] CTest runner - {compact(str(error), limit)}")
        return 1
    return parse_output(f"{completed.stdout}\n{completed.stderr}", limit, completed.returncode)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--max-reason-length", type=int, default=300)
    parser.add_argument("--input-file", help="Parse captured CTest output without invoking CTest.")
    args = parser.parse_args()
    if args.max_reason_length < 20:
        parser.error("--max-reason-length must be at least 20")
    if args.input_file:
        try:
            output = Path(args.input_file).read_text(encoding="utf-8")
        except OSError as error:
            print(f"[FAIL] CTest runner - {compact(str(error), args.max_reason_length)}")
            return 1
        return parse_output(output, args.max_reason_length)
    return run_ctest(Path(args.build_dir), args.configuration, args.max_reason_length)


if __name__ == "__main__":
    sys.exit(main())
