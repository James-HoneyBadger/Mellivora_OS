#!/usr/bin/env python3
"""Boot the v4 volume, write through its journal, and audit what is left.

The 64-bit nucleus reads HBFS v3 and v4; a v4 volume that advertises a journal
mounts writable, and every metadata operation goes through the undo log, with
each touched run restamped so its checksum still holds afterwards. That is a
claim about the *kernel*, and the only way to check it is to boot it and then
read the volume back from outside:

  * the mount line says which format it accepted and that it is writable,
  * a program reads a file off the volume (`hello from hbfs`),
  * the shell lists the volume and prints a file - the `ls` and `cat` the
    roadmap's exit criterion asks for,
  * the write self-test finishes (`append=ok move=ok`) instead of reporting a
    skip, which means create, grow, move and remove all went through the journal,
  * the shell creates and removes a file of its own,
  * the extent file still reads (`cat /frag`), and
  * afterwards the metadata is *different* - something really was written - and
    `fsck_hbfs.py` still calls the volume consistent, checksums included. That
    last pair is the whole point: a writer that left a stale run checksum would
    pass the first and fail the second.

The image is built by `make 64bit-v4`; this test boots it, writes to it and then
audits it from the host.

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
    (PROMPT, "mkdir probe"),
    (PROMPT, "append /fresh.txt first"),
    (PROMPT, "rm probe"),
    # /frag is the one file whose entry names an extent block instead of its
    # data, so this read is the only thing in the image that exercises the
    # mapping: three runs, and the last line is in the third of them.
    (PROMPT, "cat /frag"),
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
    mount = [line for line in text.split("\r\n") if "[hbfs] root=" in line]
    if not mount:
        return fail("the mount line is missing", text)
    if not re.search(r"features=0x[0-9A-Fa-f]{8}$", mount[0].rstrip()):
        return fail("the mount line does not end at the feature word, so the "
                    "volume mounted read-only: %r" % mount[0], text)

    # Reads off the volume: the program's own read, then the shell's listing
    # and `cat`, which is what the sprint asks a v4 mount to do.
    if "hello from hbfs" not in text:
        return fail("/hello did not read /motd off the volume", text)
    if "  motd size=16" not in text:
        return fail("the shell's ls did not list the volume", text)
    if text.count("hello from hbfs") < 2:
        return fail("cat /motd printed nothing", text)
    # The extent file: first line, last line, and the trailer, which are in
    # three different runs. A reader that ignored the extent block would print
    # the extent records as if they were text and none of these would appear.
    for needle in ("line 001:", "line 130:", "END OF THE FRAGMENTED FILE"):
        if needle not in text:
            return fail("cat /frag is missing %r (extent mapping?)" % needle, text)
    if "bye" not in text:
        return fail("the shell did not leave cleanly", text)

    # The write self-test is the journal's first customer: create, grow, move
    # and remove, each one a transaction.
    if "[hbfs] append=ok move=ok" not in text:
        return fail("the write self-test did not finish through the journal", text)

    # ...and the shell created and removed a file of its own.
    if "sh: created probe" not in text or "sh: removed probe" not in text:
        return fail("the shell's mkdir/rm did not go through", text)
    if "sh: appended to /fresh.txt" not in text:
        return fail("the shell's append did not go through", text)

    # The statement this test exists for: something was written, and the volume
    # is still consistent - which means every run the writer touched was
    # restamped, not just rewritten.
    after = metadata_digest(IMAGE)
    if before == after:
        return fail("nothing was written to the volume", text)

    for args in (["--complain"], []):
        audit = subprocess.run(
            [sys.executable, os.path.join(ROOT, "tools", "fsck_hbfs.py"), IMAGE]
            + args,
            capture_output=True, text=True)
        if audit.returncode != 0:
            print(audit.stdout, file=sys.stderr)
            return fail("the volume the guest wrote is not consistent", text)

    print("  PASS  v4 mounted writable, writes went through the journal "
          "(including the extent file), and the volume still checks out")
    return 0


if __name__ == "__main__":
    sys.exit(main())
