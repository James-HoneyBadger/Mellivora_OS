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
  --verify CMD     after the journal replay, type CMD at the shell and judge
                   what it printed. The separators are newlines, like the
                   command above, so a workload can span several lines.
  --expect TEXT    require TEXT in the replayed machine's output (repeatable;
                   every one must appear)
  --expect-any LIST
                   require at least one of the |-separated alternatives - the
                   way to say "old or new, not a mixture", which is the whole
                   point of a crash-consistency check
  --reject TEXT    fail if TEXT appears there (repeatable)
  --from-start     measure the kill delay from the first line typed rather than
                   from the last. A workload that takes several commands to say
                   is only crashed *inside* itself this way: from the last line,
                   every kill lands after the work has been typed.

The workload command may span several lines (separated by newlines); each line
is typed at the shell in turn before the kill point, which is what lets a case
crash the machine in the middle of something the shell needs several commands
to describe - a configuration transaction, for instance.

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
            except OSError:
                # QEMU exited and took the socket with it. That is a failed
                # boot, not a durability failure: letting the exception escape
                # here used to abort the whole run and be reported as "a
                # power-cut point left the volume inconsistent".
                return False
        return False

    def send_line(self, text: str) -> None:
        self.sock.sendall((text + "\r\n").encode())


def boot(qemu: str, image: str, tmp: str,
         tag: str = "boot") -> tuple[subprocess.Popen, str, str]:
    ser_path = os.path.join(tmp, f"serial-{tag}.sock")
    mon_path = os.path.join(tmp, f"monitor-{tag}.sock")
    errlog = os.path.join(tmp, f"qemu-{tag}.log")
    with open(errlog, "wb") as err:
        proc = subprocess.Popen(
            [qemu] + machine_args() +
            ["-drive", f"file={image},format=raw,if=ide,cache=writethrough",
             "-boot", "c", "-display", "none", "-no-reboot",
             "-serial", f"unix:{ser_path},server=on,wait=on",
             "-monitor", f"unix:{mon_path},server,nowait"],
            stdout=subprocess.DEVNULL, stderr=err)
    return proc, ser_path, errlog


def kill(proc) -> None:
    try:
        proc.kill()
        proc.wait(timeout=5)
    except Exception:
        pass


def boot_to_shell(qemu: str, image: str, tmp: str,
                  tag: str) -> tuple[subprocess.Popen | None, Serial | None, list[str]]:
    """Boot until the shell marker appears, retrying once.

    QEMU itself occasionally dies at start-up — it did so twice in seventy
    boots on this machine. That is a flake, not a filesystem verdict, and one
    retry separates the two: a volume that really cannot boot fails the retry
    too, and fsck still gets to say its piece.
    """
    notes: list[str] = []
    errlog = ""
    for attempt in range(2):
        proc, ser_path, errlog = boot(qemu, image, tmp, f"{tag}{attempt}")
        try:
            ser = Serial(ser_path)
        except SystemExit as exc:
            # QEMU died before it even opened the console socket.
            kill(proc)
            notes.append(str(exc))
            continue
        if ser.wait_for(BOOT_MARKER, 30.0, proc):
            if attempt:
                notes.append(f"boot ({tag}) needed a retry")
            return proc, ser, notes
        kill(proc)
        notes.append(f"boot ({tag}) attempt {attempt + 1} never reached the shell")
    notes.extend(qemu_tail(errlog))
    return None, None, notes


def qemu_tail(errlog: str) -> list[str]:
    """Whatever QEMU said before it died — usually the reason a boot failed."""
    try:
        with open(errlog, "rb") as f:
            text = f.read().decode("utf-8", "replace").strip()
    except OSError:
        return []
    return ["qemu: " + line for line in text.splitlines()[-6:]]


def fsck(image: str) -> tuple[bool, str]:
    out = subprocess.run(
        [sys.executable, os.path.join(os.path.dirname(__file__), "fsck_hbfs.py"),
         image],
        capture_output=True, text=True)
    return out.returncode == 0, (out.stdout + out.stderr).strip()


def crash_once(qemu: str, image: str, command: str, kill_ms: float,
               verbose: bool, keep: bool = False, verify: str = "",
               expects: list[str] | None = None,
               rejects: list[str] | None = None,
               any_of: str = "", from_start: bool = False
               ) -> tuple[bool, list[str]]:
    """Boot a copy of *image*, run *command*, kill at *kill_ms*, verify."""
    tmpdir = tempfile.mkdtemp(prefix="mellivora-crash-")
    try:
        ok, notes = _crash_once(qemu, image, command, kill_ms, verbose, tmpdir,
                                verify, expects or [], rejects or [], any_of,
                                from_start)
    finally:
        if keep:
            print(f"  kept {tmpdir}")
        else:
            # A 2 GB copy per iteration is not something to leak on every run.
            shutil.rmtree(tmpdir, ignore_errors=True)
    return ok, notes


