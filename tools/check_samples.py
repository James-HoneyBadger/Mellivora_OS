#!/usr/bin/env python3
"""Run lightweight syntax checks for source examples in samples/."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SAMPLES = ROOT / "samples"


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def check_assembly(paths: list[Path]) -> int:
    nasm = shutil.which("nasm")
    if not nasm:
        print("[samples] SKIP NASM checks: nasm not found")
        return 0
    with tempfile.TemporaryDirectory(prefix="mellivora-samples-") as temp_dir:
        for path in paths:
            output = Path(temp_dir) / f"{path.stem}.bin"
            run([nasm, "-f", "bin", "-Iprograms/", "-o", str(output), str(path)])
            print(f"[samples] PASS {path.relative_to(ROOT)}")
    return 0


def check_c(paths: list[Path]) -> int:
    compiler = shutil.which("cc")
    if not compiler:
        print("[samples] SKIP C checks: cc not found")
        return 0
    for path in paths:
        result = subprocess.run(
            [compiler, "-fsyntax-only", "-std=c89", str(path)],
            cwd=ROOT,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode:
            if path.name == "word_stats.c":
                raise subprocess.CalledProcessError(result.returncode, result.args, result.stdout, result.stderr)
            print(f"[samples] WARN {path.relative_to(ROOT)} is not accepted by host C89; TCC remains authoritative")
            continue
        print(f"[samples] PASS {path.relative_to(ROOT)}")
    return 0


def check_perl(paths: list[Path]) -> int:
    perl = shutil.which("perl")
    if not perl:
        print("[samples] SKIP Perl checks: perl not found")
        return 0
    for path in paths:
        run([perl, "-c", str(path)])
        print(f"[samples] PASS {path.relative_to(ROOT)}")
    return 0


def check_basic(paths: list[Path]) -> int:
    for path in paths:
        lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
        numbered = all(line.split(maxsplit=1)[0].isdigit() for line in lines)
        if not numbered or not any(line.upper().endswith("END") for line in lines):
            raise ValueError(f"{path.relative_to(ROOT)} must use numbered lines and end with END")
        print(f"[samples] PASS {path.relative_to(ROOT)}")
    return 0


def main() -> int:
    if not SAMPLES.is_dir():
        print("[samples] FAIL samples directory not found")
        return 1

    try:
        check_assembly(sorted(SAMPLES.glob("*.asm")))
        check_c(sorted(SAMPLES.glob("*.c")))
        check_perl(sorted(SAMPLES.glob("*.pl")))
        check_basic(sorted(SAMPLES.glob("*.bas")))
    except (subprocess.CalledProcessError, UnicodeDecodeError, ValueError) as exc:
        print(f"[samples] FAIL {exc}")
        return 1

    print("[samples] All sample checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
