#!/usr/bin/env python3
"""Boot two Mellivora machines on one virtual wire and drive them from here.

Every other QEMU gate in this tree boots one machine and proves what it can do
to itself; loopback made the share protocol testable and also hid the fact that
a packet never left a NIC. This tool is the other half: two QEMU processes,
each with the profile's RTL8139, joined by a socket netdev pair, and a
serial console on each so a workflow can be typed into one machine and read
back from the other.

The two disks are overlays (`-snapshot`), so a run leaves the image byte for
byte as it found it and the same image can back both machines without one
guest's writes appearing in the other's filesystem. The machines get distinct
MACs on purpose: the profile's NIC would default both to 52:54:00:12:34:56.
The wire itself is a TCP socket pair (one end listens, the other connects),
because a multicast group depends on the host's multicast setup and delivers
nothing silently when it is off - which reads exactly like a broken stack.

The plan comes from stdin (or --plan FILE) as JSON:

    {
      "boot_timeout": 120,
      "step_timeout": 30,
      "steps": [
        {"node": "a", "run": "ifconfig 10.0.0.1",
         "expect": ["IP set to 10.0.0.1"]},
        {"node": "b", "run": "net view 10.0.0.1",
         "expect": ["Shares on 10.0.0.1:4450", "docs"]},
        {"sleep": 1.0}
      ]
    }

Each step may name `node` a, b or both; `run` is one or more lines sent to that
machine's serial console; `expect` strings must appear in what that machine said
*after* the command, and `reject` strings must not. A step that times out
reports what the machine actually printed.

Usage: qemu_twonode.py --image IMG [--plan FILE] [--boot-timeout N]
                       [--step-timeout N] [--json] [--keep]

Exit: 0 when every step passed, 1 otherwise (or when either machine never booted).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qemu_drive as qd  # noqa: E402  (path set above)

BOOT_MARKER = "SHELL_READY"
# A step is finished when the prompt comes back. The prompt is drawn on the VGA
# screen, not sent to COM1, so completion is read from the text buffer; typing
# the next line before it returns loses that line's tail in the UART's 16-byte
# FIFO, because nothing buffers input while a command runs. The prompt may be
# the last of several on one line: while a background task runs, the shell
# redraws it on every slice.
PROMPT_RE = re.compile(r"Lair:[^>\n]*>\s*$")
NODE_MACS = {"a": "52:54:00:12:34:01", "b": "52:54:00:12:34:02"}

# A kernel fault is logged to the serial console but the machine then returns
# to the prompt, so a prompt alone proves nothing.
FAULT_RE = re.compile(r"CPU EXCEPTION|PAGE FAULT|double fault|triple fault",
                      re.I)


def free_port() -> int:
    """A port nothing holds right now, for the listening end of the wire."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def node_args(tag: str, image: str, netdev: str) -> list[str]:
    """The profile's machine, with its user-mode NIC replaced by the wire."""
    args = qd.machine_args()
    index = args.index("-netdev")
    args[index:index + 2] = ["-netdev", netdev]
    nic = args.index("rtl8139,netdev=net0")
    args[nic] = f"rtl8139,netdev=net0,mac={NODE_MACS[tag]}"
    return args


def wait_listening(port: int, timeout: float) -> bool:
    """Wait until something listens on 127.0.0.1:port.

    The connect end of a socket netdev is opened once, at startup, and gives up
    if nothing is listening yet, so the server has to be up first. Looking for
    the LISTEN state in /proc/net/tcp checks that without opening a connection
    of our own - which QEMU would accept as the peer and then lose.
    """
    deadline = time.time() + timeout
    want = f"0100007F:{port:04X}"
    while time.time() < deadline:
        try:
            with open("/proc/net/tcp", "r", encoding="ascii") as f:
                for line in f.readlines()[1:]:
                    fields = line.split()
                    if len(fields) > 3 and fields[1] == want and fields[3] == "0A":
                        return True
        except OSError:
            # No /proc/net/tcp (a non-Linux host): give the listener a moment
            # and let the connect end report a real failure if it is not there.
            time.sleep(1.0)
            return True
        time.sleep(0.05)
    return False


def wire() -> dict[str, str]:
    """A TCP socket netdev pair: one end listens, the other connects.

    A multicast group would be the obvious choice and is not: whether two
    guests on one host hear each other depends on the host's multicast setup,
    and a run that silently delivers nothing looks exactly like a stack bug.
    The listener is started first and its port confirmed before the peer is
    launched; `wait_listening` explains how that is checked without stealing
    the connection.
    """
    port = free_port()
    return {
        "a": f"socket,id=net0,listen=127.0.0.1:{port}",
        "b": f"socket,id=net0,connect=127.0.0.1:{port}",
        "port": port,
        "label": f"tcp 127.0.0.1:{port}",
    }