def _crash_once(qemu: str, image: str, command: str, kill_ms: float,
                verbose: bool, tmpdir: str, verify: str = "",
                expects: list[str] | None = None,
                rejects: list[str] | None = None,
                any_of: str = "", from_start: bool = False
                ) -> tuple[bool, list[str]]:
    notes: list[str] = []
    copy = os.path.join(tmpdir, "crash.img")
    # A raw copy: QEMU opens it writethrough, so a completed ATA write is in the
    # host page cache and survives SIGKILL. Sparse-aware to keep it cheap.
    subprocess.run(["cp", "--sparse=always", image, copy], check=True)

    proc, ser, boot_notes = boot_to_shell(qemu, copy, tmpdir, "run")
    notes.extend(boot_notes)
    if proc is None:
        notes.append("boot marker never appeared")
        return False, notes
    try:
        time.sleep(0.2)
        typed_at = time.time()
        for line in command.splitlines():
            if line.strip():
                ser.send_line(line)
                time.sleep(0.05)
        delay = kill_ms / 1000.0
        if from_start:
            delay -= time.time() - typed_at
        time.sleep(max(0.0, delay))
    finally:
        kill(proc)

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
    proc, ser, boot_notes = boot_to_shell(qemu, copy, tmpdir, "replay")
    notes.extend(boot_notes)
    if proc is None:
        # Distinguish "the guest died" from "the replay damaged the volume":
        # the fsck verdict is the part that matters.
        notes.append("second boot never reached the shell")
        ok, report = fsck(copy)
        notes.append("after the failed replay: "
                     + (report.splitlines()[-1].strip() if report else "no output"))
        return False, notes
    verdict = ""
    try:
        time.sleep(0.5)
        if verify:
            # The guest is the authority on what the configuration store holds:
            # it reads its own keys back and prints one word, and the harness
            # only has to check that the word is one of the two allowed. Asking
            # the shell to print four values and comparing them here would make
            # the harness understand the store's output format instead.
            for line in verify.splitlines():
                if line.strip():
                    ser.send_line(line)
                    time.sleep(0.05)
            # Read until the guest has said something this case can judge -
            # one of the accepted verdicts, every required word, or a rejected
            # one - so a healthy run costs a fraction of the deadline and a
            # silent guest still fails with its own output attached.
            deadline = time.time() + 20.0
            alts = [a for a in any_of.split("|") if a] if any_of else []
            while time.time() < deadline:
                try:
                    chunk = ser.sock.recv(4096)
                    if not chunk:
                        break
                    verdict += chunk.decode("utf-8", "replace")
                except socket.timeout:
                    continue
                except OSError:
                    break
                if alts and any(a in verdict for a in alts):
                    break
                if expects and all(w in verdict for w in expects):
                    break
                if rejects and any(b in verdict for b in rejects):
                    break
    finally:
        kill(proc)

    if verify:
        if any_of:
            alts = [a for a in any_of.split("|") if a]
            if not any(a in verdict for a in alts):
                notes.append("after the replay the guest said none of "
                             + ", ".join(repr(a) for a in alts))
                notes.append("  it said: " + " ".join(verdict.split())[-200:])
                return False, notes
        for want in expects or []:
            if want not in verdict:
                notes.append(f"after the replay the guest did not say {want!r}")
                notes.append("  it said: " + " ".join(verdict.split())[-200:])
                return False, notes
        for bad in rejects or []:
            if bad in verdict:
                notes.append(f"after the replay the guest said {bad!r}, "
                             "which is the mixture this case forbids")
                return False, notes
        notes.append("the verdict after replay: "
                     + " ".join(verdict.split())[-80:])

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
    keep = "--keep" in args
    verify = _opt(args, "--verify", "") or ""
    from_start = "--from-start" in args
    expects, rejects, any_of = [], [], ""
    for i, a in enumerate(args):
        if a == "--expect" and i + 1 < len(args):
            expects.append(args[i + 1])
        elif a == "--expect-any" and i + 1 < len(args):
            any_of = args[i + 1]
        elif a == "--reject" and i + 1 < len(args):
            rejects.append(args[i + 1])

    if not os.path.exists(image):
        raise SystemExit(f"crash_inject: no such image: {image}")
    rng = random.Random(int(seed) if seed is not None else None)
    qemu = find_qemu()

    failures = 0
    for i in range(iterations):
        kill_ms = rng.uniform(min_ms, max_ms)
        ok, notes = crash_once(qemu, image, command, kill_ms, verbose, keep,
                               verify, expects, rejects, any_of, from_start)
        status = "PASS" if ok else "FAIL"
        print(f"  crash {i + 1}/{iterations} at {kill_ms:7.1f} ms  {status}")
        if notes:
            for line in notes:
                print("    " + line)
        if not ok:
            failures += 1

    if failures:
        print(f"  crash injection: {failures}/{iterations} runs did not "
              "recover cleanly")
        return 1
    print(f"  crash injection: {iterations}/{iterations} runs recovered cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
