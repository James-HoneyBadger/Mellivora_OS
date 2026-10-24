#!/usr/bin/env python3
"""Keep the shell's command table, its dispatcher and its help text in step.

`kernel/cmdtable.inc` is meant to be the single list of built-ins: `help`, tab
completion and `which` all read it. That only holds if the dispatcher never
grows a command the table does not know about, so this check compares the two.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHELL = ROOT / "kernel" / "shell.inc"
TABLE = ROOT / "kernel" / "cmdtable.inc"
DATA = ROOT / "kernel" / "data.inc"

DISPATCH_RE = re.compile(r"mov\s+(?:edi|esi),\s*(cmd_[a-z0-9_]+_str)\s*\n\s*call\s+str_starts_with")
ENTRY_RE = re.compile(
    r"^[ \t]*CMDENT[ \t]+(cmd_[a-z0-9_]+_str|[a-z0-9_]+)[ \t]*,[ \t]*(CAT_[A-Z]+)"
    r"[ \t]*,[ \t]*([a-z0-9_]+)[ \t]*,[ \t]*([a-z0-9_]+)"
    r"[ \t]*(?:;[ \t]*dispatches[ \t]+(cmd_[a-z0-9_]+_str))?",
    re.M,
)
DEF_RE = re.compile(r"^(cmd_[a-z0-9_]+_str):\s*db\s+\"([^\"]*)\"", re.M)


def dispatch_names() -> list[str]:
    text = SHELL.read_text(encoding="utf-8")
    start = text.index("shell_dispatch_core:")
    end = text.index(".cmd_help:", start)
    return DISPATCH_RE.findall(text[start:end])


def main() -> int:
    errors: list[str] = []
    if not TABLE.is_file():
        print("[check-command-table] FAIL: kernel/cmdtable.inc is missing")
        return 1

    dispatch = dispatch_names()
    table = TABLE.read_text(encoding="utf-8")
    data = DATA.read_text(encoding="utf-8")

    entries = ENTRY_RE.findall(table)
    # A keyword entry may point at a display name ("if") and name the dispatcher
    # string it stands for in a trailing comment.
    dispatched_by = {name: (alias or name) for name, _c, _s, _d, alias in entries}
    names = [name for name, _cat, _sum, _det, _alias in entries]

    defined = {label: value for label, value in DEF_RE.findall(data)}
    display = [defined.get(name, name) for name in names]

    for name in sorted({name for name in names if names.count(name) > 1}):
        errors.append(f"cmdtable.inc lists {name} more than once")

    covered = set(dispatched_by.values())
    for name in dispatch:
        if name not in covered:
            errors.append(f"{name} is dispatched in shell.inc but missing from cmdtable.inc")

    for name in names:
        if dispatched_by[name] not in dispatch:
            errors.append(f"{name} is in cmdtable.inc but is not dispatched by shell.inc")

    for name, _category, summary, detail, _alias in entries:
        if not re.search(rf"^{summary}:\s*db\s", table, re.M):
            errors.append(f"{name}: summary label {summary} is not defined")
        if not re.search(rf"^{detail}:", table, re.M):
            errors.append(f"{name}: detail label {detail} is not defined")

    referenced_categories = {cat for _n, cat, _s, _d, _a in entries}
    declared = set(re.findall(r"^(CAT_[A-Z]+)\s+equ", table, re.M))
    for category in sorted(referenced_categories - declared):
        errors.append(f"category {category} is used but not declared")

    # Two entries with the same display name would make `help <name>` ambiguous.
    for name in sorted({name for name in display if display.count(name) > 1}):
        errors.append(f"two table entries share the display name {name!r}")

    if errors:
        print("[check-command-table] FAIL")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("[check-command-table] PASS")
    print(f"  {len(names)} built-ins, {len(referenced_categories)} categories")
    return 0


if __name__ == "__main__":
    sys.exit(main())