class Node:
    """One QEMU process, its serial console and its transcript."""

    def __init__(self, tag: str, image: str, qemu: str, netdev: str,
                 workdir: str) -> None:
        self.tag = tag
        self.image = image
        self.qemu = qemu
        self.netdev = netdev
        self.workdir = workdir
        self.serial_path = os.path.join(workdir, f"{tag}.ser")
        self.monitor_path = os.path.join(workdir, f"{tag}.mon")
        self.stderr_path = os.path.join(workdir, f"{tag}.stderr")
        self.proc: subprocess.Popen | None = None
        self.console: qd.SerialConsole | None = None
        self.monitor: qd.Monitor | None = None
        self._stderr = None

    def start(self) -> None:
        self._stderr = open(self.stderr_path, "wb")
        self.proc = subprocess.Popen(
            [self.qemu] + node_args(self.tag, self.image, self.netdev) + [
                "-drive", f"file={self.image},format=raw,if=ide,"
                          "cache=writethrough",
                # An overlay per machine: the two guests share a source image
                # without sharing a filesystem, and the image stays untouched.
                "-snapshot",
                "-boot", "c", "-display", "none", "-no-reboot",
                "-serial", f"unix:{self.serial_path},server=on,wait=on",
                "-monitor", f"unix:{self.monitor_path},server,nowait",
            ],
            stdout=subprocess.DEVNULL, stderr=self._stderr)

    def connect(self) -> None:
        self.console = qd.SerialConsole(self.serial_path)
        self.monitor = qd.Monitor(self.monitor_path)

    def screen(self) -> str:
        """The 80x25 text buffer, which is where the shell writes."""
        if self.monitor is None:
            return ""
        try:
            return self.monitor.screen()
        except (OSError, SystemExit):
            return ""

    @staticmethod
    def at_prompt(screen: str) -> bool:
        """True when the last thing drawn on the screen is a bare prompt."""
        for line in reversed([l.rstrip() for l in screen.splitlines()]):
            if not line.strip():
                continue
            return bool(PROMPT_RE.search(line))
        return False

    def boot(self, timeout: float) -> tuple[bool, str]:
        deadline = time.time() + timeout
        while time.time() < deadline:
            seen = self.say()
            if BOOT_MARKER in seen and self.at_prompt(self.screen()):
                return True, ""
            if self.proc is not None and self.proc.poll() is not None:
                return False, self.diagnosis("the machine exited during boot")
            time.sleep(0.1)
        return False, self.diagnosis("the boot marker never appeared")

    def say(self) -> str:
        """The serial transcript: kernel log lines and the typed echo."""
        return self.console.text() if self.console else ""

    def diagnosis(self, why: str) -> str:
        tail = self.say()[-1200:].strip()
        err = ""
        try:
            with open(self.stderr_path, "r", encoding="utf-8",
                      errors="replace") as f:
                err = f.read()[-600:].strip()
        except OSError:
            pass
        text = f"node {self.tag}: {why}"
        if tail:
            text += f"\n--- node {self.tag} serial ---\n{tail}"
        if err:
            text += f"\n--- node {self.tag} qemu stderr ---\n{err}"
        return text

    def send(self, lines: list[str]) -> None:
        for line in lines:
            self.console.send_line(line)
            time.sleep(0.05)

    def run(self, lines: list[str], expect: list[str], reject: list[str],
            timeout: float) -> tuple[bool, str]:
        command = lines[-1] if lines else ""
        mark = len(self.say())
        self.send(lines)
        deadline = time.time() + timeout
        screen = ""
        out = ""
        while time.time() < deadline:
            screen = self.screen()
            # Everything after the echoed command line: the shell writes its
            # answers to the text buffer, so that is where --expect looks.
            out = qd._after_last(screen, command)
            serial_tail = self.say()[mark:]
            barred = [r for r in reject if r in out or r in serial_tail]
            fault = FAULT_RE.search(serial_tail)
            if fault:
                barred = barred or [fault.group(0)]
            if barred:
                return False, (f"node {self.tag} said {barred[0]!r}\n"
                               f"--- node {self.tag} screen ---\n{screen}")
            # The prompt proves the machine is ready for the next step. Typing
            # the next line before it comes back loses that line's tail in the
            # UART's 16-byte FIFO, because nothing buffers input while a
            # command runs.
            if all(e in out for e in expect) and self.at_prompt(screen):
                return True, ""
            if self.proc.poll() is not None:
                return False, self.diagnosis("the machine exited")
            time.sleep(0.1)
        missing = [e for e in expect if e not in out]
        if not missing:
            missing = ["the shell prompt to come back"]
        detail = f"node {self.tag} never said {missing}"
        if not out.strip():
            detail += " (it printed nothing after the command)"
        detail += (f"\n--- node {self.tag} screen ---\n{screen}\n"
                   f"--- node {self.tag} serial, last lines ---\n"
                   f"{self.say()[-600:] or '(empty)'}")
        return False, detail

    def stop(self) -> None:
        if self.proc is not None and self.proc.poll() is None:
            self.proc.kill()
        if self.proc is not None:
            self.proc.wait()
        if self._stderr is not None:
            self._stderr.close()

    def save_transcript(self) -> None:
        """Keep what the machine said and drew, so a failure can be read later."""
        try:
            with open(os.path.join(self.workdir, f"{self.tag}.console"), "w",
                      encoding="utf-8") as f:
                f.write("---- serial ----\n" + (self.say() or "(empty)"))
                f.write("\n---- screen ----\n" + self.screen())
        except OSError:
            pass


