#!/usr/bin/env bash
#
# build_iso.sh - Create a bootable ISO for Mellivora OS
#
# The ISO uses El Torito no-emulation boot.  The BIOS preloads the
# boot sector + stage2 + kernel directly into memory at 0x7C00, so
# the OS kernel starts without any disk reads.
#
# At runtime the kernel still needs an ATA hard disk for the HBFS
# filesystem.  When launching in QEMU, attach the disk image on
# the primary IDE channel alongside the CD-ROM (see run_iso.sh).
#
set -euo pipefail

# ── Colors (only when stdout is a terminal) ──────────────────────────
if [[ -t 1 ]]; then
  RED=$'\033[1;31m'  GRN=$'\033[1;32m'  CYN=$'\033[1;36m'
  YEL=$'\033[1;33m'  BLD=$'\033[1m'     DIM=$'\033[2m'   RST=$'\033[0m'
else
  RED='' GRN='' CYN='' YEL='' BLD='' DIM='' RST=''
fi

info()  { echo "${CYN}::${RST} $*"; }
ok()    { echo "${GRN} ✓${RST} $*"; }
warn()  { echo "${YEL} ⚠${RST} $*" >&2; }
die()   { echo "${RED} ✗${RST} $*" >&2; exit 1; }

# ── Arguments ────────────────────────────────────────────────────────
if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <staging-dir> <output.iso>" >&2
  exit 1
fi

ROOT_DIR="$1"
OUTPUT_FILE="$2"
VOLUME_NAME="${ISO_VOLUME_NAME:-MELLIVORA}"

[[ -d "$ROOT_DIR" ]] || die "Staging directory not found: $ROOT_DIR"

ROOT_DIR="$(cd "$ROOT_DIR" && pwd)"
mkdir -p "$(dirname "$OUTPUT_FILE")"
OUTPUT_FILE="$(cd "$(dirname "$OUTPUT_FILE")" && pwd)/$(basename "$OUTPUT_FILE")"
rm -f "$OUTPUT_FILE"

# ── Boot-load-size validation ────────────────────────────────────────
BOOT_LOAD_SIZE="${ISO_BOOT_SECTORS:-}"

if [[ -z "$BOOT_LOAD_SIZE" ]]; then
  warn "ISO_BOOT_SECTORS not set — defaulting to 33 (MBR+stage2 only, no kernel preload)"
  BOOT_LOAD_SIZE=33
elif [[ "$BOOT_LOAD_SIZE" -le 33 ]]; then
  warn "ISO_BOOT_SECTORS=$BOOT_LOAD_SIZE covers only MBR+stage2 — kernel will not be preloaded"
fi

# ── Verify boot image exists ────────────────────────────────────────
BOOT_IMAGE="$ROOT_DIR/boot/mellivora.img"
[[ -f "$BOOT_IMAGE" ]] || die "Boot image not found: $BOOT_IMAGE"

# ── Build summary ────────────────────────────────────────────────────
FILE_COUNT=$(find "$ROOT_DIR" -type f | wc -l)

echo ""
info "Volume:     ${BLD}$VOLUME_NAME${RST}"
info "Boot image: ${DIM}$BOOT_IMAGE${RST}"
info "Load size:  ${BLD}$BOOT_LOAD_SIZE${RST} sectors ($(( BOOT_LOAD_SIZE * 512 / 1024 )) KB preloaded by BIOS)"
info "Staging:    ${BLD}$FILE_COUNT${RST} files"
echo ""

# ── Common mkisofs arguments ────────────────────────────────────────
MKISOFS_ARGS="-R -J -V $VOLUME_NAME \
  -b boot/mellivora.img -no-emul-boot -boot-load-size $BOOT_LOAD_SIZE"

# ── Build ISO with the first available tool ──────────────────────────
TOOL=""
if command -v xorriso >/dev/null 2>&1; then
  TOOL="xorriso"
  info "Tool: ${BLD}xorriso${RST}"
  xorriso -as mkisofs $MKISOFS_ARGS \
    -o "$OUTPUT_FILE" "$ROOT_DIR"
elif command -v genisoimage >/dev/null 2>&1; then
  TOOL="genisoimage"
  info "Tool: ${BLD}genisoimage${RST}"
  genisoimage $MKISOFS_ARGS \
    -o "$OUTPUT_FILE" "$ROOT_DIR"
elif command -v mkisofs >/dev/null 2>&1; then
  TOOL="mkisofs"
  info "Tool: ${BLD}mkisofs${RST}"
  mkisofs $MKISOFS_ARGS \
    -o "$OUTPUT_FILE" "$ROOT_DIR"
elif command -v hdiutil >/dev/null 2>&1; then
  TOOL="hdiutil"
  info "Tool: ${BLD}hdiutil${RST}"
  (
    cd "$ROOT_DIR"
    hdiutil makehybrid \
      -ov \
      -o "$OUTPUT_FILE" \
      . \
      -iso -joliet \
      -default-volume-name "$VOLUME_NAME" \
      -eltorito-boot boot/mellivora.img \
      -no-emul-boot >/dev/null
  )
  if [[ ! -f "$OUTPUT_FILE" && -f "${OUTPUT_FILE}.cdr" ]]; then
    mv "${OUTPUT_FILE}.cdr" "$OUTPUT_FILE"
  fi
else
  die "No ISO creation tool found (need xorriso, genisoimage, mkisofs, or hdiutil)"
fi

[[ -f "$OUTPUT_FILE" ]] || die "ISO creation produced no output file"

# ── Summary ──────────────────────────────────────────────────────────
ISO_SIZE=$(wc -c < "$OUTPUT_FILE")
ISO_SIZE_HR=$(numfmt --to=iec-i --suffix=B "$ISO_SIZE" 2>/dev/null || echo "${ISO_SIZE} bytes")
SHA256=$(sha256sum "$OUTPUT_FILE" | cut -d' ' -f1)

echo ""
ok "Created ${BLD}$(basename "$OUTPUT_FILE")${RST}"
echo "   Size:   ${BLD}$ISO_SIZE_HR${RST}"
echo "   SHA256: ${DIM}$SHA256${RST}"
