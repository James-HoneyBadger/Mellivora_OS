#!/usr/bin/env python3
"""Exercise host package install, registration, rollback, and removal."""

from __future__ import annotations

import os
from io import BytesIO
import subprocess
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HBPKG = ROOT / "tools" / "hbpkg"


def build_package(cache: Path, name: str, files: dict[str, bytes],
                  associations: list[str] | None = None,
                  extra_members: dict[str, bytes] | None = None) -> None:
    package_root = cache / f"{name}.hbp"
    metadata_files = ", ".join(f'"{path}"' for path in files)
    metadata = (
        f'name = "{name}"\n'
        'version = "1.0.0"\n'
        f"files = [{metadata_files}]\n"
    )
    if associations:
        metadata += "associations = [" + ", ".join(
            f'"{entry}"' for entry in associations
        ) + "]\n"

    with tarfile.open(package_root, "w:gz") as archive:
        metadata_info = tarfile.TarInfo("HBPKG/meta.toml")
        metadata_bytes = metadata.encode()
        metadata_info.size = len(metadata_bytes)
        archive.addfile(metadata_info, BytesIO(metadata_bytes))
        for path, content in files.items():
            info = tarfile.TarInfo(f"HBPKG/files/{path}")
            info.size = len(content)
            archive.addfile(info, BytesIO(content))
        for path, content in (extra_members or {}).items():
            info = tarfile.TarInfo(path)
            info.size = len(content)
            archive.addfile(info, BytesIO(content))


def run(root: Path, *args: str, succeeds: bool = True) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["HBPKG_ROOT"] = str(root)
    result = subprocess.run(
        [str(HBPKG), *args], env=environment, capture_output=True, text=True
    )
    expected = 0 if succeeds else 1
    if result.returncode != expected:
        raise AssertionError(
            f"hbpkg {' '.join(args)} returned {result.returncode}, expected {expected}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mellivora-hbpkg-") as directory:
        root = Path(directory)
        cache = root / "var" / "pkg" / "cache"
        cache.mkdir(parents=True)

        build_package(cache, "sample", {"usr/bin/sample": b"sample app"},
                      [".txt:sample"])
        run(root, "install", "sample")
        installed_file = root / "usr" / "bin" / "sample"
        manifest = root / "var" / "pkg" / "installed" / "sample.files"
        association_db = root / "var" / "pkg" / "associations.db"
        assert installed_file.read_bytes() == b"sample app"
        assert manifest.read_text() == "usr/bin/sample\n"
        assert association_db.read_text() == ".txt\tsample\tsample\n"

        build_package(cache, "collision", {"usr/bin/collision": b"must roll back"},
                      [".TXT:collision"])
        run(root, "install", "collision", succeeds=False)
        assert not (root / "usr" / "bin" / "collision").exists()
        assert not (root / "var" / "pkg" / "installed" / "collision.files").exists()
        assert association_db.read_text() == ".txt\tsample\tsample\n"

        build_package(cache, "unsafe", {"usr/bin/unsafe": b"not extracted"},
                      extra_members={"HBPKG/../../outside": b"escape"})
        run(root, "install", "unsafe", succeeds=False)
        assert not (root / "var" / "pkg" / "outside").exists()

        # A record written by a version of hbpkg that kept no manifest still
        # leaves the database, and says why the files were not removed.
        build_package(cache, "legacy", {"usr/bin/legacy": b"legacy app"})
        run(root, "install", "legacy")
        (root / "var" / "pkg" / "installed" / "legacy.files").unlink()
        result = run(root, "remove", "legacy")
        assert "older hbpkg" in result.stderr, result.stderr
        assert b"legacy" not in (root / "var" / "pkg" / "installed.db").read_bytes()

        run(root, "remove", "sample")
        assert not installed_file.exists()
        assert not manifest.exists()
        assert not association_db.exists()
        print("[hbpkg] PASS install, unsafe archive rejection, association "
              "rollback, legacy removal, and exact removal")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
