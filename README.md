# Mellivora OS

![Release][release]
![License][license]
![Platform][platform]
![Language][language]
![Tests][tests]

[release]: https://img.shields.io/github/v/release/James-HoneyBadger/Mellivora_OS?display_name=tag
[license]: https://img.shields.io/github/license/James-HoneyBadger/Mellivora_OS
[platform]: https://img.shields.io/badge/platform-Core%202%20Duo%2B%20%7C%20QEMU-blue
[language]: https://img.shields.io/badge/language-NASM%20x86--64-informational
[tests]: https://img.shields.io/badge/tests-1600-brightgreen

**A bare-metal 64-bit x86-64 operating system written entirely in NASM assembly.**

Mellivora OS is a from-scratch hobby operating system that boots
on real Core 2 Duo+ hardware or in QEMU. It features a three-stage
bootloader, a custom HBFS filesystem with directory caching and
real-time timestamps, preemptive multitasking (16 tasks) with
ring 0/3 privilege separation, 4-level paging with 2 MB pages, a
full TCP/IP networking stack with RTL8139 driver, Sound Blaster 16
audio, a windowed desktop environment with screensavers, IPC
(pipes, shared memory, and message queues), an in-OS C compiler,
140 bundled assembly programs, and 17 sample scripts — all
written in x86-64 assembly.

> New to the project? Start with the
> [Installation Guide](docs/INSTALL.md),
> then try the [Tutorial](docs/TUTORIAL.md)
> or explore the [User Guide](docs/USER_GUIDE.md).

---

## 🦡 At a Glance

| | |
| --- | --- |
| **Boot** | 3-stage BIOS boot → 64-bit long mode, 4-level paging |
| **Shell** | 64 commands, pipes, redirection, globs, scripting |
| **Programs** | 140 asm programs + 17 samples (11 C, 6 Perl) |
| **Networking** | TCP/IP — ARP, IP, ICMP, UDP, TCP, DHCP, DNS |
| **Desktop** | Burrows GUI — 640×480×32, mouse, themes, screensavers |
| **Filesystem** | HBFS — 4 KB blocks, subdirs, symlinks, dir caching |
| **Audio** | Sound Blaster 16 + PC speaker |
| **Languages** | TCC, BASIC, FORTH, Perl — in-OS |
| **Syscalls** | 84 via `INT 0x80` (files, net, GUI, audio, IPC, mem) |
| **Tests** | 1,600 regression tests (build checks + HBFS integrity) |

---

## ✨ Features

### Kernel & Architecture

- **64-bit long mode** with 4-level paging (PML4), identity-mapped
  4 GB via 2 MB pages
- **Ring 0 / Ring 3** privilege separation — user programs run in unprivileged mode
- **84 syscalls** via `INT 0x80`: file I/O, networking,
  GUI, audio, IPC, memory allocation, process control, serial, date/time
- **Preemptive multitasking** — round-robin scheduler,
  100 ms quantum, up to 16 tasks, per-task kernel stacks
- **Physical memory manager** — bitmap allocator with `malloc`/`free` for user programs
- **Page fault handler** with graceful recovery to shell prompt
- **ELF64 loader** — supports flat binaries and ELF executables
- **Inter-process communication** — 8 pipes + 8 shared memory regions + message queues
- **Sound Blaster 16 driver** — DMA playback, WAV parsing, ISA DMA
- **Three-stage boot**: MBR → Stage 2
  (A20, E820 memory map, GDT, long mode) → Kernel

### TCP/IP Networking

- **RTL8139 NIC driver** — PCI auto-detect, software reset, interrupt-driven RX/TX
- **Full protocol stack**: Ethernet II → ARP → IPv4 → ICMP / UDP / TCP
- **DHCP client** — automatic IP configuration (discover → offer → request → ack)
- **DNS resolver** — hostname-to-IP resolution via UDP queries
- **TCP state machine** — complete handshake
  (SYN/SYN-ACK/ACK), data transfer, graceful close
- **Socket API** — 10 syscalls: socket, connect, send,
  recv, bind, listen, accept, dns, close, ping
- **5 shell commands**: `net`, `dhcp`, `ping`, `arp`, `ifconfig`
- **11 network programs**: Forager web browser, BForager GUI browser,
  Telnet, FTP, Gopher, Ping, Mail, News, IRC, HTTP server, NTP, Package manager

### Burrows Desktop Environment

- **640×480×32 graphics** via Bochs VBE/BGA framebuffer
  with double buffering
