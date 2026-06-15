#!/usr/bin/env python3
"""Generate a machine-readable app catalog from bundled manifest files."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST_DIR = ROOT / "docs" / "app-manifests"
OUTPUT = ROOT / "docs" / "app-catalog.json"


def load_manifest(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    data["manifest_file"] = str(path.relative_to(ROOT))
    return data


def build_catalog(manifests: list[dict]) -> dict:
    manifests = sorted(manifests, key=lambda item: (item.get("category", ""), item.get("name", "")))
    categories: dict[str, int] = {}
    permissions: dict[str, int] = {}

    for item in manifests:
        category = item.get("category", "unknown")
        categories[category] = categories.get(category, 0) + 1
        for perm in item.get("permissions", []):
            permissions[perm] = permissions.get(perm, 0) + 1

    return {
        "generated_from": str(MANIFEST_DIR.relative_to(ROOT)),
        "app_count": len(manifests),
        "categories": categories,
        "permissions": permissions,
        "apps": manifests,
    }


def render_catalog(catalog: dict) -> str:
    return json.dumps(catalog, indent=2, sort_keys=False) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Mellivora app catalog")
    parser.add_argument("--check", action="store_true", help="Fail if docs/app-catalog.json is stale")
    args = parser.parse_args()

    manifests = []
    if MANIFEST_DIR.is_dir():
        for path in sorted(MANIFEST_DIR.glob("*.json")):
            manifests.append(load_manifest(path))

    rendered = render_catalog(build_catalog(manifests))

    if args.check:
        if not OUTPUT.exists():
            print("[app-catalog] FAIL docs/app-catalog.json is missing")
            return 1
        current = OUTPUT.read_text(encoding="utf-8")
        if current != rendered:
            print("[app-catalog] FAIL docs/app-catalog.json is stale")
            print("  Run: python3 tools/generate_app_catalog.py")
            return 1
        print(f"[app-catalog] PASS {OUTPUT.relative_to(ROOT)} is up to date")
        return 0

    OUTPUT.write_text(rendered, encoding="utf-8")
    print(f"[app-catalog] WROTE {OUTPUT.relative_to(ROOT)} ({len(manifests)} apps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
