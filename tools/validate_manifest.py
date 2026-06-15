#!/usr/bin/env python3
"""Validate app.json manifest files against docs/app-manifest.schema.json.

Uses jsonschema if available; falls back to a lightweight structural check
so CI doesn't hard-require the extra package.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMA_PATH = ROOT / "docs" / "app-manifest.schema.json"
EXAMPLE_DIR = ROOT / "docs" / "app-manifests"
PROGRAM_DIR = ROOT / "programs"

REQUIRED_FIELDS = {"id", "name", "version", "entry", "category"}
VALID_CATEGORIES = {
    "editor", "file", "system", "network", "media",
    "game", "dev", "terminal", "demo", "utility",
}
ID_PATTERN = __import__("re").compile(r"^[a-z][a-z0-9-]*(?:\.[a-z][a-z0-9-]*)+$")
VERSION_PATTERN = __import__("re").compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


def lite_validate(manifest: dict, path: Path) -> list[str]:
    errors: list[str] = []
    for field in REQUIRED_FIELDS:
        if field not in manifest:
            errors.append(f"missing required field: {field!r}")
    if "id" in manifest and not ID_PATTERN.match(manifest["id"]):
        errors.append(f"id {manifest['id']!r} does not match pattern")
    if "version" in manifest and not VERSION_PATTERN.match(manifest["version"]):
        errors.append(f"version {manifest['version']!r} must be X.Y.Z")
    if "category" in manifest and manifest["category"] not in VALID_CATEGORIES:
        errors.append(f"unknown category {manifest['category']!r}")
    if "name" in manifest and (len(manifest["name"]) < 1 or len(manifest["name"]) > 48):
        errors.append("name must be 1-48 chars")
    return errors


def jsonschema_validate(manifest: dict, schema: dict, path: Path) -> list[str]:
    try:
        import jsonschema
        validator = jsonschema.Draft7Validator(schema)
        return [e.message for e in validator.iter_errors(manifest)]
    except ImportError:
        return lite_validate(manifest, path)


def main() -> int:
    if not SCHEMA_PATH.exists():
        print(f"[validate-manifest] FAIL schema not found: {SCHEMA_PATH.relative_to(ROOT)}")
        return 1

    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    examples = sorted(EXAMPLE_DIR.glob("*.json")) if EXAMPLE_DIR.is_dir() else []

    if not examples:
        print("[validate-manifest] INFO no example manifests found in docs/app-manifests/ — skipping")
        return 0

    total = 0
    failures = 0
    seen_ids: dict[str, str] = {}
    seen_entries: dict[str, str] = {}
    for path in examples:
        total += 1
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            print(f"[validate-manifest] FAIL {path.name}: JSON parse error: {exc}")
            failures += 1
            continue

        errors = jsonschema_validate(manifest, schema, path)

        manifest_id = manifest.get("id")
        if manifest_id:
            if manifest_id in seen_ids:
                errors.append(f"duplicate id {manifest_id!r}; also used by {seen_ids[manifest_id]}")
            else:
                seen_ids[manifest_id] = path.name

        entry = manifest.get("entry")
        if entry:
            if entry in seen_entries:
                errors.append(f"duplicate entry {entry!r}; also used by {seen_entries[entry]}")
            else:
                seen_entries[entry] = path.name
            program_path = PROGRAM_DIR / f"{entry}.asm"
            if not program_path.exists():
                errors.append(f"entry {entry!r} does not map to existing program source {program_path.relative_to(ROOT)}")

        if errors:
            for e in errors:
                print(f"[validate-manifest] FAIL {path.name}: {e}")
            failures += 1
        else:
            print(f"[validate-manifest] PASS {path.name}")

    print(f"[validate-manifest] {total - failures}/{total} manifests valid")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
