#!/usr/bin/env python3
"""Validate syscall number consistency between kernel and user headers."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KERNEL_PATH = ROOT / "kernel.asm"
USER_SYSCALLS_PATH = ROOT / "programs" / "syscalls.inc"

SYSCALL_RE = re.compile(r"^(SYS_[A-Z0-9_]+)\s+equ\s+([0-9]+)\b")


def parse_syscalls(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split(";", 1)[0].strip()
        m = SYSCALL_RE.match(line)
        if not m:
            continue
        values[m.group(1)] = int(m.group(2))
    return values


def main() -> int:
    kernel_sys = parse_syscalls(KERNEL_PATH)
    user_sys = parse_syscalls(USER_SYSCALLS_PATH)

    errors: list[str] = []

    missing_in_user = sorted(set(kernel_sys) - set(user_sys))
    if missing_in_user:
        errors.append(
            "Kernel-only syscall symbols not present in programs/syscalls.inc: "
            + ", ".join(missing_in_user)
        )

    for name in sorted(set(kernel_sys) & set(user_sys)):
        if kernel_sys[name] != user_sys[name]:
            errors.append(
                f"Mismatch for {name}: kernel={kernel_sys[name]} user={user_sys[name]}"
            )

    if errors:
        print("[validate-syscalls] FAIL")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("[validate-syscalls] PASS")
    print(f"  compared {len(kernel_sys)} kernel syscall symbols against user header")
    return 0


if __name__ == "__main__":
    sys.exit(main())
