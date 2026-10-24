#!/usr/bin/env python3
"""Check the documentation tree for structural drift.

Three things are verified:

1. `docs/ROADMAP.md` has no duplicated section headings. The roadmap is long and
   is edited by appending; a bad merge once left four copies of P4-P11 in it.
2. Every relative markdown link in the repository's documentation resolves.
3. The release version quoted in the docs agrees with the single source of truth
   in `tools/validate_version_strings.py`.
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_SOURCE = ROOT / "tools" / "validate_version_strings.py"

DOC_FILES = [
    ROOT / "README.md",
    ROOT / "CONTRIBUTING.md",
    ROOT / "SECURITY.md",
    ROOT / "CHANGELOG.md",
    ROOT / "samples" / "README.md",
    ROOT / "install" / "README.md",
]

FENCE_RE = re.compile(r"^\s*(```+|~~~+)")
HEADING_RE = re.compile(r"^(#{2,4})\s+(.+?)\s*$")
LINK_RE = re.compile(r"\[[^\]]*\]\(\s*(<[^>]*>|[^)\s]+)(?:\s+[\"'(][^)]*)?\)")


def iter_doc_files() -> list[Path]:
    files = [path for path in DOC_FILES if path.is_file()]
    files.extend(sorted((ROOT / "docs").rglob("*.md")))
    seen: set[Path] = set()
    unique: list[Path] = []
    for path in files:
        if path not in seen:
            seen.add(path)
            unique.append(path)
    return unique


def strip_fenced_blocks(text: str) -> list[str]:
    """Return the file's lines with fenced code blocks blanked out."""
    out: list[str] = []
    fence: str | None = None
    for line in text.splitlines():
        match = FENCE_RE.match(line)
        if match:
            marker = match.group(1)[0]
            if fence is None:
                fence = marker
            elif fence == marker:
                fence = None
            out.append("")
            continue
        out.append("" if fence is not None else line)
    return out


def check_roadmap_headings(errors: list[str]) -> None:
    roadmap = ROOT / "docs" / "ROADMAP.md"
    if not roadmap.is_file():
        errors.append("docs/ROADMAP.md is missing")
        return
    headings = [
        (len(match.group(1)), match.group(2))
        for line in strip_fenced_blocks(roadmap.read_text(encoding="utf-8"))
        if (match := HEADING_RE.match(line))
    ]
    duplicates = [f"{'#' * level} {name}" for (level, name), count in
                  Counter(headings).items() if count > 1]
    for heading in sorted(duplicates):
        errors.append(f"docs/ROADMAP.md repeats a section heading: {heading}")


def check_links(errors: list[str]) -> None:
    for path in iter_doc_files():
        for number, line in enumerate(strip_fenced_blocks(path.read_text(encoding="utf-8")), 1):
            for match in LINK_RE.finditer(line):
                target = match.group(1).strip()
                if target.startswith("<") and target.endswith(">"):
                    target = target[1:-1].strip()
                if not target or target.startswith("#"):
                    continue
                if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
                    continue  # absolute URL such as https:, mailto:
                target = target.split("#", 1)[0]
                if not target:
                    continue
                resolved = (path.parent / target).resolve()
                if resolved.is_dir():
                    continue
                if not resolved.exists():
                    rel = path.relative_to(ROOT)
                    errors.append(f"{rel}:{number}: link target does not exist: {target}")


def version_source() -> tuple[str, str]:
    text = VERSION_SOURCE.read_text(encoding="utf-8")
    os_version = re.search(r'^OS_VERSION\s*=\s*"([^"]+)"', text, re.M)
    shell_version = re.search(r'^SHELL_VERSION\s*=\s*"([^"]+)"', text, re.M)
    if not os_version or not shell_version:
        raise SystemExit("[check-docs] cannot read versions from tools/validate_version_strings.py")
    return os_version.group(1), shell_version.group(1)


def check_versions(errors: list[str]) -> None:
    os_version, shell_version = version_source()
    expectations = {
        "README.md": [f"v1.0 → v{os_version}"],
        "docs/INSTALL.md": [f"current: v{os_version}"],
        "CHANGELOG.md": [f"## v{os_version} - "],
        "kernel/cmdtable.inc": [f"HB Lair v{shell_version} commands:"],
    }
    for rel_path, needles in expectations.items():
        path = ROOT / rel_path
        if not path.is_file():
            errors.append(f"{rel_path} is missing")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                errors.append(f"{rel_path}: expected version string not found: {needle!r}")


def main() -> int:
    errors: list[str] = []
    check_roadmap_headings(errors)
    check_links(errors)
    check_versions(errors)

    if errors:
        print("[check-docs] FAIL")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("[check-docs] PASS")
    print(f"  documents checked: {len(iter_doc_files())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
