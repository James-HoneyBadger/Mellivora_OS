# Mellivora OS — Installation & Build Guide

## Overview

Mellivora OS is a bare-metal, from-scratch 32-bit protected-mode operating system written entirely in NASM
assembly language. It targets BIOS-bootable x86 hardware and runs under QEMU or on compatible real
hardware.

This guide covers everything you need to build, run, and test the OS.

---

## Prerequisites

### Required Tools

| Tool | Version | Purpose |
| ------ | --------- | --------- |
| **NASM** | 2.15+ | Netwide Assembler — assembles all `.asm` sources |
| **GNU Make** | 4.0+ | Build orchestration |
| **QEMU** | 6.0+ | `qemu-system-x86_64` — i486-compatible emulator for testing |
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

### Linux Host Architectures

Mellivora is a 32-bit x86 guest. On an x86_64 Linux host, run it with QEMU or boot it on compatible x86 hardware. On an ARM64/aarch64 Linux host, run it under QEMU's x86 system emulator; QEMU translates the guest instructions in software, so performance is typically slower. ARM hardware cannot boot Mellivora natively.

#### Install a packaged emulator

Use the distribution's x86 system-emulation package, not an ARM-only QEMU target:

```bash
# Debian/Ubuntu, including ARM64
sudo apt update
sudo apt install qemu-system-x86

# Arch Linux x86_64
sudo pacman -Syu qemu-full
```

On Arch Linux ARM, pacman may report that `qemu-system-x86` requires `seabios` or `edk2-ovmf` packages that are unavailable in the enabled repositories. A full sync does not fix dependencies absent from those repositories. Do not skip the package, force-install it with `--nodeps`, or install x86_64 host packages on ARM. The normal `make run` path is BIOS boot; it does not need OVMF. Build QEMU on the ARM host instead.

The Makefile searches for `qemu-system-x86_64`, then `qemu-system-i386`, then `~/.local/bin/qemu-system-x86_64`; it also supports the VS Code Flatpak host launcher. On ARM, host audio defaults to `none`; on other hosts the existing PulseAudio default is retained. Set `QEMU` or `QEMU_AUDIO_BACKEND` explicitly to override these choices.

#### Build QEMU from source

Install the native build dependencies for your host distribution:

```bash
# Debian/Ubuntu
sudo apt install build-essential git python3 pkg-config ninja-build \
  libglib2.0-dev libpixman-1-dev libslirp-dev

# Fedora
sudo dnf install gcc gcc-c++ git make python3 pkgconf-pkg-config ninja-build \
  glib2-devel pixman-devel libslirp-devel

# Arch Linux / Arch Linux ARM
sudo pacman -S --needed base-devel git python pkgconf glib2 pixman libslirp ninja
```

Then build only the x86 system emulator. These commands build QEMU for the current host (including ARM) while enabling x86 guests:

```bash
mkdir -p ~/src
cd ~/src
git clone https://gitlab.com/qemu-project/qemu.git
cd qemu
mkdir build
cd build
../configure --prefix="$HOME/.local" --target-list=x86_64-softmmu
make -j"$(nproc)"
make install
```

QEMU's source tree provides the default x86 BIOS firmware used by `make run`. After installation, the Makefile discovers the local binary automatically:

```bash
cd /path/to/Mellivora_OS
make run
```

If auto-detection does not find it, or you want to choose a particular backend, use:

```bash
make run QEMU="$HOME/.local/bin/qemu-system-x86_64" QEMU_AUDIO_BACKEND=none
```

`x86_64-softmmu` supports this 32-bit x86 guest. The host QEMU binary is still ARM-native; only the guest is x86-emulated.

#### Optional UEFI boot

`make run-uefi` is separate from the BIOS `make run` path. It needs an x86_64 UEFI loader toolchain and **x86_64 OVMF firmware**. On ARM, do not use AArch64 OVMF files: QEMU is emulating an x86 machine, so its firmware must also be x86. Firmware package names and paths vary by distro. Locate both code and vars files, then pass their paths:

On an ARM build host, `make uefi` also needs an x86_64 cross-compiler, x86_64 binutils, and GNU-EFI headers/libraries. Set `CC_EFI`, `LD_EFI`, `OBJCOPY`, `EFI_INCLUDE`, and `EFI_LIB` to the corresponding cross-toolchain paths if their names or locations differ. This is only for the optional UEFI loader; the regular BIOS build does not need a C cross-compiler.

```bash
find /usr/share -type f \( -iname 'OVMF_CODE*.fd' -o -iname 'OVMF_VARS*.fd' \)
make run-uefi OVMF_CODE=/path/to/x86_64/OVMF_CODE.fd \
  OVMF_VARS=/path/to/x86_64/OVMF_VARS.fd
```

If the ARM distro does not package x86_64 OVMF, use BIOS mode (`make run`) or obtain compatible x86_64 OVMF firmware separately. `make run-uefi` reports which firmware file is missing.

### Installing on macOS (Homebrew)

```bash
brew install nasm qemu make python3
```

> **Note:** On macOS, `dd` and `hdiutil` are pre-installed. You may need to use `gmake`
> instead of `make` if the system `make` is too old.

### Optional ISO-building tools on Linux

To build `mellivora.iso` on Linux, install one of the following:

```bash
# Debian/Ubuntu
sudo apt install xorriso

# Fedora
sudo dnf install xorriso

# Arch
sudo pacman -S xorriso
```

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
7. Runs `populate.py` to create subdirectories and write the current file set into HBFS (235 programs + 37 samples + docs)

### Build Targets

