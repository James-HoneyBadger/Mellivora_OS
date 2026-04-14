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
6. [Paging](#paging)
7. [HBFS Filesystem](#hbfs-filesystem)
8. [Directory Navigation & Stacking](#directory-navigation--stacking)
9. [PATH Search Mechanism](#path-search-mechanism)
10. [Path-Aware File I/O](#path-aware-file-io)
11. [ATA/IDE Disk Driver](#ataide-disk-driver)
12. [VGA Text Mode](#vga-text-mode)
13. [VBE/BGA Framebuffer](#vbebga-framebuffer)
14. [Keyboard Driver](#keyboard-driver)
15. [PS/2 Mouse Driver](#ps2-mouse-driver)
16. [PIT Timer](#pit-timer)
17. [Serial Port](#serial-port)
18. [PC Speaker](#pc-speaker)
19. [Preemptive Scheduler](#preemptive-scheduler)
20. [TCP/IP Networking Stack](#tcpip-networking-stack)
21. [Burrows Desktop Environment](#burrows-desktop-environment)
22. [Process Execution](#process-execution)
23. [Syscall Interface](#syscall-interface)
24. [Shell Architecture](#shell-architecture)
25. [Environment Variables](#environment-variables)
26. [Alias System](#alias-system)
27. [Command History](#command-history)
28. [Tab Completion](#tab-completion)
29. [Sound Blaster 16 Audio Driver](#sound-blaster-16-audio-driver)
30. [Inter-Process Communication (IPC)](#inter-process-communication-ipc)
31. [Screensavers](#screensavers)

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
4. Loads the kernel from disk starting at **LBA 33**, reading the generated
   `KERNEL_SECTORS` value from `kernel_sectors.inc` into low memory at `0x20000`
   (segment `0x2000`), in chunks of up to 64 sectors
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
   - PMM (using E820 map)
   - ATA/IDE disk detection
   - Serial port (COM1 at 115200 baud)
   - TSS (Ring 3 support)
   - Scheduler (task table, quantum)
   - IPC (pipes and shared memory)
   - Network stack (RTL8139, ARP, TCP/IP)
   - Paging (identity-map 128 MB + LFB)
   - PS/2 mouse driver
   - Sound Blaster 16 audio driver
   - VBE/BGA framebuffer detection
   - Burrows desktop initialization
   - Drain 8042 controller
   - Enable interrupts (`sti`)
   - Print system info banner
   - HBFS filesystem
2. Enters the interactive shell loop (`shell_main`)

### Disk Layout

| LBA Range | Size | Contents |
| --- | --- | --- |
| 0 | 512 B | MBR bootloader (boot.asm) |
| 1–32 | 16 KB | Stage 2 loader (stage2.asm) |
| 33+ | Variable | Kernel (kernel.asm + include modules) |
| 417 | 512 B | HBFS Superblock |
| 418–545 | 64 KB | Block allocation bitmap (16 blocks) |
| 546–801 | 128 KB | Root directory (32 blocks, 455 entries) |
| 802+ | — | Data blocks (4 KB each) |

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
           │ Kernel temp load buffer │  Variable size (copied to 1 MB)
0x0009FC00 ├─────────────────────────┤
           │ Kernel stack (grows ↓)  │  Top of conventional memory
0x000A0000 ├─────────────────────────┤
           │ Video memory / ROM      │
0x000B8000 │ VGA text framebuffer    │  4000 bytes (80×25×2)
0x00100000 ├─────────────────────────┤  ← 1 MB
           │ Kernel code + data      │  Variable (generated sector count)
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
| `KERNEL_SECTORS` | Generated at build time | Kernel disk size in sectors |
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

## Paging

Mellivora uses a flat identity-mapped paging scheme — virtual addresses equal physical
addresses for the first 128 MB. Paging is enabled primarily to support Ring 3 user-mode
execution and page-fault recovery.

### Page Directory & Tables

| Property | Value |
| --- | --- |
| Page directory address | `PAGE_DIR_ADDR` = `0x00380000` |
| Page table base | `PAGE_TABLE_BASE` = `0x00381000` |
| Page directory size | 4 KB (1024 dword entries) |
| Page tables | 32 tables × 4 KB = 128 KB total |
| Identity-mapped range | 0–128 MB (32 tables × 1024 pages × 4 KB) |

### Page Entry Flags

| Flag | Value | Description |
| --- | --- | --- |
| `PG_PRESENT` | `0x01` | Page is present in memory |
| `PG_WRITABLE` | `0x02` | Page is read/write |
| `PG_USER` | `0x04` | Page accessible from Ring 3 |
| `PG_WRITE_THROUGH` | `0x08` | Write-through caching |

### Initialization

1. Zero the page directory (1024 entries)
2. Build 32 page tables, each covering 4 MB (1024 × 4 KB pages)
3. Each entry is set to `(physical_addr) | PG_PRESENT | PG_WRITABLE | PG_USER`
4. Page directory entries point to the corresponding page table with the same flags
5. Load `PAGE_DIR_ADDR` into CR3
6. Set bit 31 (PG) in CR0 to enable paging

### `paging_map_page`

Maps a single virtual page to a physical address:

- Computes page directory index: `virtual >> 22`
- Computes page table index: `(virtual >> 12) & 0x3FF`
- Writes the physical address with flags into the correct page table entry

This routine is used to identity-map the VBE linear framebuffer (8 MB / 2048 pages)
after the LFB physical address is detected via PCI.

### Page Fault Handler (ISR 14)

When an invalid page access occurs:

1. Reads the faulting address from CR2
2. Stores the error code, faulting EIP, and faulting address in `pf_errcode`, `pf_eip`,
   `pf_addr`
3. Prints a diagnostic message with all three values
4. Recovers by jumping to `shell_main` rather than halting — this allows the system to
   remain usable after a program crash

---

## HBFS Filesystem

HBFS (HoneyBadger File System) is a simple block-based filesystem with flat directories,
a block allocation bitmap, and support for nested subdirectories.

### On-Disk Layout

| Region | LBA Start | LBA Count | Size | Description |
| --- | --- | --- | --- | --- |
| Superblock | 417 | 1 | 512 B | Filesystem metadata |
| Bitmap | 418 | 128 | 64 KB | Block allocation bitmap (16 blocks) |
| Root Directory | 546 | 256 | 128 KB | 32 blocks × 4 KB |
| Data Area | 802 | — | — | File/directory data blocks |

### Constants

| Constant | Value |
| --- | --- |
| `HBFS_MAGIC` | `0x48424653` (`'HBFS'`) |
| `HBFS_BLOCK_SIZE` | 4096 (4 KB) |
| `HBFS_SECTORS_PER_BLK` | 8 |
| `HBFS_MAX_FILENAME` | 252 characters |
| `HBFS_DIR_ENTRY_SIZE` | 288 bytes |
| `HBFS_ROOT_DIR_BLOCKS` | 32 |
| `HBFS_ROOT_DIR_SECTS` | 256 (32 × 8) |
| `HBFS_ROOT_DIR_SIZE` | 131,072 bytes |
| `HBFS_MAX_FILES` | 455 (131,072 / 288) |
| `HBFS_SUBDIR_BLOCKS` | 16 |
| `HBFS_SUBDIR_MAX_ENTRIES` | 224 (16 × 4096 / 288) |
| `HBFS_SUPERBLOCK_LBA` | 417 |
| `HBFS_BITMAP_START` | 418 |
| `HBFS_ROOT_DIR_START` | 546 |
| `HBFS_DATA_START` | 802 |

### Superblock Structure (512 bytes at LBA 417)

| Offset | Size | Field | Default |
| --- | --- | --- | --- |
| 0 | 4 | Magic (`'HBFS'`) | `0x48424653` |
| 4 | 4 | Version | 1 |
| 8 | 4 | Total blocks | 524,288 |
| 12 | 4 | Free blocks | 524,288 |
| 16 | 4 | Root directory LBA | 546 |
| 20 | 4 | Bitmap start LBA | 418 |
| 24 | 4 | Data start LBA | 802 |
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
| 268 | 4 | Created timestamp (packed RTC date/time) |
| 272 | 4 | Modified timestamp (packed RTC date/time) |
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
| 5 | Link | Symbolic link (target stored in data block) |

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

### Directory Caching

The kernel caches the most recently loaded directory to avoid redundant disk reads
when the same directory is accessed repeatedly (e.g., during PATH searches).

| Variable | Description |
| --- | --- |
| `dir_cache_lba` | LBA of the cached directory |
| `dir_cache_sects` | Sector count of the cached directory |

`hbfs_load_root_dir` checks whether the requested directory matches the cache tag.
On a hit, the disk read is skipped entirely. On a miss, the directory is read from
disk and the cache tags are updated.

### Timestamps

File timestamps use the RTC (Real-Time Clock) rather than PIT tick counts. When a
file is created or modified, `rtc_get_timestamp` reads the current date/time from
CMOS and packs it into a 32-bit value:

```text
Bits 31–25: Year (offset from 2000)
Bits 24–21: Month (1–12)
Bits 20–16: Day (1–31)
Bits 15–11: Hour (0–23)
Bits 10–5:  Minute (0–59)
Bits 4–0:   Second / 2 (0–29)
```

This is the same encoding as DOS/FAT timestamps, providing human-readable
file dates displayed by `dir -l` and `stat` as `YYYY-MM-DD HH:MM`.

### Symbolic Links

Files with type `FTYPE_LINK` (5) store the target path in their data block.
The `ln -s TARGET LINKNAME` shell command creates a symbolic link. The `stat`
command resolves and displays link targets.

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

## VBE/BGA Framebuffer

Mellivora supports a 32-bit graphical framebuffer via the Bochs Graphics Adapter (BGA)
interface, used by QEMU/Bochs and VirtualBox. This provides the display backend for the
Burrows desktop environment.

### BGA Register Interface

Configuration is performed through two I/O ports:

| Port | Name |
| --- | --- |
| `0x01CE` | BGA Index Register |
| `0x01CF` | BGA Data Register |

### BGA Register Indices

| Index | Name | Description |
| --- | --- | --- |
| 0 | `BGA_ID` | Identification — valid range `0xB0C0`–`0xB0C5` |
| 1 | `BGA_XRES` | Horizontal resolution |
| 2 | `BGA_YRES` | Vertical resolution |
| 3 | `BGA_BPP` | Bits per pixel |
| 4 | `BGA_ENABLE` | Enable/disable display |
| 5 | `BGA_BANK` | Bank number (unused with LFB) |
| 6 | `BGA_VIRT_W` | Virtual width |
| 7 | `BGA_VIRT_H` | Virtual height |
| 8 | `BGA_X_OFF` | X offset |
| 9 | `BGA_Y_OFF` | Y offset |
| 10 | `BGA_FB_ADDR` | Framebuffer address |

### Mode Setting

1. Write `VBE_DISPI_DISABLED` (0x00) to `BGA_ENABLE`
2. Set `BGA_XRES` = 640, `BGA_YRES` = 480, `BGA_BPP` = 32
3. Write `VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED` (0x41) to `BGA_ENABLE`

### Linear Framebuffer (LFB)

The LFB address is detected in priority order:

1. PCI BAR 0 of the VGA device (Bus 0, Device 2, Function 0) — config address `0x80001010`
2. BGA register `INDEX_FB_ADDR` (index 10)
3. Fallback constant `VBE_LFB_FALLBACK` = `0xFD000000`

Once detected, the LFB region (8 MB / 2048 pages) is identity-mapped via `paging_map_page`.

### Pixel Addressing

```text
pixel_offset = y * pitch + x * Bpp + lfb_addr
```

Where `pitch` = width × bytes-per-pixel (2560 for 640×32bpp) and `Bpp` = bpp / 8 (4).

### Drawing Primitives

| Routine | Parameters | Description |
| --- | --- | --- |
| `vbe_putpixel` | x, y, color | Plot a single 32-bit pixel |
| `vbe_fill_rect` | x, y, w, h, color | Filled rectangle |
| `vbe_clear` | color | Fill entire screen |

### State Variables

| Variable | Type | Description |
| --- | --- | --- |
| `vbe_available` | byte | BGA detected on this hardware |
| `vbe_active` | byte | Currently in graphical mode |
| `vbe_lfb_addr` | dword | Physical/virtual LFB address |
| `vbe_width` | dword | Screen width in pixels (640) |
| `vbe_height` | dword | Screen height in pixels (480) |
| `vbe_bpp` | dword | Bits per pixel (32) |
| `vbe_Bpp` | dword | Bytes per pixel (4) |
| `vbe_pitch` | dword | Bytes per scanline (2560) |

### SYS_FRAMEBUF (Syscall 37)

| Sub-function | EBX | Action |
| --- | --- | --- |
| 0 | Get info | Returns EAX=LFB addr, EBX=width, ECX=height, EDX=bpp |
| 1 | Set mode | Switch to 640×480×32 VBE mode |
| 2 | Restore text | Return to 80×25 VGA text mode |

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

## PS/2 Mouse Driver

Mellivora includes a PS/2 mouse driver that provides cursor position and button state
to user programs and the Burrows desktop environment.

### Hardware Setup

| Property | Value |
| --- | --- |
| IRQ | IRQ12 → INT `0x2C` (PIC2, line 4) |
| Controller ports | `0x60` (data), `0x64` (command/status) |

### Initialization Sequence

1. Send `0xA8` to port `0x64` — enable auxiliary (mouse) port
2. Read command byte: send `0x20` to `0x64`, read from `0x60`
3. Set bit 1 (IRQ12 enable), write back via `0x60`/`0x64`
4. Send `0xD4` to `0x64` (route next byte to mouse), then `0xF6` (set defaults) to `0x60`
5. Send `0xD4`, then `0xF4` (enable data reporting) to `0x60`
6. Unmask IRQ12: clear bit 4 on PIC2 slave mask and bit 2 on PIC1 (cascade)

### Packet Format (3 Bytes)

| Byte | Contents |
| --- | --- |
| 0 | Flags: bits 0–2 = buttons (left/right/middle), bit 3 = always 1 (sync), bit 4 = X sign, bit 5 = Y sign |
| 1 | Delta-X (signed, relative movement) |
| 2 | Delta-Y (signed, relative movement, PS/2 Y-axis inverted) |

The handler validates packets by checking bit 3 of byte 0 — if not set, the packet is
discarded and the byte counter resets. X/Y deltas are sign-extended using bits 4/5 of
byte 0. The Y axis is negated (`neg eax`) to convert from PS/2 convention (up-positive)
to screen coordinates (down-positive).

### Position Tracking

| Variable | Type | Description |
| --- | --- | --- |
| `mouse_x` | dword | Current X position, clamped to `[0..mouse_max_x]` |
| `mouse_y` | dword | Current Y position, clamped to `[0..mouse_max_y]` |
| `mouse_buttons` | byte | Button state (bit 0=left, 1=right, 2=middle) |
| `mouse_present` | byte | Set to 1 if mouse detected during init |
| `mouse_pkt_idx` | dword | Current byte index within 3-byte packet |
| `mouse_packet` | 4 bytes | Packet assembly buffer |

### Display Modes

| Mode | `mouse_max_x` | `mouse_max_y` | `mouse_scale` |
| --- | --- | --- | --- |
| Text (80×25) | 79 | 24 | 1 |
| GUI (640×480) | 639 | 479 | 2 |

### SYS_MOUSE (Syscall 36)

Returns the current mouse state:

- **EAX** = X position
- **EBX** = Y position
- **ECX** = Button state

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

## Preemptive Scheduler

Mellivora implements a preemptive round-robin scheduler supporting up to 16 concurrent
tasks with Ring 3 isolation. The scheduler is driven by the PIT timer interrupt and
maintains per-task kernel stacks for clean context switching.

### Configuration

| Property | Value |
| --- | --- |
| `MAX_TASKS` | 16 |
| `SCHED_QUANTUM` | 10 PIT ticks (100 ms at 100 Hz) |
| `TCB_SIZE` | 32 bytes per entry |
| Kernel stack | 4 KB page per task (allocated via PMM) |

### Task States

| Constant | Value | Meaning |
| --- | --- | --- |
| `TASK_FREE` | 0 | Slot is unused |
| `TASK_READY` | 1 | Runnable, waiting for CPU |
| `TASK_RUNNING` | 2 | Currently executing |

### Task Control Block (TCB)

Each 32-byte TCB contains:

| Field | Offset | Type | Description |
| --- | --- | --- | --- |
| `TCB_ESP` | 0 | dword | Saved kernel-mode stack pointer |
| `TCB_STATE` | 4 | dword | Task state (FREE/READY/RUNNING) |
| `TCB_KSTACK` | 8 | dword | Top of kernel stack (for TSS ESP0) |
| `TCB_USTACK` | 12 | dword | User-mode stack top |
| `TCB_ENTRY` | 16 | dword | Program entry point |
| `TCB_PID` | 20 | dword | Task ID |
| `TCB_PAD1` | 24 | dword | Reserved |
| `TCB_PAD2` | 28 | dword | Reserved |

### Ring 3 Selectors

| Selector | Value | Description |
| --- | --- | --- |
| `USER_CS` | `0x1B` | User code segment (GDT entry 3, RPL 3) |
| `USER_DS` | `0x23` | User data segment (GDT entry 4, RPL 3) |
| `TSS_SEL` | `0x28` | Task State Segment selector |

### Task Creation

When a new task is spawned:

1. Find a `TASK_FREE` slot in the task table
2. Allocate a 4 KB kernel stack page via `pmm_alloc_page`
3. Build an initial stack frame (52 bytes) at the top of the kernel stack:
   - `PUSHAD` frame (32 bytes) — all general registers zeroed
   - `IRETD` frame (20 bytes) — SS3 (`USER_DS`), ESP3 (user stack), EFLAGS (`0x200` =
     interrupts enabled), CS3 (`USER_CS`), EIP (program entry point)
4. Set TCB fields: ESP to the constructed frame, state to `TASK_READY`

### Scheduling Algorithm

The scheduler uses round-robin scanning:

1. Starting from `task_current + 1`, scan forward through the task table
2. Wrap around at `MAX_TASKS`
3. Select the first `TASK_READY` slot
4. If no other task is ready, continue running the current task

### Preemption

The PIT IRQ0 handler (`irq_timer`) drives preemption:

1. Increment `sched_tick`
2. Check if `sched_tick >= SCHED_QUANTUM` (10 ticks = 100 ms)
3. **Only preempt Ring 3 code** — checks the CS RPL field on the interrupt stack frame;
   if the interrupted code was in Ring 0, skip preemption (avoids corrupting kernel state)
4. Save the current task's context (all registers on kernel stack)
5. Update TSS ESP0 to the new task's kernel stack top: `mov [tss_struct + 4], kstack_top`
6. Restore the new task's saved context and `IRETD`

### Cooperative Yield

`SYS_YIELD` (syscall 35) allows a task to voluntarily give up its time slice. It uses
the same round-robin selection logic as the preemptive path.

### State Variables

| Variable | Description |
| --- | --- |
| `task_table` | Array of `MAX_TASKS` × `TCB_SIZE` bytes |
| `task_count` | Number of active tasks |
| `task_current` | Index of the currently running task |
| `sched_tick` | Ticks since last context switch |

---

## TCP/IP Networking Stack

Mellivora includes a full TCP/IP networking stack (~3,800 lines) supporting Ethernet,
ARP, IPv4, ICMP, UDP, TCP, DHCP, and DNS. The implementation is contained entirely in
`kernel/net.inc`.

### RTL8139 NIC Driver

The network driver targets the Realtek RTL8139 Fast Ethernet controller, which is the
default NIC in QEMU's user-mode networking.

#### PCI Detection

| Property | Value |
| --- | --- |
| Vendor ID | `0x10EC` (Realtek) |
| Device ID | `0x8139` (RTL8139) |
| Config ports | `PCI_CONFIG_ADDR` = `0x0CF8`, `PCI_CONFIG_DATA` = `0x0CFC` |

The driver scans PCI buses 0–7, devices 0–31, function 0, reading vendor/device IDs.
When found, it reads BAR0 for the I/O base and the interrupt line for the IRQ number.

#### Register Map

| Offset | Name | Description |
| --- | --- | --- |
| `0x00` | MAC0 | MAC address bytes 0–3 |
| `0x04` | MAC4 | MAC address bytes 4–5 |
| `0x10`–`0x1C` | TxStatus0–3 | TX descriptor status (4 descriptors) |
| `0x20`–`0x2C` | TxAddr0–3 | TX descriptor buffer addresses |
| `0x30` | RxBuf | RX ring buffer physical address |
| `0x37` | CMD | Command register (Reset/RxEn/TxEn) |
| `0x38` | CAPR | Current Address of Packet Read |
| `0x3A` | CBR | Current Buffer address (write pointer) |
| `0x3C` | IMR | Interrupt Mask Register |
| `0x3E` | ISR | Interrupt Status Register |
| `0x40` | TxConfig | TX configuration |
| `0x44` | RxConfig | RX configuration |
| `0x52` | Config1 | Power management |

#### Buffer Layout

| Buffer | Size | Description |
| --- | --- | --- |
| RX ring | 8208 bytes (8 KB + 16) | 3 PMM pages, wrapping ring buffer |
| TX buffers | 1536 bytes × 4 | One per descriptor, round-robin |
| Packet buffer | 1536 bytes (1 page) | Temporary assembly area |

#### Initialization

1. Reset the chip (write `0x10` to CMD, poll until clear)
2. Read MAC address from ports `0x00`–`0x05`
3. Set RX buffer address (physical pointer to 8208-byte ring)
4. Set TX buffer addresses for all 4 descriptors
5. Configure IMR for ROK (`0x0001`) and TOK (`0x0004`)
6. Set RxConfig to `0x8F` (wrap + accept broadcast/multicast/physical)
7. Enable receiver and transmitter (write `0x0C` to CMD)

### Protocol Stack

#### Ethernet Frame

```text
[dst MAC 6B][src MAC 6B][EtherType 2B][payload...][FCS 4B]
```

EtherTypes: `0x0800` = IPv4, `0x0806` = ARP.

#### ARP (Address Resolution Protocol)

| Property | Value |
| --- | --- |
| Cache size | 16 entries × 12 bytes |
| Entry format | IP (4B, offset 0), MAC (6B, offset 4), State (1B, offset 10) |
| States | 0 = free, 1 = pending, 2 = resolved |

ARP resolution: if the target IP is outside the local subnet, ARP resolves the gateway
MAC instead. ARP requests are broadcast to `FF:FF:FF:FF:FF:FF`. Replies update the cache
and trigger any pending TCP/UDP retransmissions.

#### IPv4

| Field | Value |
| --- | --- |
| Header length | 20 bytes (no options) |
| TTL | 64 |
| Protocols | ICMP (1), TCP (6), UDP (17) |

Builds standard IPv4 headers with checksum. Validates incoming packet checksums and
dispatches by protocol number to ICMP, TCP, or UDP handlers.

#### ICMP

Handles echo request/reply (ping). `SYS_PING` (syscall 48) sends an ICMP echo request
and waits for the reply with a configurable timeout.

#### UDP

Stateless datagram protocol used for DHCP and DNS. No connection tracking — packets are
dispatched to matching sockets by destination port.

#### TCP

Full TCP state machine with connection setup, data transfer, and teardown:

| State | Value | Description |
| --- | --- | --- |
| `CLOSED` | 0 | Initial/final state |
| `LISTEN` | 1 | Waiting for incoming SYN |
| `SYN_SENT` | 2 | SYN sent, awaiting SYN-ACK |
| `SYN_RCVD` | 3 | SYN received, SYN-ACK sent |
| `ESTABLISHED` | 4 | Connection open, data transfer |
| `FIN_WAIT_1` | 5 | FIN sent, awaiting ACK |
| `FIN_WAIT_2` | 6 | FIN ACKed, awaiting peer FIN |
| `CLOSE_WAIT` | 7 | Peer sent FIN, awaiting local close |
| `CLOSING` | 8 | Both sides sent FIN simultaneously |
| `LAST_ACK` | 9 | Sent FIN after CLOSE_WAIT, awaiting ACK |
| `TIME_WAIT` | 10 | Waiting before final close |

TCP flag constants: `FIN=0x01`, `SYN=0x02`, `RST=0x04`, `PSH=0x08`, `ACK=0x10`,
`URG=0x20`.

### Socket Table

| Property | Value |
| --- | --- |
| `MAX_SOCKETS` | 8 |
| `SOCK_SIZE` | 128 bytes per entry |
| `SOCK_BUF_SIZE` | 4096 bytes per recv/send buffer |

#### Socket Structure

| Field | Offset | Type | Description |
| --- | --- | --- | --- |
| `SOCK_TYPE` | 0 | dword | 0=free, 1=TCP, 2=UDP |
| `SOCK_STATE` | 4 | dword | TCP state (see table above) |
| `SOCK_LOCAL_PORT` | 8 | word | Local port number |
| `SOCK_REMOTE_PORT` | 10 | word | Remote port number |
| `SOCK_REMOTE_IP` | 12 | dword | Remote IP (network byte order) |
| `SOCK_SEQ` | 16 | dword | TCP send sequence number |
| `SOCK_ACK` | 20 | dword | TCP expected receive sequence |
| `SOCK_RECV_BUF` | 24 | dword | Pointer to 4 KB receive buffer |
| `SOCK_RECV_LEN` | 28 | dword | Bytes available in receive buffer |
| `SOCK_SEND_BUF` | 32 | dword | Pointer to 4 KB send buffer |
| `SOCK_SEND_LEN` | 36 | dword | Bytes pending in send buffer |
| `SOCK_FLAGS` | 40 | dword | Miscellaneous flags |
| `SOCK_TIMER` | 44 | dword | Timeout tick counter |
| `SOCK_PENDING` | 48 | dword | TCP flags to send from non-ISR context |

### ISR TX Re-Entrance Pattern (SOCK_PENDING)

The RTL8139 RX interrupt fires when a packet arrives. If the TCP handler needs to send a
response (e.g., SYN-ACK, ACK), it cannot safely call the TX path from within the ISR
because the TX registers may be in use. Instead:

1. The ISR sets `SOCK_PENDING` with the TCP flags to send (e.g., `SYN | ACK`)
2. The ISR returns
3. The next non-ISR `sys_recv` or `sys_connect` poll loop checks `SOCK_PENDING`
4. If non-zero, it calls the TX path to send the pending flags, then clears the field

This prevents TX register corruption and ensures clean handshake completion.

### DHCP

| Property | Value |
| --- | --- |
| Client port | 68 |
| Server port | 67 |
| Magic cookie | `0x63538263` |
| Timeout | ~500 ticks (~5 seconds) |

Four-phase sequence: DISCOVER → OFFER → REQUEST → ACK. Extracts IP address, subnet mask,
gateway, and DNS server from DHCP options. Configures the network stack with the assigned
parameters.

### DNS

| Property | Value |
| --- | --- |
| Port | 53 (UDP) |
| Ephemeral port start | 49152 |
| Query type | A record (IPv4 address) |

`SYS_DNS` (syscall 46) takes a hostname string and returns the resolved IPv4 address.
Uses the DHCP-assigned DNS server. Encodes the query in DNS wire format (length-prefixed
labels) and parses the response to extract the first A record.

### Default Network Configuration (QEMU)

| Parameter | Value |
| --- | --- |
| IP address | 10.0.2.15 |
| Subnet mask | 255.255.255.0 |
| Gateway | 10.0.2.2 |
| DNS server | 10.0.2.3 |

These defaults are used as fallbacks if DHCP does not complete.

### Networking Syscalls

| # | Name | Args | Returns |
| --- | --- | --- | --- |
| 39 | `SYS_SOCKET` | EBX=type (1=TCP, 2=UDP) | EAX=socket fd (-1 error) |
| 40 | `SYS_CONNECT` | EBX=fd, ECX=IP, EDX=port | EAX=0/-1 |
| 41 | `SYS_SEND` | EBX=fd, ECX=buf, EDX=len | EAX=bytes sent |
| 42 | `SYS_RECV` | EBX=fd, ECX=buf, EDX=max | EAX=bytes received |
| 43 | `SYS_BIND` | EBX=fd, ECX=port | EAX=0/-1 |
| 44 | `SYS_LISTEN` | EBX=fd | EAX=0/-1 |
| 45 | `SYS_ACCEPT` | EBX=fd | EAX=new fd (-1 error) |
| 46 | `SYS_DNS` | EBX=hostname | EAX=IP (0=fail) |
| 47 | `SYS_SOCKCLOSE` | EBX=fd | EAX=0 |
| 48 | `SYS_PING` | EBX=IP, ECX=timeout | EAX=RTT ms (-1=timeout) |

---

## Burrows Desktop Environment

Burrows is Mellivora's graphical desktop environment, built on the VBE framebuffer and
PS/2 mouse driver. It provides a windowed GUI with a taskbar, application menu, and
theme support.

### Display

| Property | Value |
| --- | --- |
| Resolution | 640×480×32bpp |
| Back buffer | `GUI_BACKBUF` = `0x02000000` |
| Pitch | 2560 bytes per scanline |
| Font | 8×16 bitmap, characters 32–126 |
| Compositing | Double-buffered (draw to back buffer, flip to LFB) |

### Window Manager

| Property | Value |
| --- | --- |
| `MAX_WINDOWS` | 16 |
| `WIN_STRUCT_SIZE` | 96 bytes per window |

#### Window Structure

| Field | Offset | Size | Description |
| --- | --- | --- | --- |
| `WIN_FLAGS` | 0 | dword | Bit 0 = active, Bit 1 = visible |
| `WIN_X` | 4 | dword | Content area X position |
| `WIN_Y` | 8 | dword | Content area Y position |
| `WIN_W` | 12 | dword | Content area width |
| `WIN_H` | 16 | dword | Content area height |
| `WIN_Z` | 20 | dword | Z-order (higher = on top) |
| `WIN_TITLE` | 24 | 64 B | Null-terminated title string |
| `WIN_APP_ID` | 88 | dword | Owning application ID (0 = desktop) |

#### Window Decorations

| Element | Value |
| --- | --- |
| `TITLEBAR_H` | 22 pixels |
| `BORDER_W` | 2 pixels |
| `CLOSE_BTN_SIZE` | 16 pixels |

Windows are drawn with a title bar, border, and close button. The close button is rendered
as an "×" glyph in the top-right corner of the title bar.

### Taskbar

| Property | Value |
| --- | --- |
| `TASKBAR_H` | 28 pixels |
| `TASKBAR_Y` | 452 (bottom of screen minus taskbar height) |
| Position | Full-width bar at screen bottom |

The taskbar displays a "Menu" button on the left and buttons for each open window. Clicking
a window's taskbar button brings it to focus.

### Application Menu

| Property | Value |
| --- | --- |
| `GUI_MENU_X` | 4 |
| `GUI_MENU_W` | 160 pixels |
| `GUI_MENU_ITEM_H` | 24 pixels |
| `GUI_MENU_ITEMS` | 10 |
| Built-in applets | About, Clock, Settings |

### Event System

| Event | Value | Description |
| --- | --- | --- |
| `EVT_NONE` | 0 | No event pending |
| `EVT_MOUSE_CLICK` | 1 | Mouse button pressed |
| `EVT_MOUSE_MOVE` | 2 | Mouse position changed |
| `EVT_KEY_PRESS` | 3 | Keyboard key pressed |
| `EVT_CLOSE` | 4 | Window close requested |

### Theme System

Each theme is 48 bytes (12 colors × 4 bytes each):

| Offset | Field | Description |
| --- | --- | --- |
| 0 | `DESKTOP_BG` | Desktop background color |
| 4 | `TASKBAR_BG` | Taskbar background |
| 8 | `TITLE_ACTIVE` | Active window title bar |
| 12 | `TITLE_INACTIVE` | Inactive window title bar |
| 16 | `TITLE_TEXT` | Title bar text color |
| 20 | `WINDOW_BG` | Window content background |
| 24 | `WINDOW_BORDER` | Window border color |
| 28 | `MENU_BG` | Menu background |
| 32 | `MENU_TEXT` | Menu text color |
| 36 | `MENU_HIGHLIGHT` | Menu hover highlight |
| 40 | `BUTTON_BG` | Button background |
| 44 | `ACCENT` | Accent / highlight color |

#### Preset Themes

| Theme | Desktop BG | Taskbar BG | Accent |
| --- | --- | --- | --- |
| Blue (default) | `0x285078` | `0x303030` | `0x3060A0` |
| Dark | `0x202020` | `0x181818` | `0x608060` |
| Light | `0xB0C4DE` | `0xE0E0E0` | `0x4080C0` |

### SYS_GUI (Syscall 38) Sub-Functions

| Sub | Name | Parameters | Description |
| --- | --- | --- | --- |
| 0 | `GUI_CREATE_WINDOW` | ECX=x\|y (packed), EDX=w\|h, ESI=title | Create window → EAX=win_id |
| 1 | `GUI_DESTROY_WINDOW` | ECX=win_id | Destroy a window |
| 2 | `GUI_FILL_RECT` | ECX=win_id, EDX=x\|y, ESI=w\|h, EDI=color | Fill rectangle in window |
| 3 | `GUI_DRAW_TEXT` | ECX=win_id, EDX=x\|y, ESI=text, EDI=color | Draw text in window |
| 4 | `GUI_POLL_EVENT` | — | Returns event in EAX/EBX/ECX |
| 5 | `GUI_GET_THEME` | — | Get current theme data |
| 6 | `GUI_SET_THEME` | ECX=theme index | Switch theme |
| 7 | `GUI_DRAW_PIXEL` | ECX=win_id, EDX=x\|y, ESI=color | Plot single pixel |
| 8 | `GUI_DRAW_LINE` | ECX=win_id, EDX=x1\|y1, ESI=x2\|y2, EDI=color | Draw line |
| 9 | `GUI_COMPOSE` | — | Trigger desktop compositing |
| 10 | `GUI_FLIP` | — | Flip back buffer to screen |
| 11 | `GUI_DESKTOP` | — | Enter desktop mode |
| 20 | `GUI_DRAW_BUTTON` | ECX=win_id, EDX=x\|y, ESI=w\|h, EDI=label | Draw button widget |
| 21 | `GUI_DRAW_CHECKBOX` | ECX=win_id, EDX=x\|y, ESI=checked, EDI=label | Draw checkbox widget |
| 22 | `GUI_DRAW_PROGRESS` | ECX=win_id, EDX=x\|y, ESI=w\|value, EDI=color | Draw progress bar |
| 23 | `GUI_DRAW_TEXTBOX` | ECX=win_id, EDX=x\|y, ESI=w\|h, EDI=text | Draw text input box |
| 24 | `GUI_DRAW_LISTBOX` | ECX=win_id, EDX=x\|y, ESI=w\|h, EDI=items | Draw list box widget |
| 25 | `GUI_DRAW_LABEL` | ECX=win_id, EDX=x\|y, ESI=text, EDI=color | Draw label widget |
| 26 | `GUI_DRAW_RECT` | ECX=win_id, EDX=x\|y, ESI=w\|h, EDI=color | Draw rectangle outline |

Sub-functions 12–19 are reserved. Total: 20 sub-functions (0–11 core, 20–26 widgets).

### State Variables

| Variable | Description |
| --- | --- |
| `gui_desktop_active` | Desktop mode is running |
| `gui_exit_flag` | Exit requested |
| `gui_menu_open` | Application menu is visible |
| `gui_dragging` | Window drag in progress |
| `gui_focused_win` | Index of focused window |
| `gui_win_count` | Number of active windows |
| `gui_next_z` | Next z-order value to assign |
| `gui_prev_mouse_btn` | Previous mouse button state (for edge detection) |
| `gui_win_table` | Window structure array (`MAX_WINDOWS` × `WIN_STRUCT_SIZE`) |
| `gui_theme` | Current theme data (48 bytes) |

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
| 34 | `SYS_STDIN_READ` | EBX=buf, ECX=max | EAX=bytes read (line-buffered) |
| 35 | `SYS_YIELD` | — | EAX=0 (cooperative task yield) |
| 36 | `SYS_MOUSE` | — | EAX=x, EBX=y, ECX=buttons |
| 37 | `SYS_FRAMEBUF` | EBX=sub-function | (see VBE/BGA section) |
| 38 | `SYS_GUI` | EBX=sub-function | (see Burrows section, 20 sub-functions) |
| 39 | `SYS_SOCKET` | EBX=type (1=TCP, 2=UDP) | EAX=socket fd (-1 error) |
| 40 | `SYS_CONNECT` | EBX=fd, ECX=IP, EDX=port | EAX=0/-1 |
| 41 | `SYS_SEND` | EBX=fd, ECX=buf, EDX=len | EAX=bytes sent |
| 42 | `SYS_RECV` | EBX=fd, ECX=buf, EDX=max | EAX=bytes received |
| 43 | `SYS_BIND` | EBX=fd, ECX=port | EAX=0/-1 |
| 44 | `SYS_LISTEN` | EBX=fd | EAX=0/-1 |
| 45 | `SYS_ACCEPT` | EBX=fd | EAX=new fd (-1 error) |
| 46 | `SYS_DNS` | EBX=hostname | EAX=IP address (0=fail) |
| 47 | `SYS_SOCKCLOSE` | EBX=fd | EAX=0 |
| 48 | `SYS_PING` | EBX=IP, ECX=timeout | EAX=RTT ms (-1=timeout) |
| 49 | `SYS_SETDATE` | EBX=6-byte buf, ECX=century | EAX=0 |
| 50 | `SYS_AUDIO_PLAY` | EBX=buf, ECX=len, EDX=format | EAX=0/-1 |
| 51 | `SYS_AUDIO_STOP` | — | EAX=0 |
| 52 | `SYS_AUDIO_STATUS` | — | EAX=state, EBX=present |
| 53 | `SYS_KILL` | EBX=pid | EAX=0/-1 |
| 54 | `SYS_GETPID` | — | EAX=current pid |
| 55 | `SYS_CLIPBOARD_COPY` | EBX=buf, ECX=len | EAX=0 |
| 56 | `SYS_CLIPBOARD_PASTE` | EBX=buf, ECX=max | EAX=bytes pasted |
| 57 | `SYS_NOTIFY` | EBX=text, EDX=color | EAX=0 |
| 58 | `SYS_FILE_OPEN_DLG` | EBX=title, EDX=filter | EAX=1/0, ECX=filename |
| 59 | `SYS_FILE_SAVE_DLG` | EBX=title, EDX=filter | EAX=1/0, ECX=filename |
| 60 | `SYS_PIPE_CREATE` | — | EAX=pipe_id (-1 error) |
| 61 | `SYS_PIPE_WRITE` | EBX=id, ECX=buf, EDX=len | EAX=bytes written |
| 62 | `SYS_PIPE_READ` | EBX=id, ECX=buf, EDX=max | EAX=bytes read |
| 63 | `SYS_PIPE_CLOSE` | EBX=id | EAX=0 |
| 64 | `SYS_SHMGET` | EBX=key, ECX=size | EAX=shm_id (-1 error) |
| 65 | `SYS_SHMADDR` | EBX=shm_id | EAX=pointer |
| 66 | `SYS_PROCLIST` | EBX=slot (0–15), ECX=buf (16B) | EAX=0/-1 |
| 67 | `SYS_MEMINFO` | — | EAX=free pages, EBX=total |

**Total: 68 syscalls (0–67).**

---

## Shell Architecture

### Command Dispatch

The shell reads input into `line_buffer` (256 bytes) and processes it through:

1. **Alias expansion** — checks first word against alias table
2. **Variable expansion** — expands `$VAR` references in echo
3. **Built-in command matching** — linear scan of `command_table` (58 commands + 6 aliases)
4. **External program loading** — current directory, then PATH search

### Built-in Commands (64 dispatched names)

The 58 unique commands with 6 aliases (`ls`→`dir`, `rm`→`del`, `mv`→`ren`,
`move`→`ren`, `cls`→`clear`, `type`→`cat`) produce 64 dispatched command names.

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

---

## Sound Blaster 16 Audio Driver

Mellivora includes a Sound Blaster 16 driver for ISA DMA audio playback.

### Configuration (SB16)

| Property | Value |
| --- | --- |
| Base port | `SB16_BASE` = `0x220` |
| IRQ | 5 (INT vector `0x25`) |
| 8-bit DMA channel | 1 |
| 16-bit DMA channel | 5 |
| DSP version required | ≥ 4.00 |

### Initialization (sb16_init)

1. Reset DSP: write `0x01` to port `0x226`, delay, write `0x00`
2. Read `0x22A` and verify `0xAA` (ready signal)
3. Get DSP version: send command `0xE1`, read major/minor
4. If version < 4.00, mark as unavailable
5. Set `sb16_present = 1` and install IRQ5 handler

### Playback States

| Constant | Value | Meaning |
| --- | --- | --- |
| `SB_STATE_IDLE` | 0 | No playback |
| `SB_STATE_PLAYING` | 1 | Audio playing |
| `SB_STATE_DONE` | 2 | Playback finished |

### Format Encoding

The `EDX` format parameter for `SYS_AUDIO_PLAY` encodes:

| Bits | Field |
| --- | --- |
| 0–15 | Sample rate in Hz |
| 16 | `SB_FMT_16BIT` — 16-bit samples |
| 17 | `SB_FMT_STEREO` — stereo |
| 18 | `SB_FMT_SIGNED` — signed samples |

### Audio Syscalls

| # | Name | Args | Returns |
| --- | --- | --- | --- |
| 50 | `SYS_AUDIO_PLAY` | EBX=buffer, ECX=length, EDX=format | EAX=0/-1 |
| 51 | `SYS_AUDIO_STOP` | — | EAX=0 |
| 52 | `SYS_AUDIO_STATUS` | — | EAX=state, EBX=present |

---

## Inter-Process Communication (IPC)

Mellivora provides pipes and shared memory regions for communication between tasks.

### Pipes

| Property | Value |
| --- | --- |
| `IPC_MAX_PIPES` | 8 |
| `IPC_PIPE_SIZE` | 4096 bytes (4 KB per pipe) |
| `PIPE_STRUCT_SIZE` | 4112 bytes (16 header + 4096 data) |

Each pipe structure contains a circular buffer with read/write offsets and a
byte count. Writes block (return 0) when the buffer is full; reads block when
empty.

### Shared Memory

| Property | Value |
| --- | --- |
| `IPC_MAX_SHM` | 4 |
| `IPC_SHM_SIZE` | 4096 bytes (4 KB per region) |
| `SHM_STRUCT_SIZE` | 4108 bytes (12 header + 4096 data) |

Shared memory regions are identified by an integer key. `SYS_SHMGET` creates
or retrieves a region by key, and `SYS_SHMADDR` returns the data pointer.

### IPC Syscalls

| # | Name | Args | Returns |
| --- | --- | --- | --- |
| 60 | `SYS_PIPE_CREATE` | — | EAX=pipe_id (-1 error) |
| 61 | `SYS_PIPE_WRITE` | EBX=id, ECX=buf, EDX=len | EAX=bytes written |
| 62 | `SYS_PIPE_READ` | EBX=id, ECX=buf, EDX=max | EAX=bytes read |
| 63 | `SYS_PIPE_CLOSE` | EBX=id | EAX=0 |
| 64 | `SYS_SHMGET` | EBX=key, ECX=size | EAX=shm_id (-1 error) |
| 65 | `SYS_SHMADDR` | EBX=shm_id | EAX=pointer |

---

## Screensavers

The Burrows desktop includes a screensaver system that activates after an idle
period with no keyboard or mouse input.

### Configuration (Screensaver)

| Property | Value |
| --- | --- |
| `SCR_IDLE_TIMEOUT` | 30,000 ticks (~5 minutes at 100 Hz) |
| Modes | 4 |

### Screensaver Modes

| Mode | Name | Description |
| --- | --- | --- |
| 0 | Starfield | 64 parallax stars with depth simulation |
| 1 | Matrix | Green cascading character columns (80×30 grid) |
| 2 | Pipes | 6 colored pipes growing randomly |
| 3 | Bouncing Logo | Mellivora text bouncing around the screen |

### State Variables (Screensaver)

| Variable | Type | Description |
| --- | --- | --- |
| `scr_idle_count` | dword | Idle tick counter (reset on input) |
| `scr_mode` | byte | Active screensaver mode (0–4) |

The `scrsaver` shell command cycles through modes or sets a specific mode.
