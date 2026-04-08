#!/usr/bin/env bash
#
# test_build.sh - Build-time regression tests for Mellivora OS
#
# Checks binary sizes, image layout, HBFS integrity, program binaries,
# and constant consistency between kernel.asm and populate.py.
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

# Check ALL program binaries build successfully
echo ""
echo "[All program binaries]"
PROG_COUNT=0
PROG_PASS=0
for src in programs/*.asm; do
    [[ ! -f "$src" ]] && continue
    bin="${src%.asm}.bin"
    name=$(basename "$src" .asm)
    if [[ -f "$bin" ]]; then
        SZ=$(stat -c %s "$bin")
        if [[ $SZ -gt 0 ]]; then
            ((PROG_PASS++))
        else
            fail "$name.bin is empty"
        fi
        ((PROG_COUNT++))
    else
        fail "$name.bin not built"
        ((PROG_COUNT++))
    fi
done
check "All $PROG_COUNT programs built successfully" "[[ $PROG_PASS -eq $PROG_COUNT ]]"

# Verify no program exceeds 1MB (PROGRAM_MAX_SIZE)
echo ""
echo "[Program size limits]"
OVERSIZED=0
for bin in programs/*.bin; do
    [[ ! -f "$bin" ]] && continue
    SZ=$(stat -c %s "$bin")
    if [[ $SZ -gt 1048576 ]]; then
        fail "$(basename $bin) exceeds 1MB ($SZ bytes)"
        ((OVERSIZED++))
    fi
done
check "No program exceeds 1MB" "[[ $OVERSIZED -eq 0 ]]"

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

# ---------- Constant consistency (kernel.asm vs populate.py) ----------
echo "[Constant consistency]"

# Extract key constants from kernel.asm and compare with populate.py
K_MAGIC=$(grep -oP "HBFS_MAGIC\s+equ\s+\K0x[0-9A-Fa-f]+" kernel.asm)
P_MAGIC=$(grep -oP "HBFS_MAGIC\s*=\s*\K0x[0-9A-Fa-f]+" populate.py)
check "HBFS_MAGIC matches (kernel=$K_MAGIC, populate=$P_MAGIC)" "[[ '$K_MAGIC' == '$P_MAGIC' ]]"

K_BLKSZ=$(grep -oP "HBFS_BLOCK_SIZE\s+equ\s+\K[0-9]+" kernel.asm)
P_BLKSZ=$(grep -oP "BLOCK_SIZE\s*=\s*\K[0-9]+" populate.py | head -1)
check "BLOCK_SIZE matches (kernel=$K_BLKSZ, populate=$P_BLKSZ)" "[[ '$K_BLKSZ' == '$P_BLKSZ' ]]"

K_SB_LBA=$(grep -oP "HBFS_SUPERBLOCK_LBA\s+equ\s+\K[0-9]+" kernel.asm)
P_SB_LBA=$(grep -oP "HBFS_SUPERBLOCK_LBA\s*=\s*\K[0-9]+" populate.py)
check "SUPERBLOCK_LBA matches (kernel=$K_SB_LBA, populate=$P_SB_LBA)" "[[ '$K_SB_LBA' == '$P_SB_LBA' ]]"

K_BM_START=$(grep -oP "HBFS_BITMAP_START\s+equ\s+\K[0-9]+" kernel.asm)
P_BM_START=$(grep -oP "HBFS_BITMAP_START\s*=\s*\K[0-9]+" populate.py)
check "BITMAP_START matches (kernel=$K_BM_START, populate=$P_BM_START)" "[[ '$K_BM_START' == '$P_BM_START' ]]"

K_ROOT=$(grep -oP "HBFS_ROOT_DIR_START\s+equ\s+\K[0-9]+" kernel.asm)
P_ROOT=$(grep -oP "HBFS_ROOT_DIR_START\s*=\s*\K[0-9]+" populate.py)
check "ROOT_DIR_START matches (kernel=$K_ROOT, populate=$P_ROOT)" "[[ '$K_ROOT' == '$P_ROOT' ]]"

K_DATA=$(grep -oP "HBFS_DATA_START\s+equ\s+\K[0-9]+" kernel.asm)
P_DATA=$(grep -oP "HBFS_DATA_START\s*=\s*\K[0-9]+" populate.py)
check "DATA_START matches (kernel=$K_DATA, populate=$P_DATA)" "[[ '$K_DATA' == '$P_DATA' ]]"

K_ENTRY=$(grep -oP "HBFS_DIR_ENTRY_SIZE\s+equ\s+\K[0-9]+" kernel.asm)
P_ENTRY=$(grep -oP "HBFS_DIR_ENTRY_SIZE\s*=\s*\K[0-9]+" populate.py)
check "DIR_ENTRY_SIZE matches (kernel=$K_ENTRY, populate=$P_ENTRY)" "[[ '$K_ENTRY' == '$P_ENTRY' ]]"

K_ROOTBLK=$(grep -oP "HBFS_ROOT_DIR_BLOCKS\s+equ\s+\K[0-9]+" kernel.asm)
P_ROOTBLK=$(grep -oP "HBFS_ROOT_DIR_BLOCKS\s*=\s*\K[0-9]+" populate.py)
check "ROOT_DIR_BLOCKS matches (kernel=$K_ROOTBLK, populate=$P_ROOTBLK)" "[[ '$K_ROOTBLK' == '$P_ROOTBLK' ]]"

K_SUBBLK=$(grep -oP "HBFS_SUBDIR_BLOCKS\s+equ\s+\K[0-9]+" kernel.asm)
P_SUBBLK=$(grep -oP "HBFS_SUBDIR_BLOCKS\s*=\s*\K[0-9]+" populate.py)
check "SUBDIR_BLOCKS matches (kernel=$K_SUBBLK, populate=$P_SUBBLK)" "[[ '$K_SUBBLK' == '$P_SUBBLK' ]]"

# Verify syscall numbers match between kernel.asm and programs/syscalls.inc
echo ""
echo "[Syscall number consistency]"
SYSCALL_MISMATCH=0
for SYS in SYS_EXIT SYS_PUTCHAR SYS_GETCHAR SYS_PRINT SYS_READ_KEY SYS_OPEN SYS_READ \
           SYS_WRITE SYS_CLOSE SYS_DELETE SYS_SEEK SYS_STAT SYS_MKDIR SYS_READDIR \
           SYS_SETCURSOR SYS_GETTIME SYS_SLEEP SYS_CLEAR SYS_SETCOLOR SYS_MALLOC \
           SYS_FREE SYS_EXEC SYS_DISK_READ SYS_DISK_WRITE SYS_BEEP SYS_DATE \
           SYS_CHDIR SYS_GETCWD SYS_SERIAL SYS_GETENV SYS_FREAD SYS_FWRITE \
           SYS_GETARGS SYS_SERIAL_IN; do
    K_VAL=$(grep -oP "${SYS}\s+equ\s+\K[0-9]+" kernel.asm 2>/dev/null || echo "")
    P_VAL=$(grep -oP "${SYS}\s+equ\s+\K[0-9]+" programs/syscalls.inc 2>/dev/null || echo "")
    if [[ -z "$K_VAL" || -z "$P_VAL" ]]; then
        fail "$SYS: missing (kernel='$K_VAL', syscalls.inc='$P_VAL')"
        ((SYSCALL_MISMATCH++))
    elif [[ "$K_VAL" != "$P_VAL" ]]; then
        fail "$SYS: mismatch (kernel=$K_VAL, syscalls.inc=$P_VAL)"
        ((SYSCALL_MISMATCH++))
    fi
done
check "All 34 syscall numbers consistent" "[[ $SYSCALL_MISMATCH -eq 0 ]]"

echo ""

# ---------- Kernel entry point ----------
echo "[Kernel binary structure]"
# Kernel at LBA 33 (offset 0x4200). First instruction should be valid x86.
# kernel.asm starts with `mov ax, 0x10` which is 66 B8 10 00 in 32-bit.
KERN_OFF=$((33 * 512))
KERN_FIRST=$(xxd -s $KERN_OFF -l 4 -p "$IMG" 2>/dev/null)
# In 32-bit mode, `mov eax, 0x10` is B8 10 00 00 00, but `mov ax, 0x10`
# uses operand-size prefix: 66 B8 10 00. Accept either pattern.
check "Kernel starts with valid x86 (first bytes: $KERN_FIRST)" \
    "[[ '$KERN_FIRST' == '66b81000' || '$KERN_FIRST' == 'b8100000' ]]"

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
