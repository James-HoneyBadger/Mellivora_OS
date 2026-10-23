#!/usr/bin/env python3
"""Boot the v4 volume and prove the mount did not write to it.

The 64-bit nucleus reads HBFS v3 and v4; a v4 volume mounts read-only, because
its metadata runs carry checksums this port does not recompute, so one write
would leave the volume failing its own audit. That is a claim about the *kernel*,
and the only way to check it is to boot it:

  * the mount line says which format it accepted,
  * a program reads a file off the volume (`hello from hbfs`),
  * the shell lists the volume and prints a file - the `ls` and `cat` the
    roadmap's exit criterion asks for, both of them reads,
  * the write self-test reports the skip instead of pretending to have written,
  * and the metadata - superblock, bitmap and root directory - is byte-identical
    afterwards, which no stray write could leave it.

The image is built by `make 64bit-v4`; this test only boots it and reads it.

Usage: tests/test_longmode_v4.py [image]

Env:
  LONGMODE_ALLOW_SKIP=1  exit 0 instead of failing when QEMU is missing
  LONGMODE_TIMEOUT=45    seconds to wait for the shell prompt
"""

import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import test_longmode as lm  # noqa: E402  (shares the boot harness)
import qemu_drive as qd  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
IMAGE = ARGS[0] if ARGS else os.path.join(ROOT, "mellivora64-v4.img")

# The shell is the last task and waits for input. Everything typed here is a
# read - a listing, a file, and the command that leaves - because a read-only
# volume has nothing to offer a write but a failure.
PROMPT = "mellivora:/> "
SHELL_SCRIPT = (
    (PROMPT, "ls"),
    (PROMPT, "cat /motd"),
    (PROMPT, "quit"),
)

SECTOR = 512
METADATA_START = 4096 * SECTOR           # superblock
METADATA_END = 4481 * SECTOR             # end of the root directory run


def metadata_digest(image):
    """A hash of the region every metadata write must touch."""
    with open(image, "rb") as f:
        f.seek(METADATA_START)
        return hashlib.sha256(f.read(METADATA_END - METADATA_START)).hexdigest()


def fail(message, text=None):
    print("  FAIL  %s" % message, file=sys.stderr)
    if text:
        print("---- serial ----", file=sys.stderr)
        print(text[-1200:], file=sys.stderr)
    return 1


def main():
    qemu = qd.find_qemu()
    if not qemu:
        if lm.ALLOW_SKIP:
            print("  SKIP  v4 boot test: no QEMU found on this host")
            return 0
        print("  FAIL  v4 boot test: no QEMU found", file=sys.stderr)
        return 1
    if not os.path.exists(IMAGE):
        print("  FAIL  v4 boot test: missing %s (run 'make 64bit-v4')" % IMAGE,
              file=sys.stderr)
        return 1

    before = metadata_digest(IMAGE)
    text = lm.boot_and_capture(qemu, IMAGE, 256, "bye", SHELL_SCRIPT)

    if lm.MARKER not in text:
        return fail("the nucleus did not report in (no %s)" % lm.MARKER, text)

    # The format it says it mounted, and that it says it is read-only.
    m = re.search(r"\[hbfs\][^\r\n]* version=(\d+)[^\r\n]*", text)
    if not m:
        return fail("the mount line does not report a version", text)
    if m.group(1) != "4":
        return fail("the image mounted as v%s, expect v4" % m.group(1), text)
    if not re.search(r"\[hbfs\][^\r\n]* features=0x[0-9A-Fa-f]{8} ro\b",
                     text):
        return fail("the mount line does not report the volume read-only", text)

    # Reads off the volume: the program's own read, then the shell's listing
    # and `cat`, which is what the sprint asks a v4 mount to do.
    if "hello from hbfs" not in text:
        return fail("/hello did not read /motd off the volume", text)
    if "  motd size=16" not in text:
        return fail("the shell's ls did not list the volume", text)
    if text.count("hello from hbfs") < 2:
        return fail("cat /motd printed nothing", text)
    if "bye" not in text:
        return fail("the shell did not leave cleanly", text)

    # The write self-test has to notice and say so, rather than claim a pass.
    if "[hbfs] append=skipped (read-only volume)" not in text:
        return fail("the write self-test did not report the read-only skip", text)
    if "read back:" in text or "write path ok" in text:
        return fail("something wrote to the volume anyway", text)

    # The statement this test exists for: the mount left the metadata alone.
    after = metadata_digest(IMAGE)
    if before != after:
        return fail("the boot changed the volume's metadata", text)

    audit = subprocess.run(
        [sys.executable, os.path.join(ROOT, "tools", "fsck_hbfs.py"), IMAGE,
         "--complain"],
        capture_output=True, text=True)
    if audit.returncode != 0:
        print(audit.stdout, file=sys.stderr)
        return fail("the volume fails its own checksum audit after a boot", text)

    print("  PASS  v4 mounted read-only, ls and cat off the volume, "
          "metadata unchanged, checksums intact")
    return 0


if __name__ == "__main__":
    sys.exit(main())
