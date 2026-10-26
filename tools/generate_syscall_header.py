#!/usr/bin/env python3
"""Generate the C view of the syscall numbers.

programs/syscalls.inc is the one place a syscall number is allocated
(docs/spec/ABI.md section 6). NASM programs include it directly; C programs
cannot, so they get this header, generated from the same file and checked in
CI exactly like docs/syscalls.json. A number that exists on one side and not
the other is then a lint failure instead of a program that calls the wrong
thing.

Usage:
    python3 tools/generate_syscall_header.py           # rewrite the header
    python3 tools/generate_syscall_header.py --check   # fail if it is stale
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "programs" / "syscalls.inc"
OUTPUT = ROOT / "c" / "include" / "mv" / "syscall_numbers.h"

SYSCALL_RE = re.compile(r"^(SYS_[A-Z0-9_]+)\s+equ\s+([0-9]+)\b")


def parse_syscalls(path: Path) -> list[tuple[str, int]]:
    found: list[tuple[str, int]] = []
    seen: dict[str, int] = {}

    for raw in path.read_text(encoding="utf-8").splitlines():
        code = raw.split(";", 1)[0].strip()
        match = SYSCALL_RE.match(code)
        if not match:
            continue
        name = match.group(1)
        number = int(match.group(2))
        if name in seen and seen[name] != number:
            raise SystemExit(f"{path.name}: {name} is defined twice with different numbers")
        seen[name] = number
        found.append((name, number))

    found.sort(key=lambda item: item[1])
    return found


def render(syscalls: list[tuple[str, int]]) -> str:
    lines = [
        "/*",
        " * syscall_numbers.h - generated, do not edit.",
        " *",
        " * Source: programs/syscalls.inc, the one place a syscall number is",
        " * allocated (docs/spec/ABI.md section 6). Regenerate with",
        " * `make syscall-header`; `make check-syscall-header` fails when this file",
        " * and that one disagree.",
        " */",
        "#ifndef MV_SYSCALL_NUMBERS_H",
        "#define MV_SYSCALL_NUMBERS_H",
        "",
    ]
    for name, number in syscalls:
        lines.append(f"#define {name} {number}")
    lines.append("")
    lines.append(f"#define SYS_COUNT {len(syscalls)}")
    lines.append("")
    lines.append("#endif /* MV_SYSCALL_NUMBERS_H */")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the header is stale")
    args = parser.parse_args()

    syscalls = parse_syscalls(SOURCE)
    text = render(syscalls)

    if args.check:
        if not OUTPUT.is_file():
            print(f"[syscall-header] FAIL: {OUTPUT.relative_to(ROOT)} is missing")
            return 1
        if OUTPUT.read_text(encoding="utf-8") != text:
            print(f"[syscall-header] FAIL: {OUTPUT.relative_to(ROOT)} is out of date")
            print("  run: make syscall-header")
            return 1
        print(f"[syscall-header] PASS  {len(syscalls)} numbers match programs/syscalls.inc")
        return 0

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"[syscall-header] wrote {OUTPUT.relative_to(ROOT)} ({len(syscalls)} numbers)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
