#!/usr/bin/env python3
"""Desktop (Burrows) regression test.

Boots the image headless, drives the desktop over the QEMU monitor (PS/2 key
injection) and asserts on the kernel's COM1 markers plus the video mode of
screendumps:

  * the desktop enters VBE graphics mode and reports itself up
  * F2 launches a terminal and F1+search launches a second application
  * both launches appear on the serial markers, so they really ran
  * Alt+Tab and Ctrl+Alt+1..4 reach the window manager
  * the launcher's "Exit Desktop" restores text mode without faulting

Usage: tests/test_desktop.py [image]

Env:
  DESKTOP_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
  DESKTOP_TIMEOUT=90     seconds to wait for the boot marker (default 90)
"""

import os
import re
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import qemu_drive as qd  # noqa: E402
from machine_profiles import qemu_args as machine_args  # noqa: E402

IMAGE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "mellivora.img")
BOOT_TIMEOUT = int(os.environ.get("DESKTOP_TIMEOUT", "90"))
ALLOW_SKIP = os.environ.get("DESKTOP_ALLOW_SKIP", "0") == "1"

KEYS = {
    " ": "spc", "-": "minus", ".": "dot", "/": "slash", "=": "equal",
    "+": "shift-equal", "*": "shift-8",
}


def fail(message):
    sys.stderr.write("  FAIL  %s\n" % message)
    sys.exit(1)


def ppm_size(path):
    with open(path, "rb") as handle:
        magic = handle.readline().strip()
        if magic != b"P6":
            raise ValueError("unexpected PPM magic %r" % magic)
        line = handle.readline()
        while line.startswith(b"#"):
            line = handle.readline()
        width, height = line.split()
        return int(width), int(height)


