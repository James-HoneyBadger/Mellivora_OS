#!/usr/bin/env python3
"""Validate syscall number consistency across the kernel and the user header.

Three sources must agree:

  1. kernel.asm            - SYS_* equ declarations (kernel-side symbols)
  2. programs/syscalls.inc - SYS_* equ declarations (user-side; also the source
                             that docs/syscalls.json and docs/syscalls.md are
                             generated from)
  3. kernel/syscall.inc    - the POSITIONAL dispatch table, whose ROW INDEX is
                             the syscall number

(3) is the one that actually decides which handler runs, and it used to be
unvalidated by anything: a row inserted at the wrong position, duplicated, or
missing dispatched to the wrong handler (or off the end of the table) while
every other check in this repo still passed.  Numbers 103-180 were not even
declared in kernel.asm, so the previous version of this script skipped them.

Checks performed:

  A. every SYS_* declared in kernel.asm also exists in programs/syscalls.inc,
     with the same number
  B. every dispatch-table row sits at the position its comment claims
  C. every dispatch-table row names a SYS_* that programs/syscalls.inc declares
     at that same number
  D. no duplicate numbers and no duplicate handler symbols in the table
  E. the `times (SYSCALL_MAX - N)` filler count matches the real row count
  F. every SYS_* declared in programs/syscalls.inc has a matching table row
     (catches "documented but unimplemented")
  G. no SYS_* number is >= SYSCALL_MAX
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KERNEL_PATH = ROOT / "kernel.asm"
KERNEL_SYSCALL_PATH = ROOT / "kernel" / "syscall.inc"
USER_SYSCALLS_PATH = ROOT / "programs" / "syscalls.inc"

#   SYS_FOO equ 12
SYSCALL_RE = re.compile(r"^(SYS_[A-Z0-9_]+)\s+equ\s+([0-9]+)\b")

#   dd sys_exit             ;  0  SYS_EXIT
#   dd sys_defrag           ; 104 SYS_DEFRAG           (HBFS v2)
TABLE_ROW_RE = re.compile(
    r"^\s*dd\s+([A-Za-z_][A-Za-z0-9_]*)\s*;\s*([0-9]+)\s+(SYS_[A-Z0-9_]+)\b"
)

#   times (SYSCALL_MAX - 182) dd syscall_handler.syscall_bad
FILLER_RE = re.compile(
    r"^\s*times\s*\(\s*SYSCALL_MAX\s*-\s*([0-9]+)\s*\)\s*dd\s+([A-Za-z_][A-Za-z0-9_.]*)\s*$"
)

SYSCALL_MAX_RE = re.compile(r"^\s*SYSCALL_MAX\s+equ\s+([0-9]+)\b", re.MULTILINE)

#   ENOENT equ 2
ERRNO_RE = re.compile(r"^(E[A-Z0-9_]+)\s+equ\s+([0-9]+)\b")

# The errno contract.  Listed explicitly rather than sniffed with a regex so
# that unrelated kernel constants starting with E (ELF_MAGIC, ...) cannot leak
# in, and so the list doubles as documentation of what is promised.
ERRNO_NAMES = {
    "EPERM", "ENOENT", "ESRCH", "EINTR", "EIO", "ENXIO", "ENOEXEC",
    "EBADF", "ECHILD", "EAGAIN", "ENOMEM", "EACCES", "EFAULT", "EBUSY",
    "EEXIST", "EXDEV", "ENODEV", "ENOTDIR", "EISDIR", "EINVAL", "ENFILE",
    "EMFILE", "ENOTTY", "EFBIG", "ENOSPC", "ESPIPE", "EROFS", "EMLINK",
    "EPIPE", "ERANGE", "ENAMETOOLONG", "ENOSYS", "ENOTEMPTY", "ELOOP",
}


def parse_named_constants(path: Path, names: set[str]) -> dict[str, int]:
    """Return {NAME: value} for `NAME equ N` lines whose NAME is in `names`."""
    values: dict[str, int] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split(";", 1)[0].strip()
        m = ERRNO_RE.match(line)
        if m and m.group(1) in names:
            values[m.group(1)] = int(m.group(2))
    return values


def parse_syscalls(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split(";", 1)[0].strip()
        m = SYSCALL_RE.match(line)
        if not m:
            continue
        values[m.group(1)] = int(m.group(2))
    return values


def parse_table(path: Path):
    """Parse the positional dispatch table out of kernel/syscall.inc.

    Returns (rows, filler) where rows are (handler_symbol, number, sys_name,
    line_number) in file order, and filler is (base_number, target_symbol,
    line_number) or None.
    """
    lines = path.read_text(encoding="utf-8").splitlines()

    start = None
    for i, line in enumerate(lines):
        if line.strip().startswith("syscall_table:"):
            start = i
            break
    if start is None:
        raise SystemExit(
            "[validate-syscalls] FAIL\n"
            f"  - no `syscall_table:` label found in {path.relative_to(ROOT)}"
        )

    rows: list[tuple[str, int, str, int]] = []
    filler: tuple[int, str, int] | None = None

    for i in range(start + 1, len(lines)):
        line = lines[i]
        stripped = line.strip()

        # Blank lines and pure comments inside the table are fine.
        if stripped == "" or stripped.startswith(";"):
            continue

        m = FILLER_RE.match(line)
        if m:
            filler = (int(m.group(1)), m.group(2), i + 1)
            break

        m = TABLE_ROW_RE.match(line)
        if m:
            rows.append((m.group(1), int(m.group(2)), m.group(3), i + 1))
            continue

        # Anything else ends the table.
        break

    return rows, filler


def main() -> int:
    kernel_sys = parse_syscalls(KERNEL_PATH)
    user_sys = parse_syscalls(USER_SYSCALLS_PATH)
    rows, filler = parse_table(KERNEL_SYSCALL_PATH)

    m = SYSCALL_MAX_RE.search(KERNEL_SYSCALL_PATH.read_text(encoding="utf-8"))
    if not m:
        raise SystemExit(
            "[validate-syscalls] FAIL\n  - no `SYSCALL_MAX equ N` found in "
            f"{KERNEL_SYSCALL_PATH.relative_to(ROOT)}"
        )
    syscall_max = int(m.group(1))

    errors: list[str] = []

    # --- A. kernel.asm <-> user header --------------------------------
    missing_in_user = sorted(set(kernel_sys) - set(user_sys))
    if missing_in_user:
        errors.append(
            "Kernel-only syscall symbols not present in programs/syscalls.inc: "
            + ", ".join(missing_in_user)
        )
    for name in sorted(set(kernel_sys) & set(user_sys)):
        if kernel_sys[name] != user_sys[name]:
            errors.append(
                f"Mismatch for {name}: kernel.asm={kernel_sys[name]} "
                f"programs/syscalls.inc={user_sys[name]}"
            )

    # --- B/C/D. dispatch table ---------------------------------------
    seen_numbers: dict[int, int] = {}
    seen_symbols: dict[str, int] = {}

    for index, (symbol, number, sys_name, line_no) in enumerate(rows):
        # B: the table is positional, so row `index` IS syscall `index`.
        if number != index:
            errors.append(
                f"kernel/syscall.inc:{line_no}: row {index} is annotated {number} "
                f"({sys_name}, handler {symbol}); the table is POSITIONAL, so "
                f"syscall {index} would dispatch to the wrong handler"
            )

        # D: duplicates.
        if number in seen_numbers:
            errors.append(
                f"kernel/syscall.inc:{line_no}: duplicate table number {number} "
                f"({sys_name}; first seen at line {seen_numbers[number]})"
            )
        else:
            seen_numbers[number] = line_no

        if symbol in seen_symbols:
            errors.append(
                f"kernel/syscall.inc:{line_no}: duplicate handler symbol "
                f"{symbol} (first seen at line {seen_symbols[symbol]})"
            )
        else:
            seen_symbols[symbol] = line_no

        # C: the annotated SYS_* must be declared at that number.
        declared = user_sys.get(sys_name)
        if declared is None:
            errors.append(
                f"kernel/syscall.inc:{line_no}: table names {sys_name}, which "
                f"programs/syscalls.inc does not declare"
            )
        elif declared != number:
            errors.append(
                f"kernel/syscall.inc:{line_no}: table row {number} names "
                f"{sys_name}, but programs/syscalls.inc declares it as {declared}"
            )

    # --- E. filler count ---------------------------------------------
    if filler is None:
        errors.append(
            "kernel/syscall.inc: no `times (SYSCALL_MAX - N) dd ...syscall_bad` "
            "filler found; the table cannot be size-checked"
        )
    else:
        base, target, line_no = filler
        if base != len(rows):
            errors.append(
                f"kernel/syscall.inc:{line_no}: filler reserves "
                f"{syscall_max - base} entries starting at {base}, but there are "
                f"{len(rows)} real rows, so the table has "
                f"{len(rows) + (syscall_max - base)} entries, not SYSCALL_MAX"
            )
        if not target.endswith("syscall_bad"):
            errors.append(
                f"kernel/syscall.inc:{line_no}: filler target {target} is not the "
                f"invalid-syscall handler"
            )

    # --- F. declared but unimplemented -------------------------------
    table_numbers = set(seen_numbers)
    for name in sorted(user_sys, key=lambda n: user_sys[n]):
        if user_sys[name] not in table_numbers:
            errors.append(
                f"{name} = {user_sys[name]} is declared in programs/syscalls.inc "
                f"but has no row in the kernel dispatch table"
            )

    # --- G. range ----------------------------------------------------
    for name, number in sorted(user_sys.items(), key=lambda kv: kv[1]):
        if number >= syscall_max:
            errors.append(f"{name} = {number} is >= SYSCALL_MAX ({syscall_max})")

    # --- H. errno constants ------------------------------------------
    # A program can only compare against errno if both sides agree on the
    # values, so the two headers must not drift.
    kernel_errno = parse_named_constants(KERNEL_PATH, ERRNO_NAMES)
    user_errno = parse_named_constants(USER_SYSCALLS_PATH, ERRNO_NAMES)

    for name in sorted(ERRNO_NAMES):
        if name in kernel_errno and name not in user_errno:
            errors.append(
                f"errno {name} = {kernel_errno[name]} is defined in kernel.asm "
                f"but not in programs/syscalls.inc, so user code cannot compare "
                f"against it"
            )
        elif name in kernel_errno and name in user_errno:
            if kernel_errno[name] != user_errno[name]:
                errors.append(
                    f"errno mismatch for {name}: kernel.asm={kernel_errno[name]} "
                    f"programs/syscalls.inc={user_errno[name]}"
                )

    # --- report ------------------------------------------------------
    if errors:
        print("[validate-syscalls] FAIL")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("[validate-syscalls] PASS")
    print(f"  compared {len(kernel_sys)} kernel.asm symbols against the user header")
    print(f"  verified {len(rows)} dispatch-table rows (positional, 0..{len(rows) - 1})")
    print(f"  SYSCALL_MAX = {syscall_max}; {syscall_max - len(rows)} free slots")
    print(f"  errno constants agreed: {len(kernel_errno)} in kernel.asm")
    return 0


if __name__ == "__main__":
    sys.exit(main())