- **Window manager** — up to 16 draggable windows with title bars and close buttons
- **Taskbar** with application launcher menu and clock
- **Mouse support** — PS/2 IRQ12 driver with cursor tracking
- **4 built-in themes** — Blue, Dark, Light, Amber
- **5 screensaver modes** — Starfield, Matrix, Pipes, Bouncing logo, Plasma
- **GUI syscall API** — 19 sub-functions for windows,
  drawing, widgets, events, and compositing
- **Widget toolkit** — Button, Checkbox, Progress bar, Textbox, Listbox, Label
- **12 GUI applications**: terminal, text editor,
  file manager, calculator, paint, system monitor,
  web browser, music player, spreadsheet, sticky notes,
  settings, image viewer

### HB Lair Shell

- **64+ built-in commands** with aliases: file management,
  text processing, system info, networking
- **Tab completion**, **command history** (Up/Down),
  **!! and !n recall**, **Ctrl+C** hard-abort with cleanup
- **Pipes and redirection** —
  `|`, `>`, `>>`, `<`, `&&`, `||`
- **Environment variables** with `$VAR` expansion
- **Batch scripting** — `.bat` files with
  `:LABEL`/`goto`, `if [not] errorlevel`, `rem`, `@cmd`
- **PATH-based program search** — run programs from any directory
- **Wildcard expansion** — `*` and `?` globbing for all commands

### HBFS Filesystem

- **Honey Badger File System** — custom filesystem with 4 KB blocks
- **Hierarchical directories** — up to 16 levels deep
- **File descriptors** — open/read/write/close/seek (8 simultaneous FDs)
- **Wildcards** — `*` and `?` pattern matching with global expansion
- **Symbolic links** — `ln -s` creates symlinks, resolved by `stat`
- **Directory caching** — avoids redundant disk reads for repeated directory access
- **Real-time timestamps** — files stamped with RTC date/time (YYYY-MM-DD HH:MM)
- **Filesystem checking** — `fsck` validates superblock, bitmap, and directory integrity
- **File types**: text, executable, directory, batch script, symbolic link

### Drivers

| Driver | Details |
| -------- | --------- |
| **VGA** | Text mode 80×25, 16 colors, hardware cursor |
| **VBE/BGA** | 640×480×32 linear framebuffer for Burrows desktop |
| **PS/2 Keyboard** | Full key map with shift, ctrl, special keys (IRQ1) |
| **PS/2 Mouse** | Position tracking, 3-button support (IRQ12) |
| **ATA PIO** | LBA48 addressing with retry logic |
| **RTL8139** | PCI Ethernet NIC with interrupt-driven RX/TX |
| **PIT** | Programmable interval timer at 100 Hz |
| **PC Speaker** | Square wave generation for sound/music |
| **Sound Blaster 16** | ISA DMA audio playback, WAV format support |
| **Serial** | COM1 at 115200 baud for debug output |
| **RTC** | Real-time clock for date/time via CMOS |

---

## 🎮 Included Programs (140 Assembly + 17 Samples)

### Games & Puzzles (32)

| Program | Description |
| --------- | ------------- |
| `2048` | Sliding tile number game |
| `adventure` | Interactive text adventure with dungeon exploration |
| `blackjack` | Blackjack (21) card game |
| `breakout` | Breakout/Arkanoid brick-breaking game |
| `chess` | Two-player chess with move validation and check detection |
| `connect4` | Connect Four against the computer |
| `freecell` | FreeCell solitaire in a GUI window |
| `galaga` | Space shooter with enemy waves |
| `guess` | Number guessing game with hints |
| `hangman` | Word-guessing hangman game |
| `hanoi` | Towers of Hanoi puzzle |
| `kingdom` | Medieval kingdom management simulation |
| `life` | Conway's Game of Life (78×23 grid) |
| `mastermind` | Guess a hidden color code in ten attempts |
| `maze` | Random maze generator with BFS solver |
| `mine` | Minesweeper with flag and reveal mechanics |
| `neurovault` | Pattern memory game |
| `outbreak` | Zombie survival strategy game |
| `piano` | PC speaker musical keyboard (15 notes) |
| `pipes` | Place pipe pieces to connect source and drain |
| `pong` | Classic Pong against the CPU |
| `puzzle15` | Slide numbered tiles to solve the 15-puzzle |
| `raycaster` | Wolfenstein-style 3D maze with raycasting |
| `rogue` | ASCII roguelike dungeon crawler with FOV and inventory |
| `simon` | Repeat growing color sequences in a memory game |
| `snake` | Classic snake — eat food, grow, avoid walls and tail |
| `sokoban` | Box-pushing puzzle game with multiple levels |
| `solitaire` | Klondike solitaire with mouse drag-and-drop |
| `tetris` | Tetris with 7 tetrominoes, rotation, scoring, and levels |
| `tictactoe` | Tic-Tac-Toe against the CPU |
| `wordle` | Guess a 5-letter word in six attempts |
| `worm` | Grow a worm by eating food without hitting walls |

