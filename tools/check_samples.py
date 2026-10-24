#!/usr/bin/env python3
"""Run lightweight syntax checks for source examples in samples/."""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SAMPLES = ROOT / "samples"
TABLE = ROOT / "kernel" / "cmdtable.inc"
DATA = ROOT / "kernel" / "data.inc"
PROGRAMS = ROOT / "programs"
STRUCTURAL = {"then", "else", "do", "elif", "fi", "done", "esac"}


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


def shell_vocabulary() -> set[str]:
    """Built-in names (from cmdtable.inc) plus every program source name."""
    names: set[str] = set()
    if TABLE.is_file():
        data = DATA.read_text(encoding="utf-8")
        display = {
            label: value
            for label, value in re.findall(r'^(cmd_[a-z0-9_]+_str):\s*db\s+"([^"]*)"', data, re.M)
        }
        keyword_names = dict(re.findall(r"^(nm_[a-z0-9_]+):\s*db\s+\"([^\"]*)\"", TABLE.read_text(encoding="utf-8"), re.M))
        for entry, _cat, _sum, _det, alias in re.findall(
            r"^\s*CMDENT\s+([a-z0-9_]+)\s*,\s*(CAT_[A-Z]+)\s*,\s*([a-z0-9_]+)\s*,\s*([a-z0-9_]+)"
            r"[ \t]*(?:;[ \t]*dispatches[ \t]+(cmd_[a-z0-9_]+_str))?",
            TABLE.read_text(encoding="utf-8"),
            re.M,
        ):
            if entry in display:
                names.add(display[entry].strip())
            elif entry in keyword_names:
                names.add(keyword_names[entry].strip())
    for source in PROGRAMS.glob("*.asm"):
        names.add(source.stem)
    return {name for name in names if name}


def check_shell_scripts(paths: list[Path]) -> int:
    """Every command word must exist, and block keywords must balance.

    Counting only the first word of each `;`-separated clause keeps words in
    messages ("done") from being mistaken for keywords.
    """
    if not paths:
        return 0
    vocabulary = shell_vocabulary()
    if not vocabulary:
        print("[samples] SKIP shell checks: no command table found")
        return 0

    for path in paths:
        text = path.read_text(encoding="utf-8")
        local_names = set(re.findall(r"^\s*function\s+([A-Za-z0-9_]+)", text, re.M))
        commands: list[str] = []

        def known(word: str) -> bool:
            return word in vocabulary or word in local_names

        for number, line in enumerate(text.splitlines(), 1):
            stripped = line.strip()
            if not stripped or stripped.upper().startswith("REM ") or stripped.startswith(":"):
                continue
            for clause in stripped.split(";"):
                words = clause.replace("{", " ").replace("}", " ").split()
                # A case arm starts with PATTERN); the command follows it.
                while words and words[0].endswith(")"):
                    words.pop(0)
                if not words:
                    continue
                first = words[0]
                if first in STRUCTURAL:
                    commands.append(first)
                    if first not in ("fi", "done", "esac") and len(words) > 1:
                        if not known(words[1]):
                            raise ValueError(
                                f"{path.relative_to(ROOT)}:{number}: unknown command {words[1]!r}"
                            )
                        commands.append(words[1])
                    continue
                if not known(first):
                    raise ValueError(
                        f"{path.relative_to(ROOT)}:{number}: unknown command {first!r}"
                    )
                commands.append(first)

        for opener, closer in (("do", "done"), ("if", "fi"), ("case", "esac")):
            opened = commands.count(opener)
            closed = commands.count(closer)
            if opened != closed:
                raise ValueError(
                    f"{path.relative_to(ROOT)}: {opened} {opener!r} but {closed} {closer!r}"
                )
        if text.count("{") != text.count("}"):
            raise ValueError(f"{path.relative_to(ROOT)}: unbalanced function braces")

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
        check_shell_scripts(sorted(SAMPLES.glob("*.bat")))
    except (subprocess.CalledProcessError, UnicodeDecodeError, ValueError) as exc:
        print(f"[samples] FAIL {exc}")
        return 1

    print("[samples] All sample checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
