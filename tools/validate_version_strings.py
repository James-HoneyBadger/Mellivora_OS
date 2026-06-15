#!/usr/bin/env python3
"""Validate release-version consistency across runtime, docs, and metadata."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

OS_VERSION = "13.0.0"
SHELL_VERSION = "13.0"


def read_text(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8")


def expect_substring(errors: list[str], rel_path: str, substring: str) -> None:
    text = read_text(rel_path)
    if substring not in text:
        errors.append(f"Missing expected string in {rel_path}: {substring!r}")


def expect_json_value(errors: list[str], rel_path: str, key: str, expected: str) -> None:
    data = json.loads((ROOT / rel_path).read_text(encoding="utf-8"))
    actual = data.get(key)
    if actual != expected:
        errors.append(f"Unexpected {key} in {rel_path}: expected {expected!r}, found {actual!r}")


def main() -> int:
    errors: list[str] = []

    expect_substring(errors, "kernel/data.inc", f"Mellivora OS  v{OS_VERSION}")
    expect_substring(errors, "kernel/data.inc", f"Mellivora OS v{OS_VERSION} - HBFS v3")
    expect_substring(errors, "kernel/data.inc", f"HB Lair v{SHELL_VERSION} (Honey Badger Lair)")

    expect_substring(errors, "programs/neofetch.asm", f"Mellivora OS v{OS_VERSION}")
    expect_substring(errors, "programs/neofetch.asm", f"Mellivora v{OS_VERSION} (i486 32-bit)")
    expect_substring(errors, "programs/neofetch.asm", f"HB Lair v{SHELL_VERSION} (Honey Badger Lair)")

    expect_substring(errors, "README.md", f"v1.0 → v{OS_VERSION}")
    expect_substring(errors, "docs/INSTALL.md", f"current: v{OS_VERSION}")
    expect_substring(errors, "CHANGELOG.md", f"## v{OS_VERSION} - ")
    expect_substring(errors, "Containerfile", f'org.opencontainers.image.version="{OS_VERSION}"')

    expect_json_value(errors, "docs/app-manifests/bedit.json", "version", OS_VERSION)
    expect_json_value(errors, "docs/app-manifests/bforager.json", "version", OS_VERSION)

    if errors:
        print("[validate-version] FAIL")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("[validate-version] PASS")
    print(f"  OS version: v{OS_VERSION}")
    print(f"  Shell version: v{SHELL_VERSION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
