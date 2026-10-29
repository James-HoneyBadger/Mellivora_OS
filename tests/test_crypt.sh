#!/usr/bin/env bash
#
# test_crypt.sh - the volume-encryption gate (STORAGE.md section 8, the
# M3 exit criterion "an encrypted volume round-trips, and a refusal leaves it
# byte-identical").
#
# `crypt format` destroys the volume it is given, so this gate works on a copy.
# Running it against the image the rest of the suite boots is what left 240
# programs tombstoned in .populate_cache: every later case then tested an empty,
# locked disk and failed for a reason that had nothing to do with the feature.
#
# What is proved, in order:
#
#   1. a copy is formatted with AES-256-XTS and a marker is written through the
#      block layer, then read back in the same boot;
#   2. the marker is not in the raw image - a block written in the clear would
#      put it there, and the journal and directory blocks are plaintext by
#      design, so only the file's own block could carry it;
#   3. a reboot without the key refuses the mount, and `crypt status` says the
#      volume is encrypted and locked rather than "not encrypted";
#   4. the wrong key is refused and leaves the image byte-identical;
#   5. the right key unlocks it and the marker reads back.
#
# Usage:  tests/test_crypt.sh [image]
#
# Env:
#   CRYPT_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   CRYPT_TIMEOUT=180    seconds to wait for the boot marker per boot
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${CRYPT_IMAGE:-$ROOT/mellivora.img}}"
DRIVE="$ROOT/tools/qemu_drive.py"
BOOT_WAIT="${CRYPT_TIMEOUT:-180}"

# A 32-byte secret in hex, and a different one of the same length: the volume
# names the entry, so the *name* is what `crypt unlock` looks up.
KEY="000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F"
WRONG="E0E1E2E3E4E5E6E7E8E9EAEBECEDEEEFF0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF"
MARK="HELLOMARKER1234"
PROBE="cryptprobe.txt"

pass() { printf '  %-46s PASS\n' "$*"; }

fail() {
    printf '  %-46s FAIL  %s\n' "$1" "$2" >&2
    exit 1
}

qemu_available() {
    [ -n "${QEMU:-}" ] && return 0
    command -v qemu-system-x86_64 >/dev/null 2>&1 && return 0
    command -v qemu-system-i386 >/dev/null 2>&1 && return 0
    [ -x "$HOME/.local/bin/qemu-system-x86_64" ]
}

if ! qemu_available; then
    if [ "${CRYPT_ALLOW_SKIP:-0}" = 1 ]; then
        echo "  volume encryption: SKIP (no QEMU found)"
        exit 0
    fi
    fail "volume encryption" "no QEMU found"
fi

[ -f "$IMAGE" ] || fail "volume encryption" "no image at $IMAGE"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
img="$work/crypt.img"
cp --reflink=auto --sparse=always "$IMAGE" "$img" 2>/dev/null || cp "$IMAGE" "$img" \
    || fail "volume encryption" "could not copy $IMAGE"

echo "  volume encryption: $(basename "$IMAGE") -> a copy"

# step NAME -- qemu_drive args...
# One boot. On failure the machine's own words are printed, because a FAIL line
# with no evidence behind it is a line nobody can act on.
step() {
    local name="$1"
    shift
    local log="$work/step-$((++step_no)).log"
    if timeout $((BOOT_WAIT + 240)) python3 "$DRIVE" "$img" "$@" \
            --timeout "$BOOT_WAIT" --out "$log" >/dev/null 2>&1; then
        pass "$name"
        rm -f "$log"
        return 0
    fi
    echo "  $(printf '%-46s' "$name") FAIL" >&2
    sed -n '1,120p' "$log" | sed 's/^/        /' >&2
    exit 1
}
step_no=0

# 1. Format, write, read back in the same boot.
step "a volume is formatted with AES-256-XTS" \
    "key add volkey $KEY" \
    --then "crypt format volkey confirm" \
    --then "crypt status" \
    --then "write $PROBE" \
    --then "$MARK" \
    --then "" \
    --then "cat $PROBE" \
    --expect "key: stored" \
    --expect "crypt: volume formatted with AES-256-XTS" \
    --expect "encrypted (AES-256-XTS), key 'volkey'" \
    --reject "the format failed" \
    --wait 20

# 2. The marker is on the disk only as ciphertext. `grep` reads the raw image,
#    so a block the transform missed shows up here whether it is a file, a
#    journal pre-image or a stale cache flush.
if grep -a -q "$MARK" "$img"; then
    fail "the marker is not in the raw image" "found $MARK in the clear"
fi
pass "the marker is not in the raw image"

# 3. A reboot with an empty key store: the volume is refused, not mounted empty.
step "a reboot refuses the volume without the key" \
    "crypt status" \
    --expect "HBFS: volume is encrypted" \
    --expect "key 'volkey' - locked"

# 4. The wrong key is refused, and refusing must not touch the disk.
before="$(sha256sum "$img" | cut -d' ' -f1)"
step "the wrong key is refused" \
    "key add volkey $WRONG" \
    --then "crypt unlock volkey" \
    --expect "crypt: the volume did not open with that key"
after="$(sha256sum "$img" | cut -d' ' -f1)"
if [ "$before" != "$after" ]; then
    fail "the refusal leaves the image byte-identical" "the image changed"
fi
pass "the refusal leaves the image byte-identical"

# 5. The right key opens it and the file reads back.
step "the right key unlocks the volume" \
    "key add volkey $KEY" \
    --then "crypt unlock volkey" \
    --then "cat $PROBE" \
    --expect "crypt: volume unlocked" \
    --expect "$MARK" \
    --wait 20

echo "  PASS  volume encryption"
