# Contributing to Mellivora OS

Thank you for your interest in contributing! Mellivora OS is written entirely in NASM
x86 assembly. This document covers everything you need to get set up, understand the
project conventions, and submit a contribution.

Please also read the [Code of Conduct](CODE_OF_CONDUCT.md) before participating.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Project Structure](#project-structure)
3. [Build System](#build-system)
4. [Types of Contributions](#types-of-contributions)
5. [Writing a New Program](#writing-a-new-program)
6. [Modifying the Kernel](#modifying-the-kernel)
7. [Code Style](#code-style)
8. [Testing](#testing)
9. [Submitting a Pull Request](#submitting-a-pull-request)
10. [Reporting Bugs](#reporting-bugs)

---

## Getting Started

### Prerequisites

| Tool | Version | Purpose |
| --- | --- | --- |
| **NASM** | 2.15+ | Assembles all `.asm` sources |
| **GNU Make** | 4.0+ | Build orchestration |
| **QEMU** | 6.0+ | `qemu-system-x86_64` for testing |
| **Python 3** | 3.6+ | Runs `populate.py` to build the disk image |
| **dd** | any | Disk image construction (standard on Linux/macOS) |

### Installing dependencies

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

### Initial build

```bash
git clone https://github.com/James-HoneyBadger/Mellivora_OS.git
cd Mellivora_OS
make full   # build everything
make run    # launch in QEMU
```

---

## Project Structure

```text
Mellivora_OS/
├── boot.asm           Stage 1 MBR boot sector (512 bytes, 16-bit real mode)
├── stage2.asm         Stage 2 loader (A20, E820, protected-mode switch)
├── kernel.asm         Kernel entry point + %include graph
├── kernel/            26 kernel include modules (sched, vga, hbfs, net, ...)
├── programs/          User-space assembly programs (~218 total)
│   ├── syscalls.inc   Shared syscall constants — include this first
│   └── lib/           17 reusable library includes (string, io, math, vbe, ...)
├── samples/           C, Perl, and BASIC source files for TCC / interpreters
├── tests/             Regression test suite
│   ├── test_build.sh  Build-time checks (sizes, HBFS layout, constants)
│   └── test_hbfs.py   HBFS filesystem integrity checks
├── docs/              Full documentation suite
│   ├── STYLE_GUIDE.md    ← read this before writing any code
│   ├── API_REFERENCE.md
│   ├── PROGRAMMING_GUIDE.md
│   ├── TECHNICAL_REFERENCE.md
│   └── ...
├── Makefile
└── populate.py        Writes programs and samples into the HBFS image
```

The single most important document for contributors is
[docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md). Read it before writing or
modifying any code in `programs/`.

---

## Build System

### Common targets

| Command | Description |
| --- | --- |
| `make full` | Complete build: kernel + programs + populate filesystem |
| `make run` | Launch the built image in QEMU |
| `make debug` | QEMU with monitor on stdio and interrupt/reset logging |
| `make check` | Run the regression suite and HBFS integrity tests |
| `make sanitize` | Build with `KERNEL_DEBUG_BOUNDS=1` for bounds-check hardening |
| `make iso` | Create `mellivora.iso` bootable installer |
| `make clean` | Remove all build artifacts |
| `make sizes` | Print component sizes |

### Building a single program

```bash
nasm -f bin -O0 -Iprograms/ -o programs/myprog.bin programs/myprog.asm
```

The `-Iprograms/` flag makes `%include "syscalls.inc"` and
`%include "lib/..."` resolve correctly from the `programs/` directory.
The `-O0` flag disables NASM's multi-pass optimizer, which is required to
avoid label-oscillation errors in the flat binary format.

### Parallel builds

`make programs` runs NASM on all programs in parallel. The number of jobs
defaults to the host CPU count. Override with `make programs NPROC=1` if
needed.

---

## Types of Contributions

| Type | Where to put it |
| --- | --- |
| New user-space program | `programs/<name>.asm` |
| New shared library | `programs/lib/<name>.inc` |
| Kernel bug fix | `kernel/<module>.inc` |
| New kernel feature | `kernel/<module>.inc` + `kernel/syscall.inc` + `kernel/data.inc` |
| New sample (C/Perl/BASIC) | `samples/<name>.<ext>` |
| Documentation fix | `docs/<file>.md` or `README.md` |
| Build / tooling | `Makefile` or `populate.py` |
| Regression test | `tests/test_build.sh` or `tests/test_hbfs.py` |

---

## Writing a New Program

### Minimal template

```nasm
; myprog.asm - One-line description.
; Usage: myprog [args]
%include "syscalls.inc"

start:
        mov eax, SYS_PRINT
        mov ebx, msg
        int 0x80

        xor eax, eax            ; SYS_EXIT
        int 0x80

msg:    db "Hello from myprog!", 10, 0
```

### VBE game template

```nasm
; mygame.asm - Short description.
; ARROW KEYS = move  ENTER = action  Q/ESC = quit
%include "syscalls.inc"
%include "lib/vbe_game.inc"
%include "lib/font.inc"
%include "lib/vbe_ui.inc"
%include "lib/palette.inc"

COL_BG   equ MV_BG_DARK
COL_TEXT equ MV_FG_BRIGHT

start:
        VBE_GAME_INIT
        call init_state
        call draw_all

.main_loop:
        VBE_GAME_POLL_KEY
        cmp eax, -1
        je  .no_key

        cmp al, 'q'
        je  .quit
        cmp al, 'Q'
        je  .quit
        cmp al, KEY_ESC
        je  .quit
        ; ... handle other keys ...

.no_key:
        call draw_all
        VBE_GAME_PRESENT
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        jmp .main_loop

.quit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2              ; restore text mode
        int 0x80
        xor eax, eax
        int 0x80
```

### Key rules for programs

- Always `%include "syscalls.inc"` first.
- Never include `[BITS 32]` or `[ORG ...]` — `syscalls.inc` provides them.
- Build with `-f bin -O0`.
- For variables read before being written, use `dd 0` / `times N db 0`
  instead of `resd`/`resb` in `section .bss`. See
  [STYLE_GUIDE.md §1.2](docs/STYLE_GUIDE.md) for the full explanation.
- All strings drawn with the VBE font must be **uppercase** — the 5×7
  bitmap font only covers `0x20..0x5F`.
- VBE programs must restore text mode before exiting
  (`SYS_FRAMEBUF` sub 2).
- VBE games must accept both `Q`/`q` and `KEY_ESC` to quit.
- Use color constants from `lib/palette.inc` (`MV_BG_DARK`, `MV_FG_BRIGHT`,
  etc.) — do not hard-code hex color literals.

The [PROGRAMMING_GUIDE.md](docs/PROGRAMMING_GUIDE.md) covers the complete
syscall interface. The [API_REFERENCE.md](docs/API_REFERENCE.md) documents
every library function.

---

## Modifying the Kernel

Kernel code lives in `kernel.asm` (entry point and constants) and the 26
include files in `kernel/`. Each module has a clear responsibility:

| File | Responsibility |
| --- | --- |
| `kernel/data.inc` | All global variables, constants, and BSS layout |
| `kernel/shell.inc` | HB Lair shell and built-in commands |
| `kernel/hbfs.inc` | Honey Badger File System |
| `kernel/sched.inc` | Preemptive scheduler, task control blocks |
| `kernel/syscall.inc` | `INT 0x80` dispatch table |
| `kernel/vbe.inc` | VBE/BGA framebuffer driver |
| `kernel/net.inc` | TCP/IP networking stack |
| `kernel/ipc.inc` | Pipes, shared memory, semaphores, message queues |
| `kernel/paging.inc` | Paging and demand-paging |
| `kernel/pmm.inc` | Physical memory manager (bitmap + buddy allocator) |

### Adding a syscall

1. Pick the next free number in `programs/syscalls.inc` and add a
   `SYS_NAME equ N` line with a comment describing arguments and return.
2. Add the same constant to the dispatch table in `kernel/syscall.inc`.
3. Implement the handler in the appropriate kernel module.
4. Document the new syscall in:
   - `docs/TECHNICAL_REFERENCE.md` — Complete Syscall Table section
   - `docs/PROGRAMMING_GUIDE.md` — Syscall Numbers listing

### Kernel build rules

- The kernel is assembled with `-f bin -O0 -w-zeroing`.
- All global state belongs in `kernel/data.inc`; no BSS in other modules.
- The kernel must stay under 2048 sectors (1 MB). `make full` will fail
  with an error if this limit is exceeded.
- `kernel_sectors.inc` is generated automatically from `kernel.bin` size
  — do not edit it manually.

---

## Code Style

The canonical reference is [docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md).
Key points:

### Formatting

- 8-column tabs, displayed as spaces (NASM convention).
- Operands aligned to column 16 (labels at column 0, mnemonics at column 8).
- Section headers: `;=== Major Section ===` and `;--- Sub-section ---`.
- Every file begins with a header comment: filename, one-line description,
  and brief usage or controls summary.

### Functions in shared libraries

All functions in `programs/lib/*.inc` must preserve every register via
`pushad`/`popad` unless they deliberately return multiple values. Return
values go in `EAX` (and optionally `ECX`/`EDI`); document clobbers
explicitly.

### Pre-commit checklist

Before opening a pull request, verify each item that applies to your change:

- [ ] `nasm -f bin -O0 -Iprograms/ -o /tmp/x.bin programs/<name>.asm` succeeds with no errors
- [ ] Variables read before being written are initialized with `dd 0` / `times N db 0`
- [ ] All VBE strings are uppercase
- [ ] VBE programs restore text mode on exit
- [ ] VBE games accept both `Q`/`q` and `KEY_ESC` to quit
- [ ] No hard-coded hex color literals (use `MV_*` palette constants)
- [ ] `SYS_BEEP` followed by another syscall reloads `EAX` first
- [ ] All shared-library functions preserve registers
- [ ] New syscalls are documented in `syscalls.inc`, `TECHNICAL_REFERENCE.md`, and `PROGRAMMING_GUIDE.md`
- [ ] `make check` passes

---

## Testing

### Running the test suite

```bash
make full       # build must be current before testing
make check      # runs both test_build.sh and test_hbfs.py
```

`test_build.sh` checks binary sizes, disk image layout, HBFS magic number
and superblock constants, and consistency between `kernel.asm` and
`populate.py`.

`test_hbfs.py` mounts the HBFS image via Python and verifies directory
structure, file integrity, and entry counts.

### Bounds-check build

```bash
make sanitize
```

Builds with `KERNEL_DEBUG_BOUNDS=1`, which enables additional runtime
assertions in the kernel. Run this when changing memory management or
filesystem code.

### Manual testing in QEMU

After `make full` and `make run`, exercise the relevant code paths in the
shell. For a new program, test:
- Normal operation
- Edge cases (empty input, large input, missing files)
- Ctrl+C abort
- Q / ESC quit (for interactive programs)
- Run from a directory other than `/bin` or `/games` to verify PATH search

For kernel changes, use `make debug` (QEMU monitor on stdio) and watch for
unexpected exceptions or resets.

---

## Submitting a Pull Request

1. **Fork** the repository and create a branch from `main`:
   ```bash
   git checkout -b feature/my-new-thing
   ```

2. **Make your changes** following the style guide and checklist above.

3. **Run the full test suite:**
   ```bash
   make clean && make full && make check
   ```

4. **Write a clear commit message.** Use the imperative mood and reference
   the relevant area:
   ```
   programs: add myprog — short description

   Longer explanation if needed. Reference related issues or PRs.
   ```

5. **Update documentation** if your change affects behavior visible to
   users or developers:
   - New program → no doc change required (self-documenting via `man` and `help`)
   - New syscall → update `TECHNICAL_REFERENCE.md` and `PROGRAMMING_GUIDE.md`
   - New library function → update `API_REFERENCE.md`
   - Changed build behavior → update `INSTALL.md`
   - Changed shell command → update `USER_GUIDE.md`

6. **Open a pull request** against the `main` branch. The PR description
   should explain what the change does and why, and note any testing done.

### What to expect

- PRs are reviewed for correctness, style-guide compliance, and test
  coverage.
- Feedback will be specific and constructive; please respond to each
  comment.
- Small, focused PRs are much easier to review than large omnibus changes.
  If you are adding multiple unrelated programs, consider separate PRs.

---

## Reporting Bugs

Please open a GitHub issue with:

- **OS version** — run `ver` inside Mellivora to get the version string
- **Host and QEMU version** — `qemu-system-i386 --version`
- **Steps to reproduce** — exact shell commands or sequence of actions
- **Expected behavior** — what should have happened
- **Actual behavior** — what actually happened, including any error output
- **Serial log** (if available) — run `make run` with `-serial stdio` appended to `QEMU_FLAGS`

For security-related bugs, see [SECURITY.md](SECURITY.md).