| Target | Command | Description |
| -------- | --------- | ------------- |
| **OS image** | `make` or `make all` | Build boot + Stage 2 + kernel, create disk image |
| **Programs** | `make programs` | Assemble all programs in `programs/` |
| **Populate** | `make populate` | Write files and programs into the disk image |
| **Full build** | `make full` | All of the above in order |
| **Bootable ISO** | `make iso` | Create `mellivora.iso` with install docs and user guide included |
| **Clean** | `make clean` | Remove all generated files (`.bin`, `.lst`, `.img`, `.iso`) |
| **Sizes** | `make sizes` | Show component sizes |
| **Run** | `make run` | Launch in QEMU |
| **Debug** | `make debug` | Launch in QEMU with monitor + debug logging |

### Build Outputs

After a successful build:

```text
mellivora.img          2 GB bootable raw disk image
mellivora.iso          Bootable ISO media with docs and install guide
boot.bin               512-byte MBR boot sector
stage2.bin             Stage 2 loader (≤16 KB)
kernel.bin             32-bit protected-mode kernel
programs/*.bin         Compiled user programs (current assembly program set)
*.lst                  Assembly listing files (useful for debugging)
```

### Creating a Bootable ISO

```bash
make iso
```

This creates `mellivora.iso`, which:

1. boots directly in BIOS/legacy-compatible VMs,
2. includes `mellivora.img` as the El Torito hard-disk boot image,
3. bundles the full `INSTALL.md` and `USER_GUIDE.md` documentation inside the ISO.

The ISO staging tree includes:

```text
boot/mellivora.img
README.txt
docs/INSTALL.md
docs/USER_GUIDE.md
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

### Booting the ISO in QEMU

```bash
make iso
qemu-system-i386 -m 128 -cdrom mellivora.iso -boot d -no-reboot -no-shutdown
```

This is the recommended way to test the distributable install media exactly as users will receive it.

This launches QEMU with:

| Setting | Value |
| --------- | ------- |
| **CPU** | i486-compatible x86 emulation |
| **RAM** | 128 MB |
| **Disk** | `mellivora.img` as raw IDE drive |
| **Boot** | Hard disk (drive C) |
| **Behavior** | No auto-reboot, no auto-shutdown |

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
qemu-system-i386 -m 128 \
  -drive file=mellivora.img,format=raw,if=ide,cache=writethrough \
  -boot c -no-reboot -no-shutdown
```

Useful additional options:

| Option | Description |
| -------- | ------------- |
| `-m 256` | Increase RAM to 256 MB |
| `-serial stdio` | Route serial output (COM1) to your terminal |
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
LBA 33+         variable    32-bit kernel (sector count generated from `kernel.bin` size)
LBA 4096        512 B       HBFS superblock
LBA 4097–4224   64 KB       Block allocation bitmap
LBA 4225–4480   128 KB      Root directory (32 blocks, 455 entries)
LBA 4481+       ~2 GB       Data blocks (4 KB each)
```

### On-Disk Directory Structure

The `populate.py` script creates 5 subdirectories and places the curated runtime file set (235 programs + 37 samples + docs):

```text
/
├── bin/           Utility programs (hello, edit, grep, sort, tcc, ...)
├── games/         Games (snake, tetris, galaga, pacman, mine, ...)
├── Burrows/       Burrows desktop applications
├── samples/       C, Perl, and BASIC source files (hello.c, fib.c, hello.pl, hello.bas, ...)
├── docs/           5 text files (readme, license, notes, todo, poem)
└── script.bat     Example batch script
```

---

## Writing to Real Hardware

> **⚠ WARNING:** Writing to a real disk will **destroy all data** on that disk.
> Only do this on a dedicated test machine or USB drive.

### Requirements

- i486-or-newer x86 CPU with BIOS legacy boot support
- IDE or SATA disk / USB drive with BIOS legacy boot
- At least 1 MB RAM (128 MB recommended)
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
3. Give the VM at least **128 MB RAM**
4. Boot from the ISO
5. For a persistent install, attach a virtual disk and write `boot/mellivora.img` from the host onto that disk

---

## Project Structure

```text
Mellivora_OS/
├── boot.asm               Stage 1 MBR boot sector (16-bit real mode)
├── stage2.asm              Stage 2 loader (A20, E820, protected mode switch)
├── kernel.asm              Kernel entry and include graph (main file + 26 include modules)
├── Makefile                Build system
├── populate.py             HBFS image populator with subdirectory support
├── CHANGELOG.md            Version history (current: v13.0.0)
├── README.md               Project overview
│
├── programs/               User-space assembly programs (~235 sources total)
│   ├── syscalls.inc        Shared constants and helpers
│   ├── hello.asm           ... through ...
│   └── wc.asm
│
├── samples/                C, Perl, and BASIC source files
│   ├── hello.c             ... through ...
│   ├── hello.pl            ...
│   └── hello.bas
│
└── docs/                   Full documentation suite
    ├── INSTALL.md           This file
    ├── USER_GUIDE.md        Shell command reference
    ├── PROGRAMMING_GUIDE.md Writing programs for Mellivora
    ├── TECHNICAL_REFERENCE.md Architecture and internals
    └── TUTORIAL.md          Beginner walkthrough
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
qemu-system-i386 -m 128 \
  -drive file=mellivora.img,format=raw,if=ide -boot c \
  -audiodev id=snd,driver=sdl -machine pcspk-audiodev=snd
```

### Kernel size and sector count

Kernel sector count is generated automatically from `kernel.bin` into `kernel_sectors.inc`.
Check current size with `ls -la kernel.bin`; Stage 2 reads the generated sector count at boot.

### Serial debug output

```bash
qemu-system-i386 -m 128 \
  -drive file=mellivora.img,format=raw,if=ide -boot c \
  -serial stdio
```
