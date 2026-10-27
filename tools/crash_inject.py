#!/usr/bin/env python3
"""Crash-consistency harness for HBFS.

Boots a *copy* of the image, types a metadata-heavy command, kills QEMU at a
random moment (no clean shutdown, no cache flush), then asks fsck to prove the
filesystem is still consistent. Finally the copy is booted once more so the
kernel replays the write-ahead journal, and fsck runs again: the metadata must
end up either entirely old or entirely new, never a mixture.

Usage: crash_inject.py <image> "<command>" [options]

Options:
  --iterations N   number of crash points to try (default 5)
  --min-ms N       earliest kill time after the command is typed (default 50)
  --max-ms N       latest kill time (default 2500)
  --seed N         deterministic kill points
  --keep           keep the crashed images (printed paths)
  --quiet          only print the summary line

Exit: 0 if every iteration survives fsck, 1 otherwise.
"""
from __future__ import annotations

import os
import random
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from machine_profiles import qemu_args as machine_args  # noqa: E402

BOOT_MARKER = "MELLIVORA_BOOT_OK"
QEMU_CANDIDATES = (
    "qemu-system-x86_64",
    "qemu-system-i386",
    os.path.expanduser("~/.local/bin/qemu-system-x86_64"),
)


def find_qemu() -> str:
    for cand in QEMU_CANDIDATES:
        path = shutil.which(cand) if not os.path.isabs(cand) else cand
        if path and os.path.exists(path):
            return path
    raise SystemExit("crash_inject: no qemu-system-x86_64 on this machine")


def _opt(args: list[str], name: str, default=None):
    if name in args:
        i = args.index(name)
        if i + 1 < len(args):
            return args[i + 1]
    return default


class Serial:
    def __init__(self, path: str, timeout: float = 20.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(path)
                self.sock.settimeout(0.05)
                return
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.05)
        raise SystemExit(f"crash_inject: cannot connect to {path}")

    def wait_for(self, needle: str, timeout: float, proc) -> bool:
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if proc.poll() is not None:
                return False
            try:
                chunk = self.sock.recv(4096)
                if not chunk:
                    return False
                buf += chunk
                if needle.encode() in buf:
                    return True
            except socket.timeout:
                continue
        return False

    def send_line(self, text: str) -> None:
        self.sock.sendall((text + "\r\n").encode())


def boot(qemu: str, image: str, tmp: str) -> tuple[subprocess.Popen, str]:
    ser_path = os.path.join(tmp, "serial.sock")
    mon_path = os.path.join(tmp, "mon.sock")
    proc = subprocess.Popen(
        [qemu] + machine_args() +
        ["-drive", f"file={image},format=raw,if=ide,cache=writethrough",
         "-boot", "c", "-display", "none", "-no-reboot",
         "-serial", f"unix:{ser_path},server=on,wait=on",
         "-monitor", f"unix:{mon_path},server,nowait"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return proc, ser_path


def fsck(image: str) -> tuple[bool, str]:
    out = subprocess.run(
        [sys.executable, os.path.join(os.path.dirname(__file__), "fsck_hbfs.py"),
         image],
        capture_output=True, text=True)
    return out.returncode == 0, (out.stdout + out.stderr).strip()


def crash_once(qemu: str, image: str, command: str, kill_ms: float,
               verbose: bool) -> tuple[bool, list[str]]:
    """Boot a copy of *image*, run *command*, kill at *kill_ms*, verify."""
    notes: list[str] = []
    tmpdir = tempfile.mkdtemp(prefix="mellivora-crash-")
    copy = os.path.join(tmpdir, "crash.img")
    # A raw copy: QEMU opens it writethrough, so a completed ATA write is in the
    # host page cache and survives SIGKILL. Sparse-aware to keep it cheap.
    subprocess.run(["cp", "--sparse=always", image, copy], check=True)

    proc, ser_path = boot(qemu, copy, tmpdir)
    try:
        ser = Serial(ser_path)
        if not ser.wait_for(BOOT_MARKER, 30.0, proc):
            notes.append("boot marker never appeared")
            return False, notes
        time.sleep(0.2)
        ser.send_line(command)
        time.sleep(kill_ms / 1000.0)
    finally:
        try:
            proc.kill()
            proc.wait(timeout=5)
        except Exception:
            pass

    # The filesystem on the disk is allowed to be inconsistent at this point,
    # and with an *undo* journal it normally is: the transaction's pre-images
    # are in the journal and its metadata writes are half done. What must never
    # happen is reaching here with damage and no journal to explain it - that
    # would be a write that landed without its undo record, which no replay can
    # repair. So the immediate check is a diagnostic unless it finds a mess and
    # the volume is not marked for replay.
    ok, report = fsck(copy)
    dirty_journal = "journal is dirty" in report
    if not ok:
        if not dirty_journal:
            notes.append("fsck failed after the crash with no journal to replay")
            notes.extend("  " + line for line in report.splitlines()[-24:])
            return False, notes
        notes.append("torn transaction, journal dirty as expected")

    # Boot again so the kernel replays the journal, then re-check with the
    # VM shut down again.
    proc, ser_path = boot(qemu, copy, tmpdir)
    try:
        ser = Serial(ser_path)
        if not ser.wait_for(BOOT_MARKER, 30.0, proc):
            notes.append("second boot never reached the shell")
            return False, notes
        time.sleep(0.5)
    finally:
        try:
            proc.kill()
            proc.wait(timeout=5)
        except Exception:
            pass

    # After the replay the volume must be consistent: this is the assertion.
    ok, report = fsck(copy)
    if not ok:
        notes.append("fsck failed after journal replay")
        notes.extend("  " + line for line in report.splitlines()[-24:])
    elif dirty_journal:
        notes.append("replay restored the torn transaction")

    if verbose and ok:
        notes.append(report.splitlines()[-1].strip() if report else "")
    return ok, notes


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 2:
        raise SystemExit(__doc__)
    image, command = args[0], args[1]

    iterations = int(_opt(args, "--iterations", 5))
    min_ms = float(_opt(args, "--min-ms", 50))
    max_ms = float(_opt(args, "--max-ms", 2500))
    seed = _opt(args, "--seed")
    verbose = "--quiet" not in args

    if not os.path.exists(image):
        raise SystemExit(f"crash_inject: no such image: {image}")
    rng = random.Random(int(seed) if seed is not None else None)
    qemu = find_qemu()

    failures = 0
    for i in range(iterations):
        kill_ms = rng.uniform(min_ms, max_ms)
        ok, notes = crash_once(qemu, image, command, kill_ms, verbose)
        status = "PASS" if ok else "FAIL"
        print(f"  crash {i + 1}/{iterations} at {kill_ms:7.1f} ms  {status}")
        if notes:
            for line in notes:
                print("    " + line)
        if not ok:
            failures += 1

    if failures:
        print(f"  crash injection: {failures}/{iterations} runs left the "
              "filesystem inconsistent")
        return 1
    print(f"  crash injection: {iterations}/{iterations} runs recovered cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
