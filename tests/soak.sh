#!/usr/bin/env bash
#
# soak.sh - the wave's exit test: several processors under mixed load for a
# long time, with the machine required to still be answering at the end.
#
# The gate form in `make check` runs for a minute. The exit test runs for a day,
# which is the same command with a different number:
#
#     SOAK_MINUTES=1440 make soak          # 24 hours
#     SOAK_MINUTES=5    make soak          # a coffee break
#
# How it works, and why it is not a loop around `smpstress`:
#
#   * the guest drives itself. The shell line that starts the load is
#     `smpstress`, and `smpstress` returns; a loop in the host would only prove
#     that QEMU can be sent keys. Instead the guest runs a self-timed soak in
#     which every round queues allocator and block-cache work for the other
#     processors, runs a user program beside it, and reports a heartbeat on
#     COM1. A machine that hangs stops heartbeating, which the host sees.
#   * the host watches three things: heartbeats must keep arriving, the error
#     counters must stay zero, and the guest must still answer a command at the
#     end - a machine that is alive but wedged is not a pass.
#
# Usage: tests/soak.sh [image]
#
# Env:
#   SOAK_MINUTES=1        how long to soak (the exit test uses 1440)
#   SOAK_ROUNDS=0         how many load rounds per command (0 = one per minute)
#   SOAK_SMP=4            processors
#   SOAK_ALLOW_SKIP=1     exit 0 instead of failing when QEMU is missing
#
# Exits 0 when the machine survived, 1 otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${ROOT}/mellivora.img}"
MINUTES="${SOAK_MINUTES:-1}"
ROUNDS="${SOAK_ROUNDS:-0}"
SMP="${SOAK_SMP:-4}"
ALLOW_SKIP="${SOAK_ALLOW_SKIP:-0}"

fail() { printf '  FAIL  %s\n' "$*" >&2; exit 1; }

qemu="$(command -v qemu-system-x86_64 || command -v qemu-system-i386 || true)"
if [ -z "$qemu" ] && [ -x "$HOME/.local/bin/qemu-system-x86_64" ]; then
    qemu="$HOME/.local/bin/qemu-system-x86_64"
fi
if [ -z "$qemu" ]; then
    [ "$ALLOW_SKIP" = "1" ] && { echo "  SKIP  soak: no QEMU found"; exit 0; }
    fail "soak: no QEMU found (install qemu-system-x86, or set SOAK_ALLOW_SKIP=1)"
fi
[ -f "$IMAGE" ] || fail "soak: no disk image at $IMAGE (run 'make full' first)"

# A round per minute unless told otherwise: the point is continuous load, not
# one big burst at the start.
if [ "$ROUNDS" = "0" ]; then
    ROUNDS=$(awk -v m="$MINUTES" 'BEGIN { r = int(m * 60); if (r < 1) r = 1; print r }')
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
sock="$work/serial.sock"
log="$work/soak.log"

python3 -u - "$IMAGE" "$sock" "$SMP" "$ROUNDS" "$MINUTES" "$log" "$qemu" <<'PY'
import os
import subprocess
import sys
import time

image, sock, smp, rounds, minutes, log, qemu = sys.argv[1:8]
rounds, minutes = int(rounds), float(minutes)

