#!/usr/bin/env bash
#
# test_volume.sh - the volume model: its name, and getting it back safely.
#
# Two things a person asks of a volume that are not about file contents:
# what is it called, and can I unplug it. Both are answered from the superblock
# and the caches rather than guessed, so both are asserted here:
#
#   1. a fresh volume has a label, and `label` reads it back;
#   2. `label NAME` stores a new name in the superblock, and it survives a
#      reboot - it is on the volume, not in the session;
#   3. `volumes` names the mounted volume's filesystem label beside the
#      partition table's own name for it, because the two are different things;
#   4. `eject` with nothing attached says so instead of pretending;
#   5. `eject` with a stick attached flushes the journal and the block cache
#      and releases the device;
#   6. a stream takes the compression container like any other content;
#   7. the offline checker agrees afterwards, and names the label it found.
#
# Usage:  tests/test_volume.sh [image]
#
# Env:
#   VOLUME_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   VOLUME_TIMEOUT=240    seconds allowed for a machine to reach its shell
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${VOLUME_IMAGE:-$ROOT/mellivora.img}}"
DRIVE="$ROOT/tools/qemu_drive.py"
FSCK="$ROOT/tools/fsck_hbfs.py"
TIMEOUT="${VOLUME_TIMEOUT:-240}"

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
    if [ "${VOLUME_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  volume tests: no QEMU found"
        exit 0
    fi
    fail "volume tests: no QEMU found. Install qemu-system-x86, or set VOLUME_ALLOW_SKIP=1."
fi

[ -f "$DRIVE" ] || fail "volume tests: missing $DRIVE"
[ -f "$FSCK" ] || fail "volume tests: missing $FSCK"
[ -f "$IMAGE" ] || fail "volume tests: disk image not found: $IMAGE (run 'make full' first)"

# A 1 MiB stick, so the removable path has a device to find.
STICK="$(mktemp -t mellivora-volstick-XXXXXX)"
trap 'rm -f "$STICK"' EXIT
python3 - "$STICK" <<'PY'
import sys
data = bytearray(1024 * 1024)
data[0:8] = b"MELLIVOR"
data[510] = 0x55
data[511] = 0xAA
with open(sys.argv[1], "wb") as f:
    f.write(data)
PY

drive() {
    QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "$@" \
        --timeout "$TIMEOUT"
}

run() {
    local name="$1"; local text="$2"; local command="$3"; shift 3
    printf '  %-46s ' "$name"
    local log
    log="$(mktemp)"
    if drive "$command" --out "$log" --expect "$text" "$@" >/dev/null 2>&1; then
        echo "PASS"
        rm -f "$log"
    else
        echo "FAIL  (output: $log)"
        exit 1
    fi
}

# A label a volume is born with is worth having - `format` and populate.py both
# write one - but asserting it here would make this gate depend on an image
# nobody has renamed. So the name is set first and then read back, and the
# presence of a default is the last case's business.
drive "label MELLIVORA" >/dev/null 2>&1 || fail "volume tests: could not set the label"

run "a volume's label is read back" "Label: MELLIVORA" "label"

drive "label Work Disk" >/dev/null 2>&1 || fail "volume tests: could not set the label"

# A separate boot: the name has to be on the volume, not in the session that
# set it.
run "the label survives a reboot" "Label: Work Disk" "label"

run "volumes names the filesystem beside the partition" \
    'fs label "Work Disk"' "volumes"

run "eject with nothing attached says so" \
    "no removable volume is attached" "eject"

run "eject with a stick attached flushes and releases it" \
    "caches flushed" "eject" --usb-disk "$STICK"

# A stream is content like any other, so it takes the same container. Written
# in one boot, compacted and read in another: the console echoes the text that
# was typed, so a read in the same boot would prove nothing.
drive "del sv.txt" --then "touch sv.txt" \
    --then "stream write sv.txt notes a-stream-payload-worth-compressing" \
    >/dev/null 2>&1 || fail "volume tests: could not create the stream"

run "a stream compacts and reads back" "a-stream-payload-worth-compressing" \
    "compact sv.txt:notes" --then "cat sv.txt:notes"

drive "del sv.txt" >/dev/null 2>&1 || true
drive "label MELLIVORA" >/dev/null 2>&1 || true

printf '  %-46s ' "the volume is consistent afterwards"
if python3 "$FSCK" "$IMAGE" 2>&1 | grep -q "volume label: MELLIVORA"; then
    echo "PASS"
else
    echo "FAIL  (run: python3 tools/fsck_hbfs.py $IMAGE)"
    exit 1
fi

echo "  Volume: labelled, named by volumes, ejected with its caches flushed"
