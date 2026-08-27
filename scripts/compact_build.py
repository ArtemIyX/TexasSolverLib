#!/usr/bin/env python3
"""Run the compact TexasSolver Debug build and save its output."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


def build_command(root: Path) -> list[str]:
    if os.name != "nt":
        return [
            "cmake", "--build", "build", "--config", "Debug", "--parallel", "--",
            "/nologo", "/v:q", "/clp:ErrorsOnly;NoSummary",
        ]

    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(program_files) / "Microsoft Visual Studio/Installer/vswhere.exe"
    query = subprocess.run(
        [str(vswhere), "-latest", "-products", "*",
         "-requires", "Microsoft.Component.MSBuild",
         "-find", r"MSBuild\**\Bin\MSBuild.exe"],
        capture_output=True,
        text=True,
        check=True,
    )
    msbuild = query.stdout.splitlines()[0]
    return [
        msbuild, str(root / "build/ALL_BUILD.vcxproj"),
        "/p:Configuration=Debug", "/p:Platform=x64", "/m",
        "/nologo", "/v:q", "/clp:ErrorsOnly;NoSummary",
    ]


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

    command = build_command(root)
    environment = {
        key: value for key, value in os.environ.items()
        if key.casefold() != "path"
    }
    environment["Path"] = os.environ.get("PATH", os.environ.get("Path", ""))
    with output.open("w", encoding="utf-8", newline="") as log:
        process = subprocess.Popen(command, cwd=root, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True,
                                   encoding="utf-8", errors="replace",
                                   env=environment)
        assert process.stdout is not None
        for line in process.stdout:
            log.write(line)
        exit_code = process.wait()

    print(f"\nlog: {output}")
    print(f"result: {'PASS' if exit_code == 0 else 'FAIL'} (exit={exit_code})")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
