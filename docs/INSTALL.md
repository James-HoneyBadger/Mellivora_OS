# Mellivora OS — Installation & Build Guide

## Overview

Mellivora OS is a bare-metal, from-scratch 64-bit operating system written entirely in NASM
assembly language. It targets Core 2 Duo+ processors and runs under QEMU or on compatible real
hardware.

This guide covers everything you need to build, run, and test the OS.

---

## Prerequisites

### Required Tools

| Tool | Version | Purpose |
| ------ | --------- | --------- |
| **NASM** | 2.15+ | Netwide Assembler — assembles all `.asm` sources |
| **GNU Make** | 4.0+ | Build orchestration |
| **QEMU** | 6.0+ | `qemu-system-x86_64` — Core 2 Duo emulator for testing |
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

> **Note:** On macOS, `dd` and `hdiutil` are pre-installed. You may need to use `gmake`
> instead of `make` if the system `make` is too old.

### Optional ISO-building tools on Linux

To build `mellivora.iso` on Linux, install one of the following ISO-creation tools:

```bash
# Debian/Ubuntu
sudo apt install xorriso

# Fedora
sudo dnf install xorriso

# Arch
sudo pacman -S xorriso
```

To run the standalone ISO launcher (`run_iso.sh`), you also need one of
`xorriso`, `bsdtar`, or `7z` to extract the disk image from the ISO.

### Installing on Windows

Use WSL2 (Windows Subsystem for Linux) with an Ubuntu distribution, then follow the
Debian/Ubuntu instructions above. Native Windows builds are not supported.

---

## Building Mellivora OS

### Quick Start

```bash
git clone https://github.com/James-HoneyBadger/Mellivora_OS.git
cd Mellivora_OS
make full
```

This single command:

1. Assembles the boot sector (`boot.asm` → `boot.bin`, 512 bytes)
2. Assembles the Stage 2 loader (`stage2.asm` → `stage2.bin`, ≤16 KB)
3. Assembles the kernel (`kernel.asm` → `kernel.bin`)
4. Creates a 2 GB raw disk image (`mellivora.img`)
5. Writes boot sector, Stage 2, and kernel to the image
6. Assembles all user-space assembly programs in `programs/` into flat binaries
7. Runs `populate.py` to create subdirectories and write the current file set into HBFS (220 files)

### Build Targets

| Target | Command | Description |
| -------- | --------- | ------------- |
| **OS image** | `make` or `make all` | Build boot + Stage 2 + kernel, create disk image |
| **Programs** | `make programs` | Assemble all programs in `programs/` |
| **Populate** | `make populate` | Write files and programs into the disk image |
| **Full build** | `make full` | All of the above in order |
| **Bootable ISO** | `make iso` | Create `mellivora.iso` with install docs and user guide included |
| **Lite ISO** | `make iso-lite` | Create a smaller `mellivora-lite.iso` (64 MB truncated image) |
| **Verify ISO** | `make iso-verify` | Validate El Torito boot record in the ISO |
| **Clean** | `make clean` | Remove all generated files (`.bin`, `.lst`, `.img`, `.iso`) |
| **Sizes** | `make sizes` | Show component sizes (includes ISO if present) |
| **Run** | `make run` | Launch in QEMU |
| **Run ISO** | `make run-iso` | Build ISO and launch in QEMU (CD-ROM + IDE disk) |
| **Debug** | `make debug` | Launch in QEMU with monitor + debug logging |

### Build Outputs

After a successful build:

```text
mellivora.img          2 GB bootable raw disk image (HBFS filesystem)
mellivora.iso          Bootable ISO media with docs and install guide
mellivora-lite.iso     Smaller ISO with 64 MB truncated disk image
boot.bin               512-byte MBR boot sector
stage2.bin             Stage 2 loader (≤16 KB)
kernel.bin             64-bit kernel
programs/*.bin         Compiled user programs (current assembly program set)
*.lst                  Assembly listing files (useful for debugging)
```

