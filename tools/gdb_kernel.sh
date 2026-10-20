#!/usr/bin/env bash
#
# gdb_kernel.sh - boot the image under QEMU's GDB stub with kernel symbols.
#
# The kernel is shipped as a flat binary, so a second ELF32 build (kernel.elf)
# exists purely as a symbol table. It has no ORG, so its symbols are relative to
# the load base; GDB is told the base with add-symbol-file.
#
# Usage: tools/gdb_kernel.sh [image]
#
# The guest keeps running after GDB detaches? No - QEMU is killed on exit.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${BOOT_IMAGE:-$ROOT/mellivora.img}}"
ELF="$ROOT/kernel.elf"
LOG_DIR="$ROOT/.build"
LOG="$LOG_DIR/gdb-serial.log"

if [ ! -f "$ELF" ]; then
    echo "kernel.elf not found - run 'make symbols' first" >&2
    exit 1
fi
if [ ! -f "$IMAGE" ]; then
    echo "image not found: $IMAGE - run 'make full' first" >&2
    exit 1
fi

QEMU="${QEMU:-}"
if [ -z "$QEMU" ]; then
    for c in qemu-system-x86_64 qemu-system-i386; do
        if command -v "$c" >/dev/null 2>&1; then QEMU="$(command -v "$c")"; break; fi
    done
fi
if [ -z "$QEMU" ] && [ -x "$HOME/.local/bin/qemu-system-x86_64" ]; then
    QEMU="$HOME/.local/bin/qemu-system-x86_64"
fi
if [ -z "$QEMU" ]; then
    echo "QEMU not found (set QEMU=/path/to/qemu-system-x86_64)" >&2
    exit 1
fi

mkdir -p "$LOG_DIR"

# -S freezes the CPU at reset and -s opens the gdbstub on TCP port 1234, so the
# debugger is attached before the first instruction executes.
"$QEMU" -S -s \
    -cpu 486 -m 128 \
    -drive "file=$IMAGE,format=raw,if=ide,cache=writethrough" \
    -boot c -display none -no-reboot \
    -serial "file:$LOG" -monitor none \
    -audiodev none,id=snd0 -machine pcspk-audiodev=snd0,usb=off \
    -device sb16,audiodev=snd0 \
    -netdev user,id=net0 -device rtl8139,netdev=net0 &
QPID=$!
trap 'kill "$QPID" 2>/dev/null || true' EXIT

# Give QEMU a moment to open the gdbstub.
sleep 0.5

# QEMU x86_64 reports an x86-64 target even for a 32-bit guest. Do not force
# `set architecture i386` (GDB rejects the target description); connect in the
# native mode and use $rip/$rax/$rbx. For correct 32-bit disassembly install
# gdb-multiarch (or a qemu-system-i386 build) and select i386 there.
gdb -q \
    -ex 'set pagination off' \
    -ex 'set disassembly-flavor intel' \
    -ex 'target remote :1234' \
    -ex "add-symbol-file $ELF 0x00100000" \
    -ex 'break shell_main' \
    -ex 'continue'
