#!/usr/bin/env python3
"""Check the installer ISO: structure first, then a real install.

Structure checks (always):
  * the ISO has an El Torito BIOS boot entry that loads 4 sectors (2 KB),
  * the boot image contains both the stub and the installer payload,
  * /MELLIVORA.IMG is in the ISO at exactly the size of the disk image,
  * that disk image is bootable (0x55AA at offset 510).

End-to-end check (--end-to-end or MELLIVORA_ISO_E2E=1):
  * boot the ISO in QEMU with a blank disk, drive the installer unattended,
  * require "Installation complete.",
  * require the target disk to start with the image,
  * require the installed disk to boot to the shell prompt.

Usage:
    tests/test_iso.py [--iso FILE] [--image FILE] [--end-to-end] [--keep]
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / "mellivora.iso"
IMAGE = ROOT / "mellivora-install.img"
TOOLS = ROOT / "install" / "tools"

failures: list[str] = []


def check(name: str, condition: bool, detail: str = "") -> None:
    if condition:
        print(f"[iso] PASS {name}")
    else:
        print(f"[iso] FAIL {name}{(': ' + detail) if detail else ''}")
        failures.append(name)


def _opt(args: list[str], name: str, default: str | None = None) -> str | None:
    return args[args.index(name) + 1] if name in args else default


def read_extent(f, lba: int, size: int) -> bytes:
    f.seek(lba * 2048)
    return f.read(size)


def iso9660_find(f, want: str) -> tuple[int, int] | None:
    """Return (lba, size) of `want` anywhere in the ISO9660 tree.

    Reads one extent at a time: the disc is mostly the disk image, and loading
    all of it just to walk a few directory records was 258 MB of RSS.
    """
    pvd = read_extent(f, 16, 2048)
    if pvd[1:4] != b"CD0":
        return None

    def walk(lba: int, size: int, depth: int) -> tuple[int, int] | None:
        directory = read_extent(f, lba, size)
        offset = 0
        while offset < len(directory):
            length = directory[offset]
            if length == 0:
                offset = (offset // 2048 + 1) * 2048
                continue
            name_len = directory[offset + 32]
            name = directory[offset + 33:offset + 33 + name_len].decode("latin1").split(";")[0]
            ent_lba = struct.unpack_from("<I", directory, offset + 2)[0]
            ent_size = struct.unpack_from("<I", directory, offset + 10)[0]
            flags = directory[offset + 25]
            if name.lower() == want.lower():
                return ent_lba, ent_size
            if flags & 2 and depth < 4 and name not in ("\x00", "\x01"):
                hit = walk(ent_lba, ent_size, depth + 1)
                if hit:
                    return hit
            offset += length
        return None

    root_lba = struct.unpack_from("<I", pvd, 156 + 2)[0]
    root_size = struct.unpack_from("<I", pvd, 156 + 10)[0]
    return walk(root_lba, root_size, 0)


def check_structure(iso: Path, image: Path) -> None:
    check("ISO exists", iso.is_file(), str(iso))
    if not iso.is_file():
        return

    check("disk image exists", image.is_file(), str(image))
    if not image.is_file():
        return

    image_size = image.stat().st_size

    with iso.open("rb") as f:
        pvd = read_extent(f, 16, 2048)
        check("ISO9660 primary volume descriptor", pvd[1:6] == b"CD001")

        boot = iso9660_find(f, "installer.bin")
        check("boot image is in the ISO", boot is not None)
        if boot:
            lba, size = boot
            payload = read_extent(f, lba, size)
            check("boot image is at least the 2 KB stub + payload", size >= 66 * 1024, f"{size} bytes")
            check("boot image carries the installer payload", b"Mellivora OS installer" in payload)
            check("boot image carries the image name", b"MELLIVORA.IMG" in payload)

        found = iso9660_find(f, "MELLIVORA.IMG")
        check("MELLIVORA.IMG is in the ISO", found is not None)
        if found:
            lba, size = found
            check("MELLIVORA.IMG size matches the disk image", size == image_size,
                  f"ISO has {size}, image is {image_size}")
            head = read_extent(f, lba, 512)
            check("MELLIVORA.IMG starts with a bootable MBR", head[510:512] == b"\x55\xaa")

    xorriso = shutil.which("xorriso")
    if xorriso:
        report = subprocess.run([xorriso, "-indev", str(iso), "-report_el_torito", "plain"],
                                capture_output=True, text=True).stdout
        check("El Torito boot record present", "El Torito" in report)
        rows = [line for line in report.splitlines() if "El Torito boot img" in line]
        check("exactly one El Torito boot image", len(rows) == 1, f"{len(rows)} entries")
        if rows:
            fields = rows[0].split()
            # El Torito boot img : 1 BIOS y none 0x0000 0x00 4 34
            check("boot image is a BIOS entry", fields[6] == "BIOS" and fields[7] == "y", rows[0])
            check("boot-load-size is 4 sectors", fields[-2] == "4", rows[0])
    else:
        print("[iso] SKIP El Torito report: xorriso not installed")


def run_end_to_end(iso: Path, workdir: Path) -> None:
    target = workdir / "target.img"
    log = workdir / "install.log"
    proc = subprocess.run(
        [sys.executable, str(TOOLS / "autoinstall.py"), str(iso), str(target),
         "--size", "512", "--boot-check", "--out", str(log), "--json"],
        capture_output=True, text=True)
    print(proc.stdout.strip() or proc.stderr.strip())
    check("unattended install reported success", proc.returncode == 0, proc.stderr.strip()[:300])
    check("target disk matches the image", '"image_matches": true' in proc.stdout)
    check("installed disk boots to the shell", '"boots": true' in proc.stdout)


def main() -> int:
    args = sys.argv[1:]
    iso = Path(_opt(args, "--iso", str(ISO)))
    image = Path(_opt(args, "--image", str(IMAGE)))
    end_to_end = "--end-to-end" in args or os.environ.get("MELLIVORA_ISO_E2E") == "1"

    check_structure(iso, image)

    if end_to_end:
        if not shutil.which("qemu-system-i386") and not shutil.which("qemu-system-x86_64"):
            print("[iso] SKIP end-to-end: QEMU not installed")
        else:
            workdir = Path(tempfile.mkdtemp(prefix="mellivora-iso-e2e-"))
            try:
                run_end_to_end(iso, workdir)
            finally:
                if "--keep" in args:
                    print(f"[iso] kept {workdir}")
                else:
                    shutil.rmtree(workdir, ignore_errors=True)
    else:
        print("[iso] skipping the QEMU install (pass --end-to-end to run it)")

    print()
    if failures:
        print(f"[iso] {len(failures)} check(s) failed")
        return 1
    print("[iso] all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
