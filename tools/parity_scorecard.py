#!/usr/bin/env python3
"""parity_scorecard.py - the Windows 2000 parity scorecard, as a test.

docs/ROADMAP.md defines parity as fourteen user jobs rather than a list of APIs.
This tool is the executable form of that table. Each row names the evidence that
would make it pass, and the status is computed from the tree instead of being
written down by hand:

    pass      every required piece of evidence exists
    partial   some of it exists; the row lists what is still missing
    fail      none of it exists

`--check` compares the result with docs/parity-baseline.json and fails when a row
is worse than the recorded baseline, so losing a capability is caught the same
way a broken build is. `--update-baseline` records the current answer, and is
the only way a row may improve: the baseline is reviewed with the change that
earns it.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "docs" / "parity-baseline.json"
SYSCALLS = ROOT / "docs" / "syscalls.json"

STATUS_RANK = {"fail": 0, "partial": 1, "pass": 2}


@dataclass
class Row:
    number: int
    job: str
    criteria: str
    required: list[tuple] = field(default_factory=list)
    evidence: list[tuple] = field(default_factory=list)


class Tree:
    """Cached views of the repository, so fourteen rows cost one read each."""

    def __init__(self, root: Path = ROOT):
        self.root = root
        self._syscalls: set[str] | None = None
        self._kernel: str | None = None
        self._files: dict[str, str] = {}

    def syscalls(self) -> set[str]:
        if self._syscalls is None:
            data = json.loads((self.root / "docs" / "syscalls.json").read_text(encoding="utf-8"))
            self._syscalls = {entry["name"] for entry in data["syscalls"]}
        return self._syscalls

    def kernel(self) -> str:
        if self._kernel is None:
            parts = [(self.root / "kernel.asm").read_text(encoding="utf-8", errors="replace")]
            for path in sorted((self.root / "kernel").glob("*.inc")):
                parts.append(path.read_text(encoding="utf-8", errors="replace"))
            self._kernel = "\n".join(parts)
        return self._kernel

    def file(self, name: str) -> str:
        if name not in self._files:
            path = self.root / name
            self._files[name] = (path.read_text(encoding="utf-8", errors="replace")
                                 if path.is_file() else "")
        return self._files[name]


def present(tree: Tree, probe: tuple) -> bool:
    kind = probe[0]
    if kind == "file":
        return (tree.root / probe[1]).is_file()
    if kind == "program":
        return (tree.root / "programs" / f"{probe[1]}.asm").is_file()
    if kind == "syscall":
        return probe[1] in tree.syscalls()
    if kind == "text":
        return re.search(probe[2], tree.file(probe[1])) is not None
    if kind == "kernel":
        return re.search(probe[1], tree.kernel()) is not None
    raise ValueError(f"unknown probe kind: {kind}")


def readable(pattern: str) -> str:
    """Turn an alternation of word-anchored regexes into something a person reads."""
    parts = [re.sub(r"\\b", "", alt) for alt in pattern.split("|")]
    return " or ".join(part for part in parts if part)


def describe(probe: tuple) -> str:
    kind = probe[0]
    if kind == "file":
        return probe[1]
    if kind == "program":
        return f"programs/{probe[1]}.asm"
    if kind == "syscall":
        return probe[1]
    if kind == "text":
        return f"{probe[1]}: {readable(probe[2])}"
    return f"kernel: {readable(probe[1])}"


# The fourteen rows of docs/ROADMAP.md, in the same order. `required` is what
# makes a row pass; `evidence` is what makes it partial. Every probe names a
# real artefact, so a row can only move when something a user could exercise
# exists in the tree.
SCORECARD: list[Row] = [
    Row(1, "Log on",
        "Logon screen, per-user profile and desktop, switch user without reboot",
        required=[("kernel", r"\blogon_user\b"), ("syscall", "SYS_LOGON")],
        evidence=[("kernel", r"\bsession_id\b")]),
    Row(2, "Manage users",
        "Accounts, groups, passwords, quotas and permissions edited from a GUI",
        required=[("syscall", "SYS_GETUID"), ("syscall", "SYS_CHMOD"),
                  ("kernel", r"\bacl_check\b|\baccess_check\b")],
        evidence=[("syscall", "SYS_GETUID"), ("syscall", "SYS_CHMOD"),
                  ("syscall", "SYS_SETUID")]),
    Row(3, "Run services",
        "Services start before logon, have dependencies and restart on failure",
        required=[("kernel", r"\bscm_start\b"), ("kernel", r"\bservice_depend\b")],
        evidence=[("kernel", r"\bsvc_\w+\b")]),
    Row(4, "Configure the machine",
        "One hierarchical transactional store for kernel, service and user settings",
        required=[("kernel", r"\bsett_open\b"), ("kernel", r"\bsett_commit\b")],
        evidence=[("kernel", r"\bsett_\w+\b")]),
    Row(5, "Install software",
        "Install and uninstall with dependencies, registration and clean removal",
        required=[("file", "tools/hbpkg.c"), ("syscall", "SYS_PKG_INSTALL"),
                  ("program", "uninstall")],
        evidence=[("file", "tools/hbpkg.c"), ("program", "pkg"), ("program", "pkginfo")]),
    Row(6, "Use the desktop",
        "Multi-app shell, taskbar, file manager, drag-drop, clipboard, associations",
        required=[("text", "kernel/burrows.inc", r"\btaskbar\b"),
                  ("syscall", "SYS_SURFACE_CREATE"),
                  ("syscall", "SYS_CLIPBOARD_COPY"),
                  ("kernel", r"\bshell_namespace\b|\bfile_assoc\b")],
        evidence=[("text", "kernel/burrows.inc", r"\btaskbar\b"),
                  ("syscall", "SYS_SURFACE_CREATE")]),
    Row(7, "Multitask hard",
        "Preemptive, protected, per-process memory; a hung app cannot take the machine down",
        required=[("syscall", "SYS_MMAP"), ("syscall", "SYS_MPROTECT"),
                  ("syscall", "SYS_FORK"),
                  ("text", "kernel/paging.inc", r"paging_clone_pd|paging_free_pd")],
        evidence=[("syscall", "SYS_MMAP")]),
    Row(8, "Network",
        "TCP/IP, DHCP, DNS, and a shared folder another machine mounts",
        required=[("syscall", "SYS_SOCKET"), ("kernel", r"\bdhcp_"),
                  ("kernel", r"\bshare_mount\b|\bshare_serve\b")],
        evidence=[("syscall", "SYS_SOCKET"), ("kernel", r"\bdhcp_")]),
    Row(9, "Print",
        "Spooler with queued, cancellable jobs",
        required=[("kernel", r"\bspool_enqueue\b"), ("kernel", r"\bspool_cancel\b")],
        evidence=[("kernel", r"\bspool_\w+\b")]),
    Row(10, "Detect hardware",
        "ACPI, PnP enumeration, power states, sleep and soft-off",
        required=[("kernel", r"\bacpi_"), ("kernel", r"\blapic_|\bioapic_"),
                  ("kernel", r"\bpnp_")],
        evidence=[("kernel", r"\bpnp_\w+\b")]),
    Row(11, "Survive failure",
        "Crash dump, safe mode, recovery console, filesystem repair and restore points",
        required=[("kernel", r"\bcrash_dump\b|\bkernel_dump\b"),
                  ("kernel", r"\bsafe_mode\b")],
        evidence=[("kernel", r"\bhbfs_journal\b|\bjournal_"),
                  ("file", "tools/crash_inject.py")]),
    Row(12, "Develop for it",
        "Documented driver model and SDK, shared libraries, a C runtime, samples",
        required=[("program", "tcc"), ("kernel", r"\bpe_load\b|\bIMAGE_DOS\b"),
                  ("kernel", r"\bdll_load\b")],
        evidence=[("program", "tcc"), ("text", "kernel.asm", r"ELF_MAGIC")]),
    Row(13, "Administer remotely",
        "Remote console, remote service and configuration management",
        required=[("kernel", r"\bremote_admin\b"), ("kernel", r"\brpc_")],
        evidence=[("kernel", r"\bremote_\w+\b")]),
    Row(14, "Play media",
        "Mixed audio from several programs, video playback, a media framework",
        required=[("syscall", "SYS_AUDIO_OPEN"), ("kernel", r"\baudio_mix\b"),
                  ("kernel", r"\bvideo_play\b|\bvideo_decode\b")],
        evidence=[("syscall", "SYS_AUDIO_OPEN"), ("syscall", "SYS_AUDIO_PLAY")]),
]


def evaluate(tree: Tree, row: Row) -> tuple[str, list[str]]:
    """Return the row's status and the evidence it is still missing."""
    missing = [describe(p) for p in row.required if not present(tree, p)]
    if not missing:
        return "pass", []
    found = [p for p in row.evidence if present(tree, p)]
    if found or len(missing) < len(row.required):
        return "partial", missing
    return "fail", missing


