#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <staging-dir> <output.iso>" >&2
  exit 1
fi

ROOT_DIR="$1"
OUTPUT_FILE="$2"
VOLUME_NAME="${ISO_VOLUME_NAME:-MELLIVORA}"

if [[ ! -d "$ROOT_DIR" ]]; then
  echo "error: staging directory not found: $ROOT_DIR" >&2
  exit 1
fi

ROOT_DIR="$(cd "$ROOT_DIR" && pwd)"
mkdir -p "$(dirname "$OUTPUT_FILE")"
OUTPUT_FILE="$(cd "$(dirname "$OUTPUT_FILE")" && pwd)/$(basename "$OUTPUT_FILE")"
rm -f "$OUTPUT_FILE"

if command -v xorriso >/dev/null 2>&1; then
  xorriso -as mkisofs \
    -R -J -V "$VOLUME_NAME" \
    -b boot/mellivora.img -hard-disk-boot \
    -o "$OUTPUT_FILE" "$ROOT_DIR"
elif command -v genisoimage >/dev/null 2>&1; then
  genisoimage \
    -R -J -V "$VOLUME_NAME" \
    -b boot/mellivora.img -hard-disk-boot \
    -o "$OUTPUT_FILE" "$ROOT_DIR"
elif command -v mkisofs >/dev/null 2>&1; then
  mkisofs \
    -R -J -V "$VOLUME_NAME" \
    -b boot/mellivora.img -hard-disk-boot \
    -o "$OUTPUT_FILE" "$ROOT_DIR"
elif command -v hdiutil >/dev/null 2>&1; then
  (
    cd "$ROOT_DIR"
    hdiutil makehybrid \
      -ov \
      -o "$OUTPUT_FILE" \
      . \
      -iso -joliet \
      -default-volume-name "$VOLUME_NAME" \
      -eltorito-boot boot/mellivora.img \
      -hard-disk-boot >/dev/null
  )
  if [[ ! -f "$OUTPUT_FILE" && -f "${OUTPUT_FILE}.cdr" ]]; then
    mv "${OUTPUT_FILE}.cdr" "$OUTPUT_FILE"
  fi
else
  echo "error: no ISO creation tool found (need xorriso, genisoimage, mkisofs, or hdiutil)" >&2
  exit 1
fi

echo "Created bootable ISO: $OUTPUT_FILE"
