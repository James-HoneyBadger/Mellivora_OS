#!/usr/bin/env python3
"""Keep the numbers and absences the docs quote tied to the tree.

The prose that describes this tree drifts in two ways that no link check can
see. A count goes stale when the thing it counts grows ("97 shell built-ins"
outlived the command table by twenty-seven entries), and a "there is no X"
sentence outlives X landing, so the document that is meant to be the contract
ends up denying a feature the kernel implements.

Two rules are checked here, both against a source of truth the build already
owns:

1. **Counts.** A quoted number must equal the number the tree can produce -
   syscalls from `docs/syscalls.json` (cross-checked against the dispatch
   table), built-ins from `kernel/cmdtable.inc` using the same regex the
   command-table check uses, programs and library includes from the file
   system, and the kernel size limit from `KERNEL_MAX_BYTES` in the Makefile.
2. **Absences.** A sentence claiming a feature does not exist is refused once
   the artefact that implements it is in the tree.

Each claim names the file it lives in, so a failure points at the sentence to
edit rather than at a number.

Usage:
    python3 tools/check_doc_claims.py [--root DIR]

Exit: 0 when every claim holds, 1 otherwise.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

# The command-table check owns the definition of "a built-in"; reusing its
# regex means this check counts entries exactly the way `make lint` does.
from check_command_table import ENTRY_RE  # noqa: E402

ROOT = TOOLS.parent


# ----------------------------------------------------------------------
# Sources of truth
# ----------------------------------------------------------------------

def read(root: Path, name: str) -> str:
    path = root / name
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def syscall_count(root: Path) -> int:
    data = json.loads(read(root, "docs/syscalls.json"))
    count = len(data["syscalls"])
    # The generated catalogue and the dispatch table are two views of one list:
    # if they disagree, a claim cannot be checked against either.
    reserved = re.search(r"times \(SYSCALL_MAX - (\d+)\)", read(root, "kernel/syscall.inc"))
    if not reserved or int(reserved.group(1)) != count:
        raise SystemExit(
            "[check-doc-claims] FAIL: docs/syscalls.json and the dispatch table "
            "disagree; run 'make syscalls-json' first"
        )
    return count


def builtin_count(root: Path) -> int:
    return len(ENTRY_RE.findall(read(root, "kernel/cmdtable.inc")))


def nasm_programs(root: Path) -> int:
    return len(list((root / "programs").glob("*.asm")))


def c_programs(root: Path) -> int:
    return len(list((root / "c" / "prog").rglob("*.c")))


def library_includes(root: Path) -> int:
    return len(list((root / "programs" / "lib").glob("*.inc")))


def kernel_limit(root: Path) -> tuple[int, int]:
    match = re.search(r"^KERNEL_MAX_BYTES\s*=\s*(\d+)", read(root, "Makefile"), re.M)
    if not match:
        raise SystemExit("[check-doc-claims] FAIL: Makefile has no KERNEL_MAX_BYTES")
    limit = int(match.group(1))
    return limit // 512, limit // (1024 * 1024)


# ----------------------------------------------------------------------
# The claims
# ----------------------------------------------------------------------

# (file, pattern, source, label). The pattern captures the number(s) the
# document quotes; every match in the file must agree with the source.
COUNT_CLAIMS = [
    ("docs/ROADMAP.md", r"(\d+) syscalls,", syscall_count, "syscall count"),
    ("docs/ROADMAP.md", r"(\d+) shell built-ins", builtin_count, "shell built-ins"),
    ("docs/ROADMAP.md", r"(\d+) NASM programs", nasm_programs, "NASM programs"),
    ("docs/ROADMAP.md", r"(\d+) C program\s+sources", c_programs, "C program sources"),
    ("README.md", r"(\d+) syscalls", syscall_count, "syscall count"),
    ("docs/TECHNICAL_REFERENCE.md", r"Total: (\d+) syscalls defined",
     syscall_count, "syscall count"),
    ("CONTRIBUTING.md", r"(\d+) reusable library includes",
     library_includes, "library includes"),
    ("README.md", r"(\d+) reusable `\.inc`", library_includes, "library includes"),
    # The boot banner, `ver` and the system summary are the first thing a
    # user reads; they quote the syscall count in three places.
    ("kernel/data.inc", r"(\d+) [Ss]yscalls", syscall_count, "syscall count"),
]

# The kernel limit is quoted twice in one sentence: sectors and megabytes.
KERNEL_LIMIT_CLAIM = (
    "CONTRIBUTING.md",
    r"under (\d+) sectors \((\d+) MB\)",
    "kernel size limit",
)

# (document, sentence that must not survive, artefact, pattern in it, feature).
# A document may describe what is missing; it may not deny what is there.
ABSENT_CLAIMS = [
    ("docs/spec/EXEC.md", "No detach, no cancellation",
     "programs/syscalls.inc", r"SYS_THREAD_DETACH", "thread detach"),
    ("docs/spec/ABI.md", "Bare small integers",
     "kernel/object.inc", r"HANDLE_GEN_SHIFT", "generational handles"),
    # "no remote surface" outlived the surface by a whole milestone: `net svc`
    # and the RPC interfaces had shipped and the row still said it.
    ("docs/spec/SERVICES.md", "no remote surface",
     "kernel/share.inc", r"SH_MSG_SVC|cmd_net_svc", "a remote management surface"),
]


def check(root: Path = ROOT) -> list[str]:
    errors: list[str] = []
    values = {
        syscall_count: syscall_count(root),
        builtin_count: builtin_count(root),
        nasm_programs: nasm_programs(root),
        c_programs: c_programs(root),
        library_includes: library_includes(root),
    }

    for path, pattern, source, label in COUNT_CLAIMS:
        text = read(root, path)
        found = re.findall(pattern, text)
        if not found:
            errors.append(f"{path}: no sentence matches {pattern!r} ({label})")
            continue
        want = values[source]
        for got in found:
            quoted = got if isinstance(got, str) else got[0]
            if int(quoted) != want:
                errors.append(f"{path}: says {label} = {quoted}; the tree has {want}")

    path, pattern, label = KERNEL_LIMIT_CLAIM
    found = re.findall(pattern, read(root, path))
    if not found:
        errors.append(f"{path}: no sentence matches {pattern!r} ({label})")
    else:
        want_sectors, want_mb = kernel_limit(root)
        for sectors, mb in found:
            if int(sectors) != want_sectors or int(mb) != want_mb:
                errors.append(
                    f"{path}: says {label} = {sectors} sectors ({mb} MB); "
                    f"the Makefile says {want_sectors} sectors ({want_mb} MB)")

    for doc, sentence, artifact, pattern, feature in ABSENT_CLAIMS:
        if re.search(pattern, read(root, artifact)) and sentence in read(root, doc):
            errors.append(
                f"{doc}: still claims {sentence!r}, but {feature} exists in {artifact}")

    return errors


def main(argv: list[str]) -> int:
    root = Path(argv[argv.index("--root") + 1]) if "--root" in argv else ROOT
    errors = check(root)
    if errors:
        print("[check-doc-claims] FAIL: a document disagrees with the tree")
        for error in errors:
            print(f"  {error}")
        return 1
    print(f"[check-doc-claims] PASS: {len(COUNT_CLAIMS) + 1} counts and "
          f"{len(ABSENT_CLAIMS)} absence claims agree with the tree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
