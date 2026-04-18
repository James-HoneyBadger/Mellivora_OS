# Mellivora OS

![Release](https://img.shields.io/github/v/release/James-HoneyBadger/Mellivora_OS?display_name=tag) ![License](https://img.shields.io/github/license/James-HoneyBadger/Mellivora_OS) ![Platform](https://img.shields.io/badge/platform-x86%20%7C%20QEMU-blue) ![Language](https://img.shields.io/badge/language-NASM%20x86-informational)

**A bare-metal 32-bit x86 operating system written in NASM assembly.**

Mellivora OS is a from-scratch hobby OS that boots on real x86 hardware or in QEMU. It includes a custom HBFS filesystem, ring 3 user-mode execution, a DOS-inspired interactive shell with POSIX features, 80 syscalls, priority-based preemptive scheduling, signal support, an in-OS Tiny C Compiler, 56+ bundled assembly programs, and 11 C samples.

> New to the project? Start with the [Installation Guide](docs/INSTALL.md), then try the [Tutorial](docs/TUTORIAL.md) or browse the [Technical Reference](docs/TECHNICAL_REFERENCE.md).

## 🦡 At a Glance

- **Boot path:** 3-stage BIOS boot flow into 32-bit protected mode
- **Userland:** 50+ shell commands, 56 assembly programs, and 11 bundled C samples
- **Core pieces:** HBFS filesystem, ELF32 loader, PMM allocator, serial/VGA/ATA drivers
- **Developer-ready:** API docs, programming guide, regression tests, and release packaging

---

## ✨ Features

### Kernel & Architecture

- **32-bit protected mode** with flat memory model
- **Ring 0 / Ring 3** privilege separation — programs run in user mode
- **80 syscalls** via `INT 0x80` (POSIX-inspired: open, read, write, close, seek, stat, mkdir, signals, priorities, ...)
- **Priority-based preemptive scheduler** — 4 priority levels (HIGH/NORMAL/LOW/IDLE), 64 concurrent tasks
- **POSIX-style signals** — SIGINT, SIGKILL, SIGTERM, SIGTSTP, SIGCONT, SIGUSR1/2, SIGALRM, SIGCHLD
- **Process groups** — PGID support for job control
- **ELF32 loader** — supports flat binaries and ELF executables
- **Physical memory manager** with bitmap allocator (malloc/free/realloc for user programs)
- **Three-stage boot**: MBR → Stage 2 (A20, memory map, protected mode) → Kernel

### Ratel Init System

- **Sequential hardware initialization** — VGA, PIC, IDT, PIT, keyboard, PMM, ATA, serial, TSS
- **Filesystem mount** — HBFS detection, validation, and auto-format
- **Shell handoff** — drops into HB Lair interactive prompt after init completes

### HB Lair Shell (v3.0)

- **50+ built-in shell commands** with aliases: file management, text processing, system info, process control
- **Tab completion**, **command history** (128 entries), **Ctrl+C** hard-abort with proper cleanup
- **Enhanced line editing** — Ctrl+A/E (home/end), Ctrl+U (kill line), Ctrl+W (delete word), Ctrl+L (clear+redraw)
- **Process management** — `ps`, `jobs`, `kill`, `bg`, `fg`, `nice` for task control
- **Pipes, redirection, and chaining** — `|`, `>`, `>>`, `<`, `&&`, and `||` for shell workflows
- **Alias system** — define custom command shortcuts
- **32 environment variables** with `$VAR` expansion in echo and batch scripts
- **Batch scripting** — execute `.bat` files with sequential command processing
- **`source` / `.`** — execute scripts in current shell context
- **PATH-based program search** — run programs from any directory
- **Full path support** — `cat /docs/readme`, `run /bin/hello`, `diff /docs/a /docs/b`
- **Multi-level subdirectories** — up to 16 levels deep with `cd`, `mkdir`, `pwd`

### HBFS Filesystem

- **Honey Badger File System** — custom filesystem with 4 KB blocks
- **455 entries** per root directory, **224 entries** per subdirectory
- **File types**: text, executable, directory, batch script
- **File descriptors**: open/read/write/close/seek (8 simultaneous FDs)
- **Wildcards**: `*` and `?` pattern matching in `del` and `copy`

### Drivers

- **VGA** text mode (80×25, 16 colors)
- **PS/2 keyboard** with shift, ctrl, and special key support
- **ATA PIO** disk with LBA48 addressing
- **PIT timer** at 100 Hz
- **PC speaker** for sound/music
- **Serial port** (COM1 at 115200 baud) for debug output
- **RTC** real-time clock for date/time

### Programs (56 assembly + 11 C samples)

- **Games**: Snake, Tetris, Minesweeper, Sokoban, 2048, Galaga, Game of Life, Maze, Kingdom, Outbreak, Neurovault
- **HBU (Honey Badger Utilities)**: grep, sort, sed, tr, wc, cut, head, tail, diff, find, uniq, rev, paste, and more
- **Tools**: Text editor, hex viewer, file pager, CSV viewer
- **Demos**: Mandelbrot renderer, piano, banner, colors, calendar, calculator
- **Languages**: TCC (Tiny C Compiler), BASIC interpreter, Brainfuck interpreter
- **API Libraries**: 6 reusable `.inc` libraries (string, I/O, math, VGA, memory, data structures)
- **C samples**: Hello World, Fibonacci, primes, Tower of Hanoi, Hunt the Wumpus, and more

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
```

---

## 📁 Directory Structure

### On-Disk (Virtual Drive)

```text
/
├── bin/          43 utility programs (hello, edit, grep, sort, tcc, ...)
├── games/        13 games (snake, tetris, 2048, galaga, mine, ...)
├── samples/      11 C source files (hello.c, fib.c, wumpus.c, ...)
├── docs/          5 text files (readme, license, notes, todo, poem)
└── script.bat     Example batch script
```

Programs in `/bin` and `/games` are in the default PATH, so they run from any directory.

### Source Tree

```text
Mellivora_OS/
├── boot.asm               Stage 1 MBR boot sector (512 bytes, 16-bit)
├── stage2.asm              Stage 2 loader (A20, E820, long mode switch)
├── kernel.asm              Kernel entry + modular includes (13 files in `kernel/`)
├── Makefile                Build system (make full / make run / make debug)
├── populate.py             HBFS image populator with subdirectory support
├── CHANGELOG.md            Version history (v1.0 → v1.15)
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
│   └── ...                 (56 programs total)
├── samples/                C source files for TCC
│   ├── hello.c, fib.c, primes.c, calc.c, matrix.c
│   ├── hanoi.c, bf.c, wumpus.c, boxes.c, stars.c, echo.c
│   └── ...                 (11 samples total)
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
| `sokoban` | Box-pushing puzzle game with multiple levels |
| `2048` | Sliding tile number game |
| `galaga` | Space shooter with enemy waves |
| `guess` | Number guessing game with hints |
| `kingdom` | Medieval kingdom management simulation |
| `life` | Conway's Game of Life (78×23 grid) |
| `maze` | Random maze generator with BFS solver |
| `neurovault` | Sci-fi dungeon crawler RPG |
| `outbreak` | Zombie survival strategy game |
| `piano` | PC speaker piano with 15 notes |

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
| `basic` | BASIC language interpreter (interactive & file mode) |
| `bf` | Brainfuck interpreter |

### API Libraries (`programs/lib/`)

| Library | Functions | Description |
| --------- | --------- | ------------- |
| `string.inc` | 30+ | String manipulation, comparison, search, memory ops |
| `io.inc` | 20+ | Console I/O, file operations, argument parsing |
| `math.inc` | 10+ | Number parsing/formatting, arithmetic |
| `vga.inc` | 15+ | VGA text mode, cursor, color, UI drawing |
| `mem.inc` | 10+ | Heap allocation, pool/arena allocators |
| `data.inc` | 10+ | Stacks, queues, bitmaps, dynamic arrays |

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
| Kernel source | Compact entry file + 13 modular include files |
| Kernel binary | ~238 KB |
| Syscalls | 36 (via `INT 0x80`) |
| Shell experience | 50+ commands, aliases, history, completion, batch files |
| User programs | 56 assembly apps + 11 bundled C samples |
| API libraries | 6 reusable `.inc` modules (95+ functions) |
| Disk image | 64 MB HBFS image |
| Files on disk | 73 files across 4 subdirectories |
| Test coverage | 580+ regression and filesystem checks |

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