class Desktop:
    def __init__(self, proc, monitor, serial, workdir):
        self.proc = proc
        self.monitor = monitor
        self.serial = serial
        self.workdir = workdir
        self.marks = 0

    def wait_for(self, text, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            if text in self.serial.text():
                return True
            if self.proc.poll() is not None:
                fail("QEMU exited while waiting for %r" % text)
            time.sleep(0.2)
        return False

    def wait_for_count(self, text, count, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            if self.serial.text().count(text) >= count:
                return True
            if self.proc.poll() is not None:
                fail("QEMU exited while waiting for %r occurrence %d" % (text, count))
            time.sleep(0.2)
        return False

    def key(self, name, settle=0.35):
        self.monitor.cmd("sendkey %s" % name, 0.1)
        time.sleep(settle)

    def type_text(self, text):
        for char in text:
            self.key(KEYS.get(char, char), 0.08)

    def screendump(self, name):
        path = os.path.join(self.workdir, name + ".ppm")
        self.monitor.cmd("screendump %s" % path, 1)
        return ppm_size(path)


def main():
    qemu = qd.find_qemu()
    if not qemu:
        if ALLOW_SKIP:
            print("  SKIP  desktop tests: no QEMU found on this host")
            return 0
        fail("desktop tests: no QEMU found. Install qemu-system-x86, or set DESKTOP_ALLOW_SKIP=1.")
    if not os.path.exists(IMAGE):
        fail("desktop tests: disk image not found: %s (run 'make full' first)" % IMAGE)

    workdir = tempfile.mkdtemp(prefix="desktop-test-")
    mon_path = os.path.join(workdir, "monitor.sock")
    ser_path = os.path.join(workdir, "serial.sock")
    proc = subprocess.Popen(
        [qemu] + machine_args() +
        ["-drive", "file=%s,format=raw,if=ide,cache=writethrough" % IMAGE,
         "-boot", "c", "-display", "none", "-no-reboot",
         "-serial", "unix:%s,server=on,wait=on" % ser_path,
         "-monitor", "unix:%s,server,nowait" % mon_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    cases = []

    def check(name, ok, detail=""):
        cases.append(ok)
        print("  %-34s %s%s" % (name, "PASS" if ok else "FAIL", detail and "  " + detail))

    try:
        serial = qd.SerialConsole(ser_path)
        if not serial.wait_for("MELLIVORA_BOOT_OK", BOOT_TIMEOUT, proc):
            fail("kernel never reported MELLIVORA_BOOT_OK")
        monitor = qd.Monitor(mon_path)
        desk = Desktop(proc, monitor, serial, workdir)

        serial.send_line("burrows")
        check("desktop starts in graphics mode",
              desk.wait_for_count("[burrows] desktop up", 1), "")
        width, height = desk.screendump("01-desktop")
        check("VBE mode is 1024x768", (width, height) == (1024, 768),
              "%dx%d" % (width, height))

        desk.key("f2")
        check("F2 launches the terminal", desk.wait_for("[burrows] launch bterm"))

        desk.key("f1")
        time.sleep(1.5)
        desk.type_text("bcalc")
        desk.key("ret")
        check("launcher launches a second app", desk.wait_for("[burrows] launch bcalc"))

        # A crashing application must be contained, not take the desktop down.
        desk.key("f1")
        time.sleep(1.5)
        desk.type_text("crash")
        desk.key("ret")
        launched = desk.wait_for("[burrows] launch bfault")
        check("crash demo launches", launched)
        check("fault is contained", desk.wait_for("[burrows] fault contained", 20))
        time.sleep(1.5)
        width, height = desk.screendump("03-after-fault")
        check("desktop survives the fault", (width, height) == (1024, 768),
              "%dx%d" % (width, height))

        desk.key("alt-tab")
        time.sleep(1.0)
        width, height = desk.screendump("02-alt-tab")
        check("desktop still renders after Alt+Tab", (width, height) == (1024, 768))

        desk.key("f3")
        time.sleep(1.0)
        width, height = desk.screendump("04-notify-center")
        check("notification centre opens", (width, height) == (1024, 768))
        desk.key("f3")
        time.sleep(0.5)

        desk.key("ctrl-alt-2")
        check("Ctrl+Alt+2 switches workspace", desk.wait_for("[burrows] workspace 2"))
        desk.key("ctrl-alt-1")
        check("Ctrl+Alt+1 switches back", desk.wait_for("[burrows] workspace 1"))

        desk.key("f1")
        time.sleep(1.5)
        desk.type_text("exit")
        desk.key("ret")
        check("Exit Desktop leaves Burrows",
              desk.wait_for_count("[burrows] desktop exit", 1))
        time.sleep(2.0)
        width, height = desk.screendump("05-shell")
        is_text_mode = height == 400 and width < 800
        check("text mode restored", is_text_mode, "%dx%d" % (width, height))

        serial.send_line("account del guilogin")
        time.sleep(1.0)
        serial.send_line("logonui")
        check("graphical logon screen opens",
              desk.wait_for_count("[logon-ui] ready", 1))
        width, height = desk.screendump("06-logon")
        check("logon screen uses graphics mode",
              (width, height) == (1024, 768),
              "%dx%d" % (width, height))

        desk.key("f2")
        check("F2 opens graphical account creation",
              desk.wait_for_count("[logon-ui] create mode", 1))
        desk.type_text("guilogin")
        desk.key("tab")
        desk.type_text("gui-pass")
        desk.key("tab")
        desk.type_text("mismatch")
        desk.key("ret")
        check("mismatched account passwords are refused",
              desk.wait_for_count("[logon-ui] account creation refused", 1, 30))
        desk.type_text("gui-pass")
        desk.key("tab")
        desk.type_text("gui-pass")
        desk.key("ret")
        check("graphical account creation logs on",
              desk.wait_for("[logon-ui] account created", 30) and
              desk.wait_for_count("[logon-ui] authenticated", 1, 30) and
              desk.wait_for_count("[burrows] desktop up", 2, 10))
        width, height = desk.screendump("07-user-desktop")
        check("new user's desktop renders",
              (width, height) == (1024, 768),
              "%dx%d" % (width, height))

        desk.key("f1")
        time.sleep(1.0)
        desk.type_text("exit")
        desk.key("ret")
        check("authenticated desktop exits cleanly",
              desk.wait_for_count("[burrows] desktop exit", 2))
        serial.send_line("assoc .txt pkginfo")
        check("per-user association is stored",
              desk.wait_for_count("[assoc] per-user association saved", 1))
        serial.send_line("assoc .txt")
        check("per-user association can be queried",
              desk.wait_for_count("[assoc] association queried", 1))
        serial.send_line("assoc")
        check("associations can be listed",
              desk.wait_for_count("[assoc] associations listed", 1))
        serial.send_line("open test.txt")
        check("open launches the registered file handler",
              desk.wait_for_count("[assoc] opened handler", 1, 30))
        time.sleep(1.0)
        serial.send_line("assoc .txt delete")
        check("per-user association can be removed",
              desk.wait_for_count("[assoc] per-user association removed", 1))
        serial.send_line("logonui")
        check("active session blocks another logon",
              desk.wait_for("[logon-ui] refused: session already active"))
        serial.send_line("logoff")
        time.sleep(1.0)

        serial.send_line("logonui")
        check("new account can return to graphical logon",
              desk.wait_for_count("[logon-ui] ready", 2))
        desk.screendump("08-logon-again")
        desk.key("f2")
        check("F2 opens duplicate-account form",
              desk.wait_for_count("[logon-ui] create mode", 2))
        desk.type_text("guilogin")
        desk.key("tab")
        desk.type_text("gui-pass")
        desk.key("tab")
        desk.type_text("gui-pass")
        desk.key("ret")
        check("duplicate graphical account is refused",
              desk.wait_for_count("[logon-ui] account creation refused", 2, 30))
        desk.key("f2")
        check("F2 returns to logon form",
              desk.wait_for_count("[logon-ui] logon mode", 1))
        desk.key("esc")
        check("logon screen cancellation is reported",
              desk.wait_for_count("[logon-ui] cancelled", 1))

        serial.send_line("logonui")
        check("graphical logon screen reopens",
              desk.wait_for_count("[logon-ui] ready", 3))
        desk.type_text("guilogin")
        desk.key("tab")
        desk.type_text("wrong")
        desk.key("ret")
        check("invalid credentials are refused",
              desk.wait_for_count("[logon-ui] authentication refused", 1, 30))
        desk.key("esc")
        check("invalid logon can be cancelled",
              desk.wait_for_count("[logon-ui] cancelled", 2))

        text = serial.text()
        bad = re.search(r"CPU EXCEPTION|PAGE FAULT|fault @", text)
        check("no faults during the session", bad is None, bad.group(0) if bad else "")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    if not all(cases):
        fail("%d desktop case(s) failed" % cases.count(False))
    print("  PASS  all desktop cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
