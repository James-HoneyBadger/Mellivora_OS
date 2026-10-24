#!/usr/bin/env bash
#
# build_iso.sh - build the Mellivora OS installer ISO
#
# The ISO is a real installer: its El Torito boot image is a 2 KB real-mode
# stub (install/boot/stage1.asm) that loads the installer payload
# (install/boot/installer.asm) from the CD, finds MELLIVORA.IMG in the ISO9660
# tree, and writes it to whichever BIOS hard disk the user picks.  The
# installed disk is a plain Mellivora disk - it boots on its own afterwards.
#
# Usage:
#   build_iso.sh [DISK_IMAGE] [OUTPUT_ISO]
#   build_iso.sh --image DISK_IMAGE --out OUTPUT_ISO [--no-verify]
#
# Defaults: mellivora-install.img and mellivora.iso in the repository root.
#
# Requirements: nasm, xorriso (from libisoburn or cdrtools)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ -t 1 ]]; then
  RED=$'\033[1;31m'  GRN=$'\033[1;32m'  CYN=$'\033[1;36m'
  YEL=$'\033[1;33m'  BLD=$'\033[1m'     RST=$'\033[0m'
else
  RED='' GRN='' CYN='' YEL='' BLD='' RST=''
fi

info() { echo "${CYN}::${RST} $*"; }
ok()   { echo "${GRN} ✓${RST} $*"; }
warn() { echo "${YEL} ⚠${RST} $*" >&2; }
die()  { echo "${RED} ✗${RST} $*" >&2; exit 1; }

IMAGE="$ROOT/mellivora-install.img"
OUTPUT="$ROOT/mellivora.iso"
VERIFY=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    --image)     IMAGE="$2"; shift 2 ;;
    --out)       OUTPUT="$2"; shift 2 ;;
    --no-verify) VERIFY=0; shift ;;
    -*)          die "unknown option: $1" ;;
    *)
      if [[ "$IMAGE" == "$ROOT/mellivora-install.img" ]]; then IMAGE="$1"; else OUTPUT="$1"; fi
      shift ;;
  esac
done

command -v nasm    >/dev/null 2>&1 || die "nasm not found"
command -v xorriso >/dev/null 2>&1 || die "xorriso not found (install libisoburn or cdrtools)"

[[ -f "$IMAGE" ]] || die "disk image not found: $IMAGE (run 'make iso-image' first)"

IMAGE_BYTES=$(wc -c < "$IMAGE")
[[ "$IMAGE_BYTES" -ge $((8 * 1024 * 1024)) ]] \
  || die "$IMAGE is only $IMAGE_BYTES bytes - that is not a disk image"

# The payload has to start with a bootable MBR or the installer would happily
# write a disk that cannot boot.
SIG=$(od -An -tx1 -j510 -N2 "$IMAGE" | tr -d ' \n')
[[ "$SIG" == "55aa" ]] || die "$IMAGE has no 0x55AA boot signature at offset 510"

# The installer copies in 32 KiB chunks and never splits one, so the payload has
# to be a whole number of chunks.
(( IMAGE_BYTES % 32768 == 0 )) || die "$IMAGE is not a multiple of 32 KiB"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ── Assemble the boot image ------------------------------------------
info "Assembling the El Torito boot image"
nasm -f bin -o "$WORK/stage1.bin"    "$ROOT/install/boot/stage1.asm"
nasm -f bin -o "$WORK/installer.bin" "$ROOT/install/boot/installer.asm"

STAGE1_BYTES=$(wc -c < "$WORK/stage1.bin")
PAYLOAD_BYTES=$(wc -c < "$WORK/installer.bin")

# -boot-load-size 4 loads 4 x 512 bytes, so stage1 has to fit in 2048.
[[ "$STAGE1_BYTES" -le 2048 ]] || die "stage1.bin is $STAGE1_BYTES bytes, the BIOS only loads 2048"
[[ "$PAYLOAD_BYTES" -le $((64 * 1024)) ]] || die "installer.bin is $PAYLOAD_BYTES bytes, stage1 loads 64 KiB"

