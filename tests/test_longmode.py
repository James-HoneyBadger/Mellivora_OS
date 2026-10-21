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
import re
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
# bus and the ATA disk answer, /init is read off that disk and executed, each
# makes three
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
    "hbfs] root=",
    "/init=",                # size varies with the demo program
    "/hello_tree=",
    "tasks=7 spaces=7",
    # The write demo: it creates /persist.txt, reads it back through the same
    # open/read path, makes /tmpdir, puts a file in it, unlinks it and sees the
    # directory go empty again.
    "read back: written by mellivora64",
    "write path ok",
    # The shell: a prompt with the working directory, a listing, a file's
    # contents, a rejected command and a clean exit.
    "mellivora:/> ",
    "  motd size=16",
    "sh: created probe",
    "  probe/ size=0",
    "sh: removed probe",
    "sh: unknown command",
    "bye",
    "exit=0x005A",
    "rtc=ok",
    "pipe=ok",
    "api ok: malloc/free, load, pipe",
    # /init exec's /hello with a command line, and /hello asks for it back
    # along with one environment entry.
    "args=alpha beta",
    "env os=mellivora64",
    # One of the 32-bit tree's own programs, built by `make programs` from
    # programs/hello.asm and loaded unchanged at its 0x00300000 program base.
    "Hello, World! Welcome to Mellivora OS!",
    "This program is running in 32-bit protected mode.",
    "32-bit file calls ok",
    # The directory demo: a listing of the root (including the docs
    # subdirectory), stat, a seek into /motd, then the same three calls in
    # /docs from the 64-bit task's own working directory.
    "name=docs type=2 size=0",
    "stat /motd type=1 size=16",
    "seek4=o fr",
    "cwd=/docs",
    "ent 0 name=readme.txt type=1 size=16",
    "miss=ok",
    "[exec] /hello",
    "hello from hbfs",
    "hello from 32-bit mode",
    "mode=32",
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


def boot_and_capture(qemu, image, mem_mb, want, script=()):
    """Boot the image with mem_mb of RAM and return the COM1 text once want appears.

    `script` is a list of (marker, line to type) steps: when a marker shows up on
    the console, that line is sent back down the serial line. That is how the
    shell task is driven - it is the last task to run and it waits for input, so
    nothing after it happens on its own.
    """
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
    steps = list(script)
    try:
        serial = qd.SerialConsole(ser_path)
        deadline = time.time() + TIMEOUT
        while time.time() < deadline:
            text = serial.text()
            if steps and steps[0][0] in text:
                serial.send_line(steps.pop(0)[1])
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


def check_written_volume(image):
    """Read the image the way a fresh mount would: the guest wrote these."""
    sys.path.insert(0, ROOT)
    import populate

    with open(image, "rb") as f:
        f.seek(populate.HBFS_ROOT_DIR_START * populate.SECTOR_SIZE)
        root = f.read(populate.HBFS_ROOT_DIR_SIZE)

        def entries(buf, count, entry_size):
            out = {}
            for i in range(count):
                off = i * entry_size
                if buf[off + 253] == populate.FTYPE_FREE:
                    continue
                name = buf[off:off + populate.HBFS_MAX_FILENAME]
                name = name.split(b"\0")[0].decode("ascii", "replace")
                size, start, blocks = __import__("struct").unpack_from(
                    "<III", buf, off + 256)
                out[name] = (buf[off + 253], size, start, blocks)
            return out

        top = entries(root, populate.HBFS_MAX_FILES,
                      populate.HBFS_DIR_ENTRY_SIZE)
        if "persist.txt" not in top:
            return "no /persist.txt in the root directory"
        ftype, size, start, blocks = top["persist.txt"]
        if ftype != populate.FTYPE_TEXT or size != 23:
            return "/persist.txt is type %d size %d" % (ftype, size)

        f.seek((populate.HBFS_DATA_START + start * populate.SECTORS_PER_BLOCK)
               * populate.SECTOR_SIZE)
        data = f.read(size)
        if data != b"written by mellivora64\n":
            return "/persist.txt reads %r" % data

        if "tmpdir" not in top:
            return "no /tmpdir in the root directory"
        ftype, _, start, blocks = top["tmpdir"]
        if ftype != populate.FTYPE_DIR or blocks != populate.HBFS_SUBDIR_BLOCKS:
            return "/tmpdir is type %d with %d blocks" % (ftype, blocks)

        f.seek((populate.HBFS_DATA_START + start * populate.SECTORS_PER_BLOCK)
               * populate.SECTOR_SIZE)
        inner = entries(f.read(blocks * populate.BLOCK_SIZE),
                        populate.HBFS_SUBDIR_MAX_FILES,
                        populate.HBFS_DIR_ENTRY_SIZE)
        if inner:
            return "/tmpdir still holds %s after unlink" % sorted(inner)

        if "probe" in top:
            return "the shell's 'rm probe' left the entry behind"
    return None


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

    # The shell is the last task: it prints a prompt, lists the working
    # directory, prints a file, rejects a command it does not know and leaves.
    SHELL_SCRIPT = (
        ("mellivora:/> ", "ls"),
        ("mellivora:/> ", "cat /motd"),
        ("mellivora:/> ", "date"),
        ("mellivora:/> ", "mkdir probe"),
        ("mellivora:/> ", "ls"),
        ("mellivora:/> ", "rm probe"),
        ("mellivora:/> ", "nonsense"),
        ("mellivora:/> ", "quit"),
    )
    text = boot_and_capture(qemu, IMAGE, 256, MARKER, SHELL_SCRIPT)
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

    stamp = re.search(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}", text)
    if not stamp:
        print("  FAIL  the shell never printed a date", file=sys.stderr)
        return 1
    guest_year = int(stamp.group(0)[:4])
    host_year = int(__import__("time").strftime("%Y"))
    if abs(guest_year - host_year) > 1:
        print("  FAIL  the clock says %s, this host says %d"
              % (stamp.group(0), host_year), file=sys.stderr)
        return 1

    # The guest wrote real HBFS structures, so the host can read them back with
    # the same constants populate.py builds images with.
    problem = check_written_volume(IMAGE)
    if problem:
        print("  FAIL  the volume the guest wrote is wrong: %s" % problem,
              file=sys.stderr)
        return 1

    # Scale: a guest past 4 GB only reports sane numbers if the E820 map was
    # read as 64-bit, and 8 GB is the P2 gate.
    if SKIP_BIG:
        print("  PASS  long mode, VMM, tasks, NX/SMEP/SMAP, syscalls, PCI/ATA, HBFS, exec, 32-bit mode (big-memory skipped)")
        return 0

    big = boot_and_capture(qemu, IMAGE, BIG_MB, MARKER, SHELL_SCRIPT)
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

    print("  PASS  long mode, VMM, tasks, NX/SMEP/SMAP, syscalls, PCI/ATA, HBFS, exec, 32-bit mode, %d MB of RAM" % usable_mb)
    return 0


if __name__ == "__main__":
    sys.exit(main())
