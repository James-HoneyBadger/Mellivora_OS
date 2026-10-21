#!/usr/bin/env python3
"""64-bit boot regression test.

Builds nothing: it boots the 64-bit image produced by `make 64bit` under
QEMU with a 64-bit CPU model and asserts that stage2 negotiated long mode
and the nucleus reported in over COM1.

Usage: tests/test_longmode.py [image]

The default assertions are the ones the C nucleus establishes: the image runs
at its link address in the top 2 GB, a `syscall` reaches a handler, and a
ring-3 program runs and calls back in. Pass
--marker-only for the hand-written asm nucleus, which only proves the boot.

Usage: tests/test_longmode.py [image] [--marker-only]

The default (C nucleus) run boots twice: 256 MB for the boot assertions, then a
large guest to prove the frame allocator reads a 64-bit E820 map - a guest past
4 GB only reports the right totals if it does. 8 GB is the roadmap's scale gate.

Env:
  LONGMODE_ALLOW_SKIP=1  exit 0 instead of failing when QEMU is missing
  LONGMODE_TIMEOUT=45    seconds to wait for the marker
  LONGMODE_BIG_MB=8192   guest size for the big-memory run
  LONGMODE_SKIP_BIG=1    skip it (hosts that cannot give QEMU that much RAM)
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
# the image runs at its *link* address in the top 2 GB, the SYSCALL/SYSRET MSRs
# are programmed and a 64-bit `syscall` reaches a handler and returns, the frame
# allocator and the page tables agree about a mapped page (and the MMU agrees
# once it is unmapped), the slab heap hands out distinct zeroed objects across
# two slabs, two ring-3 tasks with their own page tables run with interrupts on,
# are preempted by timer ticks and use write/brk/yield over LP64 syscalls, the PCI
# bus and the ATA disk answer, each makes three
# syscalls from the user page and exits with the value it round-tripped through
# its own stack, and a fault it takes is caught on the kernel stack the TSS
# names and resumed.
REQUIRED = (
    "MELLIVORA64_OK",
    "base=0xFFFFFFFF80100000",
    "syscall=ok",
    "pmm=ok",
    "vmm=ok",
    "kmem=ok",
    "ring3=ok",
    "fault=ok",
    "timer=ok",
    "sched=ok",
    "nx=ok",
    "nx=on smep=on smap=on",
    "abi=lp64",
    "brk=ok",
    "hello",
    "blk=ok",
)

# A 4 GB guest has usable E820 ranges above 0x100000000 (there is a hole below
# 4 GB), so the frame allocator only reports the right numbers if it read the
# map as 64-bit. The roadmap's scale gate is 8 GB, which is what this asks for.
BIG_MB = int(os.environ.get("LONGMODE_BIG_MB", "8192"))

# qemu64 leaves SMEP and SMAP out; the nucleus enables them when it sees them, so
# the test asks for a CPU that has them and requires the hardened report. NX is
# part of the base model either way.
CPU_MODEL = os.environ.get("LONGMODE_CPU", "qemu64,+smep,+smap")
SKIP_BIG = os.environ.get("LONGMODE_SKIP_BIG", "0") == "1"


def boot_and_capture(qemu, image, mem_mb, want):
    """Boot the image with mem_mb of RAM and return the COM1 text once want appears."""
    workdir = tempfile.mkdtemp(prefix="lm-test-")
    ser_path = os.path.join(workdir, "serial.sock")
    proc = subprocess.Popen(
        [qemu, "-cpu", CPU_MODEL, "-m", str(mem_mb),
         "-drive", "file=%s,format=raw,if=ide,cache=writethrough" % image,
         "-boot", "c", "-display", "none", "-no-reboot",
         "-serial", "unix:%s,server=on,wait=on" % ser_path,
         "-monitor", "none", "-net", "none"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    text = ""
    try:
        serial = qd.SerialConsole(ser_path)
        deadline = time.time() + TIMEOUT
        while time.time() < deadline:
            text = serial.text()
            if want in text:
                break
            if proc.poll() is not None:
                break
            time.sleep(0.3)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
    return text


def parse_pmm(text):
    """Pull usable=MB and top=0x... out of the nucleus report."""
    m = __import__("re").search(r"usable=(\d+)MB top=0x([0-9A-F]+)", text)
    if not m:
        return None
    return int(m.group(1)), int(m.group(2), 16)


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

    text = boot_and_capture(qemu, IMAGE, 256, MARKER)
    required = (MARKER,) if MARKER_ONLY else REQUIRED
    missing = [needle for needle in required if needle not in text]
    if missing:
        print("  FAIL  missing on COM1: %s" % ", ".join(missing), file=sys.stderr)
        print("---- serial ----", file=sys.stderr)
        print(text[-800:], file=sys.stderr)
        return 1

    if MARKER_ONLY:
        print("  PASS  long mode entered and nucleus reported")
        return 0

    # Scale: a guest past 4 GB only reports sane numbers if the E820 map was
    # read as 64-bit, and 8 GB is the P2 gate.
    if SKIP_BIG:
        print("  PASS  long mode, VMM, slab heap, tasks, NX/SMEP/SMAP, LP64 syscalls, PCI/ATA (big-memory run skipped)")
        return 0

    big = boot_and_capture(qemu, IMAGE, BIG_MB, MARKER)
    stats = parse_pmm(big)
    if stats is None:
        print("  FAIL  no frame-allocator report at %d MB" % BIG_MB, file=sys.stderr)
        print(big[-800:], file=sys.stderr)
        return 1
    usable_mb, top = stats
    # Not all of the guest is usable: the firmware reserves MMIO just below 4 GB
    # (QEMU puts the PCI hole at ~0xB0000000), so a fraction of the RAM is
    # missing from the report by design. 85% still fails loudly if the map was
    # read as 32-bit, which is the mistake this checks for.
    expected = BIG_MB * 85 // 100
    if usable_mb < expected or top <= 0x100000000:
        print("  FAIL  %d MB guest reported usable=%d MB top=0x%X"
              % (BIG_MB, usable_mb, top), file=sys.stderr)
        print(big[-800:], file=sys.stderr)
        return 1

    print("  PASS  long mode, VMM, slab heap, tasks, NX/SMEP/SMAP, LP64 syscalls, PCI/ATA, %d MB of RAM" % usable_mb)
    return 0


if __name__ == "__main__":
    sys.exit(main())
