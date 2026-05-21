# Mellivora OS

![Release](https://img.shields.io/github/v/release/James-HoneyBadger/Mellivora_OS?display_name=tag) ![License](https://img.shields.io/github/license/James-HoneyBadger/Mellivora_OS) ![Platform](https://img.shields.io/badge/platform-x86%20%7C%20QEMU-blue) ![Language](https://img.shields.io/badge/language-NASM%20x86-informational)

**A bare-metal 32-bit x86 operating system written in NASM assembly.**

Mellivora OS is a from-scratch hobby OS that boots on real x86 hardware or in QEMU. It includes a custom HBFS v3 filesystem with permissions and xattr, ring 3 user-mode execution, a DOS-inspired interactive shell with POSIX features, 182 syscalls, priority-based preemptive scheduling with per-task fd isolation, POSIX signals with a W^X trampoline, a VFS abstraction layer, AHCI SATA and Intel e1000 NIC drivers, a GDB remote stub,
compositor surface IPC, an in-OS Tiny C Compiler, a package manager, 231 assembly programs, and 31 bundled samples (C, Perl, and BASIC).

> New to the project? Start with the [Installation Guide](docs/INSTALL.md), then try the [Tutorial](docs/TUTORIAL.md) or browse the [Technical Reference](docs/TECHNICAL_REFERENCE.md).

## 🦡 At a Glance

- **Boot path:** BIOS 3-stage boot or UEFI (gnu-efi PE32+) into 32-bit protected mode; optional x86-64 long-mode path
- **Userland:** 90+ shell commands, 231 assembly programs, and 31 bundled samples (C, Perl, and BASIC)
- **Core pieces:** HBFS v3 filesystem + VFS layer, ELF32 loader, buddy PMM allocator, serial/VGA/ATA drivers
- **Developer-ready:** API docs, programming guide, regression tests, release packaging, and `hbpkg` package manager

---

## ✨ Features

### Kernel & Architecture

- **32-bit protected mode** with flat memory model; optional **x86-64 long-mode** path (`make 64bit`)
- **UEFI boot** — `boot/uefi_loader.efi` (gnu-efi PE32+) for UEFI firmware; `make uefi` / `make run-uefi`
- **Ring 0 / Ring 3** privilege separation — programs run in user mode
- **182 syscalls** via `INT 0x80` — v10 adds dup/pipe2/mmap/mprotect/clock_gettime/surfaces; v11 adds sigaction/sigreturn/alarm/xattr; v12.0 adds full POSIX session/uid/signal/timer/termios set; v12.1 adds per-task errno (`SYS_GETERRNO`)
- **Virtual filesystem (VFS)** — unified open/read/write/stat/readdir front-end; backends: HBFS (`/`), procfs (`/proc`), devfs (`/dev`), tmpfs (`/tmp`)
- **Priority-based preemptive scheduler** — 4 priority levels (HIGH/NORMAL/LOW/IDLE), 128 concurrent ring-3 tasks; blocking `waitpid`
- **Per-task fd isolation** (v12.1) — each task has a private 4 KB fd table page; no cross-task aliasing
- **POSIX-style signals** — SIGINT, SIGKILL, SIGTERM, SIGTSTP, SIGCONT, SIGUSR1/2, SIGALRM, SIGCHLD; W^X sigreturn trampoline at 0x1FFFF000
- **Per-task errno** (v12.1) — `TCB_ERRNO` in the TCB; read via `SYS_GETERRNO`
- **Process groups** — PGID and SID support for job control
- **ELF32 loader** — supports flat binaries and ELF executables
- **Physical memory manager** with buddy allocator (order 0–11; malloc/free/realloc for user programs)
- **VBE/BGA graphics driver** — high-resolution framebuffer modes (640×480, 800×600, 1024×768 at 32 bpp) with double buffering
- **Compositor surface IPC** — kernel-managed pixel buffers with z-order and dirty-rect compositing (SYS_SURFACE_CREATE/COMMIT/DESTROY/MOVE/RESIZE)
- **Three-stage BIOS boot**: MBR → Stage 2 (A20, memory map, protected mode) → Kernel

### Ratel Init System

- **Sequential hardware initialization** — VGA, PIC, IDT, PIT, keyboard, PMM, ATA, serial, TSS, scheduler, IPC, networking, e1000 NIC, paging, mouse, SB16, VBE, PCI, AC'97, ATA DMA, VirtIO, AHCI SATA, GDB stub
- **Filesystem mount** — HBFS detection, validation, and auto-format
- **Shell handoff** — drops into HB Lair interactive prompt after init completes

### HB Lair Shell (v3.0)

- **90+ built-in shell commands** with aliases: file management, text processing, system info, process control
- **Tab completion**, **command history** (128 entries), **Ctrl+C** hard-abort with proper cleanup
- **Enhanced line editing** — Ctrl+A/E (home/end), Ctrl+U (kill line), Ctrl+W (delete word), Ctrl+L (clear+redraw)
- **Process management** — `ps`, `jobs`, `kill`, `bg`, `fg`, `nice` for task control
- **Pipes, redirection, and chaining** — `|`, `>`, `>>`, `<`, `&&`, and `||` for shell workflows
- **Alias system** — define custom command shortcuts
- **32 environment variables** with `$VAR` expansion and `$(cmd)` command substitution, `$((expr))` arithmetic expansion
- **Batch scripting** — execute `.bat` files with sequential command processing
- **`source` / `.`** — execute scripts in current shell context
- **PATH-based program search** — run programs from any directory
- **Full path support** — `cat /docs/readme`, `run /bin/hello`, `diff /docs/a /docs/b`
- **Multi-level subdirectories** — up to 16 levels deep with `cd`, `mkdir`, `pwd`

### HBFS Filesystem (v3)

- **Honey Badger File System v3** — custom filesystem with 4 KB blocks and write-ahead journal
- **455 entries** per root directory, **224 entries** per subdirectory (288-byte entries, 253-char max filename)
- **File types**: text, executable, directory, batch script
- **File permissions** — Unix-style 12-bit mode (`HBFS_DE_MODE`); owner/group/other rwx enforced by `hbfs_check_permission`
- **Extended attributes** — per-file xattr block; `hbfs_xattr_get` / `hbfs_xattr_set` kernel API
- **Feature flags** — superblock `HBFS_SB_FEATURES`: journal, xattr, permissions
- **VFS integration** — accessed via the VFS layer; raw `fd_open` / `fd_read` / `fd_write` still available
- **File descriptors**: open/read/write/close/seek (8 legacy FDs; 64 VFS FDs)
- **Wildcards**: `*` and `?` pattern matching in `del` and `copy`

### Drivers

- **VGA** text mode (80×25, 16 colors)
- **PS/2 keyboard** with shift, ctrl, and special key support
- **PS/2 mouse** — 3-byte packet, IRQ12, cursor tracking
- **ATA PIO** disk with LBA48 addressing
- **ATA Bus Master DMA** — Intel PIIX3/4 IDE controller, PRD-based DMA reads
- **AHCI SATA** (v11) — polling-mode SATA driver; auto-detected via PCI; falls back to ATA PIO
- **PIT timer** at 100 Hz
- **PC speaker** for sound/music
- **Sound Blaster 16** ISA DMA PCM playback
- **AC'97 audio** — Intel ICH2/3/4 PCI DMA playback
- **Serial port** (COM1 at 115200 baud) for debug output and GDB remote stub
- **Intel e1000 NIC** (v11) — 82540EM, 16-entry TX ring / 32-entry RX ring; Ctrl+Alt+G for GDB breakpoint
- **RTC** real-time clock for date/time
- **PCI bus** — enumeration (buses 0–7), 64-entry device table
- **VirtIO** PCI legacy — virtio-blk (block) and virtio-net (network)
- **GDB remote stub** (v11) — RSP protocol over COM1; supports registers, memory, continue, step, breakpoints

### Programs (231 assembly + 31 bundled samples)

- **Games (31)**: Snake, Tetris, Minesweeper, Galaga, Pac-Man, Game of Life, Maze, Kingdom, Outbreak, Neurovault, Blackjack, Rogue, Solitaire, Breakout, Raycaster, Robot Town, and more
- **HBU (Honey Badger Utilities)**: grep, sort, sed, awk, tr, wc, cut, head, tail, diff, find, uniq, rev, paste, xargs, tar, nm, and more
- **Tools**: Text editor, hex viewer, file pager (`more`/`pager`), CSV viewer, dual-pane file manager (burrow), `top` process monitor
- **Demos**: Mandelbrot/Julia renderers, plasma effect, VBE sprite blit, rotating cube, banner, colors, Doom fire effect
- **Languages**: TCC (Tiny C Compiler), BASIC interpreter (`basic` + `basicc` compiler), Brainfuck interpreter, Perl interpreter, Forth interpreter
- **Network tools**: ping, wget, nc, ftp, telnet, irc, gopher, dig, traceroute, whois, daytime
- **Daily-driver suite (new)**: `tutorial`, `pkginfo`, `meminfo`, `journal`, `bcal`, `theme`, `tag`, `histgrep`, `bnotify`, `mkprog`, `dnslook`, `play`, `nim`, `plasma`, `tldr`, `todo`, `pomodoro`, `morse`, `wiki`, `color`, `stopwatch`, `countdown`, `passgen`, `dice`, `coin`, `tip`, `roll`, `pick`, `reverse`, `upper`, `lower`, `countc`
- **API Libraries**: 17 reusable `.inc` libraries in `programs/lib/` (string, I/O, math, VGA, memory, data, net, GUI, VBE, font, audio, highscore, and more)
- **Samples**: 17 C programs + 9 Perl scripts + 5 BASIC scripts in `/samples`

---

## 🚀 Quick Start

### Prerequisites

```bash
# Debian/Ubuntu
sudo apt install nasm qemu-system-x86 make python3

# Fedora
sudo dnf install nasm qemu-system-x86 make python3

# Arch Linux
sudo pacman -S nasm qemu-full make python

# macOS
brew install nasm qemu make python3
```

### Build & Run

```bash
git clone https://github.com/James-HoneyBadger/Mellivora_OS.git
cd Mellivora_OS
make full      # Build everything
make run       # Launch in QEMU
```

That's it. You'll see the HB Lair boot banner and a shell prompt:

```text
Lair:/>
```

Type `help` to see all available commands, or just start exploring:

```text
Lair:/> dir                    # List files and directories
Lair:/> cd games               # Enter the games directory
Lair:/> snake                  # Play Snake!
Lair:/> cd /                   # Back to root
Lair:/> cat /docs/readme       # Read documentation
Lair:/> tetris                 # Play Tetris (found via PATH)
Lair:/> tcc /samples/hello.c   # Compile and run a C program
Lair:/> perl /samples/hello.pl # Run a Perl script
```

---

## 📁 Directory Structure

### On-Disk (Virtual Drive)

```text
/
├── bin/          190 utility programs (edit, grep, sort, tcc, wget, nc, ...)
├── games/         29 games (snake, tetris, galaga, pacman, rogue, robotown, ...)
├── samples/       31 source files (hello.c, fib.c, hello.pl, fizzbuzz.pl, hello.bas, ...)
├── docs/           text files (readme.txt, license.txt, notes.txt, ...)
└── script.bat      Example batch script
```

Programs in `/bin` and `/games` are in the default PATH, so they run from any directory.

### Source Tree

```text
Mellivora_OS/
├── boot.asm               Stage 1 MBR boot sector (512 bytes, 16-bit)
├── stage2.asm              Stage 2 loader (A20, E820, long mode switch)
├── kernel.asm              Kernel entry + modular includes (22 files in `kernel/`)
├── Makefile                Build system (make full / make run / make debug)
├── populate.py             HBFS image populator with subdirectory support
├── CHANGELOG.md            Version history (v1.0 → v12.4.0)
├── README.md               This file
├── programs/               User-space assembly programs
│   ├── syscalls.inc        Shared syscall constants and helpers
│   ├── lib/                Reusable API libraries (string, io, math, vga, mem, data)
│   ├── hello.asm           Hello World
│   ├── edit.asm            Full-screen text editor
│   ├── snake.asm           Snake game
│   ├── tetris.asm          Tetris with rotation, scoring, levels
│   ├── galaga.asm          Space shooter
│   ├── tcc.asm             Tiny C Compiler (subset)
│   ├── grep.asm            Pattern search
│   ├── sort.asm            Line sorting
│   └── ...                 (231 programs total)
├── samples/                C, Perl, and BASIC source files
│   ├── hello.c, fib.c, primes.c, calc.c, matrix.c, hanoi.c
│   ├── bf.c, wumpus.c, boxes.c, stars.c, echo.c
│   ├── hello.pl, factorial.pl, fizzbuzz.pl, guess.pl, strings.pl, arrays.pl
│   ├── hello.bas, fib.bas, blackjack.bas, mandelbrot.bas, snake.bas
│   └── ...                 (31 samples total — 17 C + 9 Perl + 5 BASIC)
├── tests/                  Regression test suite
│   ├── test_build.sh       Build-time checks
│   └── test_hbfs.py        HBFS filesystem integrity checks
└── docs/                   Documentation
    ├── API_REFERENCE.md     Library API reference
    ├── INSTALL.md           Build & installation guide
    ├── USER_GUIDE.md        Shell commands & usage manual
    ├── PROGRAMMING_GUIDE.md Writing programs for Mellivora
    ├── TECHNICAL_REFERENCE.md  OS internals & architecture
    └── TUTORIAL.md          Step-by-step beginner tutorial
```

---

## 📖 Documentation

| Document | Description |
| ---------- | ------------- |
| [Installation Guide](docs/INSTALL.md) | Prerequisites, building, QEMU, real hardware |
| [User Guide](docs/USER_GUIDE.md) | Complete shell command reference and usage |
| [Programming Guide](docs/PROGRAMMING_GUIDE.md) | Writing assembly programs with syscalls |
| [Technical Reference](docs/TECHNICAL_REFERENCE.md) | Architecture, memory map, HBFS, drivers |
| [Tutorial](docs/TUTORIAL.md) | Step-by-step beginner walkthrough |
| [API Reference](docs/API_REFERENCE.md) | Library functions and calling conventions |
| [Changelog](CHANGELOG.md) | Version history and release notes |

---

## 🎮 Included Programs

### Games

| Program | Description |
| --------- | ------------- |
| `snake` | Classic snake — eat food, grow, avoid walls and tail |
| `tetris` | Tetris with 7 tetrominoes, rotation, scoring, levels |
| `mine` | Minesweeper with flag and reveal mechanics |
| `galaga` | Space shooter with enemy waves |
| `blackjack` | Blackjack (21) card game |
| `rogue` | ASCII dungeon crawler |
| `adventure` | Text adventure (interactive fiction) |
| `connect4` | Connect Four |
| `mastermind` | Mastermind code-breaking game |
| `hangman` | Hangman word game |
| `tictactoe` | Tic-tac-toe |
| `simon` | Simon says memory game |
| `guess` | Number guessing game with hints |
| `kingdom` | Medieval kingdom management simulation |
| `life` | Conway's Game of Life (78×23 grid) |
| `maze` | Random maze generator with BFS solver |
| `neurovault` | Sci-fi dungeon crawler RPG |
| `outbreak` | Zombie survival strategy game |
| `doomfire` | Doom fire effect demo |
| `matrix` | Matrix rain effect |
| `rain` | Rainfall animation |
| `starfield` | Starfield fly-through |
| `lunar` | Lunar lander game |
| `solitaire` | Klondike solitaire card game |
| `pacman` | Pac-Man-style 21×21 maze chase — eat dots and power pellets, hunt or flee 4 ghosts |
| `iago` | Othello / Reversi — VBE board with greedy-AI opponent and persistent wins |
| `raycaster` | Wolfenstein 3D-style raycaster — fixed-point 16.16 math, WASD movement |
| `robotown` | Robot Town — logic-puzzle adventure inspired by Robot Odyssey |
| `breakout` | Breakout/Arkanoid clone — 5×10 bricks, three lives, LEFT/RIGHT paddle |

> **29 games total** in `/games` — run any from anywhere thanks to PATH.

### Utilities

| Program | Description |
| --------- | ------------- |
| `edit` | Full-screen text editor with save/load |
| `burrow` | Dual-pane file manager TUI (Midnight Commander-style) |
| `tcc` | Tiny C Compiler — compile C to ELF inside the OS |
| `grep` | Pattern search in files |
| `sort` | Sort lines alphabetically |
| `hexdump` | Hex/ASCII file viewer |
| `sed` | Stream editor (search and replace) |
| `tr` | Character translator |
| `csv` | CSV file viewer with formatted columns |
| `wc` | Line, word, and byte counter |
| `pager` | File pager (like `more`) |
| `cal` | Calendar with current day highlighted |
| `calc` | Interactive calculator (+, -, ×, ÷, %) |
| `mandel` | Mandelbrot set renderer (fixed-point) |
| `mandelbrot` | Full-color Mandelbrot at 640×480×32 bpp, rainbow escape-time palette |
| `julia` | Interactive Julia set renderer — arrow keys move *c*, +/- zoom |
| `more` | Paging text viewer — SPACE=next page, ENTER=line, Q=quit |
| `top` | Real-time process monitor with memory bar, refreshes every second |
| `tar` | HBTAR1.0 flat archive — create, extract, list (up to 64 files) |
| `nm` | ELF32 symbol table reader — address, type, and name |
| `awk` | Pattern/action processor — `$n`, `NR`, `NF`, `print`, `gsub`, `/regex/`, `BEGIN`/`END` |
| `basic` | GW-BASIC-style interpreter with strings, loops, DATA/READ, and file mode |
| `bf` | Brainfuck interpreter |

### API Libraries (`programs/lib/` and `programs/`)

| Library | Functions | Description |
| --------- | --------- | ------------- |
| `string.inc` | 30+ | String manipulation, comparison, search, memory ops |
| `io.inc` | 20+ | Console I/O, file operations, argument parsing |
| `math.inc` | 10+ | Number parsing/formatting, arithmetic |
| `vga.inc` | 15+ | VGA text mode, cursor, color, UI drawing |
| `mem.inc` | 10+ | Heap allocation, pool/arena allocators |
| `data.inc` | 10+ | Stacks, queues, bitmaps, dynamic arrays |
| `net.inc` | 10+ | TCP/UDP sockets, DNS, ICMP ping |
| `gui.inc` | 10+ | Burrows desktop GUI wrappers |
| `sprite.inc` | 4 | VBE sprite drawing: alpha, opaque, color-key, scaled |

---

## 🔧 Build Targets

| Command | Description |
| --------- | ------------- |
| `make full` | Complete build: boot + kernel + programs + filesystem |
| `make run` | Launch in QEMU (i486-compatible x86, 128 MB RAM) |
| `make debug` | Launch with QEMU monitor on stdio |
| `make iso` | Create a bootable installer/live ISO with docs included |
| `make check` | Run the regression suite and HBFS integrity checks |
| `make clean` | Remove all build artifacts |
| `make sizes` | Show component sizes |

---

## 🖥️ System Requirements

### Emulation (Recommended)

- QEMU 6.0+ with `qemu-system-i386` (or `qemu-system-x86_64` in compatibility mode)
- Any modern host OS (Linux, macOS, Windows with WSL)

### Real Hardware

- i486-or-newer x86 CPU with BIOS legacy boot support
- 1 MB RAM minimum (128 MB recommended)
- IDE/SATA disk or USB drive (BIOS legacy boot)
- VGA-compatible display
- PS/2 keyboard

---

## 📊 Stats

| Metric | Value |
| -------- | ------- |
| Kernel source | Entry file + 26 modular include files |
| Syscalls | 182 (via `INT 0x80`) |
| Shell commands | 90+ built-ins, aliases, history (128 entries), tab completion |
| User programs | 231 assembly apps |
| Bundled samples | 31 (17 C + 9 Perl + 5 BAS) in `/samples` |
| API libraries | 17 reusable `.inc` modules in `programs/lib/` |
| Disk image | 2 GB raw HBFS image |
| HBFS root capacity | 455 files; 224 files per subdirectory |
| Concurrent tasks | 128 (preemptive scheduler, 4 priority levels) |

---

## 📜 License

This project is licensed under the [MIT License](LICENSE).

Copyright (c) 2026 Honey Badger Universe

---

## 🦡 Why "Mellivora"?

*Mellivora capensis* — the honey badger. Small, tough, and fearless. Just like this OS.

### Component Naming

| Component | Name | Full Name |
| --- | --- | --- |
| Kernel | **Mellivora** | Mellivora OS kernel |
| Init System | **Ratel** | Hardware & subsystem initialization |
| Shell | **HB Lair** | Honey Badger Lair |
| Filesystem | **HBFS** | Honey Badger File System |
| Utilities | **HBU** | Honey Badger Utilities (GNU-like tools) |
