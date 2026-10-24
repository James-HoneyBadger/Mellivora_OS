#!/usr/bin/env python3
"""lm_console.py - boot a Mellivora image and drive its console.

Shared by the 64-bit milestone tests (M1 onwards). It boots an image under QEMU,
waits for a marker on COM1, types command lines at the shell, and returns what
the guest said. Two details matter and both are easy to get wrong:

  * the guest writes to its own volume, so every session runs on a private copy
    - otherwise the second run of a test sees the first run's files;
  * the shell is a task, so a line typed before it reaches its read loop is
    lost. A line is retyped while the expected output has not appeared.

Usage:
    lm_console.py IMAGE "cmd" ["cmd" ...] [options]

Options:
    --expect TEXT     require TEXT in the captured output (repeatable)
    --reject TEXT     fail if TEXT appears (repeatable)
    --marker TEXT     readiness marker (default: the shell prompt)
    --timeout N       seconds to wait for the marker (default 90)
    --settle N        seconds between a prompt and the next typed line (1.0)
    --mem MB          guest RAM (default 256)
    --keep            keep the private image copy and print its path
    --json            machine-readable result
    --out FILE        write the captured console text to FILE

Exit: 0 when every expectation held, 1 otherwise, 2 for a usage problem.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import qemu_drive as qd  # noqa: E402

PROMPT = "mellivora:/> "
# Ready for input is the shell's prompt, not the nucleus's OK line: the OK line
# is printed once the demo session has finished, so waiting for it before typing
# anything would wait forever.
DEFAULT_MARKER = PROMPT

# The nucleus needs a CPU with SMEP/SMAP to exercise those paths, and it has an
# e1000 driver (not an rtl8139): booting with another NIC leaves the network
# initialisation waiting and the boot marker never arrives.
CPU_MODEL = os.environ.get("LONGMODE_CPU", "qemu64,+smep,+smap")
NIC = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]



def scratch_dir(prefix: str) -> str:
    """A working directory for a booted guest's private image.

    The image is hundreds of megabytes to gigabytes, and /tmp is a tmpfs on
    many hosts - a few copies there fill it and the next boot fails with
    ENOSPC. Prefer $LM_SCRATCH, then the repository's .build (on the same
    filesystem as the image), then the system temp directory.
    """
    candidates = [os.environ.get("LM_SCRATCH"), os.path.join(ROOT, ".build")]
    for base in candidates:
        if not base:
            continue
        try:
            os.makedirs(base, exist_ok=True)
            return tempfile.mkdtemp(prefix=prefix, dir=base)
        except OSError:
            continue
    return tempfile.mkdtemp(prefix=prefix)

class Session:
    """One booted guest, with a private copy of its disk image."""

    def __init__(self, image: str, mem_mb: int = 256, marker: str = DEFAULT_MARKER,
                 timeout: float = 90.0, keep: bool = False):
        self.image = image
        self.mem_mb = mem_mb
        self.marker = marker
        self.timeout = timeout
        self.keep = keep
        self.workdir = scratch_dir("lm-session-")
        self.disk = os.path.join(self.workdir, "image.img")
        shutil.copyfile(image, self.disk)
        self.proc = None
        self.serial = None

    def __enter__(self) -> "Session":
        ser_path = os.path.join(self.workdir, "serial.sock")
        self.proc = subprocess.Popen(
            [qd.find_qemu(), "-cpu", CPU_MODEL, "-m", str(self.mem_mb),
             "-drive", "file=%s,format=raw,if=ide,cache=writethrough" % self.disk,
             "-boot", "c", "-display", "none", "-no-reboot",
             "-serial", "unix:%s,server=on,wait=on" % ser_path,
             "-audiodev", "none,id=snd0",
             "-machine", "pcspk-audiodev=snd0,usb=off"] + NIC,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.serial = qd.SerialConsole(ser_path)
        if not self.serial.wait_for(self.marker, self.timeout, self.proc):
            raise RuntimeError("the boot marker %r never appeared" % self.marker)
        return self

    def __exit__(self, *exc) -> None:
        if self.proc is not None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()
        if self.keep:
            print("[lm-console] kept %s" % self.workdir, file=sys.stderr)
        else:
            shutil.rmtree(self.workdir, ignore_errors=True)

    def text(self) -> str:
        return self.serial.text()

    def command(self, line: str, expect: str | None = None,
                settle: float = 1.0, tries: int = 3,
                timeout: float = 15.0) -> str:
        """Type one line, and retype it while `expect` has not shown up."""
        prompt_seen = self.serial.text().count(PROMPT)
        for attempt in range(tries):
            time.sleep(settle)
            self.serial.send_line(line)
            if expect is None:
                time.sleep(settle)
                return self.text()
            deadline = time.time() + timeout
            while time.time() < deadline:
                if expect in self.text():
                    return self.text()
                time.sleep(0.1)
            if attempt + 1 < tries:
                # The shell may not have been reading yet: wait for the prompt
                # count to grow before retyping, so the retry is not another
                # keystroke into the same gap.
                wait_until = time.time() + 5
                while time.time() < wait_until:
                    if self.text().count(PROMPT) > prompt_seen:
                        break
                    time.sleep(0.1)
        return self.text()


def _opt(args: list[str], name: str, default=None):
    return args[args.index(name) + 1] if name in args else default


def main() -> int:
    argv = [a for a in sys.argv[1:]]
    flags_with_value = {"--expect", "--reject", "--marker", "--timeout",
                        "--settle", "--mem", "--out"}
    positional: list[str] = []
    skip = False
    for i, arg in enumerate(argv):
        if skip:
            skip = False
            continue
        if arg in flags_with_value:
            skip = True
            continue
        if arg.startswith("--"):
            continue
        positional.append(arg)

    if len(positional) < 2 or "--help" in argv or "-h" in argv:
        print(__doc__)
        return 0 if positional else 2

    image, commands = positional[0], positional[1:]
    expects = [argv[i + 1] for i, a in enumerate(argv[:-1]) if a == "--expect"]
    rejects = [argv[i + 1] for i, a in enumerate(argv[:-1]) if a == "--reject"]
    marker = _opt(argv, "--marker", DEFAULT_MARKER)
    timeout = float(_opt(argv, "--timeout", "90"))
    settle = float(_opt(argv, "--settle", "1.0"))
    mem_mb = int(_opt(argv, "--mem", "256"))
    out_file = _opt(argv, "--out")

    if not os.path.isfile(image):
        print("lm-console: no such image: %s" % image, file=sys.stderr)
        return 2

    try:
        with Session(image, mem_mb, marker, timeout, "--keep" in argv) as session:
            for line in commands:
                want = next((e for e in expects if e not in session.text()), None)
                session.command(line, want, settle=settle)
            text = session.text()
    except RuntimeError as exc:
        print("lm-console: FAIL %s" % exc, file=sys.stderr)
        return 1

    if out_file:
        with open(out_file, "w", encoding="utf-8") as f:
            f.write(text)

    missing = [e for e in expects if e not in text]
    present = [r for r in rejects if r in text]
    ok = not missing and not present

    if "--json" in argv:
        print(json.dumps({"ok": ok, "image": image, "missing": missing,
                          "rejected": present, "chars": len(text)}, indent=2))
    else:
        for needle in missing:
            print("  FAIL  missing: %r" % needle)
        for needle in present:
            print("  FAIL  must not appear: %r" % needle)
        print("lm-console: %s (%d characters captured)"
              % ("PASS" if ok else "FAIL", len(text)))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
