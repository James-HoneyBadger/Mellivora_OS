# Mellivora App Ecosystem

Mellivora applications use JSON manifests to describe launch metadata, permissions, and default Burrows window behavior. The source manifests live in `docs/app-manifests/`; `docs/app-catalog.json` is generated from them and should not be edited by hand.

## Add An App

1. Build the program source under `programs/<entry>.asm`.
2. Create a manifest with the scaffold tool:

```text
python3 tools/new_app_manifest.py \\
  --id os.mellivora.example \\
  --name "Example App" \\
  --entry example \\
  --category utility \\
  --description "A short launcher description" \\
  --keywords example utility
```

3. Validate the manifest:

```text
make validate-manifest
```

4. Regenerate and verify the catalog:

```text
make app-catalog
make check-app-catalog
```

The manifest `entry` must map to an existing source file at `programs/<entry>.asm`. IDs and entries must be unique.

## Launcher Integration

The launcher app table is generated from `docs/app-catalog.json` into `programs/generated_app_catalog.inc`. This keeps display names and executable entries synchronized without requiring the ring 3 launcher to parse JSON at runtime.

Regenerate or verify the generated table with:

```text
make launcher-catalog
make check-launcher-catalog
```

Building `programs/blauncher.bin` depends on the generated include, so catalog changes are picked up by the normal program build.

The launcher displays 15 rows at a time. Up/down navigation keeps the selected row in view, and mouse clicks are mapped through the current viewport. Search filters the complete catalog case-insensitively by executable name, display name, category, description, and keywords, including the synthetic Exit Desktop entry.

Apps are started with `gui_launch`, which spawns a separate scheduler task and returns
immediately, so the launcher never blocks the desktop and several applications can run
at once. The launcher window closes once an app is chosen (or the desktop is left), and
`F1` reopens it at any time. Selecting Exit Desktop returns to the text shell; any apps
still running are torn down with the desktop.

For migration work, preview missing manifests before writing them:

```text
python3 tools/new_app_manifest.py --batch --dry-run --limit 10
```

The batch mode skips entries already covered by a manifest or listed in `docs/app-manifest-exclusions.json`. After reviewing a preview, remove `--dry-run` and keep using `--limit` for small, reviewable batches. Category inference is conservative and generated descriptions are placeholders that should be refined before release.

## Manifest Coverage

The opt-in coverage check compares program sources with manifest entries:

```text
make validate-manifest-coverage
```

This currently reports the migration backlog because only the curated desktop manifests have been added. Programs that are not launchable applications can be listed explicitly in `docs/app-manifest-exclusions.json`. Exclusions should be used for internal helpers, intentionally unlaunchable binaries, or programs awaiting classification, not to hide malformed manifests.

The normal `make lint` target validates existing manifests and catalog consistency but does not fail on the migration backlog. This keeps current builds stable while coverage is added in reviewed batches.

## Categories And Permissions

Use the category that best describes how a user discovers the program: `editor`, `file`, `system`, `network`, `media`, `game`, `dev`, `terminal`, `demo`, or `utility`. The scaffold tool supplies conservative category defaults for permissions and window geometry. Override those defaults when the program needs less access.

Permissions describe declared capabilities such as filesystem access, GUI drawing, networking, audio, notifications, and process control. They are metadata today; runtime enforcement must be added separately and should not be assumed from a manifest alone.

## Launcher Metadata

The launcher-facing fields are:

- `name`: display name
- `description`: one-line search result text
- `category`: grouping and default presentation
- `keywords`: search terms
- `icon`: icon asset identifier or path
- `launch_mode`: `normal`, `singleton`, or `daemon`
- `min_os_version`: minimum compatible OS version
- `window`: default size and placement hints

Keep descriptions short and specific. Keywords should cover the terms a user might type, not repeat the application name only.
