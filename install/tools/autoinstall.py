#!/usr/bin/env python3
"""autoinstall.py - drive the Mellivora installer ISO in QEMU, unattended.

Boots the ISO with a blank target disk, answers the installer's prompts
("1", "YES"), waits for "Installation complete.", then checks that the target
now holds the image and boots it.

Usage:
    autoinstall.py <installer.iso> <target.img> [options]

Options:
  --size MB        create <target.img> with this many MiB when it is missing
                   (default 512)
  --keep           do not wipe an existing target image
  --display MODE   QEMU display (default none; use gtk or sdl to watch)
  --boot-check     boot the installed disk and require a shell prompt
  --timeout N      seconds to wait for the install to finish (default 900)
  --out FILE       write the captured screen output to FILE
  --json           emit a machine-readable result object

Exit codes: 0 success, 1 install failed, 2 setup problem.
"""
from __future__ import annotations

import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))

from qemu_drive import Monitor, find_qemu  # noqa: E402

MARKER_OK = "Installation complete."
MARKER_DONE = "Press any key."


def _opt(args, name, default=None):
    if name in args:
        return args[args.index(name) + 1]
    return default


def fail(msg: str, code: int = 2) -> "NoReturn":  # type: ignore[name-defined]
    print(f"autoinstall: {msg}", file=sys.stderr)
    raise SystemExit(code)


def make_target(path: str, size_mb: int, keep: bool) -> None:
    if os.path.exists(path) and keep:
        return
    with open(path, "wb") as f:
        f.truncate(size_mb * 1024 * 1024)


def image_payload(iso: str) -> bytes:
    """Read MELLIVORA.IMG's extent straight out of the ISO9660 tree."""
    with open(iso, "rb") as f:
        f.seek(16 * 2048)
        pvd = f.read(2048)
        if pvd[1:4] != b"CD0":
            fail("not an ISO9660 image")
        lba = struct.unpack_from("<I", pvd, 156 + 2)[0]
        size = struct.unpack_from("<I", pvd, 156 + 10)[0]
        f.seek(lba * 2048)

        def walk(dir_lba: int, dir_size: int, want: str, depth: int = 0):
            f.seek(dir_lba * 2048)
            data = f.read(dir_size)
            i = 0
            while i < len(data):
                length = data[i]
                if length == 0:
                    i = (i // 2048 + 1) * 2048
                    continue
                name_len = data[i + 32]
                name = data[i + 33:i + 33 + name_len].decode("latin1")
                base = name.split(";")[0]
                ent_lba = struct.unpack_from("<I", data, i + 2)[0]
                ent_size = struct.unpack_from("<I", data, i + 10)[0]
                flags = data[i + 25]
                if base.lower() == want.lower():
                    return ent_lba, ent_size
                if flags & 2 and depth < 4 and base not in ("\x00", "\x01"):
                    hit = walk(ent_lba, ent_size, want, depth + 1)
                    if hit:
                        return hit
                i += length
            return None

        hit = walk(lba, size, "MELLIVORA.IMG")
        if not hit:
            fail("MELLIVORA.IMG is not in the ISO")
        return hit


def run_install(iso: str, target: str, display: str = "none",
                timeout: float = 900.0) -> tuple[bool, str]:
    """Boot the ISO, install onto `target`, return (ok, screen log)."""
    qemu = find_qemu()
    tmp = tempfile.mkdtemp(prefix="mellivora-install-")
    mon_path = os.path.join(tmp, "mon.sock")
    proc = subprocess.Popen(
        [qemu, "-cpu", "486", "-m", "128",
         "-cdrom", iso,
         "-drive", f"file={target},format=raw,if=ide,cache=writeback",
         "-boot", "d", "-display", display, "-no-reboot",
         "-monitor", f"unix:{mon_path},server,nowait",
         "-serial", "null",
         "-audiodev", "none,id=snd0", "-machine", "pcspk-audiodev=snd0,usb=off"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log = []
    ok = False
    try:
        mon = Monitor(mon_path)
        deadline = time.time() + timeout
        # Wait for the disk list before answering the first prompt.
        while time.time() < deadline:
            time.sleep(1.0)
            screen = mon.screen()
            if "Install to which disk?" in screen:
                break
            if "No BIOS hard disk" in screen or "not found on this CD" in screen:
                log.append(screen)
                return False, "\n".join(log)
        else:
            return False, mon.screen()

        mon.send_text("1")
        time.sleep(1.0)
        while time.time() < deadline:
            screen = mon.screen()
            if "Type YES to continue" in screen:
                break
            time.sleep(0.5)
        mon.send_text("yes")
        time.sleep(0.4)
        mon.cmd("sendkey ret", 0.3)

        while time.time() < deadline:
            time.sleep(2.0)
            screen = mon.screen()
            if MARKER_OK in screen:
                ok = True
                break
            if "failed" in screen.lower() or "Installer stopped" in screen:
                break
        log.append(mon.screen())
        return ok, "\n".join(log)
    finally:
        proc.kill()
        proc.wait()
        shutil.rmtree(tmp, ignore_errors=True)


def boot_check(target: str, timeout: float = 120.0) -> bool:
    """Boot the installed disk and look for the shell prompt."""
    qemu = find_qemu()
    tmp = tempfile.mkdtemp(prefix="mellivora-verify-")
    mon_path = os.path.join(tmp, "mon.sock")
    proc = subprocess.Popen(
        [qemu, "-cpu", "486", "-m", "128",
         "-drive", f"file={target},format=raw,if=ide,cache=writethrough",
         "-boot", "c", "-display", "none", "-no-reboot",
         "-monitor", f"unix:{mon_path},server,nowait",
         "-serial", "null",
         "-audiodev", "none,id=snd0", "-machine", "pcspk-audiodev=snd0,usb=off"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        mon = Monitor(mon_path)
        deadline = time.time() + timeout
        while time.time() < deadline:
            time.sleep(1.0)
            if "Lair:/>" in mon.screen():
                return True
        return False
    finally:
        proc.kill()
        proc.wait()
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 2 or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0

    iso, target = args[0], args[1]
    size_mb = int(_opt(args, "--size", "512"))
    keep = "--keep" in args
    display = _opt(args, "--display", "none")
    do_boot = "--boot-check" in args
    timeout = float(_opt(args, "--timeout", "900"))
    out_file = _opt(args, "--out")

    if not os.path.isfile(iso):
        fail(f"ISO not found: {iso}")
    make_target(target, size_mb, keep)

    want_lba, want_size = image_payload(iso)
    ok, log = run_install(iso, target, display, timeout)
    if out_file:
        with open(out_file, "w") as f:
            f.write(log)

    result = {"ok": ok, "iso": iso, "target": target, "image_size": want_size}

    if ok:
        with open(iso, "rb") as f:
            f.seek(want_lba * 2048)
            head = f.read(min(want_size, 4 * 1024 * 1024))
        with open(target, "rb") as f:
            disk_head = f.read(len(head))
        result["first_bytes_match"] = head == disk_head
        if not result["first_bytes_match"]:
            ok = False
            result["ok"] = False

    if ok and do_boot:
        result["boots"] = boot_check(target)
        ok = result["boots"]
        result["ok"] = ok

    if "--json" in args:
        print(json.dumps(result, indent=2))
    else:
        print("install " + ("completed" if result.get("ok") else "FAILED"))
        if "first_bytes_match" in result:
            print("target starts with the image: " + ("yes" if result["first_bytes_match"] else "no"))
        if "boots" in result:
            print("installed disk boots: " + ("yes" if result["boots"] else "no"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
