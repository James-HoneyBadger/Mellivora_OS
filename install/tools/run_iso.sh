#!/usr/bin/env bash
#
# run_iso.sh - run the Mellivora installer ISO in QEMU
#
# Without options this boots the ISO with an empty target disk so you can
# click through the installer yourself.  With --install it drives the
# installer unattended and then checks that the installed disk boots.
#
#   ./run_iso.sh                       # interactive install, blank 512 MiB disk
#   ./run_iso.sh --install             # unattended install + boot check
#   ./run_iso.sh --disk /tmp/disk.img --size 2048
#   ./run_iso.sh --iso mellivora.iso --display gtk
#
# The target disk is created if it does not exist.  Everything on it is
# erased by the installer, so do not point it at a disk you care about.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ -t 1 ]]; then
  RED=$'\033[1;31m'  GRN=$'\033[1;32m'  CYN=$'\033[1;36m'
  YEL=$'\033[1;33m'  RST=$'\033[0m'
else
  RED='' GRN='' CYN='' YEL='' RST=''
fi
info() { echo "${CYN}::${RST} $*"; }
ok()   { echo "${GRN} ✓${RST} $*"; }
warn() { echo "${YEL} ⚠${RST} $*" >&2; }
die()  { echo "${RED} ✗${RST} $*" >&2; exit 1; }

ISO="$ROOT/mellivora.iso"
DISK="$ROOT/mellivora-install-target.img"
SIZE_MB=512
INSTALL=0
DISPLAY=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --iso)     ISO="$2"; shift 2 ;;
    --disk)    DISK="$2"; shift 2 ;;
    --size)    SIZE_MB="$2"; shift 2 ;;
    --display) DISPLAY="$2"; shift 2 ;;
    --install) INSTALL=1; shift ;;
    *)         die "unknown option: $1" ;;
  esac
done

[[ -f "$ISO" ]] || die "ISO not found: $ISO (run 'make iso')"

QEMU=""
for candidate in qemu-system-x86_64 qemu-system-i386; do
  if command -v "$candidate" >/dev/null 2>&1; then
    QEMU="$candidate"
    break
  fi
done
[[ -n "$QEMU" ]] || die "QEMU not found (install qemu-system-x86)"

if [[ ! -f "$DISK" ]]; then
  info "Creating a blank ${SIZE_MB} MiB target disk: $DISK"
  truncate -s "${SIZE_MB}M" "$DISK"
else
  warn "using the existing target disk $DISK - the installer will erase it"
fi

if [[ "$INSTALL" -eq 1 ]]; then
  if [[ -z "$DISPLAY" ]]; then
    info "Unattended install (no display); pass --display gtk to watch"
    exec python3 "$SCRIPT_DIR/autoinstall.py" "$ISO" "$DISK" --boot-check
  fi
  info "Unattended install, display=$DISPLAY"
  exec python3 "$SCRIPT_DIR/autoinstall.py" "$ISO" "$DISK" --boot-check --display "$DISPLAY"
fi

info "Booting $ISO - pick the disk in the installer, then boot $DISK with:"
info "  $QEMU $(mv_machine_flags) -drive file=$DISK,format=raw,if=ide -boot c"
echo
# The machine comes from tools/machine_profiles.py, the same one `make run` and
# the tests use.
. "$SCRIPT_DIR/../../tools/machine_profile.sh"
mv_machine_args

QEMU_ARGS=(
  "${MV_MACHINE_ARGS[@]}"
  -cdrom "$ISO"
  -drive "file=$DISK,format=raw,if=ide,cache=writeback"
  -boot d -no-shutdown
)
if [[ -n "$DISPLAY" ]]; then
  QEMU_ARGS+=(-display "$DISPLAY")
fi
# shellcheck disable=SC2086
exec "$QEMU" "${QEMU_ARGS[@]}" ${QEMU_EXTRA_ARGS:-}
