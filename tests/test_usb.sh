#!/usr/bin/env bash
#
# test_usb.sh - the first M8 gate: a USB host controller, and what is on it.
#
# docs/ROADMAP.md opens M8 with the USB stack, and its exit line is "plug in a
# USB stick and copy a file, unplug it safely". This is the first step of that:
# a UHCI controller found by its PCI class, reset, given a schedule, started,
# and its root ports read - and, when a device is on one, that device is
# enumerated and configured: the port is reset, its descriptors are read, it
# is given an address, its configuration is selected, and its HID boot
# keyboard (when it is one) is switched to boot protocol, polled, and made to
# type into the shell.
#
# So the gate asserts exactly what the stack claims. Two boots, because the
# interesting difference is whether the driver notices a device:
#
#   1. the default machine, which has a PIIX3 UHCI controller and nothing
#      plugged in: the controller must be found and the root ports reported
#      empty;
#   2. the same machine with `-device usb-kbd`: the same controller must now
#      report a device on a root port, enumerate it, configure it, and turn
#      its reports into keys the shell can run.
#
# A driver that always says "connected" fails the first case. One that never
# says it fails the second. A machine without a controller at all is not
# covered here: QEMU's default machine always has one, and that case needs a
# machine option this harness does not pass yet.
#
# Usage:  tests/test_usb.sh [image]
#
# Env:
#   USB_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   USB_TIMEOUT=180    seconds allowed for a machine to reach its shell
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${USB_IMAGE:-$ROOT/mellivora.img}}"
DRIVE="$ROOT/tools/qemu_drive.py"
TIMEOUT="${USB_TIMEOUT:-180}"

fail() {
    printf '  FAIL  %s\n' "$*" >&2
    exit 1
}

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
    return 1
}

if ! find_qemu >/dev/null; then
    if [ "${USB_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  USB tests: no QEMU found on this host"
        exit 0
    fi
    fail "USB tests: no QEMU found. Install qemu-system-x86, or set USB_ALLOW_SKIP=1."
fi

[ -f "$DRIVE" ] || fail "USB tests: missing $DRIVE"
[ -f "$IMAGE" ] || fail "USB tests: disk image not found: $IMAGE (run 'make full' first)"

run_case() {
    local name="$1"; shift
    printf '  %-38s ' "$name"
    local log
    log="$(mktemp)"
    if QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "drivers" \
            --timeout "$TIMEOUT" --out "$log" "$@" >/dev/null 2>&1; then
        echo "PASS"
        rm -f "$log"
    else
        echo "FAIL  (output: $log)"
        exit 1
    fi
}

run_case "the UHCI controller is found" \
    --wait-for "usb: UHCI host controller ready"

run_case "a schedule is installed and it runs" \
    --wait-for "usb: frame list installed, controller running"

run_case "the root ports read as empty" \
    --wait-for "usb: no device on the root ports"

run_case "a plugged-in keyboard is noticed" \
    --device usb-kbd \
    --wait-for "usb: a device is plugged into a root port"

run_case "the keyboard is enumerated" \
    --device usb-kbd \
    --wait-for "usb: device enumerated"

run_case "its descriptors are read" \
    --device usb-kbd \
    --wait-for "class "

run_case "its configuration is read" \
    --device usb-kbd \
    --wait-for "usb: config "

run_case "the keyboard is configured" \
    --device usb-kbd \
    --wait-for "usb: configured"

run_case "its interrupt endpoint is found" \
    --device usb-kbd \
    --wait-for "usb: hid "

run_case "a report is read from it" \
    --device usb-kbd \
    --wait-for "usb: hid report read"

# The end-to-end claim: keys typed on the USB keyboard reach the shell. QEMU
# activates the USB keyboard when one is attached, so `--via sendkey` drives it
# (and not the PS/2 keyboard). The command is "help", and the assertion is the
# footer of its output, which only appears if the shell ran the command.
printf '  %-38s ' "it types into the shell"
log="$(mktemp)"
if QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "help" \
        --timeout "$TIMEOUT" --out "$log" --device usb-kbd --via sendkey \
        --expect "for one command" >/dev/null 2>&1; then
    echo "PASS"
    rm -f "$log"
else
    echo "FAIL  (output: $log)"
    exit 1
fi

echo "  USB: controller found and running, a device enumerated, configured, its keys read"