def step_lines(step: dict) -> list[str]:
    run = step.get("run", "")
    if isinstance(run, list):
        return [str(line) for line in run]
    return [line for line in str(run).splitlines() if line]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--image", required=True)
    parser.add_argument("--plan", default="-",
                       help="JSON plan file, or - to read stdin (default)")
    parser.add_argument("--boot-timeout", type=float, default=None)
    parser.add_argument("--step-timeout", type=float, default=None)
    parser.add_argument("--json", action="store_true",
                       help="emit one JSON result object instead of lines")
    parser.add_argument("--keep", action="store_true",
                       help="keep the per-node transcripts on disk")
    args = parser.parse_args(argv)

    if args.plan == "-":
        plan = json.load(sys.stdin)
    else:
        with open(args.plan, "r", encoding="utf-8") as f:
            plan = json.load(f)

    image = str(Path(args.image).resolve())
    if not os.path.isfile(image):
        print(f"FAIL: image not found: {image}", file=sys.stderr)
        return 1

    boot_timeout = float(args.boot_timeout or plan.get("boot_timeout", 120))
    step_timeout = float(args.step_timeout or plan.get("step_timeout", 30))

    try:
        qemu = qd.find_qemu()
    except SystemExit as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    workdir = tempfile.mkdtemp(prefix="mellivora-two-")
    net = wire()
    nodes = {tag: Node(tag, image, qemu, net[tag], workdir)
             for tag in ("a", "b")}

    results: list[dict] = []
    ok = True
    try:
        nodes["a"].start()
        if not wait_listening(int(net["port"]), 10.0):
            results.append({"step": "open the wire", "ok": False,
                            "detail": nodes["a"].diagnosis(
                                "the listening end never opened")})
            ok = False
        else:
            nodes["b"].start()
        for node in nodes.values():
            if node.proc is not None:
                node.connect()
        for node in nodes.values():
            if node.proc is None:
                continue
            booted, why = node.boot(boot_timeout)
            results.append({"step": f"boot {node.tag}", "ok": booted,
                            "detail": why})
            if not booted:
                ok = False

        if ok:
            for index, step in enumerate(plan.get("steps", []), 1):
                if "sleep" in step:
                    time.sleep(float(step["sleep"]))
                    continue
                targets = ("a", "b") if step.get("node") == "both" else (
                    step.get("node", "a"),)
                lines = step_lines(step)
                expect = step.get("expect", [])
                reject = step.get("reject", [])
                timeout = float(step.get("timeout", step_timeout))
                label = step.get("name") or f"[{'/'.join(targets)}] {lines[0]}"
                for tag in targets:
                    passed, detail = nodes[tag].run(
                        lines, expect, reject, timeout)
                    results.append({"step": f"{index}. {label}",
                                    "node": tag, "ok": passed,
                                    "detail": detail})
                    if not passed:
                        ok = False
                        break
                if not ok:
                    break
    finally:
        for node in nodes.values():
            node.save_transcript()
            node.stop()
        if not ok or args.keep:
            print(f"transcripts: {workdir}", file=sys.stderr)
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    if args.json:
        print(json.dumps({"ok": ok, "wire": net["label"],
                          "steps": results}, indent=2))
    else:
        print(f"two machines on {net['label']}")
        for result in results:
            mark = "PASS" if result["ok"] else "FAIL"
            print(f"  {mark}  {result['step']}")
            if result["detail"]:
                for line in result["detail"].splitlines():
                    print(f"        {line}")
        print(f"TWO_VM_OK steps={sum(1 for r in results if r['ok'])}" if ok
              else "TWO_VM_FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
