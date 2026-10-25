#!/usr/bin/env python3
"""Boot the image, run one or two shell commands, and assert on the result.

The guest now accepts input on COM1 as well as PS/2, so commands are normally
typed over a Unix serial socket (fast and deterministic). The QEMU monitor is
still used to read the VGA text buffer at 0xB8000 and to dump registers, which
makes it possible to assert on output as a user would see it.

Usage: qemu_drive.py <image> "<command>" [options]

Options:
  --then CMD          run another command after the first (repeatable)
  --wait N            keep polling for --expect text for up to N seconds
                      after the command is typed (default 6)
  --timeout N         seconds to wait for the boot marker (default 60)
  --via serial|sendkey  input transport (default serial)
  --expect TEXT       require TEXT in serial+screen (repeatable)
  --reject TEXT       fail if TEXT appears in serial+screen (repeatable)
  --regs              dump CPU registers at the end
  --screendump FILE   save the framebuffer as PNG (works in VBE graphics modes)
  --json              emit a machine-readable result object
  --out FILE          write the combined serial+screen output to FILE
  --mem MB            guest RAM in MB (default 128)
  --cpu MODEL         CPU model (default 486); a model with a local APIC is
                      needed to exercise the APIC interrupt path
  --smp N             give the guest N CPUs (default: QEMU's, i.e. 1); used to
                      prove a reported CPU count is read, not assumed

Exit: 0 on success, 1 if a CPU exception occurred, an --expect text was missing,
      an --reject text appeared, or the boot marker never showed up.
"""
from __future__ import annotations

import json
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

# VGA text mode: 80x25, char/attribute pairs starting at 0xB8000.
COLS, ROWS = 80, 25

KEYS = {
    "\n": "ret", "\r": "ret", "\t": "tab", " ": "spc", "&": "shift-7", "|": "shift-backslash", "-": "minus",
    ".": "dot", "/": "slash", ":": "shift-semicolon", "=": "equal",
    "'": "apostrophe", "\"": "shift-apostrophe", "_": "shift-minus",
    "*": "shift-8", ">": "shift-dot", "<": "shift-comma",
}


