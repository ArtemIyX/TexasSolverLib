#!/usr/bin/env python3
"""Run CTest, save the full transcript, and print only useful failures."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path

FAILED_HEADER = re.compile(r"The following tests FAILED:", re.I)
FAILED_TEST = re.compile(r"^\s*\d+\s*-\s*(.+?)\s*\((.*)\)\s*$")
TEST_FAILURE = re.compile(r"(?:FAILED|FAILURE|assert(?:ion)? failed|exception|error)\b", re.I)


def summarize(text: str) -> list[str]:
    lines = text.splitlines()
    failures: list[str] = []
    in_failed_list = False
    current: str | None = None
    for line in lines:
        if FAILED_HEADER.search(line):
            in_failed_list = True
            continue
        if in_failed_list:
            match = FAILED_TEST.match(line)
            if match:
                failures.append(f"{match.group(1)}: {match.group(2)}")
                continue
            if line.strip() and not line.startswith((" ", "\t")):
                in_failed_list = False
        if current and line.strip() and TEST_FAILURE.search(line):
            failures.append(f"{current}: {line.strip()}")
            current = None
        elif re.search(r"\b(?:Test|test)\s+#?\d+", line):
            current = line.strip()
    return list(dict.fromkeys(failures))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("-o", "--output", type=Path, default=Path("tmp/ctest_debug.log"),
                        help="full CTest log (default: tmp/ctest_debug.log)")
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output if args.output.is_absolute() else root / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    command = ["ctest", "--test-dir", "build", "-C", "Debug", "--output-on-failure", "--parallel"]
    result = subprocess.run(command, cwd=root, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    transcript = result.stdout + result.stderr
    output.write_text(transcript, encoding="utf-8")
    print(f"log: {output}")
    if result.returncode == 0:
        print("result: ALL TESTS PASSED")
    else:
        print(f"result: FAILED (exit={result.returncode})")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
