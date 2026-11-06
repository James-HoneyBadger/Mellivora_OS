#!/usr/bin/env bash
#
# test_packet_fuzz.sh - NETWORK.md section 9 item 2, as a gate.
#
# "packet_fuzz feeds 1 000 000 malformed frames and the stack neither panics
# nor loses a valid connection." docs/spec/NETWORK.md listed that as the
# conformance item for the Ethernet layer; tools/packet_fuzz.py generated the
# frames and there was nowhere to send them, so the item was a generator with
# no gate. tools/qemu_fuzznode.py is the gate, and this runs it.
#
# What the harness does: boots one guest with its NIC on a TCP socket netdev
# (the same wire the two-machine suite uses, with the host as the only peer),
# writes the frames from the generators, then asks the guest to prove the stack
# still works - `ifconfig`, `ping 127.0.0.1` and a loopback share - through its
# own console. A guest that survives the storm and can no longer do those has
# lost the connection, which is the half of the requirement a fault log cannot
# show.
#
# It has already found a real defect: a frame of thirteen bytes or fewer made
# the driver write a receive read pointer outside the ring, and the machine then
# stopped answering with no fault to show for it. See docs/ROADMAP.md, M7.
#
# A full run takes a while, and not because of the harness: an emulated NIC plus
# this driver manages on the order of a thousand frames a second, so a million
# frames is minutes of emulation. The wire is written as fast as QEMU accepts
# frames, which leaves a backlog for the machine to drain before its console
# gets a turn, so the first liveness answer can take a while after the last
# frame. Set FUZZ_COUNT low when iterating on the harness itself.
#
# The image is opened with -snapshot, so the run leaves it untouched.
#
# Usage:  tests/test_packet_fuzz.sh [image]
#
# Env:
#   FUZZ_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   FUZZ_COUNT=1000000  frames to send (lower it when iterating on the harness)
#   FUZZ_SEED=0         the generator seed, so a failure can be reproduced
#   FUZZ_TIMEOUT=180    seconds allowed for the guest to reach its shell
#   FUZZ_STALL=120      seconds the wire may refuse frames before the machine
#                       is called a lost connection
#   FUZZ_ANSWER=600     seconds the first liveness check may spend draining
#   FUZZ_RATE=2000      frames per second offered to the machine; 0 floods
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${FUZZ_IMAGE:-$ROOT/mellivora.img}}"
TOOL="$ROOT/tools/qemu_fuzznode.py"
COUNT="${FUZZ_COUNT:-1000000}"
SEED="${FUZZ_SEED:-0}"
BOOT_TIMEOUT="${FUZZ_TIMEOUT:-180}"
STALL_TIMEOUT="${FUZZ_STALL:-120}"
RATE="${FUZZ_RATE:-2000}"
ANSWER_TIMEOUT="${FUZZ_ANSWER:-600}"

fail() {
    printf '  FAIL  %s\n' "$*" >&2
    exit 1
}

find_qemu() {
    if [ -n "${QEMU:-}" ]; then
        printf '%s' "$QEMU"
        return 0
    fi
    local c
    for c in qemu-system-x86_64 qemu-system-i386; do
        if command -v "$c" >/dev/null 2>&1; then
            command -v "$c"
            return 0
        fi
    done
    if [ -x "$HOME/.local/bin/qemu-system-x86_64" ]; then
        printf '%s' "$HOME/.local/bin/qemu-system-x86_64"
        return 0
    fi
    return 1
}

if ! find_qemu >/dev/null; then
    if [ "${FUZZ_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  packet fuzz: no QEMU found on this host"
        exit 0
    fi
    fail "packet fuzz: no QEMU found. Install qemu-system-x86, or set FUZZ_ALLOW_SKIP=1."
fi

[ -f "$TOOL" ] || fail "packet fuzz: missing $TOOL"
[ -f "$IMAGE" ] || fail "packet fuzz: disk image not found: $IMAGE (run 'make full' first)"

echo "  packet fuzz: $COUNT malformed frames, seed $SEED"

REPORT="$(mktemp)"
trap 'rm -f "$REPORT"' EXIT

if ! QEMU="$(find_qemu)" python3 "$TOOL" \
        --image "$IMAGE" \
        --count "$COUNT" \
        --seed "$SEED" \
        --boot-timeout "$BOOT_TIMEOUT" \
        --stall-timeout "$STALL_TIMEOUT" \
        --answer-timeout "$ANSWER_TIMEOUT" \
        --rate "$RATE" \
        --json >"$REPORT" 2>&1; then
    # The harness exits 1 on a fault, a wedge or a failed liveness check; the
    # JSON it leaves behind says which, and its own stderr says where.
    sed -n '1,40p' "$REPORT" >&2
    exit 1
fi

python3 - "$REPORT" "$COUNT" <<'PY' || exit 1
import json
import sys

path, count = sys.argv[1], int(sys.argv[2])
text = open(path).read()
start = text.find('{')
if start < 0:
    sys.exit('  FAIL  packet fuzz: the harness printed no report')
try:
    report = json.loads(text[start:])
except ValueError as exc:
    sys.exit('  FAIL  packet fuzz: unreadable report: %s' % exc)

if not report.get('ok'):
    sys.exit('  FAIL  packet fuzz: the guest did not survive: %s'
             % (report.get('detail') or report.get('fault') or 'no reason given'))
if report.get('sent') != count:
    sys.exit('  FAIL  packet fuzz: sent %s of %s frames'
             % (report.get('sent'), count))
for check in ('ifconfig', 'ping', 'share'):
    if not report.get('checks', {}).get(check):
        sys.exit('  FAIL  packet fuzz: the stack stopped answering: %s' % check)
print('  ok    packet fuzz: %s frames, stack still answering' % count)
PY