mkdir -p "$WORK/tree/boot"
# The payload follows the stub inside the same boot image, padded to the
# 64 KiB that stage1 reads from the CD.
truncate -s $((64 * 1024)) "$WORK/installer.bin"
cat "$WORK/stage1.bin" "$WORK/installer.bin" > "$WORK/tree/boot/installer.bin"
ok "boot image: ${STAGE1_BYTES} byte stub + ${PAYLOAD_BYTES} byte payload"

# ── Stage the ISO tree ------------------------------------------------
info "Staging the ISO tree"
cp "$IMAGE" "$WORK/tree/MELLIVORA.IMG"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$WORK/tree/" 2>/dev/null || true
[[ -f "$ROOT/CHANGELOG.md" ]] && cp "$ROOT/CHANGELOG.md" "$WORK/tree/"
if [[ -d "$ROOT/docs" ]]; then
  mkdir -p "$WORK/tree/docs"
  cp "$ROOT/docs"/*.md "$WORK/tree/docs/" 2>/dev/null || true
fi
[[ -f "$ROOT/install/docs/ISO_README.txt" ]] && cp "$ROOT/install/docs/ISO_README.txt" "$WORK/tree/README.txt"

# ── Build the ISO -----------------------------------------------------
info "Building $OUTPUT"
rm -f "$OUTPUT"
# -iso-level 3 keeps full file names in the ISO9660 identifiers; the installer
# also understands Rock Ridge names, so the image is found either way.
xorriso -as mkisofs \
  -R -J -iso-level 3 \
  -V MELLIVORA \
  -A "Mellivora OS installer" \
  -b boot/installer.bin -no-emul-boot -boot-load-size 4 \
  -o "$OUTPUT" "$WORK/tree" >/dev/null 2>&1

[[ -f "$OUTPUT" ]] || die "xorriso did not produce $OUTPUT"

# ── Verify ------------------------------------------------------------
if [[ "$VERIFY" -eq 1 ]]; then
  info "Verifying the ISO"
  TORITO=$(xorriso -indev "$OUTPUT" -report_el_torito plain 2>&1)
  grep -q "El Torito" <<<"$TORITO" || die "no El Torito boot record in $OUTPUT"
  grep -q "El Torito boot img" <<<"$TORITO" || die "El Torito record has no boot image"
  grep -q "BIOS  y" <<<"$TORITO" || die "El Torito boot image is not a BIOS bootable entry"

  LISTING=$(xorriso -indev "$OUTPUT" -find / 2>&1)
  grep -q "/MELLIVORA.IMG" <<<"$LISTING" || die "MELLIVORA.IMG missing from $OUTPUT"
  grep -q "/boot/installer.bin" <<<"$LISTING" || die "boot image file missing from $OUTPUT"

  STAGED=$(mktemp)
  xorriso -osirrox on -indev "$OUTPUT" -extract /MELLIVORA.IMG "$STAGED" 2>/dev/null
  STAGED_BYTES=$(wc -c < "$STAGED")
  rm -f "$STAGED"
  [[ "$STAGED_BYTES" -eq "$IMAGE_BYTES" ]] \
    || die "MELLIVORA.IMG is $STAGED_BYTES bytes inside the ISO, expected $IMAGE_BYTES"
  ok "El Torito boot record, boot image and $(numfmt --to=iec-i --suffix=B "$IMAGE_BYTES") payload verified"
fi

ISO_BYTES=$(wc -c < "$OUTPUT")
echo
ok "ISO ready: ${BLD}${OUTPUT}${RST}"
info "size:   $(numfmt --to=iec-i --suffix=B "$ISO_BYTES")"
info "sha256: $(sha256sum "$OUTPUT" | cut -d' ' -f1)"
info "try:    install/tools/run_iso.sh --install   (needs QEMU)"
