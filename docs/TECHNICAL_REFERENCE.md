# Mellivora OS — Technical Reference

This document provides a detailed technical description of the Mellivora OS internals,
suitable for OS developers, contributors, and advanced users.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Boot Sequence](#boot-sequence)
3. [Memory Map](#memory-map)
4. [Global Descriptor Table (GDT)](#global-descriptor-table-gdt)
5. [Interrupt Descriptor Table (IDT)](#interrupt-descriptor-table-idt)
6. [Task State Segment (TSS)](#task-state-segment-tss)
7. [Physical Memory Manager](#physical-memory-manager)
8. [VGA Text Mode Driver](#vga-text-mode-driver)
9. [Programmable Interval Timer (PIT)](#programmable-interval-timer-pit)
10. [PS/2 Keyboard Driver](#ps2-keyboard-driver)
11. [ATA PIO Disk Driver](#ata-pio-disk-driver)
12. [Serial Port Driver](#serial-port-driver)
13. [PC Speaker Driver](#pc-speaker-driver)
14. [HBFS Filesystem](#hbfs-filesystem)
15. [File Descriptor System](#file-descriptor-system)
16. [Program Execution Model](#program-execution-model)
17. [Syscall Interface](#syscall-interface)
18. [Shell Architecture](#shell-architecture)
19. [Environment Variables](#environment-variables)
20. [Batch Script Engine](#batch-script-engine)
21. [Security Model](#security-model)

---

## Architecture Overview

Mellivora OS is a monolithic, single-tasking, 32-bit protected mode operating system.

| Property | Value |
| --- | --- |
| **Target CPU** | i486+ (32-bit x86) |
| **Mode** | 32-bit protected mode, flat memory model |
| **Privilege levels** | Ring 0 (kernel), Ring 3 (user programs) |
| **Address space** | 4 GB flat, no paging |
| **Multitasking** | None — single-tasking only |
| **Syscall interface** | `INT 0x80` with 33 syscalls |
| **Filesystem** | HBFS (Honey Badger File System) |
| **Executable formats** | Flat binary, ELF32 |

The kernel is monolithic: all drivers (VGA, keyboard, ATA, serial, speaker, RTC),
the filesystem, the shell, and the syscall dispatcher are compiled into a single
flat binary.

---

## Boot Sequence

Mellivora uses a three-stage boot process:

### Stage 1: MBR Boot Sector (boot.asm)

- Located at LBA 0 (first 512 bytes of disk)
- Executes in 16-bit real mode at `0x7C00`
- Tasks:
  1. Sets up segments and stack
  2. Saves BIOS boot drive number to `0x500`
  3. Loads Stage 2 (32 sectors starting at LBA 1) to `0x8000`
  4. Jumps to Stage 2

### Stage 2: Protected Mode Loader (stage2.asm)

- Loaded at `0x8000` in real mode
- Tasks:
  1. Queries BIOS memory map (`INT 0x15, E820h`) and stores results at `0x504`/`0x508`
  2. Enables the A20 line (keyboard controller method, fast A20 fallback)
  3. Loads the kernel (192 sectors from LBA 33) to `0x100000` (1 MB)
  4. Sets up the GDT with 5 entries + TSS descriptor
  5. Switches to 32-bit protected mode (`CR0 bit 0`)
  6. Far-jumps to kernel entry point at `0x100000`

### Stage 3: 32-bit Kernel (kernel.asm)

- Executes at `0x100000` in 32-bit protected mode
- Initialization sequence:
  1. Sets segment registers to flat data selector (`0x10`)
  2. Sets kernel stack to `0x9FC00`
  3. Zeroes BSS section
  4. Initializes current directory to root
  5. Initializes subsystems in order:
     - VGA text mode driver
     - PIC (Programmable Interrupt Controller)
     - IDT (Interrupt Descriptor Table)
     - PIT (Programmable Interval Timer)
     - PS/2 Keyboard
     - Physical Memory Manager
     - ATA disk
     - Serial port (COM1)
     - TSS (Task State Segment)
  6. Enables interrupts (`STI`)
  7. Prints boot banner and system info
  8. Initializes HBFS filesystem
  9. Enters the interactive shell (`shell_main`)

---

## Memory Map

### Physical Memory Layout

| Address Range | Size | Purpose |
| --- | --- | --- |
| `0x00000000–0x000004FF` | 1.25 KB | Real-mode IVT and BIOS data |
| `0x00000500–0x0000050B` | 12 B | Boot info struct (drive, mmap count, mmap ptr) |
| `0x00008000–0x0000BFFF` | 16 KB | Stage 2 loader (real mode, freed after boot) |
| `0x0009FC00` | — | Kernel stack top (grows downward) |
| `0x000A0000–0x000BFFFF` | 128 KB | VGA memory region |
| `0x000B8000–0x000B8F9F` | 4000 B | VGA text-mode framebuffer (80×25×2) |
| `0x000C0000–0x000FFFFF` | 256 KB | ROM BIOS area |
| `0x00100000` | ≤96 KB | **Kernel code + data + BSS** |
| `0x00200000` | 1 MB | **User program space** (`PROGRAM_BASE`) |
| `0x00300000` | varies | **PMM bitmap** (1 bit per 4 KB page) |
| `0x00400000+` | varies | **Kernel heap** (`HEAP_BASE`) |

### Boot Info Structure (at 0x500)

| Offset | Size | Field |
| --- | --- | --- |
| `0x500` | 4 bytes | BIOS boot drive number |
| `0x504` | 4 bytes | Memory map entry count |
| `0x508` | 4 bytes | Pointer to memory map data |

### User Program Memory

Programs are loaded at `PROGRAM_BASE` (0x200000) and have up to 1 MB of space:

```text
0x00200000  ┌─────────────────────────┐
            │  Program code + data     │
            │  (up to 1 MB)            │
            │                          │
0x002FFFF0  │  Exit trampoline (16 B)  │  ← PROGRAM_EXIT_ADDR
0x00300000  └─────────────────────────┘
```

The exit trampoline at the end of the program space contains:
```nasm
mov eax, 0      ; SYS_EXIT
int 0x80
```
This catches programs that use `RET` instead of calling `SYS_EXIT` explicitly.

---

## Global Descriptor Table (GDT)

The GDT is set up by Stage 2 and contains 6 entries:

| Selector | Name | Base | Limit | DPL | Type |
| --- | --- | --- | --- | --- | --- |
| `0x00` | Null | — | — | — | Null descriptor |
| `0x08` | Kernel Code | 0 | 4 GB | 0 | 32-bit, execute/read |
| `0x10` | Kernel Data | 0 | 4 GB | 0 | 32-bit, read/write |
| `0x18` | User Code | 0 | 4 GB | 3 | 32-bit, execute/read |
| `0x20` | User Data | 0 | 4 GB | 3 | 32-bit, read/write |
| `0x28` | TSS | tss_struct | 104 B | 0 | 32-bit TSS (Available) |

All segments use a flat memory model — base 0, limit 4 GB, granularity 4 KB.

The user segments (selectors `0x18` and `0x20`) have DPL 3, allowing ring 3 code to
use them. Programs use:
- `USER_CS = 0x1B` (selector `0x18` | RPL 3)
- `USER_DS = 0x23` (selector `0x20` | RPL 3)

---

## Interrupt Descriptor Table (IDT)

The IDT contains 256 entries (256 × 8 = 2048 bytes).

### CPU Exceptions (INT 0x00–0x1F)

| Vector | Name | Handler |
| --- | --- | --- |
| 0 | Divide Error | `isr_0` — prints error, halts |
| 1 | Debug | `isr_1` |
| 2 | NMI | `isr_2` |
| 3 | Breakpoint | `isr_3` |
| 4 | Overflow | `isr_4` |
| 5 | Bound Range | `isr_5` |
| 6 | Invalid Opcode | `isr_6` — prints "Invalid opcode", halts |
| 7 | Device Not Available | `isr_7` |
| 8 | Double Fault | `isr_8` — prints error, halts |
| 9 | Coprocessor Segment Overrun | `isr_9` |
| 10 | Invalid TSS | `isr_10` |
| 11 | Segment Not Present | `isr_11` |
| 12 | Stack-Segment Fault | `isr_12` |
| 13 | General Protection Fault | `isr_13` — prints "General protection fault", halts |
| 14 | Page Fault | `isr_14` — prints "Page fault at address", halts |
| 15–31 | Reserved | Generic handlers |

### Hardware IRQs (INT 0x20–0x2F)

The PIC is remapped so IRQ 0–7 map to INT 0x20–0x27, and IRQ 8–15 map to INT 0x28–0x2F.

| Vector | IRQ | Device | Handler |
| --- | --- | --- | --- |
| `0x20` | 0 | PIT Timer | `irq_timer` — increments `tick_count` |
| `0x21` | 1 | Keyboard | `irq_keyboard` — reads scancode, translates, buffers |
| `0x22–0x27` | 2–7 | — | Spurious / unused |
| `0x28–0x2F` | 8–15 | — | Spurious / unused |

### Syscall Gate (INT 0x80)

| Vector | Type | DPL | Handler |
| --- | --- | --- | --- |
| `0x80` | Trap Gate | 3 | `syscall_handler` — dispatches via syscall table |

The DPL of 3 allows ring 3 programs to invoke `INT 0x80`.

---

## Task State Segment (TSS)

The TSS is a 104-byte structure used for ring transitions (ring 3 → ring 0):

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 4 | 4 | ESP0 | `KERNEL_STACK` (0x9FC00) — ring 0 stack |
| 8 | 4 | SS0 | `0x10` — kernel data selector |
| 102 | 2 | I/O Map Base | 104 — no I/O permission bitmap |

When a ring 3 program executes `INT 0x80`, the CPU automatically loads ESP0 and SS0
from the TSS before pushing the ring 3 state and transferring to the kernel handler.

---

## Physical Memory Manager

The PMM uses a bitmap allocator with 4 KB page granularity.

### Design

- **Bitmap location:** `PMM_BITMAP` at `0x300000`
- **Page size:** 4096 bytes
- **Bit mapping:** 1 bit per page (0 = free, 1 = used)
- **Initialization:** Reads BIOS E820 memory map, marks available regions as free,
  then marks kernel area and low memory as used

### Operations

| Function | Description |
| --- | --- |
| `pmm_init` | Initialize bitmap from E820 memory map |
| `pmm_alloc_page` | Allocate a single 4 KB page, returns physical address |
| `pmm_free_page` | Free a single page given its physical address |
| `pmm_alloc_pages` | Allocate N contiguous pages |

### Syscall Interface

- `SYS_MALLOC (19)`: Takes size in bytes, rounds up to pages, returns physical address
- `SYS_FREE (20)`: Takes address and size, frees the corresponding pages

---

## VGA Text Mode Driver

### Configuration

- **Base address:** `0xB8000`
- **Resolution:** 80 columns × 25 rows
- **Character format:** 2 bytes per cell — `[ASCII byte] [attribute byte]`
- **Attribute format:** `[bg:4][fg:4]` — 4-bit background, 4-bit foreground

### Color Constants

| Value | Color | Constant |
| --- | --- | --- |
| `0x07` | Light gray on black | `COLOR_DEFAULT` |
| `0x1F` | White on blue | `COLOR_HEADER` |
| `0x4F` | White on red | `COLOR_ERROR` |
| `0x2F` | White on green | `COLOR_SUCCESS` |
| `0x0A` | Light green on black | `COLOR_PROMPT` |
| `0x0B` | Light cyan on black | `COLOR_INFO` |
| `0x0E` | Yellow on black | `COLOR_EXEC` |
| `0x0D` | Light magenta on black | `COLOR_BATCH` |

### Functions

| Function | Description |
| --- | --- |
| `vga_init` | Set default color, clear screen |
| `vga_clear` | Fill screen with spaces in default color |
| `vga_putchar` | Print character at cursor, advance, handle `\n`, `\r`, `\t` |
| `vga_print` | Print null-terminated string |
| `vga_print_color` | Print string using color from first byte of string |
| `vga_scroll` | Scroll screen up one line |
| `vga_update_cursor` | Update hardware cursor via VGA I/O ports `0x3D4`/`0x3D5` |
| `vga_set_cursor` | Set cursor to (X, Y) position |

### Scrolling

When text output reaches the bottom of the screen (row 25), the driver scrolls:
1. Copies rows 1–24 to rows 0–23
2. Clears the last row
3. Keeps cursor at row 24

---

## Programmable Interval Timer (PIT)

### Configuration

| Setting | Value |
| --- | --- |
| **Channel** | 0 (system timer) |
| **Mode** | Rate generator (mode 2) |
| **Frequency** | 100 Hz |
| **Divisor** | 1193182 / 100 = 11932 |

### Timer Interrupt (IRQ 0)

The timer IRQ handler:
1. Increments `tick_count` (32-bit counter)
2. Sends EOI to PIC
3. Returns

`tick_count` is used by:
- `SYS_GETTIME` — returns current tick count
- `SYS_SLEEP` — busy-waits with `HLT` until target tick count
- File timestamps
- `time` command (divides by 100 for seconds)

---

## PS/2 Keyboard Driver

### Design

- **Scancode set:** Set 1 (XT-compatible)
- **Translation tables:** Two lookup tables (normal and shifted) in kernel data
- **Ring buffer:** 256-byte circular buffer (`kb_buffer`)
- **State tracking:** `kb_shift` (shift key state), `kb_ctrl` (ctrl key state)

### IRQ 1 Handler

The keyboard IRQ handler:
1. Reads scancode from port `0x60`
2. Handles key-up events (bit 7 set):
   - Clears `kb_shift` if Left/Right Shift released
   - Clears `kb_ctrl` if Ctrl released
3. Handles key-down events:
   - Sets `kb_shift` if Shift pressed
   - Sets `kb_ctrl` if Ctrl pressed
   - Translates scancode to ASCII via lookup table
   - If Ctrl is held, generates control codes (e.g., Ctrl+C = 0x03)
   - **Ctrl+C during program execution:** If `program_running == 1`, immediately
     resets the stack and jumps to `shell_main` (hard abort)
   - Stores translated character in ring buffer
4. Sends EOI to PIC

### Special Keys

| Scancode | Key | ASCII/Code |
| --- | --- | --- |
| `0x48` | Up Arrow | `KEY_UP (0x80)` |
| `0x50` | Down Arrow | `KEY_DOWN (0x81)` |
| `0x4B` | Left Arrow | `KEY_LEFT (0x82)` |
| `0x4D` | Right Arrow | `KEY_RIGHT (0x83)` |
| `0x47` | Home | `0x86` |
| `0x4F` | End | `0x87` |
| `0x49` | Page Up | `0x88` |
| `0x51` | Page Down | `0x89` |
| `0x53` | Delete | `0x7F` |
| `0x01` | Escape | `0x1B` |
| `0x0F` | Tab | `0x09` |
| `0x0E` | Backspace | `0x08` |

---

## ATA PIO Disk Driver

### Configuration

| Setting | Value |
| --- | --- |
| **Mode** | PIO (Programmed I/O) |
| **Addressing** | LBA48 (48-bit logical block addressing) |
| **Ports** | Primary channel: `0x1F0`–`0x1F7`, control: `0x3F6` |
| **Max disk size** | 128 PB (LBA48 theoretical limit) |

### I/O Ports

| Port | Read | Write |
| --- | --- | --- |
| `0x1F0` | Data (16-bit) | Data (16-bit) |
| `0x1F1` | Error register | Features |
| `0x1F2` | Sector count | Sector count |
| `0x1F3` | LBA low (bits 0–7) | LBA low |
| `0x1F4` | LBA mid (bits 8–15) | LBA mid |
| `0x1F5` | LBA high (bits 16–23) | LBA high |
| `0x1F6` | Drive/head | Drive/head |
| `0x1F7` | Status | Command |
| `0x3F6` | Alt status | Device control |

### Commands Used

| Command | Value | Description |
| --- | --- | --- |
| `READ SECTORS EXT` | `0x24` | Read sectors using LBA48 |
| `WRITE SECTORS EXT` | `0x34` | Write sectors using LBA48 |
| `IDENTIFY DEVICE` | `0xEC` | Get drive identification info |
| `FLUSH CACHE` | `0xE7` | Flush write cache to disk |

### Functions

| Function | Parameters | Description |
| --- | --- | --- |
| `ata_init` | — | Detects drive, reads IDENTIFY, stores total sectors |
| `ata_read_sectors` | EAX=LBA, ECX=count, EDI=buffer | Read sectors via LBA48 |
| `ata_write_sectors` | EAX=LBA, ECX=count, ESI=buffer | Write sectors via LBA48 |
| `ata_flush` | — | Flush drive cache |

### Read Procedure

1. Wait until BSY clears
2. Select drive (master, LBA mode)
3. Write sector count and LBA bytes (high 3 bytes first, then low 3)
4. Issue READ SECTORS EXT command (0x24)
5. Wait for DRQ
6. Read 256 words (512 bytes) per sector via `REP INSW`
7. Repeat for each sector

---

## Serial Port Driver

### Configuration

| Setting | Value |
| --- | --- |
| **Port** | COM1 (`0x3F8`) |
| **Baud rate** | 115200 |
| **Data bits** | 8 |
| **Stop bits** | 1 |
| **Parity** | None |
| **FIFO** | Enabled (14-byte trigger) |

### Initialization

1. Disable interrupts on UART
2. Enable DLAB (divisor latch access)
3. Set divisor to 1 (115200 baud)
4. Set 8N1 format
5. Enable FIFO with 14-byte trigger level
6. Enable DTR, RTS, OUT2
7. Set loopback mode and test
8. If loopback test passes, mark `serial_present = 1`

### Output

`serial_putchar` waits for the transmit holding register to be empty (LSR bit 5), then
writes a byte to port `0x3F8`.

`sys_serial` prints a null-terminated string by calling `serial_putchar` for each byte.

---

## PC Speaker Driver

### Operation

The PC speaker is controlled via PIT Channel 2 and port `0x61`:

**Tone On:**
1. Program PIT Channel 2 with desired frequency divisor (1193182 / freq)
2. Set bits 0 and 1 of port `0x61` to enable speaker

**Tone Off:**
1. Clear bits 0 and 1 of port `0x61`

### Syscall

`SYS_BEEP (24)`: EBX = frequency in Hz (0 = off), ECX = duration in ticks.
The handler enables the tone, sleeps for the duration, then disables it.

---

## HBFS Filesystem

HBFS (Honey Badger File System) is a simple, custom filesystem designed for Mellivora.

### On-Disk Layout

```text
LBA 225         Superblock (512 bytes)
LBA 226–233     Block allocation bitmap (8 sectors = 4 KB)
LBA 234–249     Root directory (16 sectors = 8 KB = 2 blocks)
LBA 250+        Data blocks (4 KB each, 8 sectors per block)
```

### Superblock (LBA 225)

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0 | 4 | Magic | `0x48424653` (`'HBFS'`) |
| 4 | 4 | Version | 1 |
| 8 | 4 | Block size | 4096 |
| 12 | 4 | Total blocks | (calculated from disk size) |
| 16 | 4 | Bitmap start LBA | 226 |
| 20 | 4 | Root dir start LBA | 234 |
| 24 | 4 | Data start LBA | 250 |

### Block Allocation Bitmap

- 8 sectors (4096 bytes) starting at LBA 226
- Each bit represents one data block
- Bit 0 = free, Bit 1 = allocated
- Supports up to 32,768 blocks (128 MB of data space)

### Directory Entry (288 bytes)

| Offset | Size | Field | Description |
| --- | --- | --- | --- |
| 0 | 253 | Name | Null-terminated filename (max 252 chars) |
| 253 | 1 | Type | File type (see below) |
| 254 | 2 | Flags | Reserved flags |
| 256 | 4 | Size | File size in bytes |
| 260 | 4 | Start Block | First data block number |
| 264 | 4 | Block Count | Number of blocks allocated |
| 268 | 4 | Created | Creation timestamp (tick count) |
| 272 | 4 | Modified | Modification timestamp (tick count) |
| 276 | 12 | Reserved | Padding to 288 bytes |

### File Types

| Value | Constant | Description |
| --- | --- | --- |
| 0 | `FTYPE_FREE` | Free/empty directory entry |
| 1 | `FTYPE_TEXT` | Text file |
| 2 | `FTYPE_DIR` | Subdirectory |
| 3 | `FTYPE_EXEC` | Executable (flat binary or ELF) |
| 4 | `FTYPE_BATCH` | Batch script file |

### Directory Capacity

- Root directory: 2 blocks = 8192 bytes
- Entry size: 288 bytes
- Maximum files per directory: 8192 / 288 = **28 entries**

### Multi-Block Files

Files can span multiple contiguous blocks. The `block_count` field in the directory entry
tracks how many blocks are allocated. Data blocks start at LBA 250 and each block is
8 sectors (4 KB).

**LBA for block N:** `HBFS_DATA_START + (N × HBFS_SECTORS_PER_BLK)` = `250 + (N × 8)`

### Key Functions

| Function | Description |
| --- | --- |
| `hbfs_init` | Read superblock, verify magic, initialize bitmap |
| `hbfs_format` | Write fresh superblock, clear bitmap and root directory |
| `hbfs_read_file` | Find file in directory, read all blocks into buffer |
| `hbfs_write_file` | Allocate blocks, write data, create/update directory entry |
| `hbfs_delete_file` | Free blocks, clear directory entry |
| `hbfs_find_file` | Scan directory for matching filename |
| `hbfs_alloc_block` | Find first free bit in bitmap, mark as used |
| `hbfs_free_block` | Clear bit in bitmap |
| `hbfs_read_dir` | Read directory sectors into buffer |
| `hbfs_write_dir` | Write directory buffer back to disk |
| `hbfs_read_bitmap` | Read bitmap from disk |
| `hbfs_write_bitmap` | Write bitmap to disk |

---

## File Descriptor System

Mellivora provides a POSIX-like file descriptor interface layered on top of HBFS.

### File Descriptor Table

- **Maximum open files:** 8 (`FD_MAX`)
- **Entry size:** 32 bytes (`FD_ENTRY_SIZE`)
- **Total table size:** 256 bytes

### FD Entry Layout

| Offset | Size | Field | Description |
| --- | --- | --- | --- |
| 0 | 4 | Flags | `0`=closed, `1`=open/read, `2`=open/write |
| 4 | 4 | Size | File size in bytes |
| 8 | 4 | Start Block | First data block on disk |
| 12 | 4 | Position | Current read/write offset |
| 16 | 4 | Block Count | Number of allocated blocks |
| 20 | 12 | Reserved | Unused padding |

### Operations

**Open (`fd_open` / `SYS_OPEN`):**
1. Scan FD table for a free entry (flags == 0)
2. Find the file in the directory
3. Populate the FD entry with file metadata
4. Return FD index (0–7) or -1 on failure

**Read (`fd_read` / `SYS_READ`):**
1. Calculate which block the current position falls in
2. Read that block from disk
3. Copy requested bytes (clamped to available data)
4. Advance position
5. Return bytes read (0 = EOF, -1 = error)

**Write (`fd_write` / `SYS_WRITE`):**
1. Calculate target block from current position
2. Read the existing block from disk
3. Modify the appropriate bytes in the buffer
4. Write the modified block back to disk
5. Update file size if extended
6. Advance position
7. Return bytes written

**Seek (`fd_seek` / `SYS_SEEK`):**
- Whence 0 (`SEEK_SET`): Position = offset
- Whence 1 (`SEEK_CUR`): Position += offset
- Whence 2 (`SEEK_END`): Position = size + offset
- Returns new position

**Close (`fd_close` / `SYS_CLOSE`):**
- Sets flags to `FD_FLAG_CLOSED` (0)

---

## Program Execution Model

### Loading

When a program is executed (via the shell or `SYS_EXEC`):

1. **Filename resolution:** The shell parses the command line, separating the program
   name from arguments. Arguments are stored in `program_args_buf`.

2. **File lookup:** The kernel searches the directory for the program name.

3. **Loading:** The file contents are read into `PROGRAM_BASE` (0x200000).

4. **Format detection:**
   - If the first 4 bytes are `0x464C457F` (ELF magic `\x7FELF`): Parse ELF headers,
     load PT_LOAD segments, use ELF entry point.
   - Otherwise: Treat as flat binary with entry point at `PROGRAM_BASE`.

5. **Exit trampoline:** 16 bytes at `PROGRAM_EXIT_ADDR` are filled with:
   ```nasm
   mov eax, 0      ; SYS_EXIT
   int 0x80
   ```
   The return address on the ring 3 stack points here, catching `RET` from main.

6. **Ring 3 transition:** The kernel uses `IRETD` to switch to ring 3:
   - Pushes `USER_DS` (SS), ring 3 stack pointer, EFLAGS, `USER_CS`, entry point
   - IRETD loads these values, switching to ring 3

7. **Flag setting:** `program_running` is set to 1.

### During Execution

- The program runs in ring 3 with `USER_CS` (0x1B) and `USER_DS` (0x23)
- All kernel memory is accessible (no paging/memory protection beyond ring level)
- Syscalls via `INT 0x80` transition to ring 0 via the TSS
- **Ctrl+C** in the keyboard IRQ handler triggers a hard abort: resets the stack and
  jumps directly to `shell_main`

### Termination

Programs terminate via:
1. **`SYS_EXIT (0)`:** Explicit exit syscall — clears `program_running`, returns to shell
2. **`RET`:** Falls through to exit trampoline, which calls `SYS_EXIT`
3. **Ctrl+C:** Hard abort by keyboard IRQ handler

---

## Syscall Interface

### Calling Convention

| Register | Purpose |
| --- | --- |
| `EAX` | Syscall number (0–32) |
| `EBX` | First argument |
| `ECX` | Second argument |
| `EDX` | Third argument |
| `ESI` | Fourth argument |
| `EDI` | Fifth argument |
| `EAX` | Return value |

### Dispatch

The `syscall_handler` function:
1. Saves all registers
2. Validates EAX against syscall count (33)
3. Indexes into `syscall_table` (array of function pointers)
4. Calls the handler
5. Restores registers (with EAX modified for return value)
6. Returns via `IRETD`

See the [API Reference](API_REFERENCE.md) for the complete syscall listing.

---

## Shell Architecture

### Main Loop

The shell (`shell_main`) operates in a simple loop:
1. Print prompt (`HBDOS:<dir>> `)
2. Read a line of input (with editing, history, tab completion)
3. Parse the command (first space-separated word = command)
4. Look up command in command table
5. If found: call handler function
6. If not found: try to execute as a program from the filesystem
7. Go to step 1

### Line Input

The `shell_read_line` function provides:
- Character-by-character input with echo
- Backspace handling
- Tab completion (calls `shell_tab_complete`)
- History browsing (Up/Down arrows)
- Ctrl+C to cancel
- Home/End cursor movement

### Command Lookup

Commands are stored in a table of (name_ptr, handler_ptr) pairs. The shell does a
case-sensitive linear search through the table.

### Tab Completion

When Tab is pressed:
1. Extract the partial filename from the current input
2. Scan the directory for files whose names start with the partial string
3. If one match: complete it
4. If multiple: cycle through matches on subsequent Tab presses

### Command History

- Stored in `hist_buf` (8 entries × 256 bytes each)
- `hist_count` tracks the number of stored entries (max 8, wraps)
- `hist_browse` tracks the current browse position
- Up arrow decrements browse position, Down arrow increments

---

## Environment Variables

### Storage

- Table at `env_table`: 16 entries × 128 bytes each (2048 bytes total)
- Format: `NAME=VALUE\0` stored as a single string per entry
- Empty entries have their first byte set to 0

### Operations

| Function | Description |
| --- | --- |
| `env_get` | Linear search for `NAME=`, returns pointer past `=` |
| `env_set_str` | Set or update `NAME=VALUE` |
| `env_unset` | Zero the matching entry |
| `env_expand` | Replace `$NAME` tokens in a string with their values |

### Variable Expansion

The `echo` command and batch scripts support `$VAR` expansion:
- `$NAME` is replaced with the value of environment variable `NAME`
- Variable names end at non-alphanumeric characters
- Unknown variables expand to empty string

---

## Batch Script Engine

### Execution

The `cmd_batch` handler:
1. Reads the script file into `PROGRAM_BASE`
2. Null-terminates the content
3. Iterates through lines (split by `\n`, skip `\r`)
4. For each non-empty line:
   a. Copies line to `batch_line_buf`
   b. Prints `> <line>` in info color
   c. Calls `shell_parse_cmd` to execute as a regular command

### Capabilities

Batch scripts can use any shell command including:
- `echo` with `$VAR` expansion
- `set` / `unset` for variable management
- `run` / program names for program execution
- `batch` for nested script execution
- All file operations (`cat`, `write`, `del`, etc.)

---

## Security Model

Mellivora has a minimal security model:

### Ring-Based Protection

- **Ring 0:** Kernel code, drivers, shell, filesystem
- **Ring 3:** User programs

The GDT enforces privilege levels. User programs cannot directly execute privileged
instructions (`IN`, `OUT`, `CLI`, `STI`, `LGDT`, etc.) — these will trigger a General
Protection Fault.

### Syscall Filtering

Two syscalls are restricted when called from user programs:

| Syscall | Restriction |
| --- | --- |
| `SYS_DISK_READ (22)` | Returns -1 if `program_running == 1` |
| `SYS_DISK_WRITE (23)` | Returns -1 if `program_running == 1` |

This prevents user programs from performing raw disk I/O that could corrupt the
filesystem or other data.

### Limitations

- **No memory protection:** Without paging, user programs can read/write any physical
  memory address, including kernel memory.
- **No file permissions:** All files are accessible to all operations.
- **Single user:** No user accounts or authentication.
- **No ASLR:** Program base address is fixed at 0x200000.

---

## I/O Port Map

| Port Range | Device |
| --- | --- |
| `0x20–0x21` | PIC1 (master) — command and data |
| `0x40–0x43` | PIT — channels 0, 1, 2 and command |
| `0x60` | PS/2 keyboard data |
| `0x61` | PC speaker control |
| `0x64` | PS/2 keyboard status/command |
| `0x70–0x71` | RTC CMOS — index and data |
| `0xA0–0xA1` | PIC2 (slave) — command and data |
| `0x1F0–0x1F7` | ATA primary channel |
| `0x3D4–0x3D5` | VGA CRTC — cursor control |
| `0x3F6` | ATA device control |
| `0x3F8–0x3FD` | COM1 serial (data, LSR) |
| `0x604` | ACPI PM control (QEMU PIIX4 — shutdown) |
