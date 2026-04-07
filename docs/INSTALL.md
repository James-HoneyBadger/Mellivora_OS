# Mellivora OS — Installation & Build Guide

## Overview

Mellivora OS is a bare-metal, from-scratch 32-bit operating system written entirely in NASM
assembly language. It targets i486+ processors and runs under QEMU or on compatible real hardware.

This guide covers everything you need to build, run, and test the OS.

---

## Prerequisites

### Required Tools

| Tool | Version | Purpose |
|------|---------|---------|
| **NASM** | 2.15+ | Netwide Assembler — assembles all `.asm` sources |
| **GNU Make** | 4.0+ | Build orchestration |
| **QEMU** | 6.0+ | `qemu-system-i386` — i486 emulator for testing |
| **Python 3** | 3.6+ | Runs `populate.py` to populate the filesystem |
| **dd** | any | Disk image construction (standard on Linux/macOS) |

### Installing on Debian/Ubuntu

```bash
sudo apt update
sudo apt install nasm qemu-system-x86 make python3
```

### Installing on Fedora/RHEL

```bash
sudo dnf install nasm qemu-system-x86 make python3
```

### Installing on Arch Linux

```bash
sudo pacman -S nasm qemu-full make python
```

### Installing on macOS (Homebrew)

```bash
brew install nasm qemu make python3
```

> **Note:** On macOS, `dd` is pre-installed. You may need to use `gmake` instead of `make`
> if the system `make` is too old.

---

## Building Mellivora OS

### Quick Start (Full Build)

```bash
git clone <repository-url> Mellivora_OS
cd Mellivora_OS
make full
```

This single command:
1. Assembles the boot sector (`boot.asm` → `boot.bin`, 512 bytes)
2. Assembles the stage 2 loader (`stage2.asm` → `stage2.bin`, ≤16 KB)
3. Assembles the kernel (`kernel.asm` → `kernel.bin`, ≤96 KB)
4. Creates a 64 MB raw disk image (`mellivora.img`)
5. Writes boot sector, stage 2, and kernel to the image
6. Assembles all programs in `programs/` into flat binaries
7. Runs `populate.py` to write programs and text files into the HBFS filesystem

### Individual Build Targets

| Target | Command | Description |
|--------|---------|-------------|
| **OS image** | `make` or `make all` | Build boot + stage2 + kernel, create disk image |
| **Programs** | `make programs` | Assemble all programs in `programs/` |
| **Populate** | `make populate` | Write files/programs into the disk image filesystem |
| **Full build** | `make full` | All of the above in order |
| **Clean** | `make clean` | Remove all generated files (`.bin`, `.lst`, `.img`) |
| **Sizes** | `make sizes` | Show component sizes |

### Build Outputs

After a successful build, you will have:

```
mellivora.img          — 64 MB bootable raw disk image
boot.bin               — 512-byte MBR boot sector
boot.lst               — Boot sector listing file
stage2.bin             — Stage 2 loader (≤16 KB)
stage2.lst             — Stage 2 listing file
kernel.bin             — 32-bit kernel (≤96 KB)
kernel.lst             — Kernel listing file
programs/*.bin         — Compiled user programs
programs/*.lst         — Program listing files
```

### Expected Warnings

NASM will produce warnings like:
```
kernel.asm:NNNN: warning: uninitialized space declared in .text section: zeroing
```
These are **normal and harmless**. They occur because Mellivora uses flat binary format
(`-f bin`) and declares BSS variables with `resb`/`resd`/`resq` — NASM simply notes that
it's zeroing that space in the output binary.

---

## Running in QEMU

### Standard Launch

```bash
make run
```

This launches QEMU with the following settings:
- **CPU:** i486 emulation
- **RAM:** 128 MB
- **Disk:** `mellivora.img` as raw IDE drive
- **Boot:** from hard disk (drive C)
- **Behavior:** no auto-reboot, no auto-shutdown (stays open on crash for debugging)

### Debug Launch

```bash
make debug
```

Same as `make run`, but adds:
- **QEMU Monitor** on stdio (type QEMU commands in the terminal)
- **Interrupt/reset logging** (`-d int,cpu_reset`)

Useful QEMU monitor commands:
```
info registers          — Show CPU registers
info mem                — Show memory mappings
xp /16xw 0x100000      — Examine 16 dwords at kernel base
quit                    — Exit QEMU
```

### Custom QEMU Options

You can override QEMU flags by passing `QEMU_FLAGS`:

```bash
make run QEMU_FLAGS="-cpu 486 -m 256 -drive file=mellivora.img,format=raw,if=ide -boot c"
```

#### Useful options:

| Option | Description |
|--------|-------------|
| `-m 256` | Increase RAM to 256 MB |
| `-serial stdio` | Route serial output (COM1) to terminal |
| `-soundhw pcspk` | Enable PC speaker audio (older QEMU) |
| `-audiodev id=snd,driver=sdl -machine pcspk-audiodev=snd` | PC speaker (newer QEMU) |
| `-hdb other.img` | Attach a second drive |

