#!/usr/bin/env python3
"""Run the full build and test workflow with compact error reports."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = Path(__file__).resolve().parent


def run_script(name: str) -> int:
    return subprocess.run(
        [sys.executable, str(SCRIPTS / name)],
        cwd=ROOT,
    ).returncode


def main() -> int:
    build_errors = ROOT / "tmp" / "build_errors.txt"
    ctest_errors = ROOT / "tmp" / "ctest_errors.txt"

    if run_script("compact_build.py") != 0:
        print("Build Failed, scanning errors..")
        run_script("scan_build_errors.py")
        print(f"Errors: {build_errors}")
        return 1

    print("Build OK")

    if run_script("compact_ctest.py") != 0:
        print("TEST Failed, scanning errors..")
        run_script("scan_ctest_errors.py")
        print(f"Errors: {ctest_errors}")
        return 1

    print("TEST OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
