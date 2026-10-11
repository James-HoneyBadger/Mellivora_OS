#!/usr/bin/env python3
"""Create a starter manifest for a Mellivora app."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST_DIR = ROOT / "docs" / "app-manifests"
PROGRAM_DIR = ROOT / "programs"
EXCLUSIONS_PATH = ROOT / "docs" / "app-manifest-exclusions.json"
OS_VERSION = "13.0.0"

CATEGORY_DEFAULTS = {
    "editor": [
        "filesystem.read",
        "filesystem.write",
        "filesystem.exec",
        "gui.framebuf",
        "gui.compositor",
        "gui.clipboard",
        "gui.notify",
        "gui.mouse",
    ],
    "file": [
        "filesystem.read",
        "filesystem.write",
        "filesystem.exec",
        "gui.framebuf",
        "gui.compositor",
        "gui.clipboard",
        "gui.notify",
        "gui.mouse",
    ],
    "system": [
        "filesystem.read",
        "filesystem.write",
        "filesystem.exec",
        "gui.framebuf",
        "gui.compositor",
        "gui.mouse",
        "gui.notify",
        "system.processes",
    ],
    "network": [
        "network.tcp",
        "network.udp",
        "gui.notify",
        "gui.framebuf",
    ],
    "media": [
        "audio.play",
        "gui.framebuf",
        "gui.compositor",
    ],
    "demo": [
        "gui.framebuf",
        "gui.notify",
        "gui.clipboard",
    ],
    "utility": [
        "filesystem.read",
        "filesystem.write",
        "gui.notify",
    ],
    "terminal": [
        "filesystem.read",
        "filesystem.write",
        "filesystem.exec",
        "gui.framebuf",
        "gui.compositor",
        "gui.notify",
    ],
    "dev": [
        "filesystem.read",
        "filesystem.write",
        "filesystem.exec",
        "gui.framebuf",
        "gui.notify",
        "system.processes",
    ],
    "game": [
        "gui.framebuf",
        "gui.compositor",
        "gui.mouse",
        "gui.notify",
    ],
}

CATEGORY_DEFAULT_WINDOW = {
    "editor": {"width": 1024, "height": 768, "resizable": True, "centered": False},
    "file": {"width": 960, "height": 720, "resizable": True, "centered": False},
    "system": {"width": 800, "height": 600, "resizable": True, "centered": True},
    "network": {"width": 820, "height": 520, "resizable": False, "centered": False},
    "media": {"width": 960, "height": 600, "resizable": True, "centered": False},
    "demo": {"width": 640, "height": 400, "resizable": False, "centered": True},
    "utility": {"width": 720, "height": 480, "resizable": True, "centered": True},
    "terminal": {"width": 900, "height": 600, "resizable": True, "centered": False},
    "dev": {"width": 960, "height": 680, "resizable": True, "centered": False},
    "game": {"width": 960, "height": 720, "resizable": True, "centered": False},
}

CATEGORY_HINTS = {
    "bedit": "editor",
    "bnotes": "editor",
    "bsheet": "editor",
    "bview": "editor",
    "bhive": "file",
    "bforager": "file",
    "bterm": "terminal",
    "bcalc": "utility",
    "calc": "utility",
    "bsettings": "system",
    "bsysmon": "system",
    "blauncher": "system",
    "bnotify": "system",
    "chat": "network",
    "netgraph": "network",
    "audiodemo": "media",
    "bplayer": "media",
    "asm": "dev",
    "basic": "dev",
    "basicc": "dev",
    "gcc": "dev",
}


def slugify(value: str) -> str:
    value = value.strip().lower()
    value = re.sub(r"[^a-z0-9]+", "-", value)
    value = value.strip("-")
    return value or "app"


def infer_category(entry: str) -> str:
    if entry in CATEGORY_HINTS:
        return CATEGORY_HINTS[entry]
    if entry.endswith("demo"):
        return "demo"
    if entry.endswith("game") or entry in {"adventure", "asteroids", "blackjack", "breakout", "chess", "chip8"}:
        return "game"
    if entry.startswith(("net", "ping", "dns", "ftp", "http")):
        return "network"
    return "utility"


def load_excluded_entries() -> set[str]:
    if not EXCLUSIONS_PATH.exists():
        return set()
    data = json.loads(EXCLUSIONS_PATH.read_text(encoding="utf-8"))
    entries = data.get("entries", [])
    if not isinstance(entries, list) or not all(isinstance(entry, str) for entry in entries):
        raise ValueError("entries must be an array of strings")
    return set(entries)


def existing_entries() -> set[str]:
    entries: set[str] = set()
    for path in sorted(MANIFEST_DIR.glob("*.json")):
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            continue
        entry = manifest.get("entry")
        if isinstance(entry, str):
            entries.add(entry)
    return entries


def manifest_for(args: argparse.Namespace) -> dict:
    category = args.category
    permissions = args.permissions or CATEGORY_DEFAULTS.get(category, [])
    window = args.window or CATEGORY_DEFAULT_WINDOW.get(category, {"width": 800, "height": 600, "resizable": True, "centered": True})

    manifest = {
        "id": args.id,
        "name": args.name,
        "version": args.version,
        "entry": args.entry,
        "category": category,
        "description": args.description,
        "author": args.author,
        "keywords": args.keywords,
        "permissions": permissions,
        "settings_key": args.settings_key,
        "min_os_version": args.min_os_version,
        "window": window,
        "icon": args.icon,
        "launch_mode": args.launch_mode,
    }
    if args.startup_order is not None:
        manifest["startup_order"] = args.startup_order
    if args.startup_args:
        manifest["startup_args"] = args.startup_args
    return manifest


def batch_manifest(entry: str) -> dict:
    category = infer_category(entry)
    name = entry[:1].upper() + entry[1:]
    args = argparse.Namespace(
        id=f"os.mellivora.{slugify(entry)}",
        name=name,
        version=OS_VERSION,
        entry=entry,
        category=category,
        description=f"{name} for Mellivora OS",
        author="Mellivora OS",
        keywords=[entry, category],
        permissions=[],
        settings_key=slugify(entry).replace("-", "_"),
        min_os_version="13.0",
        window=None,
        icon="res/app.icon",
        launch_mode="normal",
        startup_order=None,
        startup_args=[],
    )
    args.window = CATEGORY_DEFAULT_WINDOW[category]
    return manifest_for(args)


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a starter app manifest for Mellivora OS")
    parser.add_argument("--id", help="Stable reverse-domain app id, e.g. os.mellivora.blauncher")
    parser.add_argument("--name", help="Human-readable app name")
    parser.add_argument("--entry", help="Executable name relative to app root, default: derived from name")
    parser.add_argument("--category", default="utility", choices=sorted(CATEGORY_DEFAULTS), help="Launcher category")
    parser.add_argument("--version", default=OS_VERSION, help="Semver version, default: %(default)s")
    parser.add_argument("--description", default="Desktop app for Mellivora OS", help="One-line launcher description")
    parser.add_argument("--author", default="Mellivora OS", help="App author")
    parser.add_argument("--keywords", nargs="*", default=[], help="Search tags for app launcher")
    parser.add_argument("--permissions", nargs="*", default=[], help="Explicit permission list; defaults to category-specific set")
    parser.add_argument("--settings-key", help="Settings key under /config/, default: auto-generated from name")
    parser.add_argument("--min-os-version", default="13.0", help="Minimum OS version, default: %(default)s")
    parser.add_argument("--icon", default="res/app.icon", help="Launcher icon asset or path")
    parser.add_argument("--launch-mode", default="normal", choices=["normal", "singleton", "daemon"], help="Launch behavior")
    parser.add_argument("--startup-order", type=int, help="Optional startup priority, 0-100")
    parser.add_argument("--startup-args", nargs="*", default=[], help="Default startup arguments")
    parser.add_argument("--window-width", type=int, help="Default window width")
    parser.add_argument("--window-height", type=int, help="Default window height")
    parser.add_argument("--resizable", action="store_true", help="Use a resizable window by default")
    parser.add_argument("--centered", action="store_true", help="Center the window by default")
    parser.add_argument("--force", action="store_true", help="Overwrite an existing manifest file")
    parser.add_argument("--batch", action="store_true", help="Generate manifests for program sources missing a manifest")
    parser.add_argument("--dry-run", action="store_true", help="Preview batch output without writing files")
    parser.add_argument("--limit", type=int, help="Limit the number of batch manifests")
    args = parser.parse_args()

    if args.batch:
        if args.limit is not None and args.limit < 1:
            parser.error("--limit must be at least 1")
        try:
            excluded = load_excluded_entries()
        except (json.JSONDecodeError, ValueError) as exc:
            parser.error(f"invalid {EXCLUSIONS_PATH.relative_to(ROOT)}: {exc}")
        covered = existing_entries()
        candidates = [
            path.stem for path in sorted(PROGRAM_DIR.glob("*.asm"))
            if path.stem not in covered and path.stem not in excluded
        ]
        if args.limit is not None:
            candidates = candidates[:args.limit]
        for entry in candidates:
            target = MANIFEST_DIR / f"{slugify(entry)}.json"
            if args.dry_run:
                print(f"[manifest] PREVIEW {target.relative_to(ROOT)} ({infer_category(entry)})")
                continue
            target.write_text(json.dumps(batch_manifest(entry), indent=2) + "\n", encoding="utf-8")
            print(f"[manifest] WROTE {target.relative_to(ROOT)}")
        print(f"[manifest] BATCH {'previewed' if args.dry_run else 'generated'} {len(candidates)} manifests")
        return 0

    if not args.id or not args.name:
        parser.error("--id and --name are required unless --batch is used")

    if not args.entry:
        args.entry = slugify(args.name)
    if not args.settings_key:
        args.settings_key = slugify(args.name).replace("-", "_")

    if args.window_width or args.window_height or args.resizable or args.centered:
        defaults = CATEGORY_DEFAULT_WINDOW.get(args.category, {"width": 800, "height": 600, "resizable": True, "centered": True})
        args.window = {
            "width": args.window_width or defaults["width"],
            "height": args.window_height or defaults["height"],
            "resizable": args.resizable or defaults.get("resizable", True),
            "centered": args.centered or defaults.get("centered", True),
        }
    else:
        args.window = CATEGORY_DEFAULT_WINDOW.get(args.category, {"width": 800, "height": 600, "resizable": True, "centered": True})

    slug = slugify(args.name)
    target = MANIFEST_DIR / f"{slug}.json"
    if target.exists() and not args.force:
        print(f"[manifest] ERROR {target.relative_to(ROOT)} already exists; use --force to overwrite")
        return 1

    manifest = manifest_for(args)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"[manifest] WROTE {target.relative_to(ROOT)}")
    print("[manifest] Next: python3 tools/generate_app_catalog.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