def score(tree: Tree) -> list[dict]:
    rows = []
    for row in SCORECARD:
        status, missing = evaluate(tree, row)
        rows.append({"number": row.number, "job": row.job, "criteria": row.criteria,
                     "status": status, "missing": missing})
    return rows


def summarise(rows: list[dict]) -> dict:
    counts = {"pass": 0, "partial": 0, "fail": 0}
    for row in rows:
        counts[row["status"]] += 1
    counts["total"] = len(rows)
    return counts


def print_table(rows: list[dict]) -> None:
    width = max(len(r["job"]) for r in rows)
    print("Mellivora parity scorecard — the fourteen jobs of docs/ROADMAP.md")
    print()
    print(f" #  {'job':<{width}}  {'status':<8}  still missing")
    print(f"──  {'─' * width}  {'─' * 8}  {'─' * 40}")
    for row in rows:
        missing = "; ".join(row["missing"]) if row["missing"] else "-"
        print(f"{row['number']:>2}  {row['job']:<{width}}  {row['status']:<8}  {missing}")
    counts = summarise(rows)
    print()
    print(f"{counts['pass']} pass, {counts['partial']} partial, {counts['fail']} fail"
          f"  ({counts['total']} rows)")


def shown(path: Path) -> str:
    """The path for a message: relative when it is inside the tree."""
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def load_baseline() -> dict[int, str] | None:
    if not BASELINE.is_file():
        return None
    data = json.loads(BASELINE.read_text(encoding="utf-8"))
    return {row["number"]: row["status"] for row in data["rows"]}


