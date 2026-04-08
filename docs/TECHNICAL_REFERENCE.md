# Mellivora OS — Technical Reference

This document is a comprehensive technical reference for the architecture, subsystems,
and internals of Mellivora OS. It is intended for contributors, OS enthusiasts, and
anyone who wants to understand how the system works under the hood.

---

## Table of Contents

1. [Boot Sequence](#boot-sequence)
2. [Global Descriptor Table (GDT)](#global-descriptor-table-gdt)
3. [Interrupt Descriptor Table (IDT)](#interrupt-descriptor-table-idt)
4. [Memory Map](#memory-map)
5. [Physical Memory Manager (PMM)](#physical-memory-manager-pmm)
6. [HBFS Filesystem](#hbfs-filesystem)
7. [Directory Navigation & Stacking](#directory-navigation--stacking)
8. [PATH Search Mechanism](#path-search-mechanism)
9. [Path-Aware File I/O](#path-aware-file-io)
10. [ATA/IDE Disk Driver](#ataide-disk-driver)
11. [VGA Text Mode](#vga-text-mode)
12. [Keyboard Driver](#keyboard-driver)
13. [PIT Timer](#pit-timer)
14. [Serial Port](#serial-port)
15. [PC Speaker](#pc-speaker)
16. [Process Execution](#process-execution)
17. [Syscall Interface](#syscall-interface)
18. [Shell Architecture](#shell-architecture)
19. [Environment Variables](#environment-variables)
20. [Alias System](#alias-system)
21. [Command History](#command-history)
22. [Tab Completion](#tab-completion)

---

## Boot Sequence

Mellivora boots in three stages: a 512-byte MBR bootloader, a 16 KB second stage, and
the 32-bit protected-mode kernel.

### Stage 1 — MBR Bootloader (boot.asm)

The BIOS loads the first sector (512 bytes) of the disk to `0x7C00`.

1. Sets up a flat 16-bit real-mode environment: `DS=ES=SS=0`, `SP=0x7C00`
2. Saves the BIOS boot drive number (`DL`) to `boot_drive`
3. Enables the A20 gate via the fast method (port `0x92`)
4. Loads Stage 2 using `INT 0x13 AH=0x42` (Extended Read) from **LBA 1**, **32 sectors**
   (16 KB) to `0x7E00`
5. Verifies the Stage 2 magic number `'BOS2'` (dword at `0x7E00`)
6. Jumps to `0x0000:0x7E04` (skipping the 4-byte magic), passing boot drive in `DL`

### Stage 2 — Protected Mode Setup (stage2.asm)

Stage 2 runs at `0x7E00` in real mode and transitions to 32-bit protected mode.

1. Re-initializes segments and stack (`SP=0x7C00`)
2. Sets VGA mode `0x03` (80×25 text), draws a blue splash/title bar at `0xB800`
3. Detects available memory via `BIOS INT 0x15 E820` (up to 32 entries × 24 bytes)
4. Loads the kernel from disk starting at **LBA 33**, reading **384 sectors** (192 KB)
   into low memory at `0x20000` (segment `0x2000`), in chunks of 64 sectors
5. Enters protected mode:
   - Loads the GDT (`lgdt [gdt_descriptor]`)
   - Sets CR0 PE bit
   - Far-jumps to `0x08:pmode_entry`
6. In 32-bit mode: sets all segment registers to `0x10` (kernel data), `ESP = 0x9FC00`
7. Stores boot info at fixed addresses:
   - Boot drive → `[0x500]`
   - Memory map count → `[0x504]`
   - Memory map pointer → `[0x508]`
8. Copies kernel from `0x20000` to `0x00100000` (1 MB) via `REP MOVSD`
9. Jumps to `0x08:0x00100000` — kernel entry point

### Stage 3 — Kernel Entry (kernel.asm)

The kernel starts executing at 1 MB in 32-bit protected mode.

1. Initializes all subsystems in order:
   - VGA clear screen
   - PIC remapping (IRQs to INT 0x20–0x2F)
   - IDT setup (256 entries)
   - PIT timer (100 Hz)
   - Keyboard driver
   - ATA/IDE disk detection
   - Serial port (COM1 at 115200 baud)
   - PMM (using E820 map)
   - TSS (Ring 3 support)
   - HBFS filesystem
   - Default environment variables
2. Prints the HB Lair banner
3. Enters the interactive shell loop

### Disk Layout

| LBA Range | Size | Contents |
| --- | --- | --- |
| 0 | 512 B | MBR bootloader (boot.asm) |
| 1–32 | 16 KB | Stage 2 loader (stage2.asm) |
| 33–416 | 192 KB | Kernel (kernel.asm) |
| 417 | 512 B | HBFS Superblock |
| 418–425 | 4 KB | Block allocation bitmap |
| 426–553 | 64 KB | Root directory (16 blocks) |
| 554+ | — | Data blocks (4 KB each) |

---

## Global Descriptor Table (GDT)

The GDT is defined in Stage 2 and uses a flat 4 GB memory model with 6 entries:

| Selector | Name | Base | Limit | Access | Flags | Ring | Description |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0x00` | Null | — | — | — | — | — | Required null descriptor |
| `0x08` | Kernel Code | 0 | 4 GB | `0x9A` | `0xCF` | 0 | Execute/Read, 32-bit, 4K gran |
| `0x10` | Kernel Data | 0 | 4 GB | `0x92` | `0xCF` | 0 | Read/Write, 32-bit, 4K gran |
| `0x18` | User Code | 0 | 4 GB | `0xFA` | `0xCF` | 3 | Execute/Read, 32-bit, 4K gran |
| `0x20` | User Data | 0 | 4 GB | `0xF2` | `0xCF` | 3 | Read/Write, 32-bit, 4K gran |
| `0x28` | TSS | Runtime | 104 B | `0x89` | `0x00` | 0 | Task State Segment |

User-mode selectors include RPL=3: `USER_CS = 0x1B`, `USER_DS = 0x23`, `TSS_SEL = 0x28`.

### Access Byte Layout

```text
  7   6  5   4   3   2   1   0
┌───┬──────┬───┬───┬───┬───┬───┐
│ P │ DPL  │ S │ E │ DC│ RW│ A │
└───┴──────┴───┴───┴───┴───┴───┘
P   = Present (1)
DPL = Descriptor Privilege Level (0 = kernel, 3 = user)
S   = Descriptor type (1 = code/data, 0 = system)
E   = Executable (1 = code, 0 = data)
DC  = Direction/Conforming
RW  = Readable (code) / Writable (data)
A   = Accessed
```

---

## Interrupt Descriptor Table (IDT)

The IDT has **256 entries** (2048 bytes total) stored at `idt_table` in BSS.

### Handler Assignment

| Vector(s) | Handler | Type | DPL | Purpose |
| --- | --- | --- | --- | --- |
| 0–7, 9, 15–16, 18–20, 22–31 | `isr_exception_noerr` | Interrupt | 0 | CPU exceptions without error code |
| 8, 10–14, 17, 21 | `isr_exception_err` | Interrupt | 0 | CPU exceptions with error code |
| 0x20 (IRQ 0) | `irq_timer` | Interrupt | 0 | PIT timer tick |
| 0x21 (IRQ 1) | `irq_keyboard` | Interrupt | 0 | Keyboard scancode |
| 0x22–0x27 | `irq_stub` | Interrupt | 0 | PIC1 stub (EOI to PIC1) |
| 0x28–0x2F | `irq_stub_pic2` | Interrupt | 0 | PIC2 stub (EOI to both PICs) |
| 0x80 | `syscall_handler` | **Trap** | **3** | Syscall entry (user-callable) |
| All others | `isr_default` | Interrupt | 0 | Simple `iretd` stub |

INT 0x80 is a **trap gate** with DPL=3 (access byte `0xEF`), allowing user-mode programs
to invoke syscalls. All other gates are interrupt gates with DPL=0 (access byte `0x8E`).

### Exception Recovery

Both exception handlers print diagnostic information (vector number, error code) and
then recover to the shell by resetting `ESP` to `KERNEL_STACK` and jumping to
`shell_main`. This prevents a single fault from crashing the entire system.

---

## Memory Map

### Physical Memory Layout

```text
0x00000000 ┌─────────────────────────┐
           │ Real-mode IVT & BDA     │
0x00000500 ├─────────────────────────┤
           │ Boot info block         │  0x500 = drive, 0x504 = mmap count, 0x508 = mmap ptr
0x00007C00 ├─────────────────────────┤
           │ MBR Bootloader          │  512 bytes
0x00007E00 ├─────────────────────────┤
           │ Stage 2 Loader          │  16 KB
0x00020000 ├─────────────────────────┤
           │ Kernel temp load buffer │  192 KB (copied to 1 MB)
0x0009FC00 ├─────────────────────────┤
           │ Kernel stack (grows ↓)  │  Top of conventional memory
0x000A0000 ├─────────────────────────┤
           │ Video memory / ROM      │
0x000B8000 │ VGA text framebuffer    │  4000 bytes (80×25×2)
0x00100000 ├─────────────────────────┤  ← 1 MB
           │ Kernel code + data      │  Up to 192 KB (384 sectors)
0x00200000 ├─────────────────────────┤  ← 2 MB
           │ User program space      │  1 MB max
0x002FFFF0 │ SYS_EXIT trampoline     │  Safety net for programs that RET
0x00300000 ├─────────────────────────┤  ← 3 MB
           │ PMM bitmap              │  128 KB (tracks up to 4 GB)
0x00400000 ├─────────────────────────┤  ← 4 MB
           │ Heap / PMM allocations  │  Usable memory starts here
           │ ...                     │
           └─────────────────────────┘
```

### Key Constants

| Constant | Value | Description |
| --- | --- | --- |
| `KERNEL_BASE` | `0x00100000` (1 MB) | Kernel load address |
| `KERNEL_SECTORS` | 384 | Kernel disk size (192 KB) |
| `KERNEL_STACK` | `0x0009FC00` | Stack top (conventional memory end) |
| `PROGRAM_BASE` | `0x00200000` (2 MB) | User program load address |
| `PROGRAM_MAX_SIZE` | `0x00100000` (1 MB) | Max program size |
| `PROGRAM_EXIT_ADDR` | `0x002FFFF0` | SYS_EXIT trampoline |
| `PMM_BITMAP` | `0x00300000` (3 MB) | Physical memory bitmap |
| `HEAP_BASE` | `0x00400000` (4 MB) | PMM allocation boundary |
| `VGA_BASE` | `0x000B8000` | VGA text mode framebuffer |
| `VGA_WIDTH` | 80 | Screen columns |
| `VGA_HEIGHT` | 25 | Screen rows |
| `VGA_SIZE` | 4000 | Framebuffer size (bytes) |
| `BOOTINFO_DRIVE` | `0x500` | Boot drive (from Stage 2) |
| `BOOTINFO_MMAP_CNT` | `0x504` | E820 entry count |
| `BOOTINFO_MMAP_PTR` | `0x508` | E820 data pointer |

---

## Physical Memory Manager (PMM)

### Overview

The PMM uses a **bitmap allocator** at `PMM_BITMAP` (0x300000). Each bit represents one
4 KB page. Bit = 1 means used, bit = 0 means free.

### Specifications

| Property | Value |
| --- | --- |
| Page size | 4096 bytes (4 KB) |
| Bitmap size | 128 KB (0x20000 bytes) |
| Total bits | 1,048,576 |
| Max addressable | 4 GB (theoretical) |
| Practical limit | ~128 MB (E820-reported) |

### Initialization

1. All bits set to 1 (mark everything as used)
2. E820 memory map entries are iterated
3. Usable regions **above 4 MB** are freed via `pmm_free_region`
4. This ensures kernel, program space, and PMM bitmap are never allocated

### Key Routines

| Routine | Parameters | Description |
| --- | --- | --- |
| `pmm_alloc_page` | — | Returns first free page address (EAX), sets bit |
| `pmm_free_page` | EAX = address | Clears bit for given address |
| `pmm_alloc_pages` | ECX = count | Allocates N contiguous pages, returns base in EAX |
| `pmm_count_free` | — | Counts free pages (Kernighan's bit-clearing method) |

---

## HBFS Filesystem

HBFS (HoneyBadger File System) is a simple block-based filesystem with flat directories,
a block allocation bitmap, and support for nested subdirectories.

### On-Disk Layout

| Region | LBA Start | LBA Count | Size | Description |
| --- | --- | --- | --- | --- |
| Superblock | 417 | 1 | 512 B | Filesystem metadata |
| Bitmap | 418 | 8 | 4 KB | Block allocation bitmap |
| Root Directory | 426 | 128 | 64 KB | 16 blocks × 4 KB |
| Data Area | 554 | — | — | File/directory data blocks |

### Constants

| Constant | Value |
| --- | --- |
| `HBFS_MAGIC` | `0x48424653` (`'HBFS'`) |
| `HBFS_BLOCK_SIZE` | 4096 (4 KB) |
| `HBFS_SECTORS_PER_BLK` | 8 |
| `HBFS_MAX_FILENAME` | 252 characters |
| `HBFS_DIR_ENTRY_SIZE` | 288 bytes |
| `HBFS_ROOT_DIR_BLOCKS` | 16 |
| `HBFS_ROOT_DIR_SECTS` | 128 (16 × 8) |
| `HBFS_ROOT_DIR_SIZE` | 65,536 bytes |
| `HBFS_MAX_FILES` | 227 (65,536 / 288) |
| `HBFS_SUBDIR_BLOCKS` | 4 |
| `HBFS_SUBDIR_MAX_ENTRIES` | 56 (4 × 4096 / 288) |
| `HBFS_SUPERBLOCK_LBA` | 417 |
| `HBFS_BITMAP_START` | 418 |
| `HBFS_ROOT_DIR_START` | 426 |
| `HBFS_DATA_START` | 554 |

### Superblock Structure (512 bytes at LBA 417)

| Offset | Size | Field | Default |
| --- | --- | --- | --- |
| 0 | 4 | Magic (`'HBFS'`) | `0x48424653` |
| 4 | 4 | Version | 1 |
| 8 | 4 | Total blocks | 32,768 |
| 12 | 4 | Free blocks | 32,768 |
| 16 | 4 | Root directory LBA | 426 |
| 20 | 4 | Bitmap start LBA | 418 |
| 24 | 4 | Data start LBA | 554 |
| 28 | 4 | Block size | 4096 |

### Directory Entry Structure (288 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 253 | Filename (null-terminated, max 252 chars) |
| 253 | 1 | Type: 0=free, 1=file, 2=directory, 3=executable, 4=batch |
| 254 | 2 | Flags (reserved) |
| 256 | 4 | File size (bytes) |
| 260 | 4 | Start block (in data area) |
| 264 | 4 | Block count |
| 268 | 4 | Created timestamp (ticks) |
| 272 | 4 | Modified timestamp (ticks) |
| 276 | 12 | Reserved |

### Block Allocation Bitmap

- 8 sectors = 4096 bytes = 32,768 bits
- Each bit represents one 4 KB data block
- 32,768 blocks × 4 KB = **128 MB** maximum filesystem size
- Bit = 1 means allocated, bit = 0 means free

### File Types

| Value | Name | Description |
| --- | --- | --- |
| 0 | Free | Unused entry |
| 1 | File | Text or binary file |
| 2 | Directory | Subdirectory (data blocks contain its entries) |
| 3 | Executable | Binary program (loaded and run by the shell) |
| 4 | Batch | Batch script (executed line-by-line) |

### Key Routines (HBFS)

| Routine | Description |
| --- | --- |
| `hbfs_init` | Reads superblock, validates magic, loads bitmap and root directory |
| `hbfs_find_file` | Scans loaded directory for filename match |
| `hbfs_create_file` | Allocates blocks, writes data, creates directory entry |
| `hbfs_delete_file` | Frees blocks, clears directory entry |
| `hbfs_read_file` | Path-aware: resolves `/` paths, reads file to buffer |
| `hbfs_load_root_dir` | Loads current directory's blocks into `dir_buffer` |
| `hbfs_alloc_blocks` | Scans bitmap for N contiguous free blocks |
| `hbfs_free_blocks` | Clears bitmap bits for a range of blocks |
| `hbfs_flush_bitmap` | Writes in-memory bitmap back to disk |
| `hbfs_flush_dir` | Writes in-memory directory back to disk |

---

## Directory Navigation & Stacking

### Directory Stack

The kernel maintains a **directory stack** to support nested subdirectory navigation up
to 16 levels deep.

| Constant | Value | Description |
| --- | --- | --- |
| `DIR_STACK_MAX` | 16 | Maximum nesting depth |
| `DIR_STACK_ENTRY_SIZE` | 264 | Bytes per stack entry |

Each stack entry stores:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Parent directory LBA |
| 4 | 4 | Parent directory sector count |
| 8 | 256 | Parent directory name |

### Navigation State

| Variable | Description |
| --- | --- |
| `current_dir_lba` | LBA of the current directory's data on disk |
| `current_dir_sects` | Number of sectors in the current directory |
| `current_dir_name` | Name of the current directory (256 bytes) |
| `dir_depth` | Current nesting level (0 = root) |
| `dir_stack` | Stack of parent directories (16 × 264 bytes) |

### cd Command Flow

1. **`cd /`** — Reset to root: `current_dir_lba = HBFS_ROOT_DIR_START`, `dir_depth = 0`
2. **`cd ..`** — Pop parent from stack: decrement `dir_depth`, restore LBA and name
3. **`cd NAME`** — Push current onto stack, find subdirectory entry, set new LBA
4. **`cd /path/to/dir`** — Reset to root, then process each component left-to-right
5. **`cd ../sibling`** — Go up, then into sibling

---

## PATH Search Mechanism

When a command is not a built-in and is not found in the current directory, the shell
searches the directories listed in the `PATH` environment variable.

### Default PATH

```text
PATH=/bin:/games
```

### Search Algorithm

```text
cmd_exec_program(name):
    1. Try hbfs_read_file(name) from current directory
    2. If found → execute
    3. path_save_cwd()          # save CWD + dir_stack
    4. Read PATH env var into path_search_buf (256 bytes)
    5. For each colon-separated component:
       a. path_restore_cwd()    # always start from original location
       b. cmd_cd_internal(component)  # cd into PATH dir
       c. hbfs_load_root_dir()
       d. hbfs_find_file(name)
       e. If found → read file data, path_restore_cwd(), execute
    6. path_restore_cwd()       # restore on failure
    7. Print "not found" error
```

### CWD Save/Restore

`path_save_cwd` and `path_restore_cwd` preserve and restore the full directory state:

- `current_dir_lba` — Current directory's disk location
- `current_dir_sects` — Current directory's sector count
- `current_dir_name` — Current directory name (256 bytes)
- `dir_depth` — Nesting level
- `dir_stack` — Entire 16-entry parent stack (16 × 264 = 4224 bytes)

This ensures the user's working directory is never disturbed by PATH searches.

---

## Path-Aware File I/O

`hbfs_read_file` transparently resolves paths containing `/` separators, allowing any
command to read files from other directories.

### Algorithm

```text
hbfs_read_file(path, buffer):
    If path contains no '/':
        Read from current directory (simple case)
    Else:
        file_save_cwd()         # separate save from PATH save
        Split path at last '/'
        cd into directory portion
        Read the filename from that directory
        file_restore_cwd()      # restore original CWD
```

### Separate Save Buffers

`file_save_cwd` / `file_restore_cwd` use their own set of save buffers (`file_save_*`),
separate from `path_save_*`. This prevents conflicts when PATH search itself calls
`hbfs_read_file` with a path argument.

### Example

```text
Lair:/> cat /docs/readme       # Reads readme from /docs without changing CWD
Lair:/> head /samples/hello.c  # Same — CWD stays at /
Lair:/> diff /docs/a /docs/b   # Both files resolved transparently
```

---

## ATA/IDE Disk Driver

### Configuration

| Property | Value |
| --- | --- |
| Mode | PIO with LBA48 addressing |
| Target | Primary master drive |
| Max addressable | 128 PB (LBA48) |

### I/O Ports

| Port | Name | Direction |
| --- | --- | --- |
| `0x1F0` | Data | R/W |
| `0x1F1` | Error / Features | R/W |
| `0x1F2` | Sector Count | W |
| `0x1F3` | LBA Low | W |
| `0x1F4` | LBA Mid | W |
| `0x1F5` | LBA High | W |
| `0x1F6` | Drive/Head | W |
| `0x1F7` | Command / Status | R/W |
| `0x3F6` | Control | W |

### Commands

| Command | Opcode | Description |
| --- | --- | --- |
| `ATA_CMD_READ` | `0x24` | READ SECTORS EXT (LBA48) |
| `ATA_CMD_WRITE` | `0x34` | WRITE SECTORS EXT (LBA48) |
| `ATA_CMD_IDENTIFY` | `0xEC` | IDENTIFY DEVICE |
| `ATA_CMD_FLUSH` | `0xE7` | FLUSH CACHE |

### Status Register Bits

| Bit | Mask | Name |
| --- | --- | --- |
| 7 | `0x80` | BSY (Busy) |
| 6 | `0x40` | DRDY (Device Ready) |
| 3 | `0x08` | DRQ (Data Request) |
| 0 | `0x01` | ERR (Error) |

### Key Routines (ATA)

| Routine | Parameters | Description |
| --- | --- | --- |
| `ata_read_sectors` | EAX=LBA, ECX=count, EDI=dest | Read sectors via LBA48 + `REP INSW` |
| `ata_write_sectors` | EAX=LBA, ECX=count, ESI=src | Write sectors via LBA48 + `REP OUTSW` |
| `ata_wait_ready` | — | Polls BSY bit with 0x100000 timeout |
| `ata_identify` | — | Reads 256 identify words, extracts total sectors |

### Security

`SYS_DISK_READ` and `SYS_DISK_WRITE` are denied to user programs — the syscall handler
checks `program_running` and returns -1 if a user program attempts raw disk access.

---

## VGA Text Mode

### Specifications (VGA)

| Property | Value |
| --- | --- |
| Base address | `0xB8000` |
| Mode | VGA Mode 3 (80×25 text) |
| Screen size | 80 columns × 25 rows |
| Framebuffer | 4000 bytes (2000 cells × 2 bytes) |
| Cell format | `[character byte] [attribute byte]` |

### Attribute Byte

```text
  7     6  5  4    3  2  1  0
┌─────┬──────────┬───────────┐
│Blink│Background│ Foreground│
└─────┴──────────┴───────────┘
```

### Color Values

| Value | Color | Value | Color |
| --- | --- | --- | --- |
| `0` | Black | `8` | Dark Gray |
| `1` | Blue | `9` | Light Blue |
| `2` | Green | `A` | Light Green |
| `3` | Cyan | `B` | Light Cyan |
| `4` | Red | `C` | Light Red |
| `5` | Magenta | `D` | Light Magenta |
| `6` | Brown | `E` | Yellow |
| `7` | Light Gray | `F` | White |

### Named Color Constants

| Constant | Value | Usage |
| --- | --- | --- |
| `COLOR_DEFAULT` | `0x07` | Light gray on black |
| `COLOR_HEADER` | `0x1F` | White on blue (banner) |
| `COLOR_ERROR` | `0x4F` | White on red |
| `COLOR_SUCCESS` | `0x2F` | White on green |
| `COLOR_PROMPT` | `0x0A` | Light green on black |
| `COLOR_INFO` | `0x0B` | Light cyan on black |
| `COLOR_EXEC` | `0x0E` | Yellow on black |
| `COLOR_BATCH` | `0x0D` | Light magenta on black |

### Cursor

- Software tracking: `vga_cursor_x` (column), `vga_cursor_y` (row)
- Hardware cursor: updated via ports `0x3D4`/`0x3D5` (register `0x0E` high byte,
  `0x0F` low byte)

### Scrolling

`vga_scroll` copies rows 1–24 to 0–23 via `REP MOVSD`, then clears the last row with
spaces in the current attribute color.

### Special Characters

| Code | Behavior |
| --- | --- |
| `0x0A` (LF) | Newline — advance to next row |
| `0x0D` (CR) | Carriage return — cursor to column 0 |
| `0x08` (BS) | Backspace — erase previous character |
| `0x09` (TAB) | Advance to next 8-column boundary |

---

## Keyboard Driver

### Hardware

| Port | Name |
| --- | --- |
| `0x60` | Keyboard data (scancode) |
| `0x64` | Keyboard status |

### Ring Buffer

| Property | Value |
| --- | --- |
| Buffer size | 256 bytes |
| Index variables | `kb_read_idx`, `kb_write_idx` (dwords) |
| Overflow behavior | Keystrokes silently dropped |

### IRQ 1 Handler Flow

1. Read scancode from port `0x60`
2. Check for modifier keys:
   - Left Shift (`0x2A`) / Right Shift (`0x36`) — set/clear `kb_shift`
   - Left Ctrl (`0x1D`) — set/clear `kb_ctrl`
3. Ignore break codes (bit 7 set, except modifiers)
4. Translate via `scancode_table` (128-byte US QWERTY layout)
5. Apply shift via `shift_table` if `kb_shift` is active
6. If Ctrl held: generate control code (AND `0x1F`); Ctrl+C = code 3
7. Store in ring buffer at `kb_write_idx`

### Special Key Codes

| Scancode | Key | Internal Code |
| --- | --- | --- |
| `0x48` | Up Arrow | `0x80` |
| `0x50` | Down Arrow | `0x81` |
| `0x4B` | Left Arrow | `0x82` |
| `0x4D` | Right Arrow | `0x83` |

### Input Routines

| Routine | Behavior |
| --- | --- |
| `kb_getchar` | Blocking — HLTs until key available |
| `kb_pollchar` | Non-blocking — returns 0 with ZF=1 if empty |

### Ctrl+C Handling

When `program_running = 1` and Ctrl+C is detected, the IRQ handler performs a hard
abort: resets `ESP` to `KERNEL_STACK` and jumps to `shell_main`.

---

## PIT Timer

### Configuration (PIT)

| Property | Value |
| --- | --- |
| Base frequency | 1,193,182 Hz |
| Target rate | 100 Hz |
| Divisor | 11,931 |
| Tick interval | 10 ms |

### Ports

| Port | Name |
| --- | --- |
| `0x40` | PIT Channel 0 data |
| `0x43` | PIT Command register |

### Init Sequence

1. Write `0x36` to port `0x43` (Channel 0, lobyte/hibyte, rate generator mode 2)
2. Write divisor low byte to port `0x40`
3. Write divisor high byte to port `0x40`

### Timer Tick

`irq_timer` (IRQ 0 / INT 0x20):

1. Increments `tick_count` (dword in BSS)
2. Sends EOI to PIC1

### Timing Conversions

- 1 tick = 10 ms
- 100 ticks = 1 second
- `SYS_GETTIME` returns raw `tick_count`
- `SYS_SLEEP(N)` waits N ticks in a HLT loop

---

## Serial Port

### Configuration (Serial)

| Property | Value |
| --- | --- |
| Port | COM1 (`0x3F8`) |
| LSR | `0x3FD` |
| Baud rate | 115,200 |
| Divisor | 1 |
| Format | 8N1 (8 data, no parity, 1 stop) |
| FIFO | Enabled |

### Init Sequence (Serial)

1. Disable interrupts (IER = 0)
2. Set DLAB, write divisor 1 (115200 baud)
3. LCR = `0x03` (8N1)
4. FCR = `0xC7` (enable FIFO)
5. MCR = `0x03` (RTS + DTR)
6. Set `serial_present = 1`

### I/O Routines

| Routine | Description |
| --- | --- |
| `serial_putchar` | Polls LSR bit 5 (THR empty), writes byte |
| `serial_getchar` | Polls LSR bit 0 (Data Ready), reads byte |

---

## PC Speaker

### Ports (Speaker)

| Port | Name |
| --- | --- |
| `0x42` | PIT Channel 2 data |
| `0x43` | PIT Command register |
| `0x61` | Speaker control |

### Frequency Calculation

```text
divisor = 1,193,182 / desired_frequency_hz
```

### Beep Sequence

1. Compute divisor from requested frequency
2. Write `0xB6` to port `0x43` (Channel 2, lobyte/hibyte, square wave mode 3)
3. Send divisor low/high to port `0x42`
4. Read port `0x61`, OR with `0x03` to enable speaker (bits 0 and 1)
5. Wait for requested duration (tick-based HLT loop)
6. `speaker_off`: AND port `0x61` with `0xFC` (clear bits 0 and 1)

---

## Process Execution

### Loading a Program

1. Shell parses command into program name (`prog_name_buf`, 256 bytes) and arguments
   (`program_args_buf`, 512 bytes)
2. Attempts to load file via `hbfs_read_file` to `PROGRAM_BASE` (0x200000)
3. If not found in current directory, searches PATH directories

### ELF vs Flat Binary Detection

The kernel checks for `ELF_MAGIC` (`0x464C457F`) at `PROGRAM_BASE`:

- **ELF:** `elf_load_program` validates ELF32 little-endian, iterates program headers,
  loads `PT_LOAD` segments (copies file data + zeroes BSS), returns entry point
- **Flat binary:** Entry point = `PROGRAM_BASE` directly

### Ring 3 Execution

1. Set `program_running = 1`, clear `ctrl_c_flag` and `program_exit_code`
2. Write **SYS_EXIT trampoline** at `PROGRAM_EXIT_ADDR` (0x2FFFF0):

   ```nasm
   mov eax, 0    ; SYS_EXIT
   int 0x80
   ```

   This catches programs that use `RET` instead of `SYS_EXIT`
3. Update TSS ESP0 to `KERNEL_STACK` (for Ring 0 transitions)
4. Set up Ring 3 stack at `PROGRAM_EXIT_ADDR - 4`, push trampoline as return address
5. Perform `IRETD` to Ring 3:
   - Push `USER_DS` (0x23) — SS3
   - Push ESP3
   - Push EFLAGS (with IF set)
   - Push `USER_CS` (0x1B) — CS3
   - Push entry point

### Return to Kernel

| Method | Trigger |
| --- | --- |
| `SYS_EXIT` (INT 0x80, EAX=0) | Normal exit — saves exit code, resets to shell |
| Ctrl+C | Hard abort — IRQ handler resets ESP, jumps to shell |
| CPU Exception | Fault handler prints info, recovers to shell |
| `RET` instruction | Hits trampoline → SYS_EXIT automatically |

---

## Syscall Interface

All syscalls are invoked via `INT 0x80`. Register conventions:

- **EAX** = syscall number
- **EBX, ECX, EDX, ESI, EDI** = arguments
- **EAX** = return value

### Complete Syscall Table

| # | Name | Args | Returns |
| --- | --- | --- | --- |
| 0 | `SYS_EXIT` | EBX=exit code | — |
| 1 | `SYS_PUTCHAR` | EBX=character | EAX=0 |
| 2 | `SYS_GETCHAR` | — | EAX=char (blocking) |
| 3 | `SYS_PRINT` | EBX=string ptr | EAX=0 |
| 4 | `SYS_READ_KEY` | — | EAX=key (0 if none, non-blocking) |
| 5 | `SYS_OPEN` | EBX=filename, ECX=mode (1=read, 2=write) | EAX=fd (-1 error) |
| 6 | `SYS_READ` | EBX=fd, ECX=buf, EDX=count | EAX=bytes read |
| 7 | `SYS_WRITE` | EBX=fd, ECX=buf, EDX=count | EAX=bytes written |
| 8 | `SYS_CLOSE` | EBX=fd | EAX=0 |
| 9 | `SYS_DELETE` | EBX=filename | EAX=0/-1 |
| 10 | `SYS_SEEK` | EBX=fd, ECX=offset | EAX=new position |
| 11 | `SYS_STAT` | EBX=filename | EAX=size (-1 not found), ECX=blocks |
| 12 | `SYS_MKDIR` | EBX=dirname | EAX=0/-1 |
| 13 | `SYS_READDIR` | EBX=name buf, ECX=index | EAX=type, ECX=size |
| 14 | `SYS_SETCURSOR` | EBX=X, ECX=Y | EAX=0 |
| 15 | `SYS_GETTIME` | — | EAX=tick_count |
| 16 | `SYS_SLEEP` | EBX=ticks | EAX=0 |
| 17 | `SYS_CLEAR` | — | EAX=0 |
| 18 | `SYS_SETCOLOR` | EBX=color byte | EAX=0 |
| 19 | `SYS_MALLOC` | EBX=size (rounded to 4 KB pages) | EAX=address (0=fail) |
| 20 | `SYS_FREE` | EBX=address, ECX=size | EAX=0 |
| 21 | `SYS_EXEC` | EBX=filename | EAX=0 |
| 22 | `SYS_DISK_READ` | EBX=LBA, ECX=count, EDX=buf | EAX=0/-1 (denied to Ring 3) |
| 23 | `SYS_DISK_WRITE` | EBX=LBA, ECX=count, EDX=buf | EAX=0/-1 (denied to Ring 3) |
| 24 | `SYS_BEEP` | EBX=freq (0=off), ECX=duration | EAX=0 |
| 25 | `SYS_DATE` | EBX=6-byte buf [s,m,h,d,mo,yr] | EAX=full year |
| 26 | `SYS_CHDIR` | EBX=dirname | EAX=0/-1 |
| 27 | `SYS_GETCWD` | EBX=dest buf | EAX=0 |
| 28 | `SYS_SERIAL` | EBX=string ptr | EAX=0 |
| 29 | `SYS_GETENV` | EBX=var name | EDI=value ptr |
| 30 | `SYS_FREAD` | EBX=filename, ECX=buf | EAX=bytes read (0=not found) |
| 31 | `SYS_FWRITE` | EBX=filename, ECX=buf, EDX=size | EAX=0/-1 |
| 32 | `SYS_GETARGS` | EBX=dest buf (512 bytes max) | EAX=arg string length |
| 33 | `SYS_SERIAL_IN` | — | EAX=char from serial port |

**Total: 34 syscalls (0–33).**

---

## Shell Architecture

### Command Dispatch

The shell reads input into `line_buffer` (256 bytes) and processes it through:

1. **Alias expansion** — checks first word against alias table
2. **Variable expansion** — expands `$VAR` references in echo
3. **Built-in command matching** — linear scan of `command_table` (38 entries + 6 aliases)
4. **External program loading** — current directory, then PATH search

### Built-in Commands (44 dispatched names)

The 38 unique commands with 6 aliases (`ls`→`dir`, `rm`→`del`, `mv`→`ren`,
`move`→`ren`, `cls`→`clear`, `type`→`cat`) produce 44 dispatched command names.

### Shell Line Editing

| Key | Action |
| --- | --- |
| Printable chars | Insert at cursor position |
| Backspace | Delete before cursor |
| Up/Down | Browse command history |
| Tab | Filename auto-complete |
| Ctrl+C | Cancel current input |
| Enter | Execute command |

---

## Environment Variables

### Storage

| Property | Value |
| --- | --- |
| Max variables | 16 (`ENV_MAX`) |
| Entry size | 128 bytes (`ENV_ENTRY_SIZE`) |
| Table size | 2048 bytes (2 KB) |
| Format | `NAME=value\0` (null-terminated) |

### Key Routines (Environment)

| Routine | Description |
| --- | --- |
| `env_set_str(ESI)` | Set variable from `"NAME=value"` string |
| `env_get(ESI)` | Get value pointer for name, returns EDI |
| `env_get_var(ESI, EDI)` | Copy value to destination buffer |
| `cmd_set` | Shell command: `set NAME VALUE` or `set` to list all |
| `cmd_unset` | Shell command: `unset NAME` |

### Variable Expansion

The `echo` command expands `$VAR` references:

```text
Lair:/> set user James
Lair:/> echo Hello $user
Hello James
```

---

## Alias System

### Storage (Aliases)

| Property | Value |
| --- | --- |
| Max aliases | 16 (`ALIAS_MAX`) |
| Name length | 32 bytes (`ALIAS_NAME_LEN`) |
| Command length | 224 bytes (`ALIAS_CMD_LEN`) |
| Entry size | 256 bytes (`ALIAS_ENTRY_SIZE`) |
| Table size | 4096 bytes (4 KB) |

### Expansion Flow

1. Before command dispatch, check if first word matches an alias name
2. If matched, copy the alias command to `alias_expand_buf` (256 bytes)
3. Append any remaining arguments from the original input
4. Re-parse the expanded command

### Built-in Aliases

The following are hardcoded in the command table (not user aliases):

| Alias | Expands To |
| --- | --- |
| `ls` | `dir` |
| `rm` | `del` |
| `mv` | `ren` |
| `move` | `ren` |
| `cls` | `clear` |
| `type` | `cat` |

---

## Command History

### Storage (History)

| Property | Value |
| --- | --- |
| Max entries | 8 (`HIST_MAX`) |
| Entry size | 256 bytes (`HIST_ENTRY_SIZE`) |
| Buffer size | 2048 bytes (2 KB) |
| Counter | `hist_count` (entries stored) |
| Browse index | `hist_browse` (current position) |

### Behavior

- **Saving:** When the buffer is full, entries 1–7 shift down to 0–6 (oldest discarded),
  new entry goes to position 7
- **Browsing:** Up arrow decrements `hist_browse`, Down arrow increments
- **Reset:** `hist_browse` is reset to `hist_count` on each new prompt

---

## Tab Completion

### Algorithm (Tab Completion)

1. Null-terminate current input
2. Load current directory entries via `hbfs_load_root_dir`
3. Scan all entries against `line_buffer` prefix
4. **Single match:** Complete the filename by appending remaining characters
5. **Multiple matches:** Print all matching filenames, then reprint the prompt
6. **No matches:** Do nothing

### Limitations

- Only matches filenames in the **current directory** (not PATH-aware)
- Does not complete built-in command names
- Does not complete directory names with trailing `/`
