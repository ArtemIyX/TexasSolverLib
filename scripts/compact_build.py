#!/usr/bin/env python3
"""Run the compact TexasSolver Debug build and save its output."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="repository root (default: script's parent repository)")
    parser.add_argument("-o", "--output", type=Path, default=Path("tmp/build_debug.log"),
                        help="log file (default: tmp/build_debug.log)")
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output if args.output.is_absolute() else root / args.output
    output.parent.mkdir(parents=True, exist_ok=True)

    command = [
        "cmake", "--build", "build", "--config", "Debug", "--parallel", "--",
        "/nologo", "/v:q", "/clp:ErrorsOnly;NoSummary",
    ]
    with output.open("w", encoding="utf-8", newline="") as log:
        process = subprocess.Popen(command, cwd=root, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True,
                                   encoding="utf-8", errors="replace")
        assert process.stdout is not None
        for line in process.stdout:
            log.write(line)
        exit_code = process.wait()

    print(f"\nlog: {output}")
    print(f"result: {'PASS' if exit_code == 0 else 'FAIL'} (exit={exit_code})")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