def find_qemu() -> str:
    for c in ("qemu-system-x86_64", "qemu-system-i386"):
        p = subprocess.run(["sh", "-c", f"command -v {c}"],
                           capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip():
            return p.stdout.strip()
    local = os.path.expanduser("~/.local/bin/qemu-system-x86_64")
    if os.access(local, os.X_OK):
        return local
    raise SystemExit("qemu not found")


class Monitor:
    def __init__(self, path: str, timeout: float = 10.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(path)
                self.sock.settimeout(2.0)
                self._drain()
                return
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.1)
        raise SystemExit(f"could not connect to QEMU monitor at {path}")

    def _drain(self) -> str:
        out = b""
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                out += chunk
        except socket.timeout:
            pass
        return out.decode("utf-8", "replace")

    def cmd(self, line: str, wait: float = 0.25) -> str:
        self.sock.sendall((line + "\n").encode())
        time.sleep(wait)
        return self._drain()

    def version(self) -> str:
        """The emulator's version string, for a note next to an environment quirk."""
        out = self.cmd("info version")
        match = re.search(r"QEMU[^\r\n(]*", out)
        return match.group(0).strip() if match else "QEMU (unknown version)"

    def send_text(self, text: str, delay: float = 0.06) -> None:
        for ch in text:
            key = KEYS.get(ch, ch)
            if ch.isupper():
                key = f"shift-{ch.lower()}"
            out = self.cmd(f"sendkey {key}", delay)
            if "unknown key" in out.lower() or "invalid" in out.lower():
                raise SystemExit(f"monitor rejected sendkey {key!r}: {out.strip()[:200]}")

    def send_mouse(self, dx: int, dy: int) -> str:
        """Move the guest pointer: a relative PS/2 packet, exactly what a hand
        produces, so the driver sees its bytes and no absolute position."""
        return self.cmd("mouse_move %d %d" % (dx, dy))

    def send_button(self, mask: int) -> str:
        """Set the button state as a bitmask (1 = left, 2 = right, 4 = middle)."""
        return self.cmd("mouse_button %d" % mask)

    def screen(self) -> str:
        # `x` format prints hex bytes (0x..); `c` format prints quoted characters.
        raw = self.cmd("xp /4000xb 0xb8000", 0.6)
        self.last_raw = raw
        data = []
        for line in raw.splitlines():
            m = re.match(r"^[0-9a-f]+:\s+(.*)$", line.strip())
            if not m:
                continue
            for tok in m.group(1).split():
                if tok.startswith("0x"):
                    try:
                        data.append(int(tok, 16))
                    except ValueError:
                        pass
        chars = data[0::2][: COLS * ROWS]          # skip attribute bytes
        text = "".join(chr(c) if 32 <= c < 127 else " " for c in chars)
        return "\n".join(text[i:i + COLS] for i in range(0, len(text), COLS))


class SerialConsole:
    """Unix-socket serial console: read boot output, send command lines."""

    def __init__(self, path: str, timeout: float = 10.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(path)
                self.sock.settimeout(0.05)
                self.buf = bytearray()
                return
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.05)
        raise SystemExit(f"could not connect to serial console at {path}")

    def pump(self) -> None:
        while True:
            try:
                chunk = self.sock.recv(65536)
            except (socket.timeout, OSError):
                return
            if not chunk:
                return
            self.buf.extend(chunk)

    def text(self) -> str:
        self.pump()
        return self.buf.decode("utf-8", "replace")

    def wait_for(self, marker: str, timeout: float, proc=None) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if marker in self.text():
                return True
            if proc is not None and proc.poll() is not None:
                return False
            time.sleep(0.05)
        return False

    def send_line(self, line: str) -> None:
        self.sock.sendall((line + "\n").encode())


def _ppm_to_png(ppm_path: str, png_path: str) -> None:
    """Convert a QEMU P6 screendump to PNG without external dependencies."""
    with open(ppm_path, "rb") as f:
        data = f.read()

    # Header: P6 <width> <height> <maxval>\n, with optional # comments.
    pos, tokens = 0, []
    while len(tokens) < 4:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    pos += 1

    magic, width, height = tokens[0], int(tokens[1]), int(tokens[2])
    if magic != b"P6":
        raise SystemExit(f"screendump is not a P6 PPM (got {magic!r})")
    stride = width * 3
    pixels = data[pos:pos + width * height * 3]
    if len(pixels) != width * height * 3:
        raise SystemExit("screendump is truncated")

    raw = bytearray()
    for y in range(height):
        raw.append(0)                       # PNG filter: none
        raw += pixels[y * stride:(y + 1) * stride]

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(png_path, "wb") as f:
        f.write(png)
    os.unlink(ppm_path)


def _opt(args, name, default=None):
    if name in args:
        return args[args.index(name) + 1]
    return default


def _opt_all(args, name):
    out = []
    for i, a in enumerate(args):
        if a == name and i + 1 < len(args):
            out.append(args[i + 1])
    return out


def _after_last(text: str, needle: str) -> str:
    """Return the text after the last occurrence of *needle*.

    The shell echoes the command line (on both VGA and the serial console), so
    matching an expected string against the raw screen would succeed even if the
    command never ran. Anchoring on the last echo makes --expect test the
    command's *output*.
    """
    if not needle:
        return text
    pos = text.rfind(needle)
    return text[pos + len(needle):] if pos >= 0 else text


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 2:
        raise SystemExit(__doc__)
    image, command = args[0], args[1]

    timeout = float(_opt(args, "--timeout", 60.0))
    transport = _opt(args, "--via", "serial")
    expects = _opt_all(args, "--expect")
    rejects = _opt_all(args, "--reject")
    thens = _opt_all(args, "--then")
    out_file = _opt(args, "--out")
    screendump = _opt(args, "--screendump")
    mem_mb = _opt(args, "--mem", "128")
    smp = _opt(args, "--smp")
    cpu = _opt(args, "--cpu")
    want_json = "--json" in args

    qemu = find_qemu()
    tmp = tempfile.mkdtemp(prefix="mellivora-drive-")
    mon_path = os.path.join(tmp, "mon.sock")
    ser_path = os.path.join(tmp, "serial.sock")

    proc = subprocess.Popen(
        [qemu, "-cpu", cpu or "486", "-m", str(mem_mb),
         "-drive", f"file={image},format=raw,if=ide,cache=writethrough",
         "-boot", "c", "-display", "none", "-no-reboot",
         # wait=on keeps the VM paused until this process connects, so the boot
         # marker cannot be emitted before the reader exists.
         "-serial", f"unix:{ser_path},server=on,wait=on",
         "-monitor", f"unix:{mon_path},server,nowait",
         "-audiodev", "none,id=snd0", "-machine", "pcspk-audiodev=snd0,usb=off",
         "-device", "sb16,audiodev=snd0",
         "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0"]
        + (["-smp", smp] if smp else []),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    ser = None
    screen = slog = regs = ""
    failure = None
    try:
        ser = SerialConsole(ser_path)
        if not ser.wait_for("MELLIVORA_BOOT_OK", timeout, proc):
            failure = "boot marker never appeared"
        else:
            time.sleep(0.3)
            mon_ctl = Monitor(mon_path)

            if transport == "serial":
                ser.send_line(command)
            else:
                mon_ctl.send_text(command)
                time.sleep(0.4)
                mon_ctl.cmd("sendkey ret", 0.3)
                # A modifier used for a shifted character can leak onto the
                # following Enter; a second Enter makes it deterministic.
                time.sleep(0.3)
                mon_ctl.cmd("sendkey ret", 0.3)
            time.sleep(1.5)

            for then in thens:
                if transport == "serial":
                    ser.send_line(then)
                else:
                    mon_ctl.send_text(then)
                    time.sleep(0.3)
                    mon_ctl.cmd("sendkey ret", 0.3)
                    time.sleep(0.3)
                    mon_ctl.cmd("sendkey ret", 0.3)
                time.sleep(3.0)

            screen = mon_ctl.screen()
            slog = ser.text()

            # A command that does real work (metadata-heavy tests, for
            # instance) can take seconds. Poll for the expected text instead of
            # sampling once, so a slow program is not mistaken for a failure.
            wait_s = float(_opt(args, "--wait", 6.0))
            deadline = time.time() + wait_s
            while expects and time.time() < deadline:
                hay = (_after_last(screen, command) + "\n"
                       + _after_last(slog, command))
                if all(e in hay for e in expects):
                    break
                time.sleep(0.25)
                screen = mon_ctl.screen()
                slog = ser.text()
            if screendump:
                # Works in VBE modes where the text buffer has nothing to read.
                ppm = os.path.abspath(screendump) + ".ppm"
                mon_ctl.cmd(f"screendump {ppm}", 1.0)
                time.sleep(0.4)
                _ppm_to_png(ppm, os.path.abspath(screendump))
            if "--regs" in args:
                regs = mon_ctl.cmd("info registers", 0.6)
    except (SystemExit, RuntimeError) as exc:
        failure = str(exc)
    finally:
        proc.kill()
        proc.wait()

    slog = ser.text() if ser is not None else ""
    # --expect is checked against output after the echoed command line;
    # --reject and fault detection scan the whole transcript.
    expect_haystack = (_after_last(screen, command) + "\n"
                       + _after_last(slog, command))
    combined = slog + "\n" + screen

    faults = re.search(r"CPU EXCEPTION|double fault|triple fault", slog, re.I)
    missing = [e for e in expects if e not in expect_haystack]
    rejected = [r for r in rejects if r in combined]

    ok = failure is None and faults is None and not missing and not rejected

    if out_file:
        with open(out_file, "w", encoding="utf-8") as f:
            f.write("---- serial ----\n")
            f.write(slog.strip() or "(empty)")
            f.write("\n---- screen ----\n")
            f.write(screen.rstrip())
            f.write("\n")

    if want_json:
        print(json.dumps({
            "ok": ok,
            "failure": failure,
            "fault": faults.group(0) if faults else None,
            "missing_expect": missing,
            "rejected": rejected,
            "serial": slog,
            "screen": screen,
            "registers": regs,
        }, indent=2))
    else:
        if failure:
            print(f"FAIL: {failure}")
        print("---- serial ----")
        print(slog.strip() or "(empty)")
        print("---- screen ----")
        print(screen.rstrip())
        if regs:
            print("---- registers ----")
            for line in regs.splitlines():
                if line.strip() and not line.startswith("QEMU"):
                    print(line)
        if faults:
            print(f"\nFAIL: exception on COM1: {faults.group(0)}")
        for e in missing:
            print(f"FAIL: expected text not found: {e!r}")
        for r in rejected:
            print(f"FAIL: unexpected text found: {r!r}")
        print("\nPASS" if ok else "\nFAIL")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