### Creating a Bootable ISO

```bash
make iso
```

This creates `mellivora.iso`, which:

1. boots directly in BIOS/legacy-compatible VMs,
2. includes `mellivora.img` as the El Torito no-emulation boot image,
3. bundles the complete documentation set inside the ISO.

For a smaller download, use `make iso-lite` which truncates the disk image to 64 MB
(all filesystem data is preserved; QEMU extends the file on writes).

The ISO staging tree includes:

```text
boot/mellivora.img          2 GB bootable disk image (HBFS)
README.txt                  Quick-start guide
README.md                   Project README
LICENSE                     MIT license
CHANGELOG.md                Release notes
docs/INSTALL.md
docs/USER_GUIDE.md
docs/PROGRAMMING_GUIDE.md
docs/TECHNICAL_REFERENCE.md
docs/TUTORIAL.md
docs/API_REFERENCE.md
docs/NETWORKING_GUIDE.md
```

### Expected Warnings

NASM will produce warnings like:

```text
kernel.asm:NNNN: warning: uninitialized space declared in .text section: zeroing
```

These are **normal and harmless**. They occur because Mellivora uses flat binary format
(`-f bin`) and declares BSS variables with `resb`/`resd` — NASM notes that it's zeroing
that space in the output binary.

---

## Running in QEMU

### Standard Launch

```bash
make run
```

`make run` now selects a host-compatible QEMU audio backend automatically:

| Host OS | Default backend |
| ------- | --------------- |
| Linux | `none` (safe default across distros) |
| macOS | `coreaudio` |

If your local QEMU supports a different Linux backend, you can override it:

```bash
make run QEMU_AUDIO_BACKEND=alsa
# or
make run QEMU_AUDIO_BACKEND=pa
```

### Booting the ISO in QEMU

The easiest method uses the Makefile target:

```bash
make run-iso
```

Or use the standalone helper script (works without the source tree):

```bash
./Experimental/tools/run_iso.sh mellivora.iso
```

Manual QEMU launch (note: **both** the ISO and the IDE disk image are required):

```bash
qemu-system-x86_64 -cpu core2duo -m 2048 \
  -cdrom mellivora.iso \
  -drive file=mellivora.img,format=raw,if=ide,cache=writethrough \
  -boot d -no-shutdown \
  -audiodev none,id=snd0 -machine pcspk-audiodev=snd0,usb=off \
  -netdev user,id=net0 -device rtl8139,netdev=net0
```

The kernel boots from the CD-ROM via El Torito but requires an ATA hard disk
for the HBFS filesystem.  The QEMU command attaches both devices.

| Setting | Value |
| --------- | ------- |
| **CPU** | Core 2 Duo emulation |
| **RAM** | 2048 MB |
| **CD-ROM** | `mellivora.iso` (El Torito boot) |
| **IDE Disk** | `mellivora.img` as raw IDE drive (HBFS) |
| **Boot** | CD-ROM (drive D) |
| **Behavior** | No auto-shutdown |

### Debug Launch

```bash
make debug
```

Adds QEMU Monitor on stdio and interrupt/reset logging. Useful monitor commands:

| Command | Description |
| --------- | ------------- |
| `info registers` | Show all CPU registers |
| `info mem` | Show memory mappings |
| `xp /16xw 0x100000` | Examine 16 dwords at kernel base |
| `quit` | Exit QEMU |

### Custom QEMU Options

```bash
qemu-system-x86_64 -cpu core2duo -m 2048 \
  -drive file=mellivora.img,format=raw,if=ide,cache=writethrough \
  -boot c -no-reboot -no-shutdown
```

Useful additional options:

| Option | Description |
| -------- | ------------- |
| `-m 256` | Increase RAM to 256 MB |
| `-serial stdio` | Route serial output (COM1) to your terminal |
| `-nic user,model=rtl8139` | Enable networking (RTL8139 NIC, QEMU user-mode) |
| `-audiodev id=snd,driver=sdl -machine pcspk-audiodev=snd` | Enable PC speaker audio |
| `-S -s` | Start paused + enable GDB server on port 1234 |

---

## Disk Image Layout

The 2 GB raw disk image has this layout:

```text
LBA Range       Size        Content
─────────────────────────────────────────────────────────
LBA 0           512 B       Stage 1 boot sector (MBR)
LBA 1–32        16 KB       Stage 2 loader
LBA 33+         variable    64-bit kernel (sector count generated from `kernel.bin` size)
LBA 2081        512 B       HBFS superblock
LBA 2082–2209   64 KB       Block allocation bitmap (16 blocks)
LBA 2210–2465   128 KB      Root directory (32 blocks, 455 entries)
LBA 2466+       ~2 GB       Data blocks (4 KB each)
```

### On-Disk Directory Structure

The `populate.py` script creates 4 subdirectories and places the curated runtime file set (220 files):

```text
/
├── bin/           Utility programs (hello, edit, grep, sort, tcc, ...)
├── games/         Games (snake, tetris, 2048, galaga, mine, ...)
├── samples/       11 C and 6 Perl source files (hello.c, fib.c, wumpus.c, ...)
├── docs/          10 text files (readme, license, notes, todo, poem, man pages)
└── script.bat     Example batch script
```

---

## Writing to Real Hardware

> **⚠ WARNING:** Writing to a real disk will **destroy all data** on that disk.
> Only do this on a dedicated test machine or USB drive.

### Requirements

- Core 2 Duo or newer x86 CPU
- IDE or SATA disk / USB drive with BIOS legacy boot
- At least 1 MB RAM (2048 MB recommended)
- PS/2 keyboard (USB works if BIOS provides PS/2 emulation)
- VGA-compatible display

### Writing the Image

```bash
# Identify your target device
lsblk

# Write the image (TRIPLE-CHECK the device name!)
sudo dd if=mellivora.img of=/dev/sdX bs=1M status=progress
sync
```

### USB Boot

1. Write `mellivora.img` to a USB drive with `dd` **or** write `mellivora.iso` with a USB imaging tool such as balenaEtcher
2. Enter BIOS setup (usually F2, DEL, or F12)
3. Enable "Legacy Boot" or "CSM" mode
4. Set USB drive or optical media as the first boot device
5. Save and reboot

> **Note:** UEFI-only systems (no CSM) will **not** boot Mellivora — it uses a
> traditional MBR boot sector and BIOS-style boot flow.

### VirtualBox / VMware / UTM

1. Create a new **x86** VM in legacy BIOS mode
2. Attach `mellivora.iso` as the VM's optical drive
3. Give the VM at least **1024 MB RAM**
4. Boot from the ISO
5. For a persistent install, attach a virtual disk and write `boot/mellivora.img` from the host onto that disk

---

## Project Structure

