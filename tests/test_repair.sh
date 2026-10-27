#!/usr/bin/env bash
#
# test_repair.sh - a deliberately corrupted volume, repaired from the shell.
#
# The wave's exit criterion is "a deliberately corrupted volume repaired from
# the recovery console". This corrupts a *copy* of the image in the three ways
# a volume really goes wrong, and proves the three things that must then hold:
#
#   1. `repair` finds every fault and does not call the volume clean;
#   2. `repair fix` repairs the allocation and says so;
#   3. the offline checker (tools/fsck_hbfs.py) agrees afterwards, and the
#      files that were there before still read back.
#
# The faults, applied straight to the bitmap the way a lost write would:
#   * a block a file needs is marked free  -> the next allocation would hand it
#     to someone else (the dangerous one);
#   * a block nothing references is marked used -> a leak;
#   * the superblock's free count no longer matches the bitmap.
#
# Usage: tests/test_repair.sh [image]
#
# Exits 0 when the volume is found, fixed and verified, 1 otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${ROOT}/mellivora.img}"
DRIVE="$ROOT/tools/qemu_drive.py"
FSCK="$ROOT/tools/fsck_hbfs.py"

fail() { printf '  FAIL  %s\n' "$*" >&2; exit 1; }

[ -f "$IMAGE" ] || fail "repair: no disk image at $IMAGE (run 'make full' first)"
[ -f "$DRIVE" ] || fail "repair: $DRIVE is missing"
[ -f "$FSCK" ] || fail "repair: $FSCK is missing"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# One image copy at a time: the image is 2 GB, /tmp is a tmpfs, and two copies
# side by side is how this gate ran out of space.
copy="$work/img"
cp "$IMAGE" "$copy"

# The three faults. Block numbers are data-area relative, which is what the
# bitmap indexes; 500 is inside the populated range and 400000 is past it.
python3 - "$copy" <<'PY'
import struct, sys
path = sys.argv[1]
SEC, BLK = 512, 4096
SB_LBA, BITMAP_LBA, BITMAP_SECTS = 4096, 4097, 128
img = open(path, "r+b")
sb = img.read(0)  # keep the handle happy; reads below are explicit
img.seek(SB_LBA * SEC)
superblock = bytearray(img.read(SEC))

def set_bit(buf, block, value):
    byte = block >> 3
    bit = block & 7
    if value:
        buf[byte] |= 1 << bit
    else:
        buf[byte] &= ~(1 << bit)

img.seek(BITMAP_LBA * SEC)
bitmap = bytearray(img.read(BITMAP_SECTS * SEC))
set_bit(bitmap, 500, 0)        # a file's block looks free
set_bit(bitmap, 400000, 1)     # nobody owns this one
img.seek(BITMAP_LBA * SEC)
img.write(bitmap)

# and the free count no longer describes the bitmap
allocated = sum(bin(b).count("1") for b in bitmap)
struct.pack_into("<I", superblock, 12, 523727 - allocated + 7)
img.seek(SB_LBA * SEC)
img.write(superblock)
img.close()
PY

expect() {   # expect <text> <file>
    grep -q -- "$1" "$2" || fail "expected '$1' in: $(tail -3 "$2" | tr '\n' ' ')"
}

out="$work/check.txt"
printf '  %-34s ' "the check finds the damage"
python3 "$DRIVE" "$copy" "repair" --wait 40 --expect REPAIR_ --out "$out" >/dev/null 2>&1 \
    || fail "repair did not run"
expect "REPAIR_ISSUES" "$out"
expect "leaks 1" "$out"
expect "lost 1" "$out"
echo PASS

out="$work/fix.txt"
printf '  %-34s ' "the fix repairs it"
python3 "$DRIVE" "$copy" "repair fix" --wait 40 --expect REPAIR_ --out "$out" >/dev/null 2>&1 \
    || fail "repair fix did not run"
expect "REPAIR_FIXED" "$out"
echo PASS

# The report above is what the check found, not what it left behind: the fix
# has to persist, so a second boot's check is the assertion.
out="$work/again.txt"
printf '  %-34s ' "the repair survives a reboot"
python3 "$DRIVE" "$copy" "repair" --wait 40 --expect REPAIR_ --out "$out" >/dev/null 2>&1 \
    || fail "the volume does not boot after the repair"
expect "REPAIR_CLEAN" "$out"
expect "leaks 0" "$out"
expect "lost 0" "$out"
echo PASS

printf '  %-34s ' "the offline checker agrees"
if python3 "$FSCK" "$copy" >"$work/fsck.txt" 2>&1; then
    echo PASS
else
    sed -n '1,20p' "$work/fsck.txt"
    fail "fsck is not satisfied after the repair"
fi

# The listing is the cheap proof that the volume still mounts and its
# directories still parse; the byte-for-byte content check is fsck's job and
# ran above. The screen keeps only the last 25 lines, so this looks at the tail
# of a 208-entry listing rather than at the first name in it.
printf '  %-34s ' "the files are still listed"
out="$work/files.txt"
python3 "$DRIVE" "$copy" "ls /bin" --wait 20 --out "$out" >/dev/null 2>&1 \
    || fail "the volume does not boot after the repair"
expect "yes" "$out"
echo PASS

# A check is a read, even though it borrows the resident bitmap as scratch:
# leaving the cleared copy behind hands the next sync a bitmap with live blocks
# marked free, which is how one pass of this tool damaged the image it looked at.
rm -f "$copy"
cp "$IMAGE" "$copy"
out="$work/untouched.txt"
printf '  %-34s ' "a check leaves the volume untouched"
python3 "$DRIVE" "$copy" "repair" --wait 40 --expect REPAIR_ --out "$out" >/dev/null 2>&1 \
    || fail "repair did not run"
python3 "$DRIVE" "$copy" "sync" --wait 20 >/dev/null 2>&1 \
    || fail "sync did not run"
if python3 "$FSCK" "$copy" >"$work/fsck2.txt" 2>&1; then
    echo PASS
else
    sed -n '1,20p' "$work/fsck2.txt"
    fail "a check-only repair changed the volume it checked"
fi

printf '  PASS  a corrupted volume was found, repaired and verified\n'
exit 0
