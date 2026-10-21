#!/usr/bin/env python3
"""64-bit boot regression test.

Builds nothing: it boots the 64-bit image produced by `make 64bit` under
QEMU with a 64-bit CPU model and asserts that stage2 negotiated long mode
and the nucleus reported in over COM1.

Usage: tests/test_longmode.py [image]

The default assertions are the ones the C nucleus establishes: the image runs
at its link address in the top 2 GB and a `syscall` reaches a handler. Pass
--marker-only for the hand-written asm nucleus, which only proves the boot.

Usage: tests/test_longmode.py [image] [--marker-only]

Env:
  LONGMODE_ALLOW_SKIP=1  exit 0 instead of failing when QEMU is missing
  LONGMODE_TIMEOUT=45    seconds to wait for the marker
"""

import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import qemu_drive as qd  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
IMAGE = ARGS[0] if ARGS else os.path.join(ROOT, "mellivora64.img")
# The hand-written asm nucleus proves the boot path only - it has no C runtime,
# so it cannot report the higher half or a SYSCALL round trip. --marker-only
# tests that nucleus; the default asserts everything the C port establishes.
MARKER_ONLY = "--marker-only" in sys.argv[1:]
TIMEOUT = int(os.environ.get("LONGMODE_TIMEOUT", "45"))
ALLOW_SKIP = os.environ.get("LONGMODE_ALLOW_SKIP", "0") == "1"
MARKER = "MELLIVORA64_OK"

# A boot marker alone would also be produced by a nucleus that ran in the low
# half. These are the facts the port has to establish, so the test checks them:
# the image runs at its *link* address in the top 2 GB, and the SYSCALL/SYSRET
# MSRs are programmed and a 64-bit `syscall` reaches a handler and returns.
REQUIRED = (
    "MELLIVORA64_OK",
    "base=0xFFFFFFFF80100000",
    "syscall=ok",
)


def main():
    qemu = qd.find_qemu()
    if not qemu:
        if ALLOW_SKIP:
            print("  SKIP  long-mode test: no QEMU found on this host")
            return 0
        print("  FAIL  long-mode test: no QEMU found", file=sys.stderr)
        return 1
    if not os.path.exists(IMAGE):
        print("  FAIL  long-mode test: missing %s (run 'make 64bit')" % IMAGE, file=sys.stderr)
        return 1

    workdir = tempfile.mkdtemp(prefix="lm-test-")
    ser_path = os.path.join(workdir, "serial.sock")
    proc = subprocess.Popen(
        [qemu, "-cpu", "qemu64", "-m", "256",
         "-drive", "file=%s,format=raw,if=ide,cache=writethrough" % IMAGE,
         "-boot", "c", "-display", "none", "-no-reboot",
         "-serial", "unix:%s,server=on,wait=on" % ser_path,
         "-monitor", "none", "-net", "none"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = False
    try:
        serial = qd.SerialConsole(ser_path)
        deadline = time.time() + TIMEOUT
        text = ""
        while time.time() < deadline:
            text = serial.text()
            if MARKER in text:
                break
            if proc.poll() is not None:
                break
            time.sleep(0.3)
        required = (MARKER,) if MARKER_ONLY else REQUIRED
        missing = [needle for needle in required if needle not in text]
        ok = not missing
        if ok:
            if MARKER_ONLY:
                print("  PASS  long mode entered and nucleus reported")
            else:
                print("  PASS  long mode, higher half and SYSCALL entry")
        else:
            print("  FAIL  missing on COM1: %s" % ", ".join(missing), file=sys.stderr)
            print("---- serial ----", file=sys.stderr)
            print(text[-800:], file=sys.stderr)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
