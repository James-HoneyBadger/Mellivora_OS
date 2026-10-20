#!/usr/bin/env bash
#
# test_boot.sh - QEMU boot smoke test for Mellivora OS.
#
# Boots the disk image headless, captures COM1, and asserts that
#
#   1. the shell reached its first prompt (the MELLIVORA_BOOT_OK marker that
#      shell.inc writes to COM1), and
#   2. no CPU exception was reported during boot.
#
# Both matter. The kernel's fault handlers recover by tearing down every ring-3
# task and jumping back to shell_main, so a kernel that faults during boot can
# still reach a prompt and look "fine" - only the serial log shows the fault.
#
# Usage:  tests/test_boot.sh [image]
#
# Env:
#   BOOT_TIMEOUT=60        seconds to wait for the marker (default 60)
#   BOOT_ALLOW_SKIP=1      exit 0 instead of failing when QEMU is not installed.
#                          'make check-boot' sets this so that 'make check' still
#                          works on hosts without QEMU; CI runs this script
#                          directly (strict) after asserting QEMU is present.
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${BOOT_IMAGE:-$ROOT/mellivora.img}}"
TIMEOUT="${BOOT_TIMEOUT:-60}"
MARKER="MELLIVORA_BOOT_OK"
FAULT_RE='CPU EXCEPTION|double fault|triple fault'

LOG_DIR="$ROOT/.build"
LOG="$LOG_DIR/boot.log"

fail() {
    printf '  FAIL  %s\n' "$*" >&2
    exit 1
}

dump_log() {
    if [ -s "$LOG" ]; then
        echo "  ---- COM1 log (tail) ----" >&2
        tail -n 40 "$LOG" >&2
        echo "  -------------------------" >&2
    else
        echo "  (COM1 produced no output at all)" >&2
    fi
}

# --- locate QEMU --------------------------------------------------------
# Mirrors the Makefile's detection order so this test finds the same emulator
# `make run` would use: $QEMU override, PATH, a user-local install, then a
# Flatpak-hosted binary.  A plain `command -v` misses $HOME/.local/bin, which is
# where QEMU is installed on some setups (and where the Makefile looks).
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
    if [ -x /usr/bin/flatpak-spawn ]; then
        printf '%s' '/usr/bin/flatpak-spawn --host qemu-system-x86_64'
        return 0
    fi
    return 1
}

QEMU="$(find_qemu || true)"
if [ -z "$QEMU" ]; then
    if [ "${BOOT_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  boot test: no QEMU found on this host"
        exit 0
    fi
    fail "boot test: no QEMU found. Install qemu-system-x86, or set BOOT_ALLOW_SKIP=1 to skip."
fi

# $QEMU may be a multi-word command (the Flatpak fallback), so word-split it.
read -r -a QEMU_CMD <<< "$QEMU"

[ -f "$IMAGE" ] || fail "boot test: disk image not found: $IMAGE (run 'make full' first)"

mkdir -p "$LOG_DIR"
: > "$LOG"

echo "  boot test: $(basename "${QEMU_CMD[0]}") -cpu 486 -m 128, timeout ${TIMEOUT}s"
echo "  image:     $IMAGE"

# --- boot headless ------------------------------------------------------
# Flags mirror the 'make run' target so the test exercises the same machine
# the developer runs, minus the display and with a non-interactive audio
# backend (CI has no PulseAudio).
"${QEMU_CMD[@]}" \
    -cpu 486 -m 128 \
    -drive "file=$IMAGE,format=raw,if=ide,cache=writethrough" \
    -boot c \
    -display none -monitor none \
    -serial "file:$LOG" \
    -no-reboot \
    -audiodev none,id=snd0 -machine pcspk-audiodev=snd0,usb=off \
    -device sb16,audiodev=snd0 \
    -netdev user,id=net0 -device rtl8139,netdev=net0 \
    >/dev/null 2>&1 &
QPID=$!

# Always reap QEMU, however we leave.
cleanup() { kill -9 "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null; }
trap cleanup EXIT

saw_marker=0
saw_fault=0
deadline=$(( SECONDS + TIMEOUT ))

while [ "$SECONDS" -lt "$deadline" ]; do
    if grep -qF -- "$MARKER" "$LOG" 2>/dev/null; then
        saw_marker=1
        break
    fi
    if grep -qEi -- "$FAULT_RE" "$LOG" 2>/dev/null; then
        saw_fault=1
        break
    fi
    # QEMU exiting this early means the guest reset (triple fault) or crashed.
    if ! kill -0 "$QPID" 2>/dev/null; then
        break
    fi
    sleep 0.2
done

cleanup
trap - EXIT

# --- verdict ------------------------------------------------------------
if [ "$saw_fault" = "1" ]; then
    dump_log
    fail "boot test: CPU exception during boot (matching /$FAULT_RE/)"
fi

if [ "$saw_marker" != "1" ]; then
    dump_log
    fail "boot test: '$MARKER' never appeared on COM1 within ${TIMEOUT}s - the kernel did not reach the shell prompt"
fi

echo "  PASS  boot reached the HB Lair prompt ($MARKER)"
echo "  PASS  no CPU exception reported on COM1"
exit 0