---

## Disk Image Layout

The 64 MB raw disk image has this layout:

```
Offset (LBA)    Size        Content
──────────────────────────────────────────────────
LBA 0           512 B       Stage 1 boot sector (MBR)
LBA 1–32        16 KB       Stage 2 loader (real → protected mode)
LBA 33–224      96 KB       32-bit kernel
LBA 225         512 B       HBFS superblock
LBA 226–233     4 KB        Block allocation bitmap
LBA 234–249     8 KB        Root directory (28 entries × 288 bytes)
LBA 250+        ~63 MB      Data blocks (4 KB each)
```

---

## Filesystem Population

The `populate.py` script writes files into the HBFS filesystem on the disk image.
It is called automatically by `make populate` or `make full`.

### What Gets Written

- **Text files:** `readme.txt`, `license.txt`, `notes.txt`, `todo.txt`, `poem.txt`, `quotes.txt`
- **Programs:** All `.bin` files from `programs/` (type is set to `FTYPE_EXEC`)

### Manual Population

```bash
python3 populate.py mellivora.img programs
```

Arguments:
1. Path to the disk image
2. Directory containing compiled program `.bin` files

---

## Writing to Real Hardware

> **⚠ WARNING:** Writing to a real disk will **destroy all data** on that disk.
> Only do this if you have a dedicated test machine.

### Requirements for Real Hardware

- i486 or newer x86 processor (Pentium, Core, etc.)
- IDE/SATA hard disk or USB drive (with BIOS legacy boot support)
- BIOS set to boot from the target drive
- At least 1 MB RAM (128 MB recommended)

### Writing the Image

```bash
# Identify your target device (e.g., /dev/sdX)
lsblk

# Write the image (TRIPLE-CHECK the device name!)
sudo dd if=mellivora.img of=/dev/sdX bs=1M status=progress

# Sync to ensure all data is flushed
sync
```

### USB Boot

Most modern BIOS/UEFI systems can boot from USB in legacy/CSM mode:

1. Write `mellivora.img` to a USB drive with `dd`
2. Enter BIOS setup (usually F2, DEL, or F12 at POST)
3. Enable "Legacy Boot" or "CSM" mode
4. Set USB drive as first boot device
5. Save and reboot

> **Note:** UEFI-only systems (no CSM) will **not** boot Mellivora, as it uses a traditional
> MBR boot sector.

---

## Troubleshooting

### QEMU: "No bootable device"

- Ensure `mellivora.img` exists and is not empty: `ls -la mellivora.img`
- Rebuild: `make clean && make full`

### NASM: "error: label ... inconsistently redefined"

- This usually means a local label (`.name`) conflicts with another in the same scope.
- Check for duplicate local labels under the same global label.

### Programs don't appear in `dir`

- Run `make populate` after `make programs` — or just use `make full`.

### Ctrl+C doesn't work in a program

- Ctrl+C sends a hard abort, terminating the program and returning to the shell.
- The program does not get a chance to clean up. This is by design.

### Serial output

To see serial/debug output from the OS:

```bash
qemu-system-i386 -cpu 486 -m 128 \
  -drive file=mellivora.img,format=raw,if=ide \
  -boot c -serial stdio
```

---

## Project Structure

```
Mellivora_OS/
├── boot.asm            Stage 1 MBR boot sector (16-bit real mode)
├── stage2.asm          Stage 2 loader (real → protected mode, A20, memory map)
├── kernel.asm          32-bit kernel (all drivers, shell, FS, syscalls)
├── Makefile            Build system
├── populate.py         Filesystem population script
├── CHANGELOG.md        Version history
├── docs/               Documentation
│   ├── INSTALL.md      This file
│   ├── USER_GUIDE.md   Shell commands and usage
│   ├── TECHNICAL_REFERENCE.md  Architecture and internals
│   ├── PROGRAMMING_GUIDE.md    Writing programs for Mellivora
│   └── API_REFERENCE.md        Complete syscall reference
└── programs/           User-space programs
    ├── syscalls.inc    Shared syscall definitions (include file)
    ├── hello.asm       Hello World
    ├── banner.asm      Colorful ASCII banner
    ├── colors.asm      VGA color palette demo
    ├── fibonacci.asm   Fibonacci sequence generator
    ├── guess.asm       Number guessing game
    ├── primes.asm      Prime number calculator
    ├── sysinfo.asm     System information display
    ├── edit.asm        Full-screen text editor
    ├── snake.asm       Snake game
    ├── mine.asm        Minesweeper game
    ├── sokoban.asm     Sokoban puzzle game
    ├── tetris.asm      Tetris game
    ├── cal.asm         Calendar display
    └── calc.asm        Interactive calculator
```
