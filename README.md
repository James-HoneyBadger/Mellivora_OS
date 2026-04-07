# 🦡 Mellivora OS

**A bare-metal 32-bit operating system written entirely in x86 assembly language.**

Mellivora OS is a from-scratch operating system that boots on real i486+ hardware (or QEMU). It features a custom filesystem, a DOS-style interactive shell, 34 syscalls, a C compiler, and over 30 user-space programs — all in ~9,600 lines of NASM assembly.

---

## ✨ Features

### Kernel & Architecture

- **32-bit protected mode** with flat 4 GB address space
- **Ring 0 / Ring 3** privilege separation — programs run in user mode
- **34 syscalls** via `INT 0x80` (POSIX-inspired: open, read, write, close, seek, stat, mkdir, ...)
- **ELF32 loader** — supports flat binaries and ELF executables
- **Physical memory manager** with bitmap allocator (malloc/free for user programs)
- **Three-stage boot**: MBR → Stage 2 (A20, memory map, protected mode) → Kernel

### HB DOS Shell

- **48+ built-in commands**: file management, text processing, system info, and more
- **Tab completion**, **command history** (Up/Down arrows), **Ctrl+C** hard-abort
- **Alias system** — define custom command shortcuts
- **Environment variables** with `$VAR` expansion in echo and batch scripts
- **Batch scripting** — execute `.bat` files with sequential command processing
- **PATH-based program search** — run programs from any directory
- **Full path support** — `cat /docs/readme`, `run /bin/hello`, `diff /docs/a /docs/b`
- **Multi-level subdirectories** — up to 16 levels deep with `cd`, `mkdir`, `pwd`

### HBFS Filesystem

- **Honey Badger File System** — custom filesystem with 4 KB blocks
- **227 files** per root directory, **56 files** per subdirectory
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

### Programs (31 assembly + 11 C samples)

- **Games**: Snake, Tetris, Minesweeper, Sokoban, 2048, Galaga, Game of Life, Maze
- **Tools**: Text editor, hex viewer, file pager, grep, sort, sed, tr, CSV viewer, wc
- **Demos**: Mandelbrot renderer, piano, banner, colors, calendar, calculator
- **Languages**: TCC (Tiny C Compiler), BASIC interpreter, Brainfuck interpreter
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

That's it. You'll see the HB DOS boot banner and a shell prompt:

```text
HBDOS:/>
```

Type `help` to see all available commands, or just start exploring:

```text
HBDOS:/> dir                    # List files and directories
HBDOS:/> cd games               # Enter the games directory
HBDOS:/> snake                  # Play Snake!
HBDOS:/> cd /                   # Back to root
HBDOS:/> cat /docs/readme       # Read documentation
HBDOS:/> tetris                 # Play Tetris (found via PATH)
```

---

## 📁 Directory Structure

### On-Disk (Virtual Drive)

```text
/
├── bin/          22 utility programs (hello, edit, grep, sort, tcc, ...)
├── games/        10 games (snake, tetris, 2048, galaga, mine, ...)
├── samples/      11 C source files (hello.c, fib.c, wumpus.c, ...)
├── docs/          5 text files (readme, license, notes, todo, poem)
└── script.bat     Example batch script
```

Programs in `/bin` and `/games` are in the default PATH, so they run from any directory.

### Source Tree

```text
Mellivora_OS/
├── boot.asm               Stage 1 MBR boot sector (512 bytes, 16-bit)
├── stage2.asm              Stage 2 loader (A20, E820, protected mode switch)
├── kernel.asm              Kernel — all drivers, shell, FS, syscalls (~9,600 lines)
├── Makefile                Build system (make full / make run / make debug)
├── populate.py             HBFS image populator with subdirectory support
├── CHANGELOG.md            Version history (v1.0 → v1.7)
├── README.md               This file
├── programs/               User-space assembly programs
│   ├── syscalls.inc        Shared syscall constants and helpers
│   ├── hello.asm           Hello World
│   ├── edit.asm            Full-screen text editor
│   ├── snake.asm           Snake game
│   ├── tetris.asm          Tetris with rotation, scoring, levels
│   ├── galaga.asm          Space shooter
│   ├── tcc.asm             Tiny C Compiler (subset)
│   ├── grep.asm            Pattern search
│   ├── sort.asm            Line sorting
│   └── ...                 (31 programs total)
├── samples/                C source files for TCC
│   ├── hello.c, fib.c, primes.c, calc.c, matrix.c
│   ├── hanoi.c, bf.c, wumpus.c, boxes.c, stars.c, echo.c
│   └── ...                 (11 samples total)
└── docs/                   Documentation
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
| `life` | Conway's Game of Life (78×23 grid) |
| `maze` | Random maze generator with BFS solver |
| `piano` | PC speaker piano with 15 notes |

### Utilities

| Program | Description |
| --------- | ------------- |
| `edit` | Full-screen text editor with save/load |
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

---

## 🔧 Build Targets

| Command | Description |
| --------- | ------------- |
| `make full` | Complete build: boot + kernel + programs + filesystem |
| `make run` | Launch in QEMU (i486, 128 MB RAM) |
| `make debug` | Launch with QEMU monitor on stdio |
| `make clean` | Remove all build artifacts |
| `make sizes` | Show component sizes |

---

## 🖥️ System Requirements

### Emulation (Recommended)

- QEMU 6.0+ with `qemu-system-i386`
- Any modern host OS (Linux, macOS, Windows with WSL)

### Real Hardware

- i486 or newer x86 CPU
- 1 MB RAM minimum (128 MB recommended)
- IDE/SATA disk or USB drive (BIOS legacy boot)
- VGA-compatible display
- PS/2 keyboard

---

## 📊 Stats

| Metric | Value |
| -------- | ------- |
| Kernel source | ~9,600 lines of NASM assembly |
| Kernel binary | ~165 KB |
| Syscalls | 34 (via INT 0x80) |
| Shell commands | 48+ (38 unique + aliases) |
| User programs | 31 assembly + 11 C samples |
| Disk image | 64 MB (HBFS formatted) |
| Files on disk | 48 files in 4 subdirectories |

---

## 📜 License

This project is provided as-is for educational purposes.

---

## 🦡 Why "Mellivora"?

*Mellivora capensis* — the honey badger. Small, tough, and fearless. Just like this OS.