```text
Mellivora_OS/
├── boot.asm               Stage 1 MBR boot sector (16-bit real mode)
├── stage2.asm              Stage 2 loader (A20, E820, long mode switch)
├── kernel.asm              Kernel entry and include graph (main file + 22 include modules)
├── Makefile                Build system
├── populate.py             HBFS image populator with subdirectory support
├── CHANGELOG.md            Version history (v1.0 → v4.0)
├── README.md               Project overview
│
├── kernel/                 Kernel subsystem modules
│   ├── ata.inc             ATA/IDE disk driver
│   ├── data.inc            Kernel data tables
│   ├── filesearch.inc      File search and PATH resolution
│   ├── hbfs.inc            HoneyBadger File System
│   ├── idt.inc             Interrupt Descriptor Table
│   ├── isr.inc             Interrupt Service Routines
│   ├── pic.inc             Programmable Interrupt Controller
│   ├── pit.inc             Programmable Interval Timer
│   ├── pmm.inc             Physical Memory Manager
│   ├── shell.inc           Shell and command interpreter
│   ├── syscall.inc         System call dispatcher
│   ├── util.inc            Kernel utility functions
│   ├── vga.inc             VGA text mode driver
│   ├── net.inc             TCP/IP networking stack
│   ├── vbe.inc             VBE/BGA framebuffer driver
│   ├── mouse.inc           PS/2 mouse driver
│   ├── sched.inc           Preemptive scheduler
│   ├── burrows.inc         Burrows desktop environment
│   ├── paging.inc          4-level paging (PML4, 2 MB pages)
│   ├── ipc.inc             Inter-process communication (pipes, shared memory, MQ)
│   ├── sb16.inc            Sound Blaster 16 audio driver
│   └── screensaver.inc     Screensaver modes
│
├── programs/               User-space assembly programs (140 programs)
│   ├── syscalls.inc        Shared constants and helpers
│   ├── lib/                Reusable libraries (string, io, math, vga, mem, data, net, gui)
│   ├── hello.asm           ... through ...
│   └── wc.asm
│
├── samples/                11 C and 6 Perl source files for TCC and the Perl interpreter
│   ├── hello.c             ... through ...
│   └── strings.pl
│
└── docs/                   Full documentation suite
    ├── INSTALL.md           This file
    ├── USER_GUIDE.md        Shell command reference
    ├── PROGRAMMING_GUIDE.md Writing programs for Mellivora
    ├── TECHNICAL_REFERENCE.md Architecture and internals
    ├── API_REFERENCE.md     Library function reference
    ├── TUTORIAL.md          Beginner walkthrough
    └── NETWORKING_GUIDE.md  Networking architecture and usage
```

---

## Troubleshooting

### QEMU: "No bootable device"

- Ensure `mellivora.img` exists and is not empty: `ls -la mellivora.img`
- Rebuild: `make clean && make full`

### NASM label oscillation errors

The kernel is built with `-O0` (optimization disabled) to prevent NASM's multi-pass
optimizer from oscillating on near/far jump encodings. If you see "label changed during
code generation", ensure `-O0` is present in the kernel build rule in the Makefile.

### Programs don't appear in `dir`

Run `make full` (not just `make`) — this includes the populate step that writes programs
to the filesystem.

### No sound from PC speaker

QEMU requires explicit audio configuration:

```bash
qemu-system-x86_64 -cpu core2duo -m 2048 \
  -drive file=mellivora.img,format=raw,if=ide -boot c \
  -audiodev id=snd,driver=sdl -machine pcspk-audiodev=snd
```

### Kernel size and sector count

Kernel sector count is generated automatically from `kernel.bin` into `kernel_sectors.inc`.
Check current size with `ls -la kernel.bin`; Stage 2 reads the generated sector count at boot.

### Serial debug output

```bash
qemu-system-x86_64 -cpu core2duo -m 2048 \
  -drive file=mellivora.img,format=raw,if=ide -boot c \
  -serial stdio
```

### Networking not working

Ensure QEMU is launched with an RTL8139 NIC. The `make run` target includes this by
default. If launching manually:

```bash
qemu-system-x86_64 -cpu core2duo -m 2048 \
  -drive file=mellivora.img,format=raw,if=ide -boot c \
  -nic user,model=rtl8139
```

Inside the OS, run `dhcp` to obtain an IP address, then `ping 10.0.2.2` to verify.
QEMU's user-mode networking provides NAT via gateway 10.0.2.2 and DNS at 10.0.2.3.

### Burrows desktop not displaying

The BGA (Bochs Graphics Adapter) must be available — this is the default for QEMU.
If the desktop shows a blank screen, ensure your QEMU version supports VBE/BGA
(all recent releases do). VirtualBox and VMware also provide compatible VBE adapters.
