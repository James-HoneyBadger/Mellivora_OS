#!/usr/bin/env python3
"""Generate syscall contract artifacts from programs/syscalls.inc.

Outputs:
- docs/syscalls.json (machine-readable contract)
- docs/syscalls.md   (human-readable reference table)
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "programs" / "syscalls.inc"
OUTPUT_JSON = ROOT / "docs" / "syscalls.json"
OUTPUT_MD = ROOT / "docs" / "syscalls.md"

SYSCALL_RE = re.compile(r"^(SYS_[A-Z0-9_]+)\s+equ\s+([0-9]+)\b")


@dataclass(frozen=True)
class Syscall:
    name: str
    number: int
    summary: str


def parse_syscalls(path: Path) -> list[Syscall]:
    result: list[Syscall] = []

    for raw in path.read_text(encoding="utf-8").splitlines():
        parts = raw.split(";", 1)
        code = parts[0].strip()
        comment = parts[1].strip() if len(parts) > 1 else ""

        m = SYSCALL_RE.match(code)
        if not m:
            continue

        name = m.group(1)
        number = int(m.group(2))
        result.append(Syscall(name=name, number=number, summary=comment))

    result.sort(key=lambda item: item.number)
    return result


def build_document(syscalls: list[Syscall]) -> dict:
    highest = max((s.number for s in syscalls), default=-1)
    return {
        "generated_from": str(SOURCE.relative_to(ROOT)),
        "syscall_count": len(syscalls),
        "highest_syscall_number": highest,
        "syscalls": [
            {
                "name": item.name,
                "number": item.number,
                "summary": item.summary,
            }
            for item in syscalls
        ],
    }


def render_json(doc: dict) -> str:
    return json.dumps(doc, indent=2, sort_keys=False) + "\n"


def render_markdown(syscalls: list[Syscall]) -> str:
    lines: list[str] = []
    lines.append("# Mellivora Syscall Catalog")
    lines.append("")
    lines.append("This file is generated from programs/syscalls.inc.")
    lines.append("Do not edit manually; run: python3 tools/generate_syscalls_json.py")
    lines.append("")
    lines.append(f"- Source: {SOURCE.relative_to(ROOT)}")
    lines.append(f"- Syscall count: {len(syscalls)}")
    lines.append("")
    lines.append("| Number | Name | Summary |")
    lines.append("| ---: | --- | --- |")

    for item in syscalls:
        summary = item.summary if item.summary else "-"
        summary = summary.replace("|", "\\|")
        lines.append(f"| {item.number} | {item.name} | {summary} |")

    lines.append("")
    return "\n".join(lines)


def check_file(path: Path, expected: str, tag: str) -> int:
    if not path.exists():
        print(f"[{tag}] FAIL {path.relative_to(ROOT)} is missing")
        return 1
    current = path.read_text(encoding="utf-8")
    if current != expected:
        print(f"[{tag}] FAIL {path.relative_to(ROOT)} is stale")
        print("  Run: python3 tools/generate_syscalls_json.py")
        return 1
    print(f"[{tag}] PASS {path.relative_to(ROOT)} is up to date")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate syscall contract artifacts")
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail if both generated artifacts are not up to date",
    )
    parser.add_argument(
        "--check-json",
        action="store_true",
        help="Fail if docs/syscalls.json is not up to date",
    )
    parser.add_argument(
        "--check-md",
        action="store_true",
        help="Fail if docs/syscalls.md is not up to date",
    )
    args = parser.parse_args()

    syscalls = parse_syscalls(SOURCE)
    rendered_json = render_json(build_document(syscalls))
    rendered_md = render_markdown(syscalls)

    check_json = args.check or args.check_json
    check_md = args.check or args.check_md

    if check_json or check_md:
        status = 0
        if check_json:
            status |= check_file(OUTPUT_JSON, rendered_json, "syscalls-json")
        if check_md:
            status |= check_file(OUTPUT_MD, rendered_md, "syscalls-md")
        return status

    OUTPUT_JSON.write_text(rendered_json, encoding="utf-8")
    OUTPUT_MD.write_text(rendered_md, encoding="utf-8")
    print(
        f"[syscalls-json] WROTE {OUTPUT_JSON.relative_to(ROOT)} "
        f"({len(syscalls)} syscalls, max={max((s.number for s in syscalls), default=-1)})"
    )
    print(f"[syscalls-md] WROTE {OUTPUT_MD.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