def write_baseline(rows: list[dict]) -> None:
    payload = {"generated_by": "tools/parity_scorecard.py --update-baseline",
               "rows": [{"number": r["number"], "job": r["job"], "status": r["status"]}
                        for r in rows]}
    BASELINE.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {shown(BASELINE)}")


def check(rows: list[dict]) -> int:
    baseline = load_baseline()
    if baseline is None:
        print(f"[parity] FAIL  no baseline at {shown(BASELINE)}; "
              "run 'make check-parity-update'")
        return 1

    problems = []
    for row in rows:
        recorded = baseline.get(row["number"])
        if recorded is None:
            problems.append(f"row {row['number']} ({row['job']}) is not in the baseline")
            continue
        if STATUS_RANK[row["status"]] < STATUS_RANK[recorded]:
            problems.append(
                f"row {row['number']} ({row['job']}) regressed: "
                f"{recorded} -> {row['status']}")

    new_rows = [r for r in rows if r["number"] not in baseline]
    for row in new_rows:
        problems.append(f"row {row['number']} ({row['job']}) is new; "
                        "update the baseline in the same change")

    if problems:
        print("[parity] FAIL")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    counts = summarise(rows)
    print(f"[parity] PASS  {counts['pass']} pass, {counts['partial']} partial, "
          f"{counts['fail']} fail ({counts['total']} rows)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="fail when a row is worse than docs/parity-baseline.json")
    parser.add_argument("--update-baseline", action="store_true",
                        help="record the current statuses as the baseline")
    parser.add_argument("--json", action="store_true", help="print the scorecard as JSON")
    args = parser.parse_args()

    rows = score(Tree())

    if args.update_baseline:
        write_baseline(rows)
        return 0
    if args.json:
        print(json.dumps({"rows": rows, "summary": summarise(rows)}, indent=2))
        return 0
    if args.check:
        return check(rows)

    print_table(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
