#!/usr/bin/env python3
"""Validate app permission usage against platform policy.

Ensures no unexpected permission expansion, checks category-specific policies,
and reports the permission matrix for security review.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST_DIR = ROOT / "docs" / "app-manifests"
CATALOG = ROOT / "docs" / "app-catalog.json"

# Permission policy: max permissions allowed per category + reserved system perms
CATEGORY_POLICY: dict[str, set[str]] = {
    "demo": {"gui.clipboard", "gui.notify", "filesystem.read", "filesystem.write", "network.tcp", "network.raw"},
    "editor": {"filesystem.read", "filesystem.write", "filesystem.exec", "gui.framebuf", "gui.compositor", "gui.clipboard", "gui.notify", "gui.mouse"},
    "file": {"filesystem.read", "filesystem.write", "filesystem.exec", "gui.framebuf", "gui.compositor", "gui.clipboard", "gui.notify", "gui.mouse"},
    "network": {"network.tcp", "network.udp", "network.raw", "gui.notify"},
    "media": {"audio.play", "audio.record", "gui.framebuf", "gui.mouse"},
    "game": {"gui.framebuf", "gui.mouse", "audio.play"},
    "dev": {"filesystem.read", "filesystem.write", "filesystem.exec", "system.processes"},
    "terminal": {"filesystem.read", "filesystem.write", "filesystem.exec", "gui.framebuf", "gui.mouse"},
    "system": {"system.processes", "system.reboot", "hardware.pci", "hardware.serial"},
    "utility": {"filesystem.read", "filesystem.write", "gui.notify"},
}

# System-restricted permissions (should only be used by trusted OS services)
SYSTEM_ONLY = {"system.reboot", "hardware.pci", "hardware.serial"}


def check_manifest_permissions(manifest: dict, path: Path) -> tuple[list[str], set[str]]:
    """Validate a single manifest's permissions and return (errors, seen_perms)."""
    errors: list[str] = []
    perms = set(manifest.get("permissions", []))
    category = manifest.get("category", "unknown")

    if category not in CATEGORY_POLICY:
        errors.append(f"unknown category {category!r}")
        return errors, perms

    allowed = CATEGORY_POLICY[category]
    for perm in perms:
        if perm not in allowed:
            errors.append(f"{path.name}: permission {perm!r} not allowed for category {category!r}")
        if perm in SYSTEM_ONLY:
            errors.append(f"{path.name}: permission {perm!r} is system-restricted and cannot be used by user apps")

    return errors, perms


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate app permission policies")
    parser.add_argument("--check", action="store_true", help="Fail if policy violations exist")
    args = parser.parse_args()

    if not CATALOG.exists():
        print("[validate-permissions] INFO no app catalog found; run 'make app-catalog' first")
        return 0

    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    all_errors: list[str] = []
    seen_perms: dict[str, int] = {}

    for app_entry in catalog.get("apps", []):
        manifest_path = Path(app_entry.get("manifest_file", ""))
        errors, perms = check_manifest_permissions(app_entry, manifest_path)
        all_errors.extend(errors)
        for perm in perms:
            seen_perms[perm] = seen_perms.get(perm, 0) + 1

    if all_errors:
        print("[validate-permissions] FAIL")
        for err in all_errors:
            print(f"  - {err}")
        return 1

    print("[validate-permissions] PASS all app permissions conform to category policy")
    print(f"  Apps with permissions: {sum(1 for a in catalog.get('apps', []) if a.get('permissions'))}/{len(catalog.get('apps', []))}")
    if seen_perms:
        print("  Permission usage:")
        for perm in sorted(seen_perms.keys()):
            print(f"    {perm}: {seen_perms[perm]} app(s)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
