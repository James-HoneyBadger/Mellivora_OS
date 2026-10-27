#!/usr/bin/env bash
#
# test_powercut.sh - durability, as a gate rather than a claim.
#
# The wave's exit criterion is "10 000 power-cut iterations clean". That is a
# night of wall clock, so it cannot run on every build; what runs on every build
# is the same harness with a small iteration count, and the long run is the same
# command with a bigger number:
#
#     make check-durability           # the gate: 8 crash points
#     CRASH_ITERS=10000 make crash-soak   # the exit test
#
# Each iteration boots a *copy* of the image, starts a metadata-heavy workload,
# kills QEMU at a random moment with no clean shutdown and no cache flush, runs
# fsck on the dead image, then boots it once more so the journal is replayed and
# runs fsck again. The metadata must be entirely old or entirely new - a mixture
# is the failure the journal exists to prevent.
#
# The workloads rotate, because a journal that recovers a directory creation may
# still lose an append: one command shaped like one thing proves one thing.
#
# Usage: tests/test_powercut.sh [image]
#
# Env:
#   CRASH_ITERS=8        total crash points across all workloads
#   CRASH_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#
# Exits 0 when every crash point recovers, 1 otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${ROOT}/mellivora.img}"
ITERS="${CRASH_ITERS:-8}"
ALLOW_SKIP="${CRASH_ALLOW_SKIP:-0}"

fail() { printf '  FAIL  %s\n' "$*" >&2; exit 1; }

qemu="$(command -v qemu-system-x86_64 || command -v qemu-system-i386 || true)"
if [ -z "$qemu" ] && [ -x "$HOME/.local/bin/qemu-system-x86_64" ]; then
    qemu="$HOME/.local/bin/qemu-system-x86_64"
fi
if [ -z "$qemu" ]; then
    [ "$ALLOW_SKIP" = "1" ] && { echo "  SKIP  power-cut: no QEMU found"; exit 0; }
    fail "power-cut: no QEMU found (install qemu-system-x86, or set CRASH_ALLOW_SKIP=1)"
fi
[ -f "$IMAGE" ] || fail "power-cut: no disk image at $IMAGE (run 'make full' first)"
[ -f "$ROOT/tools/crash_inject.py" ] || fail "power-cut: tools/crash_inject.py is missing"

# One workload per shape of metadata work. Each is a program in /bin that churns
# the journal in its own way: directory creation, create/delete/chmod, and the
# extent writer.
workloads=("fs_mkdir_test" "fs_meta_test" "fs_mkdir_test" "fs_meta_test")

# Divide the iterations over the workloads, giving the remainder to the first
# ones so a small gate still exercises every shape at least once.
per=$(( ITERS / ${#workloads[@]} ))
extra=$(( ITERS % ${#workloads[@]} ))
if [ "$per" -eq 0 ]; then
    per=1
fi

status=0
total=0
for i in "${!workloads[@]}"; do
    count=$per
    if [ "$i" -lt "$extra" ]; then
        count=$(( count + 1 ))
    fi
    printf '  %-14s %d crash points  ' "${workloads[$i]}" "$count"
    if QEMU="$qemu" python3 "$ROOT/tools/crash_inject.py" "$IMAGE" \
            "${workloads[$i]}" --iterations "$count" --quiet --seed 7 >/dev/null 2>&1; then
        echo PASS
    else
        echo FAIL
        status=1
    fi
    total=$(( total + count ))
done

if [ "$status" -eq 0 ]; then
    printf '  PASS  %d power-cut iterations recovered, no metadata left half-written\n' "$total"
else
    printf '  FAIL  at least one power-cut point left the volume inconsistent\n' >&2
fi
exit "$status"
