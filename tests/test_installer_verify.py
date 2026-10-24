#!/usr/bin/env python3
"""Prove the installer's per-chunk read-back is not decorative.

The installer writes the image in 32 KB chunks and reads each one straight back
from the target. Building a disc whose write address is deliberately one sector
late is the cheapest way to show that check works: with it, the installer stops
on the first chunk; without it, the disc reports "Installation complete." over
a disk that is wrong from sector 1 onwards.

The faulty disc is built from the same tree as the real one - only
`write_chunk` is altered - so a change to the verification path that makes it
vacuous (for example comparing a buffer with itself) fails here.

Usage:
    tests/test_installer_verify.py [--image FILE] [--keep]

Needs nasm, xorriso and QEMU. Exit: 0 when the fault is caught, 1 otherwise.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INSTALLER = ROOT / "install" / "boot" / "installer.asm"
STAGE1 = ROOT / "install" / "boot" / "stage1.asm"
AUTOINSTALL = ROOT / "install" / "tools" / "autoinstall.py"
IMAGE = ROOT / "mellivora-install.img"

# The one line that decides where a chunk is written.
ANCHOR = "        mov eax, [dst_lba]\n        mov [wdap_lba], eax"
FAULT = "        mov eax, [dst_lba]\n        inc eax                            ; FAULT: one sector late\n        mov [wdap_lba], eax"

MARKER = "did not read back as written"


def _opt(args: list[str], name: str, default: str | None = None) -> str | None:
    return args[args.index(name) + 1] if name in args else default


def need(tool: str) -> str:
    path = shutil.which(tool)
    if not path:
        raise SystemExit(f"[installer-verify] SKIP: {tool} not installed")
    return path


def main() -> int:
    args = sys.argv[1:]
    image = Path(_opt(args, "--image", str(IMAGE)))

    nasm, xorriso = need("nasm"), need("xorriso")
    if not (shutil.which("qemu-system-x86_64") or shutil.which("qemu-system-i386")):
        print("[installer-verify] SKIP: QEMU not installed")
        return 0

    if not image.is_file():
        print(f"[installer-verify] SKIP: {image} not built (run 'make iso')")
        return 0
    if image.stat().st_size % 32768:
        raise SystemExit(f"[installer-verify] FAIL: {image} is not a whole number of 32 KB chunks")

    source = INSTALLER.read_text(encoding="utf-8")
    if ANCHOR not in source:
        raise SystemExit(
            "[installer-verify] FAIL: write_chunk's address setup changed; "
            "update ANCHOR in this test"
        )

    work = Path(tempfile.mkdtemp(prefix="mellivora-fault-"))
    try:
        (work / "tree" / "boot").mkdir(parents=True)
        (work / "fault.asm").write_text(source.replace(ANCHOR, FAULT), encoding="utf-8")

        subprocess.run([nasm, "-f", "bin", "-o", str(work / "stage1.bin"), str(STAGE1)], check=True)
        subprocess.run([nasm, "-f", "bin", "-o", str(work / "payload.bin"), str(work / "fault.asm")], check=True)
        with open(work / "payload.bin", "r+b") as f:
            f.truncate(64 * 1024)
        (work / "tree" / "boot" / "installer.bin").write_bytes(
            (work / "stage1.bin").read_bytes() + (work / "payload.bin").read_bytes()
        )
        shutil.copyfile(image, work / "tree" / "MELLIVORA.IMG")

        iso = work / "fault.iso"
        subprocess.run([
            xorriso, "-as", "mkisofs", "-R", "-J", "-iso-level", "3",
            "-V", "MELLIVORA", "-A", "Mellivora OS installer",
            "-b", "boot/installer.bin", "-no-emul-boot", "-boot-load-size", "4",
            "-o", str(iso), str(work / "tree"),
        ], check=True, capture_output=True)

        log = work / "screen.txt"
        proc = subprocess.run([
            sys.executable, str(AUTOINSTALL), str(iso), str(work / "target.img"),
            "--size", "512", "--timeout", "180", "--out", str(log), "--json",
        ], capture_output=True, text=True)

        screen = log.read_text(encoding="utf-8") if log.is_file() else ""
        caught = proc.returncode != 0 and MARKER in screen
        if caught:
            print("[installer-verify] PASS the faulty disc was rejected on the first chunk")
            return 0
        print("[installer-verify] FAIL the installer did not notice a mis-addressed write")
        print(f"  autoinstall exit: {proc.returncode}")
        print(screen[-800:] or proc.stderr[-800:])
        return 1
    finally:
        if "--keep" in args:
            print(f"[installer-verify] kept {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