proc = subprocess.Popen(
    [qemu, "-m", "512", "-smp", smp,
     "-drive", f"file={image},format=raw,if=ide,cache=writethrough",
     "-boot", "c", "-display", "none", "-no-reboot",
     "-serial", f"unix:{sock},server=on,wait=on"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def log_write(text):
    with open(log, "a", encoding="utf-8") as handle:
        handle.write(text)

def fail(message):
    print(f"  FAIL  {message}", file=sys.stderr)
    proc.kill()
    sys.exit(1)

try:
    # Wait for the guest to be up.
    deadline = time.time() + 120
    buf = b""
    import socket
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    while time.time() < deadline:
        try:
            s.connect(sock)
            break
        except (FileNotFoundError, ConnectionRefusedError):
            time.sleep(0.1)
    else:
        fail("the machine never came up")
    s.settimeout(0.05)

    def pump():
        global buf
        while True:
            try:
                chunk = s.recv(65536)
            except (socket.timeout, OSError):
                return
            if not chunk:
                return
            buf += chunk
            log_write(chunk.decode("utf-8", "replace"))

    deadline = time.time() + 120
    while b"MELLIVORA_BOOT_OK" not in buf:
        if time.time() > deadline:
            fail("the boot marker never appeared")
        pump()
        time.sleep(0.05)

    # The boot marker is printed before the shell exists. Typing at a machine
    # that is not listening yet loses the line, and a lost line looks exactly
    # like a machine that stopped answering - so wait for the shell to say it
    # is ready, which it does once, on COM1.
    deadline = time.time() + 120
    while b"SHELL_READY" not in buf:
        if time.time() > deadline:
            fail("the shell never said it was ready")
        pump()
        time.sleep(0.05)

    # The load, and then the same load again: each call is self-timed, so the
    # host is not in the loop and a stall shows up as silence rather than as a
    # command that was queued but never serviced.
    # Mixed load means both halves: kernel jobs on the other processors *and* a
    # user program on this one. A soak that only queues kernel work proves the
    # work queue; the scheduler, the page tables and the drivers are exercised
    # by a program running beside it.
    guests = ["fs_meta_test", "vmtest3", "vmtest5", "fs_mkdir_test"]
    guest_markers = {"fs_meta_test": "FS_META_OK", "vmtest3": "VM3_OK",
                     "vmtest5": "VM5_OK", "fs_mkdir_test": "CRASH_LOOP_OK"}

    started = time.time()
    end = started + minutes * 60
    rounds_sent = 0
    guest_sent = 0
    while time.time() < end:
        s.sendall(f"smpstress {rounds}\n".encode())
        rounds_sent += 1
        print(f"  round {rounds_sent}: smpstress {rounds}", flush=True)
        # Wait for this round's verdict before sending the next one: a round
        # that never finishes is the failure this test exists to catch, and it
        # is caught as silence rather than as a timeout on a command.
        before = buf.count(b"SMP_STRESS_OK") + buf.count(b"SMP_STRESS_FAIL")
        round_deadline = time.time() + 300
        while True:
            pump()
            if b"SMP_STRESS_FAIL" in buf:
                fail("a load round reported allocation errors")
            if proc.poll() is not None:
                fail("QEMU exited during the soak")
            if buf.count(b"SMP_STRESS_OK") + buf.count(b"SMP_STRESS_FAIL") > before:
                break
            if time.time() > round_deadline:
                fail("a load round never finished: the machine stopped answering")
            time.sleep(0.2)

    # The user-program half of the mixed load, once more with feeling, and the
    # marker has to be the program's own verdict rather than a prompt.
    for name in guests:
        marker = guest_markers[name].encode()
        before_guest = buf.count(marker)
        s.sendall((name + "\n").encode())
        guest_sent += 1
        guest_deadline = time.time() + 180
        while buf.count(marker) <= before_guest:
            pump()
            if b"SMP_STRESS_FAIL" in buf:
                fail(f"{name} reported a failure during the soak")
            if proc.poll() is not None:
                fail("QEMU exited during the soak")
            if time.time() > guest_deadline:
                fail(f"{name} never finished during the soak")
            time.sleep(0.2)
        print(f"  guest {name}: {guest_markers[name]}", flush=True)

    # Still alive, still answering? The command has to be one whose answer
    # reaches COM1 - a machine that is alive but not reading the console is
    # exactly the failure this check exists to catch, and a screen-only answer
    # would be invisible from here. `quota` walks the filesystem and ends with a
    # marker on the serial console, so it proves the shell, the disk and the
    # kernel are all still working.
    before_answer = buf.count(b"QUOTA_OK")
    s.sendall(b"quota\n")
    deadline = time.time() + 120
    answered = False
    while time.time() < deadline:
        pump()
        if buf.count(b"QUOTA_OK") > before_answer:
            answered = True
            break
        if proc.poll() is not None:
            fail("QEMU exited after the soak")
        time.sleep(0.2)

    text = buf.decode("utf-8", "replace")
    if "CPU EXCEPTION" in text or "double fault" in text or "triple fault" in text:
        fail("the machine reported a fault during the soak")
    if "SMP_STRESS_FAIL" in text:
        fail("the allocator reported errors during the soak")
    if not answered:
        fail("the machine was alive but did not answer afterwards")

    print(f"  PASS  {smp} processors, {rounds_sent} load rounds and "
          f"{guest_sent} user programs, {minutes:g} minutes, still answering")
finally:
    proc.kill()
    proc.wait()
PY
