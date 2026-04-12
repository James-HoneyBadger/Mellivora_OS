# Mellivora OS

![Release](https://img.shields.io/github/v/release/James-HoneyBadger/Mellivora_OS?display_name=tag) ![License](https://img.shields.io/github/license/James-HoneyBadger/Mellivora_OS) ![Platform](https://img.shields.io/badge/platform-i486%2B%20%7C%20QEMU-blue) ![Language](https://img.shields.io/badge/language-NASM%20x86-informational) ![Tests](https://img.shields.io/badge/tests-709%2F709%20pass-brightgreen)

**A bare-metal 32-bit x86 operating system written entirely in NASM assembly.**

Mellivora OS is a from-scratch hobby operating system that boots on real i486+ hardware or in QEMU. It features a three-stage bootloader, a custom HBFS filesystem, preemptive multitasking with ring 0/3 privilege separation, virtual memory with paging, a full TCP/IP networking stack with RTL8139 driver, a windowed desktop environment, an in-OS C compiler, 79 bundled assembly programs, and 11 C samples — all written in x86 assembly.

> **v2.1.0** — Full TCP/IP networking stack with DHCP, DNS, and working HTTP.

> New to the project? Start with the [Installation Guide](docs/INSTALL.md), then try the [Tutorial](docs/TUTORIAL.md) or explore the [User Guide](docs/USER_GUIDE.md).

---

## 🦡 At a Glance

| | |
|---|---|
| **Boot** | 3-stage BIOS boot → 32-bit protected mode with paging |
| **Shell** | 45+ built-in commands, pipes, redirection, scripting, tab completion |
| **Programs** | 79 assembly programs + 11 C samples (games, tools, interpreters) |
| **Networking** | Full TCP/IP stack — Ethernet, ARP, IP, ICMP, UDP, TCP, DHCP, DNS |
| **Desktop** | Burrows windowed GUI — 640×480×32, mouse, themes, 6 GUI apps |
| **Filesystem** | HBFS — 4 KB blocks, subdirectories, wildcards, 8 open file descriptors |
| **Languages** | TCC (C compiler), BASIC, FORTH, Brainfuck — all running inside the OS |
| **Syscalls** | 48 system calls via `INT 0x80` (files, network, GUI, memory, process) |
| **Tests** | 709 regression tests (build checks + HBFS integrity) |

---

## ✨ Features

### Kernel & Architecture

- **32-bit protected mode** with flat 4 GB address space and identity-mapped paging (128 MB)
- **Ring 0 / Ring 3** privilege separation — user programs run in unprivileged mode
- **48 syscalls** via `INT 0x80`: file I/O, networking, GUI, memory allocation, process control, serial, date/time
- **Preemptive multitasking** — round-robin scheduler, 100 ms quantum, up to 4 tasks, per-task kernel stacks
- **Physical memory manager** — bitmap allocator with `malloc`/`free` for user programs
- **Page fault handler** with graceful recovery to shell prompt
- **ELF32 loader** — supports flat binaries and ELF executables
- **Three-stage boot**: MBR → Stage 2 (A20, E820 memory map, GDT, protected mode) → Kernel

### TCP/IP Networking

- **RTL8139 NIC driver** — PCI auto-detect, software reset, interrupt-driven RX/TX
- **Full protocol stack**: Ethernet II → ARP → IPv4 → ICMP / UDP / TCP
- **DHCP client** — automatic IP configuration (discover → offer → request → ack)
- **DNS resolver** — hostname-to-IP resolution via UDP queries
- **TCP state machine** — complete handshake (SYN/SYN-ACK/ACK), data transfer, graceful close
- **Socket API** — 10 syscalls: socket, connect, send, recv, bind, listen, accept, dns, close, ping
- **5 shell commands**: `net`, `dhcp`, `ping`, `arp`, `ifconfig`
- **7 network programs**: HTTP client, Telnet, FTP, Gopher, Ping, Mail, News

### Burrows Desktop Environment

- **640×480×32 graphics** via Bochs VBE/BGA framebuffer with double buffering
- **Window manager** — up to 16 draggable windows with title bars and close buttons
- **Taskbar** with application launcher menu and clock
- **Mouse support** — PS/2 IRQ12 driver with cursor tracking
- **3 built-in themes** — Dark, Light, Classic
- **GUI syscall API** — 12 sub-functions for windows, drawing, events, and compositing
- **6 GUI applications**: terminal, text editor, file manager, calculator, paint, system monitor

### HB Lair Shell

- **45+ built-in commands** with aliases: file management, text processing, system info, networking
- **Tab completion**, **command history** (Up/Down), **Ctrl+C** hard-abort with cleanup
- **Pipes and redirection** — `|`, `>`, `>>`, `<`, `&&`, `||`
- **Environment variables** with `$VAR` expansion
- **Batch scripting** — `.bat` files with `:LABEL`/`goto`, `if [not] errorlevel`, `rem`, `@cmd`
- **PATH-based program search** — run programs from any directory
- **Wildcard expansion** — `*` and `?` globbing for all commands

### HBFS Filesystem

- **Honey Badger File System** — custom filesystem with 4 KB blocks
- **Hierarchical directories** — up to 16 levels deep
- **File descriptors** — open/read/write/close/seek (8 simultaneous FDs)
- **Wildcards** — `*` and `?` pattern matching with global expansion
- **File types**: text, executable, directory, batch script

### Drivers

| Driver | Details |
|--------|---------|
| **VGA** | Text mode 80×25, 16 colors, hardware cursor |
| **VBE/BGA** | 640×480×32 linear framebuffer for Burrows desktop |
| **PS/2 Keyboard** | Full key map with shift, ctrl, special keys (IRQ1) |
| **PS/2 Mouse** | Position tracking, 3-button support (IRQ12) |
| **ATA PIO** | LBA48 addressing with retry logic |
| **RTL8139** | PCI Ethernet NIC with interrupt-driven RX/TX |
| **PIT** | Programmable interval timer at 100 Hz |
| **PC Speaker** | Square wave generation for sound/music |
| **Serial** | COM1 at 115200 baud for debug output |
| **RTC** | Real-time clock for date/time via CMOS |

---

## 🎮 Included Programs (79 Assembly + 11 C)

### Games (14)

| Program | Description |
|---------|-------------|
| `snake` | Classic snake — eat food, grow, avoid walls and tail |
| `tetris` | Tetris with 7 tetrominoes, rotation, scoring, and levels |
| `mine` | Minesweeper with flag and reveal mechanics |
| `sokoban` | Box-pushing puzzle game with multiple levels |
| `2048` | Sliding tile number game |
| `galaga` | Space shooter with enemy waves |
| `chess` | Two-player chess with move validation and check detection |
| `rogue` | ASCII roguelike dungeon crawler with FOV and inventory |
| `kingdom` | Medieval kingdom management simulation |
| `life` | Conway's Game of Life (78×23 grid) |
| `maze` | Random maze generator with BFS solver |
| `neurovault` | Pattern memory game |
| `outbreak` | Zombie survival strategy game |
| `guess` | Number guessing game with hints |

### Internet Clients (7)

| Program | Description |
|---------|-------------|
| `http` | HTTP client — fetch web pages (tested: example.com) |
| `ping` | ICMP echo request with RTT display |
| `telnet` | Interactive Telnet client |
| `ftp` | FTP client with passive mode (ls, cd, get, put) |
| `gopher` | Gopher protocol browser |
| `mail` | SMTP mail client |
| `news` | NNTP news reader |

### Text Processing (13)

| Program | Description |
|---------|-------------|
| `grep` | Pattern search in files |
| `sed` | Stream editor (search and replace) |
| `sort` | Sort lines (with `-r` reverse, `-n` numeric) |
| `tr` | Character translation |
| `cut` | Extract columns from text |
| `paste` | Join lines side-by-side |
| `head` | Show first N lines |
| `tail` | Show last N lines |
| `wc` | Count lines, words, and bytes |
| `uniq` | Remove adjacent duplicates (with `-c`, `-d`) |
| `rev` | Reverse each line |
| `diff` | Line-by-line file comparison |
| `od` | Octal/hex binary viewer |

### Language Interpreters (4)

| Program | Description |
|---------|-------------|
| `tcc` | Tiny C Compiler — compiles C subset to native code inside the OS |
| `basic` | BASIC interpreter (PRINT, INPUT, LET, IF/THEN, GOTO, FOR/NEXT) |
| `forth` | FORTH interpreter with stack operations and word definitions |
| `asm` | Interactive x86 assembler REPL (~25 instruction types) |

### System Utilities (13)

| Program | Description |
|---------|-------------|
| `edit` | Full-screen text editor with save/load |
| `top` | Live process/task monitor with CPU and memory display |
| `hexdump` | Hex/ASCII file viewer |
| `pager` | File pager with scrolling (like `more`) |
| `csv` | CSV file viewer with formatted columns |
| `find` | Find files matching a pattern |
| `basename` | Extract filename from path |
| `dirname` | Extract directory from path |
| `sysinfo` | System information display |
| `serial` | Serial port testing utility |
| `apitest` | Syscall API testing tool |
| `id` | Display user/group ID info |
| `whoami` | Show current user |

### Demos & Tools (14)

| Program | Description |
|---------|-------------|
| `mandel` | Mandelbrot set renderer (fixed-point arithmetic) |
| `starfield` | Animated 3D starfield with parallax depth |
| `matrix` | Matrix-style falling character rain |
| `piano` | PC speaker musical keyboard (15 notes) |
| `clock` | Analog ASCII clock with trigonometric hands |
| `cal` | Calendar with current day highlighted |
| `calc` | Interactive calculator (+, −, ×, ÷, %) |
| `banner` | ASCII art banner printer |
| `colors` | VGA color palette demo |
| `weather` | Simulated weather station display |
| `periodic` | Interactive periodic table browser |
| `primes` | Prime number generator |
| `fibonacci` | Fibonacci sequence |
| `seq` | Sequence number generator |

### Scripting Utilities (7)

| Program | Description |
|---------|-------------|
| `sleep` | Sleep for N seconds |
| `true` | Exit with status 0 |
| `false` | Exit with status 1 |
| `yes` | Print "y" continuously |
| `tee` | Split output to file and stdout |
| `uptime` | Show system uptime |
| `burrow` | Desktop environment launcher |

### Burrows GUI Applications (6)

| Program | Description |
|---------|-------------|
| `bterm` | GUI terminal emulator |
| `bedit` | GUI text editor |
| `bfiles` | GUI file manager |
| `bcalc` | GUI calculator |
| `bpaint` | GUI paint application |
| `bsysmon` | GUI system monitor |

### C Samples (11)

Compiled by the in-OS TCC compiler from `/samples/`:

| Sample | Description |
|--------|-------------|
| `hello.c` | Hello World |
| `fib.c` | Fibonacci number generator |
| `primes.c` | Prime number finder |
| `calc.c` | Expression calculator |
| `hanoi.c` | Tower of Hanoi solver |
| `echo.c` | Echo arguments |
| `boxes.c` | Box drawing demo |
| `matrix.c` | Matrix animation |
| `stars.c` | Starfield demo |
| `wumpus.c` | Hunt the Wumpus text adventure |
| `bf.c` | Brainfuck interpreter |

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
make full      # Build everything: boot + kernel + 79 programs + filesystem
make run       # Launch in QEMU
```

The HB Lair shell appears:

```text
Lair:/>
```

### Try It Out

```bash
Lair:/> help                       # List all commands
Lair:/> dir                        # List files and directories
Lair:/> snake                      # Play Snake
Lair:/> tcc /samples/hello.c       # Compile and run a C program
Lair:/> dhcp                       # Get an IP via DHCP
Lair:/> http example.com           # Fetch a web page
Lair:/> gui                        # Launch the Burrows desktop
```

---

## 📁 Project Structure

### Source Tree

```text
Mellivora_OS/
├── boot.asm                MBR boot sector (512 bytes, 16-bit real mode)
├── stage2.asm              Stage 2 loader (A20, E820, GDT, protected mode switch)
├── kernel.asm              Kernel entry point + subsystem includes
├── kernel/                 Kernel subsystems (19 modules, ~20,000 lines)
│   ├── shell.inc           HB Lair shell (45+ commands, 5000 lines)
│   ├── net.inc             TCP/IP networking stack (3800 lines)
│   ├── util.inc            Utilities, ELF loader, env vars (3300 lines)
│   ├── burrows.inc         Burrows desktop environment (2600 lines)
│   ├── hbfs.inc            HBFS filesystem driver
│   ├── data.inc            BSS data, string constants, scan tables
│   ├── syscall.inc         INT 0x80 dispatcher (48 syscalls)
│   ├── vbe.inc             Bochs VBE/BGA framebuffer driver
│   ├── ata.inc             ATA PIO disk driver (LBA48)
│   ├── paging.inc          Virtual memory (identity-mapped 128 MB)
│   ├── sched.inc           Preemptive round-robin scheduler
│   ├── mouse.inc           PS/2 mouse driver (IRQ12)
│   ├── isr.inc             Interrupt service routines
│   ├── idt.inc             Interrupt descriptor table
│   ├── pit.inc             PIT timer + keyboard driver
│   ├── pic.inc             PIC initialization
│   ├── pmm.inc             Physical memory manager (bitmap)
│   ├── vga.inc             VGA text mode driver
│   └── filesearch.inc      Global file search across directories
├── programs/               User-space programs (79 .asm files)
│   ├── syscalls.inc        Syscall numbers and macros
│   └── lib/                Reusable libraries (8 modules)
│       ├── string.inc      String manipulation (30+ functions)
│       ├── io.inc          Console I/O and file operations (25+ functions)
│       ├── math.inc        Number parsing and arithmetic (15+ functions)
│       ├── vga.inc         VGA text mode helpers
│       ├── mem.inc         Heap allocation (malloc/free, arena, pool)
│       ├── data.inc        Data structures (stack, queue, bitmap)
│       ├── net.inc         Socket API wrappers
│       └── gui.inc         Burrows GUI API wrappers
├── samples/                C source files for in-OS TCC (11 files)
├── tests/                  Regression test suite
│   ├── test_build.sh       Build-time validation (175 checks)
│   └── test_hbfs.py        HBFS filesystem integrity (534 checks)
├── docs/                   Documentation (7 guides)
├── Makefile                Build system
├── populate.py             HBFS image populator
├── Containerfile           OCI container image
├── CHANGELOG.md            Version history
└── README.md               This file
```

### On-Disk Layout (Virtual Drive)

```text
/
├── bin/          65 utility and tool programs
├── games/        14 games
├── samples/      11 C source files
├── docs/          5 text files (readme, license, notes, todo, poem)
└── script.bat    Example batch script
```

Programs in `/bin` and `/games` are in the default `PATH`, so they run from any directory.

---

## 📖 Documentation

| Document | Description |
|----------|-------------|
| **[Installation Guide](docs/INSTALL.md)** | Prerequisites, building, QEMU configuration, real hardware |
| **[User Guide](docs/USER_GUIDE.md)** | Complete shell command reference, pipes, scripting, networking |
| **[Tutorial](docs/TUTORIAL.md)** | Step-by-step walkthrough from boot to writing your first program |
| **[Programming Guide](docs/PROGRAMMING_GUIDE.md)** | Writing assembly programs: syscalls, libraries, GUI, networking |
| **[API Reference](docs/API_REFERENCE.md)** | Complete syscall and library function reference |
| **[Technical Reference](docs/TECHNICAL_REFERENCE.md)** | OS internals: boot, memory, filesystem, drivers, scheduler |
| **[Networking Guide](docs/NETWORKING_GUIDE.md)** | TCP/IP stack architecture, socket API, protocol details |
| **[Changelog](CHANGELOG.md)** | Full version history from v1.0 to v2.1.0 |

---

## 🔧 Build Targets

| Command | Description |
|---------|-------------|
| `make full` | Complete build: boot + kernel + 79 programs + filesystem population |
| `make run` | Launch in QEMU (i486 CPU, 128 MB RAM, audio, networking) |
| `make debug` | Launch with QEMU monitor on stdio for debugging |
| `make iso` | Create bootable ISO image with documentation |
| `make check` | Run the full regression suite (709 tests) |
| `make clean` | Remove all build artifacts |
| `make sizes` | Show component binary sizes |

---

## 🖥️ System Requirements

### Emulation (Recommended)

- QEMU 6.0+ with `qemu-system-i386`
- Any host OS — Linux, macOS, Windows (with WSL)

### Real Hardware

- i486 or newer x86 CPU
- 1 MB RAM minimum (128 MB recommended)
- IDE/SATA hard disk or USB drive (BIOS legacy boot)
- VGA-compatible display
- PS/2 keyboard
- RTL8139-compatible Ethernet card (for networking)

---

## 📊 Project Statistics

| Metric | Value |
|--------|-------|
| Kernel source | 20,000 lines across 19 modules |
| Kernel binary | ~399 KB |
| Program source | 42,000 lines across 79 programs |
| User libraries | 4,200 lines across 8 modules |
| Syscalls | 48 (via `INT 0x80`) |
| Shell commands | 45+ built-in |
| Programs | 79 assembly + 11 C samples |
| Disk image | 2 GB with HBFS filesystem |
| Files on disk | 96 files across 4 subdirectories |
| Test coverage | 709 regression and integrity checks |
| Networking protocols | Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS |
| GUI windows | Up to 16 simultaneous |

---

## 📜 License

This project is licensed under the [MIT License](LICENSE).

Copyright (c) 2026 Honey Badger Universe

---

## 🦡 Why "Mellivora"?

*Mellivora capensis* — the honey badger. Small, tough, and fearless. Just like this OS.

### Component Naming

| Component | Name | Full Name |
|-----------|------|-----------|
| Kernel | **Mellivora** | Mellivora OS kernel |
| Init System | **Ratel** | Hardware & subsystem initialization |
| Shell | **HB Lair** | Honey Badger Lair interactive shell |
| Filesystem | **HBFS** | Honey Badger File System |
| Utilities | **HBU** | Honey Badger Utilities (Unix-like tools) |
| Desktop | **Burrows** | Burrows Desktop Environment |
| Network Stack | **ClawNet** | TCP/IP networking subsystem |
