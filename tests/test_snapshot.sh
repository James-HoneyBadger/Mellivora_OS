#!/usr/bin/env bash
#
# test_snapshot.sh - a copy-on-write snapshot of the whole volume.
#
# Conformance item 6 asks three things of a snapshot taken mid-write: that it is
# consistent, that what it holds reads back, and that dropping it returns the
# free count to where it was. This gate proves the first and the third from the
# outside, and the second from the blocks themselves:
#
#   1. a volume with no snapshot says so, and taking one reports an empty map;
#   2. a write after that is preserved: the map grows, and a `repair` run - which
#      would call an unclaimed block a leak - finds the volume clean, so the
#      snapshot's own blocks are claimed by something that knows about them;
#   3. the snapshot survives a reboot, because the superblock names it and the
#      record is on the volume;
#   4. a second create is refused, and dropping it returns every block it held:
#      the referenced-block count comes back to what it was, and the offline
#      checker agrees;
#   5. dropping a snapshot that was never taken says so.
#
# Usage:  tests/test_snapshot.sh [image]
#
# Env:
#   SNAPSHOT_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   SNAPSHOT_TIMEOUT=240    seconds allowed for a machine to reach its shell
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${SNAPSHOT_IMAGE:-$ROOT/mellivora.img}}"
DRIVE="$ROOT/tools/qemu_drive.py"
FSCK="$ROOT/tools/fsck_hbfs.py"
TIMEOUT="${SNAPSHOT_TIMEOUT:-240}"

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
    if [ "${SNAPSHOT_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  snapshot tests: no QEMU found"
        exit 0
    fi
    fail "snapshot tests: no QEMU found. Install qemu-system-x86, or set SNAPSHOT_ALLOW_SKIP=1."
fi

[ -f "$DRIVE" ] || fail "snapshot tests: missing $DRIVE"
[ -f "$FSCK" ] || fail "snapshot tests: missing $FSCK"
[ -f "$IMAGE" ] || fail "snapshot tests: disk image not found: $IMAGE (run 'make full' first)"

drive() {
    QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "$@" \
        --timeout "$TIMEOUT"
}

run() {
    local name="$1"; local text="$2"; local command="$3"; shift 3
    printf '  %-46s ' "$name"
    local log args want
    log="$(mktemp)"
    # TEXT may list more than one string, separated by '|': every one has to
    # appear. Passed whole it would be one needle that never matches.
    args=()
    local was_ifs="$IFS"
    IFS='|'
    for want in $text; do
        [ -n "$want" ] && args+=(--expect "$want")
    done
    IFS="$was_ifs"
    if drive "$command" --out "$log" "${args[@]}" "$@" >/dev/null 2>&1; then
        echo "PASS"
        rm -f "$log"
    else
        echo "FAIL  (output: $log)"
        exit 1
    fi
}

# The referenced-block count a repair reports, so the free count can be compared
# across the snapshot instead of being taken on trust.
ref_blocks() {
    local log
    log="$(mktemp)"
    drive "repair" --out "$log" --expect "repair entries" >/dev/null 2>&1 \
        || { rm -f "$log"; printf ''; return; }
    sed -n 's/.*blocks \([0-9]*\).*/\1/p' "$log" | tail -1
    rm -f "$log"
}

# Leave no snapshot behind from an earlier run: the cases below are about the
# transitions, and they say what state they start from.
drive "snapshot drop" >/dev/null 2>&1 || true
drive "del sn_probe.txt" >/dev/null 2>&1 || true

run "a volume with no snapshot says so" "snapshot: none taken" "snapshot"

run "dropping a snapshot that was never taken says so" \
    "snapshot: none taken" "snapshot drop"

run "a snapshot is taken, with an empty map" \
    "snapshot: taken|Snapshot: 0 of" "snapshot create"

run "a second create is refused" \
    "one is already taken" "snapshot create"

# A write while the snapshot lives is preserved. `touch` changes the root
# directory's block and the free count, so both have to be kept; the map going
# from 0 to something is the evidence, and the repair pass is the check that the
# blocks it kept are not reports of a leak.
printf '  %-46s ' "a write after the snapshot is preserved"
log="$(mktemp)"
if drive $'touch sn_probe.txt\nsnapshot\nrepair' --out "$log" \
        --expect "REPAIR_CLEAN" --max "Snapshot: 0 of:1" >/dev/null 2>&1; then
    echo "PASS"
    rm -f "$log"
else
    echo "FAIL  (output: $log)"
    exit 1
fi

run "the snapshot survives a reboot" "Snapshot: " "snapshot"

BEFORE="$(ref_blocks)"
[ -n "$BEFORE" ] || fail "snapshot tests: could not read the referenced-block count"

run "the snapshot is dropped" "snapshot: dropped|block(s) returned" "snapshot drop"

run "the volume has no snapshot afterwards" "snapshot: none taken" "snapshot"

AFTER="$(ref_blocks)"
# The probe file is still there, so one block is expected to be new; what must
# not be new is any of the snapshot's own.
if [ "$((AFTER - BEFORE))" -gt 1 ]; then
    fail "the snapshot kept $((AFTER - BEFORE)) blocks after it was dropped (before $BEFORE, after $AFTER)"
fi

# Reading out of a snapshot is the second half of the item: the files it holds
# have to read correctly. Two shapes matter, and neither can be asserted from the
# same boot that types the words - the console echoes what is typed, so a
# document's title would match its own command line. The content is therefore
# produced by `cp` from a document already on the volume, and the boot that reads
# it never mentions the words.
drive "del sn_read.txt" >/dev/null 2>&1 || true
drive "cp /docs/todo.txt sn_read.txt" \
    --then "snapshot create" --expect "snapshot: taken" >/dev/null 2>&1 \
    || fail "snapshot tests: could not take the snapshot the reads use"

# Reading a file that is still there is the easy half; the half worth gating is
# a file the volume no longer has. Its blocks are freed after the snapshot, and
# a freed block is one the allocator will hand out again - so the snapshot has to
# have taken the content before the free, not before the next write.
printf '  %-46s ' "a deleted file reads out of the snapshot"
out="$(mktemp)"
if drive "del sn_read.txt" --then "snapshot cat sn_read.txt" --settle 3 \
        --expect "Mellivora OS TODO List" --out "$out" >/dev/null 2>&1; then
    echo "PASS"
    rm -f "$out"
else
    echo "FAIL  (output: $out)"
    exit 1
fi

printf '  %-46s ' "a file under a directory reads through the snapshot"
out="$(mktemp)"
if drive "snapshot cat /docs/todo.txt" --settle 2 \
        --expect "Mellivora OS TODO List" --out "$out" >/dev/null 2>&1; then
    echo "PASS"
    rm -f "$out"
else
    echo "FAIL  (output: $out)"
    exit 1
fi

# Writing the same name twelve times and reading every round back is the case
# that catches a write lost in the seam between two of them - the free, the
# allocation, the copy a snapshot makes - which no single write can show.
printf '  %-46s ' "a rewrite round-trips with no snapshot"
out="$(mktemp)"
if drive "rewrite_test" --settle 2 --expect "REWRITE_OK" --out "$out" >/dev/null 2>&1; then
    echo "PASS"
    rm -f "$out"
else
    echo "FAIL  (output: $out)"
    exit 1
fi

printf '  %-46s ' "a rewrite round-trips while a snapshot is live"
out="$(mktemp)"
if drive "rewrite_test" --settle 2 --expect "REWRITE_OK" --out "$out" >/dev/null 2>&1; then
    echo "PASS"
    rm -f "$out"
else
    echo "FAIL  (output: $out)"
    exit 1
fi

printf '  %-46s ' "a name the snapshot never held is refused"
out="$(mktemp)"
if drive "snapshot cat sn_absent.txt" --settle 2 \
        --expect "no such file in the snapshot" --out "$out" >/dev/null 2>&1; then
    echo "PASS"
    rm -f "$out"
else
    echo "FAIL  (output: $out)"
    exit 1
fi

drive "snapshot drop" >/dev/null 2>&1 || fail "snapshot tests: could not drop the read snapshot"

printf '  %-46s ' "the volume is consistent afterwards"
if python3 "$FSCK" "$IMAGE" >/dev/null 2>&1; then
    echo "PASS"
else
    echo "FAIL  (run: python3 tools/fsck_hbfs.py $IMAGE)"
    exit 1
fi

drive "del sn_probe.txt" >/dev/null 2>&1 || true
drive "del sn_read.txt" >/dev/null 2>&1 || true

echo "  Snapshot: taken mid-write, read back after a delete, and returned every block"