### Internet Programs (11)

| Program | Description |
| --------- | ------------- |
| `forager` | Web browser — fetch web pages (tested: example.com) |
| `ftp` | FTP client with passive mode (ls, cd, get, put) |
| `gopher` | Gopher protocol browser |
| `httpd` | HTTP server with directory listing |
| `irc` | IRC client for chat channels |
| `mail` | SMTP mail client |
| `news` | NNTP news reader |
| `ntpd` | Synchronize system time via NTP |
| `ping` | ICMP echo request with RTT display |
| `pkg` | Package manager — list, search, manage programs |
| `telnet` | Interactive Telnet client |

### Text Processing (16)

| Program | Description |
| --------- | ------------- |
| `cmp` | Compare two files byte by byte |
| `cut` | Extract columns from text |
| `diff` | Line-by-line file comparison |
| `grep` | Pattern search in files |
| `head` | Show first N lines |
| `nl` | Number lines of a file |
| `od` | Octal/hex binary viewer |
| `paste` | Join lines side-by-side |
| `rev` | Reverse each line |
| `sed` | Stream editor (search and replace) |
| `sort` | Sort lines (with `-r` reverse, `-n` numeric) |
| `tail` | Show last N lines |
| `tr` | Character translation |
| `uniq` | Remove adjacent duplicates (with `-c`, `-d`) |
| `wc` | Count lines, words, and bytes |
| `xxd` | Hex dump with ASCII sidebar |

### Language Interpreters (5)

| Program | Description |
| --------- | ------------- |
| `tcc` | Tiny C Compiler — compiles C subset to native code inside the OS |
| `basic` | BASIC interpreter (PRINT, INPUT, LET, IF/THEN, GOTO, FOR/NEXT) |
| `forth` | FORTH interpreter with stack operations and word definitions |
| `perl` | Perl 5 subset interpreter and REPL |
| `asm` | Interactive x86 assembler REPL (~25 instruction types) |

### System Utilities (25)

| Program | Description |
| --------- | ------------- |
| `apitest` | Syscall API testing tool |
| `base64` | Encode or decode files in Base64 format |
| `basename` | Extract filename from path |
| `csv` | CSV file viewer with formatted columns |
| `date` | Display or set the current date and time |
| `debug` | Inspect memory, registers, and hex dumps |
| `df` | Show disk usage statistics |
| `dirname` | Extract directory from path |
| `du` | Show file sizes on disk |
| `edit` | Full-screen text editor with save/load |
| `find` | Find files matching a pattern |
| `free` | Display physical memory usage |
| `help` | View built-in help and manual pages |
| `hexdump` | Hex/ASCII file viewer |
| `hive` | Dual-pane TUI file manager |
| `id` | Display user/group ID info |
| `pager` | File pager with scrolling (like `more`) |
| `ps` | List active tasks from the scheduler |
| `serial` | Serial port testing utility |
| `strings` | Extract printable strings from a binary file |
| `sysinfo` | System information display |
| `top` | Live process/task monitor with CPU and memory display |
| `touch` | Create an empty file |
| `uname` | Print system information (OS, version, arch) |
| `whoami` | Show current user |

### Demos & Fun (22)

| Program | Description |
| --------- | ------------- |
| `banner` | ASCII art banner printer |
| `cal` | Calendar with current day highlighted |
| `calc` | Interactive calculator (+, −, ×, ÷, %) |
| `clock` | Analog ASCII clock with trigonometric hands |
| `colors` | VGA color palette demo |
| `cowsay` | Display a message in a cow speech bubble |
| `echo` | Print arguments to standard output |
| `expr` | Evaluate integer arithmetic expressions |
| `factor` | Print prime factorization of a number |
| `fibonacci` | Fibonacci sequence generator |
| `fortune` | Display a random fortune or quote |
| `lolcat` | Print text in rainbow colors |
| `mandel` | Mandelbrot set renderer (fixed-point arithmetic) |
| `matrix` | Matrix-style falling character rain |
| `periodic` | Interactive periodic table browser |
| `primes` | Prime number generator |
| `rot13` | Encode or decode text with the ROT13 cipher |
| `seq` | Sequence number generator |
| `starfield` | Animated 3D starfield with parallax depth |
| `timewarp` | BASIC/PILOT/Logo editor with turtle graphics |
| `typist` | Typing practice with WPM and accuracy tracking |
| `weather` | Simulated weather station display |

