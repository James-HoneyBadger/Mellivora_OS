#!/usr/bin/env bash
#
# run_iso.sh - Launch Mellivora OS from its bootable ISO in QEMU
#
# The ISO contains the disk image at /boot/mellivora.img.  The El
# Torito boot record preloads the kernel into memory, but the HBFS
# filesystem lives on the ATA disk image.  This script extracts the
# image to a temporary directory and launches QEMU with both the
# CD-ROM and the IDE disk so the full OS works.
#
# Usage:
#   ./run_iso.sh [mellivora.iso]
#
# Requirements: qemu-system-i386, one of: xorriso, bsdtar, 7z
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

show_banner() {
  echo "${CYN}"
  echo "  ╔══════════════════════════════════════╗"
  echo "  ║         M E L L I V O R A  OS        ║"
  echo "  ║        Bootable ISO Launcher         ║"
  echo "  ╚══════════════════════════════════════╝"
  echo "${RST}"
}

show_help() {
  echo "Usage: $0 [OPTIONS] [path/to/mellivora.iso]"
  echo ""
  echo "Launch Mellivora OS from a bootable ISO in QEMU."
  echo ""
  echo "Options:"
  echo "  -h, --help    Show this help message"
  echo ""
  echo "Environment variables:"
  echo "  QEMU                  QEMU binary (default: qemu-system-i386)"
  echo "  QEMU_AUDIO_BACKEND    Audio backend: none, pa, alsa (default: none)"
  echo "  QEMU_EXTRA_ARGS       Extra arguments passed to QEMU"
  echo ""
  echo "Examples:"
  echo "  $0 mellivora.iso"
  echo "  QEMU_AUDIO_BACKEND=pa $0 mellivora.iso"
  exit 0
}

# ── Parse arguments ──────────────────────────────────────────────────
for arg in "$@"; do
  case "$arg" in
    -h|--help) show_help ;;
  esac
done

ISO="${1:-mellivora.iso}"

show_banner

if [[ ! -f "$ISO" ]]; then
  die "ISO file not found: $ISO"
fi

QEMU="${QEMU:-qemu-system-i386}"
if ! command -v "$QEMU" >/dev/null 2>&1; then
  die "$QEMU not found — install QEMU or set QEMU= to the correct binary"
fi

# ── Extract disk image from ISO ──────────────────────────────────────
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

IMG="$WORK/mellivora.img"

if command -v xorriso >/dev/null 2>&1; then
  info "Extracting disk image ${DIM}(xorriso)${RST} ..."
  xorriso -osirrox on -indev "$ISO" -extract /boot/mellivora.img "$IMG" 2>/dev/null
elif command -v bsdtar >/dev/null 2>&1; then
  info "Extracting disk image ${DIM}(bsdtar)${RST} ..."
  bsdtar -xf "$ISO" -C "$WORK" boot/mellivora.img
  mv "$WORK/boot/mellivora.img" "$IMG"
elif command -v 7z >/dev/null 2>&1; then
  info "Extracting disk image ${DIM}(7z)${RST} ..."
  7z x -o"$WORK" "$ISO" boot/mellivora.img >/dev/null
  mv "$WORK/boot/mellivora.img" "$IMG"
else
  die "Need xorriso, bsdtar, or 7z to extract the ISO"
fi

[[ -f "$IMG" ]] || die "Failed to extract boot/mellivora.img from ISO"

IMG_SIZE=$(wc -c < "$IMG")
IMG_SIZE_HR=$(numfmt --to=iec-i --suffix=B "$IMG_SIZE" 2>/dev/null || echo "${IMG_SIZE} bytes")
ok "Extracted ${BLD}mellivora.img${RST} ($IMG_SIZE_HR)"

# ── QEMU configuration ──────────────────────────────────────────────
AUDIO="${QEMU_AUDIO_BACKEND:-none}"
EXTRA="${QEMU_EXTRA_ARGS:-}"

echo ""
info "QEMU:    ${BLD}$QEMU${RST}"
info "CPU:     ${BLD}i486${RST}  RAM: ${BLD}128 MB${RST}"
info "Boot:    ${BLD}CD-ROM${RST} (El Torito) + ${BLD}IDE disk${RST} (HBFS)"
info "Audio:   ${BLD}$AUDIO${RST}"
[[ -n "$EXTRA" ]] && info "Extra:   $EXTRA"
echo ""

ok "Launching Mellivora OS ..."
echo ""

# shellcheck disable=SC2086
exec "$QEMU" \
  -cpu 486 \
  -m 128 \
  -cdrom "$ISO" \
  -drive "file=$IMG,format=raw,if=ide,cache=writethrough" \
  -boot d \
  -no-shutdown \
  -audiodev "$AUDIO",id=snd0 \
  -machine pcspk-audiodev=snd0,usb=off \
  -netdev user,id=net0 \
  -device rtl8139,netdev=net0 \
  $EXTRA
