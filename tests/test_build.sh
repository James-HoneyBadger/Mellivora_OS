#!/usr/bin/env bash
#
# test_build.sh - Build-time regression tests for Mellivora OS
#
# Checks binary sizes, image layout, and basic HBFS integrity.
# Run after "make full" (or at least "make && make populate").
#
set -uo pipefail

PASS=0
FAIL=0
IMG="mellivora.img"

pass() { ((PASS++)); printf "  \033[32mPASS\033[0m  %s\n" "$1"; }
fail() { ((FAIL++)); printf "  \033[31mFAIL\033[0m  %s\n" "$1"; }
check() {
    # check DESCRIPTION CONDITION
    if eval "$2" 2>/dev/null; then pass "$1"; else fail "$1"; fi
}

echo "=== Mellivora OS Regression Tests ==="
echo ""

# ---------- Binary size guards ----------
echo "[Build sizes]"

BOOT_SIZE=$(stat -c %s boot.bin 2>/dev/null || echo 0)
check "boot.bin exists"         "[[ $BOOT_SIZE -gt 0 ]]"
check "boot.bin <= 512 bytes"   "[[ $BOOT_SIZE -le 512 ]]"

STAGE2_SIZE=$(stat -c %s stage2.bin 2>/dev/null || echo 0)
check "stage2.bin exists"       "[[ $STAGE2_SIZE -gt 0 ]]"
check "stage2.bin <= 16384 bytes (32 sectors)" "[[ $STAGE2_SIZE -le 16384 ]]"

KERNEL_SIZE=$(stat -c %s kernel.bin 2>/dev/null || echo 0)
check "kernel.bin exists"       "[[ $KERNEL_SIZE -gt 0 ]]"
check "kernel.bin <= 512 KB"    "[[ $KERNEL_SIZE -le 524288 ]]"
check "kernel.bin > 100 KB"     "[[ $KERNEL_SIZE -ge 102400 ]]"

IMG_SIZE=$(stat -c %s "$IMG" 2>/dev/null || echo 0)
check "mellivora.img exists"    "[[ $IMG_SIZE -gt 0 ]]"
check "mellivora.img == 64 MB"  "[[ $IMG_SIZE -eq 67108864 ]]"

echo ""

# ---------- kernel_sectors.inc ----------
echo "[Kernel sectors]"
if [[ -f kernel_sectors.inc ]]; then
    KSECT=$(grep -oP 'equ\s+\K[0-9]+' kernel_sectors.inc)
    EXPECTED=$(( (KERNEL_SIZE + 511) / 512 ))
    check "kernel_sectors.inc value matches binary" "[[ $KSECT -eq $EXPECTED ]]"
else
    fail "kernel_sectors.inc exists"
fi

echo ""

# ---------- Boot sector signature ----------
echo "[Boot sector]"
SIG=$(xxd -s 510 -l 2 -p "$IMG" 2>/dev/null)
check "MBR signature 55AA"     "[[ '$SIG' == '55aa' ]]"

echo ""

# ---------- HBFS superblock ----------
echo "[HBFS superblock]"
# Superblock is at LBA 417 = byte offset 417*512 = 213504
SB_OFF=$((417 * 512))
MAGIC=$(xxd -s $SB_OFF -l 4 -p "$IMG" 2>/dev/null)
# HBFS_MAGIC = 0x48424653 -> little-endian on disk: 53 46 42 48
check "Superblock magic = HBFS" "[[ '$MAGIC' == '53464248' ]]"

VERSION=$(xxd -s $((SB_OFF + 4)) -l 4 -e -g 4 "$IMG" 2>/dev/null | awk '{print $2}')
check "Superblock version = 1"  "[[ '$VERSION' == '00000001' ]]"

BLKSZ=$(xxd -s $((SB_OFF + 28)) -l 4 -e -g 4 "$IMG" 2>/dev/null | awk '{print $2}')
check "Block size = 4096 (0x1000)" "[[ '$BLKSZ' == '00001000' ]]"

echo ""

# ---------- HBFS bitmap sanity ----------
echo "[HBFS bitmap]"
# Bitmap at LBA 418 = offset 418*512 = 214016
BM_OFF=$((418 * 512))
# First byte should not be 0x00 (at least some blocks allocated)
FIRST_BM=$(xxd -s $BM_OFF -l 1 -p "$IMG" 2>/dev/null)
check "Bitmap first byte != 0 (blocks allocated)" "[[ '$FIRST_BM' != '00' ]]"

echo ""

# ---------- Root directory ----------
echo "[HBFS root directory]"
# Root dir at LBA 426 = offset 426*512 = 218112
RD_OFF=$((426 * 512))
# First entry should have a non-null first byte (filename)
FIRST_ENTRY=$(xxd -s $RD_OFF -l 1 -p "$IMG" 2>/dev/null)
check "Root dir first entry not empty" "[[ '$FIRST_ENTRY' != '00' ]]"

# Check that a known directory exists (e.g., "bin")
BIN_FOUND=$(xxd -s $RD_OFF -l 65536 "$IMG" 2>/dev/null | grep -c 'bin' || true)
check "Root dir contains 'bin' directory" "[[ $BIN_FOUND -gt 0 ]]"

echo ""

# ---------- Program binaries ----------
echo "[Program binaries]"
for prog in hello fibonacci primes snake tetris sokoban guess mine colors banner sysinfo edit; do
    BIN="programs/${prog}.bin"
    if [[ -f "$BIN" ]]; then
        SZ=$(stat -c %s "$BIN")
        check "$BIN exists and > 0 bytes" "[[ $SZ -gt 0 ]]"
    else
        fail "$BIN exists"
    fi
done

echo ""

# ---------- TCC compiler smoke test ----------
echo "[TCC smoke test]"
TCC_BIN="programs/tcc.bin"
if [[ -f "$TCC_BIN" ]]; then
    TCC_SIZE=$(stat -c %s "$TCC_BIN")
    check "tcc.bin exists and > 1 KB"  "[[ $TCC_SIZE -ge 1024 ]]"
    check "tcc.bin < 64 KB"            "[[ $TCC_SIZE -le 65536 ]]"
else
    fail "tcc.bin exists"
fi

echo ""

# ---------- Summary ----------
TOTAL=$((PASS + FAIL))
echo "=== Results: $PASS/$TOTAL passed ==="
if [[ $FAIL -gt 0 ]]; then
    echo "$FAIL test(s) FAILED"
    exit 1
else
    echo "All tests passed!"
    exit 0
fi