### Scripting Utilities (8)

| Program | Description |
| --------- | ------------- |
| `false` | Exit with status 1 |
| `hello` | Hello World |
| `sleep` | Sleep for N seconds |
| `tac` | Print file lines in reverse order |
| `tee` | Split output to file and stdout |
| `true` | Exit with status 0 |
| `uptime` | Show system uptime |
| `yes` | Print "y" continuously |

### Burrows GUI Applications (12)

| Program | Description |
| --------- | ------------- |
| `bcalc` | BCalc GUI calculator |
| `bedit` | BEdit GUI text editor |
| `bforager` | BForager GUI web browser |
| `bhive` | BHive GUI file manager |
| `bnotes` | BNotes GUI sticky notes |
| `bpaint` | BPaint GUI paint application |
| `bplayer` | BPlayer GUI music player with VU meter |
| `bsettings` | BSettings GUI theme customizer |
| `bsheet` | BSheet GUI spreadsheet with formulas |
| `bsysmon` | BSysMon GUI system monitor |
| `bterm` | BTerm GUI terminal emulator |
| `bview` | BView GUI image viewer (24-bit BMP) |

### C Samples (11)

Compiled by the in-OS TCC compiler from `/samples/`:

| Sample | Description |
| -------- | ------------- |
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

### Perl Samples (6)

Interpreted by the in-OS Perl interpreter from `/samples/`:

| Sample | Description |
| -------- | ------------- |
| `hello.pl` | Hello World |
| `factorial.pl` | Factorial calculator |
| `fizzbuzz.pl` | FizzBuzz |
| `guess.pl` | Number guessing game |
| `arrays.pl` | Array operations demo |
| `strings.pl` | String manipulation demo |

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
make full      # Build everything: boot + kernel + 140 programs + filesystem
make run       # Launch in QEMU
```

Or create a bootable ISO:

```bash
make iso       # Full ISO (~2.1 GiB) with docs
make iso-lite  # Lite ISO (~65 MiB) — same content, truncated image
make run-iso   # Boot the ISO in QEMU
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
Lair:/> forager example.com        # Fetch a web page
Lair:/> burrows                    # Launch the Burrows desktop
```

---

## 📁 Project Structure

### Source Tree

```text
Mellivora_OS/
├── boot.asm                MBR boot sector (512 bytes, 16-bit real mode)
├── stage2.asm              Stage 2 loader (A20, E820, GDT, temporary 32-bit PM → 64-bit long mode)
├── kernel.asm              Kernel entry point + subsystem includes
├── kernel/                 Kernel subsystems (22 modules, ~28,800 lines)
│   ├── shell.inc           HB Lair shell (64 commands)
│   ├── net.inc             TCP/IP networking stack
│   ├── util.inc            Utilities, ELF loader, env vars
│   ├── burrows.inc         Burrows desktop environment
│   ├── hbfs.inc            HBFS filesystem driver
│   ├── data.inc            BSS data, string constants, scan tables
│   ├── syscall.inc         INT 0x80 dispatcher (84 syscalls)
│   ├── vbe.inc             Bochs VBE/BGA framebuffer driver
│   ├── ata.inc             ATA PIO disk driver (LBA48)
│   ├── paging.inc          4-level paging (PML4, 2 MB pages)
│   ├── sched.inc           Preemptive round-robin scheduler
│   ├── mouse.inc           PS/2 mouse driver (IRQ12)
│   ├── isr.inc             Interrupt service routines
│   ├── idt.inc             Interrupt descriptor table
│   ├── pit.inc             PIT timer + keyboard driver
│   ├── pic.inc             PIC initialization
│   ├── pmm.inc             Physical memory manager (bitmap)
│   ├── vga.inc             VGA text mode driver
│   ├── filesearch.inc      Global file search across directories
│   ├── sb16.inc            Sound Blaster 16 audio driver
│   ├── ipc.inc             Inter-process communication (pipes, shared memory, MQ)
│   └── screensaver.inc     Screensaver modes (starfield, matrix, pipes, bounce, plasma)
├── programs/               User-space programs (140 .asm files)
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
├── samples/                Sample scripts for in-OS compilers (11 C, 6 Perl)
├── tests/                  Regression test suite
│   ├── test_build.sh       Build-time validation (45 checks)
│   └── test_hbfs.py        HBFS filesystem integrity (1,555 checks)
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
├── bin/          147 utility and tool programs
├── games/         28 games
├── samples/       22 sample scripts (11 C, 6 Perl, 5 docs)
├── docs/          21 text files (readme, license, notes, todo, poem, man pages)
├── script.bat    Example batch script
└── welcome.bat   System highlights and quick-start tips
```

Programs in `/bin` and `/games` are in the default
`PATH`, so they run from any directory.

---

## 📖 Documentation

| Document | Description |
| ---------- | ------------- |
| **[Installation Guide](docs/INSTALL.md)** | Prerequisites, building, QEMU configuration, real hardware |
| **[User Guide](docs/USER_GUIDE.md)** | Shell command reference, pipes, scripting |
| **[Tutorial](docs/TUTORIAL.md)** | Step-by-step walkthrough from boot to writing your first program |
| **[Programming Guide](docs/PROGRAMMING_GUIDE.md)** | Writing assembly programs: syscalls, libraries, GUI, networking |
| **[API Reference](docs/API_REFERENCE.md)** | Complete syscall and library function reference |
| **[Technical Reference](docs/TECHNICAL_REFERENCE.md)** | OS internals: boot, memory, filesystem, drivers, scheduler |
| **[Networking Guide](docs/NETWORKING_GUIDE.md)** | TCP/IP stack architecture, socket API, protocol details |
| **[Changelog](CHANGELOG.md)** | Full version history from v1.0 to v4.0.0 |

---

## 🔧 Build Targets

| Command | Description |
| --------- | ------------- |
| `make full` | Full build: boot + kernel + programs + FS |
| `make run` | Launch in QEMU (default: qemu64 CPU, 2048 MB RAM, networking, host-aware audio backend) |
| `make debug` | Launch with QEMU monitor on stdio for debugging |
| `make iso` | Create bootable ISO image (~2.1 GiB) with documentation |
| `make iso-lite` | Create smaller ISO (~65 MiB) with truncated disk image |
| `make iso-verify` | Validate El Torito boot record in the ISO |
| `make run-iso` | Boot the ISO in QEMU (CD-ROM + IDE disk) |
| `make check` | Run the full regression suite (1,600 tests) |
| `make clean` | Remove all build artifacts |
| `make sizes` | Show component and ISO binary sizes |

---

## 🖥️ System Requirements

### Emulation (Recommended)

- QEMU 6.0+ with `qemu-system-x86_64`
- Any host OS — Linux, macOS, Windows (with WSL)

### Real Hardware

- Core 2 Duo or newer x86 CPU
- 1 MB RAM minimum (1024 MB recommended)
- IDE/SATA hard disk or USB drive (BIOS legacy boot)
- VGA-compatible display
- PS/2 keyboard
- RTL8139-compatible Ethernet card (for networking)

---

## 📊 Project Statistics

| Metric | Value |
| -------- | ------- |
| Kernel source | ~28,800 lines across 22 modules |
| Kernel binary | ~663 KB |
| Program source | ~81,500 lines across 140 programs |
| User libraries | ~4,400 lines across 8 modules |
| Syscalls | 84 (via `INT 0x80`) |
| Shell commands | 64 built-in |
| Programs | 140 assembly + 17 samples (11 C, 6 Perl) |
| Disk image | 2 GB with HBFS filesystem |
| Files on disk | 220 files across 4 subdirectories |
| Test coverage | 1,600 regression and integrity checks |
| Networking protocols | Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS |
| GUI windows | Up to 16 simultaneous |

---

## 📜 License

This project is licensed under the [MIT License](LICENSE).

Copyright (c) 2026 Honey Badger Universe

---

## 🦡 Why "Mellivora"?

*Mellivora capensis* — the honey badger.
Small, tough, and fearless. Just like this OS.

### Component Naming

| Component | Name | Full Name |
| ----------- | ------ | ----------- |
| Kernel | **Mellivora** | Mellivora OS kernel |
| Init System | **Ratel** | Hardware & subsystem initialization |
| Shell | **HB Lair** | Honey Badger Lair interactive shell |
| Filesystem | **HBFS** | Honey Badger File System |
| Utilities | **HBU** | Honey Badger Utilities |
| Desktop | **Burrows** | Burrows Desktop Environment |
| Network Stack | **ClawNet** | TCP/IP networking subsystem |
