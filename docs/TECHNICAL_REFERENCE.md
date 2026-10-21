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
6. [Virtual Filesystem (VFS)](#virtual-filesystem-vfs)
7. [HBFS Filesystem](#hbfs-filesystem)
8. [Directory Navigation & Stacking](#directory-navigation--stacking)
9. [PATH Search Mechanism](#path-search-mechanism)
10. [Path-Aware File I/O](#path-aware-file-io)
11. [ATA/IDE Disk Driver](#ataide-disk-driver)
12. [ATA Bus Master DMA](#ata-bus-master-dma)
13. [AHCI SATA Driver](#ahci-sata-driver)
14. [PCI Bus Driver](#pci-bus-driver)
15. [Intel e1000 NIC Driver](#intel-e1000-nic-driver)
16. [AC'97 Audio Driver](#ac97-audio-driver)
17. [VirtIO Devices](#virtio-devices)
18. [VGA Text Mode](#vga-text-mode)
19. [VBE/BGA Graphics Driver](#vbebga-graphics-driver-kernelvbeinc)
20. [Keyboard Driver](#keyboard-driver)
21. [PIT Timer](#pit-timer)
22. [Serial Port](#serial-port)
23. [PC Speaker](#pc-speaker)
24. [GDB Remote Stub](#gdb-remote-stub)
25. [Scheduler & Task Control](#scheduler--task-control)
26. [Process Execution](#process-execution)
27. [Syscall Interface](#syscall-interface)
28. [Shell Architecture](#shell-architecture)
29. [Environment Variables](#environment-variables)
30. [Alias System](#alias-system)
31. [Command History](#command-history)
32. [Tab Completion](#tab-completion)
33. [Fault Injection (P10)](#fault-injection-p10)
34. [64-bit Nucleus (P2)](#64-bit-nucleus-p2)

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
   (segment `0x2000`), in chunks of up to 128 sectors
5. Enters protected mode:
   - Loads the GDT (`lgdt [gdt_descriptor]`)
   - Sets the CR0 PE bit
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
   - Scheduler (`sched_init`)
   - IPC — pipes, shared memory, semaphores
   - Network stack (`net_init`)
   - Intel e1000 NIC (`e1000_init`, after `net_init`)
   - Paging / LFB identity mapping + sigreturn trampoline
   - PS/2 mouse
   - SB16 audio
   - VBE/BGA graphics
   - PCI bus enumeration
   - AC'97 audio
   - ATA Bus Master DMA
   - VirtIO devices
   - Burrows desktop compositor
   - AHCI SATA controller (`ahci_init`)
   - HBFS filesystem (with VFS mount)
   - GDB remote stub (`gdbstub_init`)
   - Default environment variables
   - PCI bus enumeration
   - AC'97 audio
   - ATA Bus Master DMA
   - VirtIO devices
   - Burrows desktop compositor
   - HBFS filesystem
   - Default environment variables
2. Prints the HB Lair banner
3. Enters the interactive shell loop

### Disk Layout

| LBA Range | Size | Contents |
| --- | --- | --- |
| 0 | 512 B | MBR bootloader (boot.asm) |
| 1–32 | 16 KB | Stage 2 loader (stage2.asm) |
| 33+ | Variable | Kernel (kernel.asm + include modules) |
| 4096 | 512 B | HBFS Superblock |
| 4097–4224 | 64 KB | Block allocation bitmap |
| 4225–4480 | 128 KB | Root directory (32 blocks) |
| 4481+ | — | Data blocks (4 KB each) |

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
0x00300000 ├─────────────────────────┤  ← 3 MB
           │ User program space      │  1 MB max
0x003FFxxx │ SYS_EXIT trampoline     │  Safety net for programs that RET;
           │ (+ stack top)           │  both move with the per-launch random top
0x00400000 ├─────────────────────────┤  ← 4 MB
           │ (gap / reserved)        │
0x00410000 ├─────────────────────────┤
           │ Page directory          │  4 KB — must NOT overlap 3-4 MB
0x00411000 ├─────────────────────────┤  (a 512 KB+ program would
           │ 32 identity page tables │  128 KB   overwrite its page dir)
0x00431000 ├─────────────────────────┤
           │ 96 demand page tables   │  384 KB (128-512 MB zone)
0x00491000 ├─────────────────────────┤
           │ (gap / reserved)        │
0x00500000 ├─────────────────────────┤  ← 5 MB
           │ PMM bitmap              │  128 KB (tracks up to 4 GB)
0x00520000 ├─────────────────────────┤
           │ PMM buddy tags          │  4 KB (currently unused)
0x00700000 ├─────────────────────────┤  ← 7 MB
           │ Heap window (unused)    │
0x00800000 ├─────────────────────────┤  ← 8 MB  KERNEL_RESERVED_END
           │ PMM-managed memory      │  Usable memory starts here —
           │ ...                     │  pmm_init frees nothing below
           └─────────────────────────┘
```

### Key Constants

| Constant | Value | Description |
| --- | --- | --- |
| `KERNEL_BASE` | `0x00100000` (1 MB) | Kernel load address |
| `KERNEL_SECTORS` | Generated at build time | Kernel disk size in sectors |
| `KERNEL_STACK` | `0x0009FC00` | Stack top (conventional memory end) |
| `PROGRAM_BASE` | `0x00300000` (3 MB) | User program load address |
| `PROGRAM_MAX_SIZE` | `0x00100000` (1 MB) | Max program size |
| `PROGRAM_EXIT_ADDR` | `0x003FFFF0` | SYS_EXIT trampoline |
| `PMM_BITMAP` | `0x00500000` (5 MB) | Physical memory bitmap |
| `PMM_BUDDY_TAGS` | `0x00520000` | Buddy allocator tag array |
| `PAGE_DIR_ADDR` | `0x00410000` | Page directory (above the program window) |
| `PAGE_TABLE_BASE` | `0x00411000` | First of 32 identity page tables |
| `HEAP_BASE` | `0x00700000` (7 MB) | Heap window (reserved, currently unused) |
| `KERNEL_RESERVED_END` | `0x00800000` (8 MB) | Lower bound of PMM-managed memory |
| `VGA_BASE` | `0x000B8000` | VGA text mode framebuffer |
| `VGA_WIDTH` | 80 | Screen columns |
| `VGA_HEIGHT` | 25 | Screen rows |
| `VGA_SIZE` | 4000 | Framebuffer size (bytes) |
| `BOOTINFO_DRIVE` | `0x500` | Boot drive (from Stage 2) |
| `BOOTINFO_MMAP_CNT` | `0x504` | E820 entry count |
| `BOOTINFO_MMAP_PTR` | `0x508` | E820 data pointer |

---

## Virtual Filesystem (VFS)

### Overview (`kernel/vfs.inc`)

The VFS provides a unified front-end for all file I/O. User programs and kernel subsystems
call the VFS routines (`vfs_open`, `vfs_read`, `vfs_write`, etc.); the VFS dispatches to
the appropriate backend based on the mount table.

### Mount Table

| Mount point | Backend | Description |
| --- | --- | --- |
| `/` | HBFS | Honey Badger File System v3 root |
| `/proc` | procfs | Per-process and system info (read-only) |
| `/dev` | devfs | Device nodes (`/dev/tty0`, `/dev/null`, `/dev/zero`, …) |
| `/tmp` | tmpfs | Volatile RAM-backed temporary storage |

### VFS API

| Function | Parameters | Returns | Description |
| --- | --- | --- | --- |
| `vfs_open` | EBX=path, ECX=flags | EAX=vfd / -1 | Open a file; allocates a VFS file descriptor (0–63) |
| `vfs_read` | EBX=vfd, ECX=buf, EDX=len | EAX=bytes / -1 | Read up to `len` bytes |
| `vfs_write` | EBX=vfd, ECX=buf, EDX=len | EAX=bytes / -1 | Write `len` bytes |
| `vfs_close` | EBX=vfd | EAX=0 / -1 | Close descriptor and release resources |
| `vfs_seek` | EBX=vfd, ECX=offset, EDX=whence | EAX=new_pos / -1 | Reposition file offset |
| `vfs_readdir` | EBX=path, ECX=buf | EAX=0 / -1 | Enumerate directory entries into `buf` |
| `vfs_stat` | EBX=path, ECX=stat_buf | EAX=0 / -1 | Fill `stat_buf` with file metadata |
| `vfs_mkdir` | EBX=path, ECX=mode | EAX=0 / -1 | Create a directory |
| `vfs_unlink` | EBX=path | EAX=0 / -1 | Remove a file or empty directory |

### VFS File Descriptor

Each open object is tracked by a 32-byte VFD in `vfs_fd_table` (64 slots, fds 0–63).
Fields (see `kernel/vfs.inc`):

| Offset | Size | Field | Description |
| --- | --- | --- | --- |
| 0 | 4 | `VFD_STATE` | `VFD_FREE` (0) / `VFD_OPEN` (1) |
| 4 | 4 | `VFD_FS_TYPE` | `VFS_FS_HBFS` / `PROC` / `DEV` / `TMP` / `SOCK` |
| 8 | 4 | `VFD_REAL_FD` | Backend handle: legacy fd index, proc/dev subtype, or socket index |
| 12 | 4 | `VFD_POS` | Current position (HBFS/proc/dev/tmp) |
| 16 | 4 | `VFD_FLAGS` | Open flags (`FD_FLAG_READ` / `FD_FLAG_WRITE`) |
| 20 | 12 | reserved | Padding to 32 bytes |

### Sockets in the fd namespace (v14)

Sockets used to have their own 0–31 namespace, so an fd meant one thing to `read()` and
something else to `recv()`. They now occupy VFDs like every other open object:

- `SYS_SOCKET` and `SYS_ACCEPT` return a **VFD** (`VFS_FS_SOCK`, `VFD_REAL_FD` = socket
  index). If no VFD slot is free, the socket is released again — no leak.
- Every socket syscall (`connect`, `send`, `recv`, `sendto`, `recvfrom`, `bind`,
  `listen`, `accept`, `poll`, `setsockopt`, `getsockopt`, `getsockname`,
  `getpeername`, `shutdown`) starts with `vfd_to_sock`, which rejects anything that is
  not an open socket VFD.
- `close()` releases the VFD and the socket, sending a TCP FIN (`sock_close`).
  `SYS_SOCKCLOSE` is now just an alias for the same path.
- `read()`/`write()` on a socket return −1: use `send`/`recv` (they carry flags and
  addresses). `seek()` returns −1 as well (ESPIPE).

The VFD table is still global; only the legacy 8-entry HBFS fd table is swapped per task.
Making `vfs_fd_table` per-task is the remaining isolation step.

---

## Physical Memory Manager (PMM)

### Overview

The PMM uses a **bitmap allocator** at `PMM_BITMAP` (0x500000). Each bit represents one
4 KB page. Bit = 1 means used, bit = 0 means free.

### Specifications

| Property | Value |
| --- | --- |
| Page size | 4096 bytes (4 KB) |
| Bitmap size | 128 KB (0x20000 bytes) |
| Total bits | 1,048,576 |
| Max addressable | 4 GB (theoretical) |
| Practical limit | 448 MB (`USER_DEMAND_BASE_MAX`: frames may not reach user space) |

The first 128 MB (`PAGING_MAP_MB`) are identity-mapped at boot; RAM above that is
mapped on demand by the page-fault handler the first time the kernel touches it, so a
large guest costs page tables, not boot time.

### Initialization

1. All bits set to 1 (mark everything as used)
2. E820 memory map entries are iterated
3. Usable regions **above 7 MB** are freed via `pmm_free_region`
4. This ensures kernel, program space, and PMM bitmap are never allocated

### Key Routines

| Routine | Parameters | Description |
| --- | --- | --- |
| `pmm_alloc_page` | — | Returns first free page address (EAX), sets bit |
| `pmm_free_page` | EAX = address | Clears bit for given address |
| `pmm_alloc_pages` | ECX = count | Allocates N contiguous pages, returns base in EAX |
| `pmm_count_free` | — | Counts free pages (Kernighan's bit-clearing method) |

### Kernel slab allocator (`kernel/kmem.inc`)

Kernel objects that exist once per task - its mmap area table (896 bytes) and its argv
snapshot (512 bytes) - used to live in arrays sized for `MAX_TASKS`, so the image carried
128 copies of each whether or not a task was alive. That was 180 KB of the 2 MB kernel
for tables most of which never existed; both are now slab objects.

| Routine | Parameters | Description |
| --- | --- | --- |
| `kmem_init` | — | Resets the cache table and registers the built-in caches |
| `kmem_cache` | EAX = object size | Registers a cache; returns its id (EAX), or -1 |
| `kmem_alloc` | EAX = cache id | Returns a zeroed object (EAX), or 0 when out of memory |
| `kmem_free` | EAX = cache id, EBX = object | Returns the object to its cache |

A cache records its object size, how many objects fit in a page, and a free-list head.
Free objects are threaded through their own first dword, so no metadata lives outside
the objects. A page is taken from the PMM only when the free list is empty, and pages are
never returned: a cache keeps them and reuses them. `kmem_alloc` zeroes every object it
hands out, because task slots are recycled as soon as their occupant exits and a stale
area table would let a new task fault in frames it does not own.

`kmem_alloc` runs on task creation and `SYS_GETARGS`, not from the fault handler: a task's
area table is allocated before it can run, and `vm_area_base` returns 0 for a slot without
one so `vm_find_area` fails closed instead of dereferencing it. Task teardown frees both
objects (`vm_free_table`, `task_args_free`) on every path - normal exit, signal kill,
ready-task cleanup and the kill-all sweep.

---

## HBFS Filesystem

HBFS (HoneyBadger File System) is a simple block-based filesystem with flat directories,
a block allocation bitmap, and support for nested subdirectories.

### Block cache

File data reads are served through a 32-entry, 4 KB block cache
(`kernel/blkcache.inc`, 128 KB of BSS):

| Routine | Purpose |
| --- | --- |
| `blk_cache_read` | Drop-in for `ata_read_sectors`: a whole, block-aligned read is served from the cache on a hit and fills the cache on a miss; anything else passes straight through |
| `blk_cache_flush` | Marks every entry empty |
| `blk_cache_init` | Flushes the cache and rewinds the replacement hand at boot |

The cache is keyed by absolute block number and uses a round-robin hand for
replacement. Coherence is blunt and total: `ata_write_sectors` calls
`blk_cache_flush` after every successful write, so a block can never serve a
stale copy. Metadata and journal traffic bypasses the cache completely
(their reads are not whole-block data reads).

`fd_read`, `hbfs_read_file` and the demand-paged file-mapping path
(`vm_fault_file_page`) all read through it.

### Page cache

Shared file mappings (`SYS_MMAP_FILE` with `MF_SHARED`) are served by a page cache
(`kernel/pagecache.inc`, 128 entries) rather than by private frames, because two
mappings of one file have to see each other's writes:

| Routine | Purpose |
| --- | --- |
| `pc_get` | Frame for an HBFS block, loaded on first use, with one reference taken |
| `pc_ref` / `pc_unref` | Add or drop a reference; `pc_unref` reports whether the cache owns the frame |
| `pc_ref_range` | Take one reference per present PTE of a range (fork) |
| `pc_init` | Marks every entry empty at boot |

The key is the HBFS block number counted from `HBFS_DATA_START` - the file's first
data sector is block-aligned within the file, but the data area does not start on a
block boundary of the disk. `pc_refs` counts mapped page-table entries, not mappings:
a demand fault that installs a PTE takes one and fork takes one per inherited PTE,
because a shared mapping is deliberately not copy-on-write. An entry with references
is never evicted; an unreferenced one is evicted round-robin and its frame goes back
to the PMM.

Dirty state is not stored in the cache: the write sets the dirty bit of the PTE that
performed it, and that mapping's own release writes the block back before dropping its
reference. So a modified frame always has at least one dirty PTE that will flush it,
and an entry is clean by the time its count reaches zero. If every entry is still
mapped, a shared page falls back to a private frame: correct data, but not coherent
with other mappings of that block until the cache has room.

### On-Disk Layout

| Region | LBA Start | LBA Count | Size | Description |
| --- | --- | --- | --- | --- |
| Superblock | 4096 | 1 | 512 B | Filesystem metadata |
| Bitmap | 4097 | 128 | 64 KB | 16 blocks × 8 sectors × 512 B |
| Root Directory | 4225 | 256 | 128 KB | 32 blocks × 4 KB |
| Data Area | 4481 | — | — | File/directory data blocks |

### Constants

| Constant | Value |
| --- | --- |
| `HBFS_MAGIC` | `0x48424653` (`'HBFS'`) |
| `HBFS_BLOCK_SIZE` | 4096 (4 KB) |
| `HBFS_SECTORS_PER_BLK` | 8 |
| `HBFS_MAX_FILENAME` | 252 characters |
| `HBFS_DIR_ENTRY_SIZE` | 288 bytes |
| `HBFS_BITMAP_BLOCKS` | 16 |
| `HBFS_BITMAP_SIZE` | 65,536 bytes (64 KB) |
| `HBFS_ROOT_DIR_BLOCKS` | 32 |
| `HBFS_ROOT_DIR_SECTS` | 256 (32 × 8) |
| `HBFS_ROOT_DIR_SIZE` | 131,072 bytes (128 KB) |
| `HBFS_MAX_FILES` | 455 (131,072 / 288) |
| `HBFS_SUBDIR_BLOCKS` | 16 |
| `HBFS_SUBDIR_SIZE` | 65,536 bytes (64 KB) |
| `HBFS_SUBDIR_MAX_ENTRIES` | 224 (65,536 / 288) |
| `HBFS_SUPERBLOCK_LBA` | 4096 |
| `HBFS_BITMAP_START` | 4097 |
| `HBFS_ROOT_DIR_START` | 4225 |
| `HBFS_DATA_START` | 4481 |
| `TOTAL_BLOCKS` | 524,288 (≈ 2 GB) |

### Superblock Structure (512 bytes at LBA 4096)

| Offset | Size | Field | Default |
| --- | --- | --- | --- |
| 0 | 4 | Magic (`'HBFS'`) | `0x48424653` |
| 4 | 4 | Version | 1 |
| 8 | 4 | Total blocks | 524,288 |
| 12 | 4 | Free blocks | (updated at populate time) |
| 16 | 4 | Root directory LBA | 4225 |
| 20 | 4 | Bitmap start LBA | 4097 |
| 24 | 4 | Data start LBA | 4481 |
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

- 16 blocks = 128 sectors = 65,536 bytes
- Each bit represents one 4 KB data block
- 524,288 blocks × 4 KB = **2 GB** maximum filesystem size
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
| `hbfs_init` | Reads superblock, validates magic, replays the journal if dirty, loads the bitmap and root directory |
| `hbfs_find_file` | Scans loaded directory for filename match |
| `hbfs_create_file` | Allocates blocks, writes data, creates directory entry — one transaction |
| `hbfs_delete_file` | Frees blocks, clears directory entry — one transaction |
| `hbfs_read_file` | Path-aware: resolves `/` paths, reads file to buffer |
| `hbfs_load_root_dir` | Loads the current directory into `hbfs_dir_buf` and mirrors it into `hbfs_dir_shadow` |
| `hbfs_save_root_dir` | Writes back **only the sectors that differ from the shadow**, each journaled |
| `hbfs_alloc_blocks` | Scans the resident bitmap for N contiguous free blocks |
| `hbfs_free_blocks` | Clears bitmap bits for a range of blocks |
| `hbfs_meta_write` | Journal-then-write primitive used by every metadata write |
| `hbfs_journal_begin` / `_log_sector` / `_commit` / `_abort` / `_recover` | Write-ahead undo log (see below) |

### Write-Ahead Journal (crash consistency)

The first `HBFS_JOURNAL_BLOCKS` (4) data blocks — 16 KB, 32 sectors — are reserved for an
**undo log**. Every metadata mutation runs inside a transaction:

1. `hbfs_journal_begin` writes `JRNL_STATE=1` to the superblock and resets the pointer.
   Nested begins only increment `hbfs_journal_depth`, so a helper that allocates blocks
   *and* writes a directory entry still produces a single atomic unit.
2. Each metadata sector is logged as a **pre-image**: a header sector
   (`[magic][type=PRE][target LBA][epoch]`) followed by a copy of the sector as it
   is *now*, written **before** the new content.
3. `hbfs_journal_commit` writes a commit marker at the write pointer, clears
   `JRNL_STATE`, and flushes the superblock.

Every transaction increments a **journal epoch** (`HBFS_SB_JRNL_EPOCH`, superblock
offset 56) and stamps it into each record. Recovery stops at the first record whose epoch
does not match the current one. Without that, a crash mid-transaction would replay not
only that transaction's records but also *stale* records left in the circular log by an
already-committed transaction — silently rolling the filesystem back to an older state
(observed as "file entry present, its block marked free").

A sector is **logged at most once per transaction** (`hbfs_journal_logged`). A create that
replaces an existing file writes the bitmap sector and the directory sector twice (free
then allocate, clear then re-add the entry); logging the second write would record an
*intermediate* state, and undoing to it would leave the directory entry and the bitmap
disagreeing. The first pre-image is the pre-transaction content, which is what undo
needs.

On mount with `JRNL_STATE == 1`, `hbfs_journal_recover` walks the log in order and writes
every pre-image back to its target LBA, stopping at the first commit marker. That is the
whole recovery rule:

| Crash point | Result after replay |
| --- | --- |
| Before the data sector is written | Undo restores the untouched sector — no change |
| After the data sector, before the commit marker | The mutation is rolled back — old state |
| After the commit marker | Replay stops; the new state stands (only `JRNL_STATE` needs clearing) |

If the log cannot take another record the transaction **fails** rather than proceeding
unjournaled: `hbfs_journal_log_sector` returns CF=1, `hbfs_meta_write` calls
`hbfs_journal_abort`, and the caller reports an error. `hbfs_journal_abort` seals the log
with a commit marker at the write pointer (so stale records from an older transaction are
never replayed), runs the same undo pass, and clears the state.

File **data** is deliberately not journaled — only metadata is. A crash can therefore
leave a file whose contents are old, but never an allocation or directory state that
contradicts itself.

Two rules matter when touching this code:

- **`ata_read_sectors` takes its destination in EDI and `ata_write_sectors` its source in
  ESI.** Both the pre-image write and the replay originally left the target LBA in EDI (and
  put the read buffer in ESI), so the read landed at a low memory address and an unfilled
  buffer was written over the real sector. Replaying a bitmap record therefore replaced
  sector 0 of the bitmap with a journal header — the filesystem then reported every block
  as free.
- **Only dirty sectors may be written.** A full directory rewrite is 128 KB and can never
  fit in a 16 KB log, which is why `hbfs_save_root_dir` diffs against `hbfs_dir_shadow`.

### Performance: what a metadata operation costs

| Operation | Before | Now |
| --- | --- | --- |
| Change one directory entry | 128 KB directory rewrite | one 512-byte sector |
| Allocate/free blocks | 64 KB bitmap rewritten | 1–2 bitmap sectors covering the range |
| Read the bitmap | 64 KB every allocation | resident (`hbfs_bitmap_valid`) |

The bitmap and the loaded directory are cached in kernel memory; the shadow copy lets the
writer compute the exact dirty set without any write tracking in the callers.

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

## ATA Bus Master DMA

### Overview (`kernel/atadma.inc`)

Extends the base ATA PIO driver with DMA read support.  Detects an Intel PIIX3/4 IDE
controller (PCI 8086:7010 or 8086:7111) and, if found, provides a faster DMA path for
bulk sector reads.

### Detection & Setup

| Step | Description |
| --- | --- |
| PCI scan | Calls `pci_find_device(0x8086, 0x7010/0x7111)` |
| BAR 4 read | Obtains Bus Master IDE I/O base (16-byte range) |
| PRD table | 4 KB contiguous buffer, aligned to 4 KB page boundary |
| Bounce buffer | 4 KB page for DMA target; physical address stored in PRD |

### Key Routine

| Routine | Parameters | Description |
| --- | --- | --- |
| `atadma_read_sectors` | EAX=LBA, ECX=count (≤8), EDI=dest | DMA read then `memcpy` to dest; falls back to `ata_read_sectors` if DMA unavailable |

---

## AHCI SATA Driver

### Overview (`kernel/ahci.inc`)

AHCI (Advanced Host Controller Interface) driver for SATA disks. Detected and
initialised after PCI enumeration.

- Probes PCI for class `0x01` / subclass `0x06` (SATA AHCI) via `pci_dev_table` scan.
- Reads **BAR5** (ABAR) for the MMIO base address.
- Finds the first port where `DET=3` (device detected) and `SIG=0x00000101` (ATA disk).
- Operates in **polling mode** (no interrupts), using command slot 0 only.
- `ahci_present` flag (byte) is set to 1 on successful init; all APIs check this flag
  and return `CF=1` silently if AHCI is unavailable (fallback to legacy ATA).

### Key Routines (AHCI)

| Routine | Parameters | Description |
| --- | --- | --- |
| `ahci_init` | — | Detect and initialise AHCI controller |
| `ahci_read_sectors` | EAX=LBA_lo, EDX=LBA_hi, ECX=count, EDI=dest | Read sectors from first AHCI drive |
| `ahci_write_sectors` | EAX=LBA_lo, EDX=LBA_hi, ECX=count, ESI=src | Write sectors to first AHCI drive |
| `ahci_flush` | — | Issue FLUSH CACHE to AHCI drive |

---

## PCI Bus Driver

### Overview (`kernel/pci.inc`)

Generic PCI bus enumerator.  Scans buses 0–7, all 32 devices, all 8 functions using
configuration mechanism 1 (port `0xCF8`/`0xCFC`).  Builds an in-memory device table
of up to 64 entries.

### I/O Ports

| Port | Description |
| --- | --- |
| `0xCF8` | PCI CONFIG_ADDRESS (write to select bus/device/function/register) |
| `0xCFC` | PCI CONFIG_DATA (read/write selected register) |

### Device Table Entry (16 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | Vendor ID |
| 2 | 2 | Device ID |
| 4 | 1 | Bus number |
| 5 | 1 | Device number |
| 6 | 1 | Function number |
| 7 | 1 | Reserved |
| 8 | 4 | BAR 0 |
| 12 | 4 | BAR 1 |

### Key Routines (PCI)

| Routine | Parameters | Description |
| --- | --- | --- |
| `pci_init` | — | Enumerate all PCI devices, populate table |
| `pci_find_device` | EBX=vendor, ECX=device_id | EAX = device table index / -1 |
| `pci_read_config32` | EBX=bdf, ECX=offset | EAX = dword |
| `pci_write_config32` | EBX=bdf, ECX=offset, EDX=value | — |
| `pci_read_bar` | EBX=bdf, ECX=bar_index | EAX = BAR value (mask applied) |

### `SYS_PCI_FIND` (syscall 101)

User programs can query the PCI table: `EBX=vendor_id ECX=device_id → EAX=bdf or -1`.

---

## Intel e1000 NIC Driver

### Overview (`kernel/e1000.inc`)

Driver for the Intel 82540EM (PCI `8086:100E`) and compatible e1000 family Ethernet
adapters. Initialised by `e1000_init` immediately after `net_init` during kernel startup.

- Probes PCI device table for vendor `0x8086`, device `0x100E`.
- Maps MMIO BAR0 as the register base (EEPROM read → 6-byte MAC).
- Configures a single 16-entry **TX descriptor ring** and 32-entry **RX descriptor ring**,
  both backed by 4 KB PMM pages.
- TX and RX are polling-based. `e1000_init` currently runs before `pci_init` and BAR0 is
  above the identity-mapped range, so the RTL8139 driver remains the active NIC in the
  shipped QEMU configuration.
- The MAC address is written into `net_mac` for the network stack.

### Key Routines (e1000)

| Routine | Parameters | Description |
| --- | --- | --- |
| `e1000_init` | — | Detect, map, and initialise the adapter |
| `e1000_send_packet` | ESI=buf, ECX=len | Copy frame into next TX descriptor and kick TCTL |
| `e1000_poll_rx` | — | Drain the RX ring (polling helper; not wired into the active stack) |

---

## AC'97 Audio Driver

### Overview (`kernel/ac97.inc`)

Intel AC'97 audio controller driver for ICH2/3/4 (PCI 8086:2415, 8086:2425, 8086:2445).
Uses PCI Bus Master DMA for PCM playback.  Falls back gracefully if device not present.

### Buffer Descriptor List (BDL)

| Property | Value |
| --- | --- |
| Entries | 32 (each 8 bytes: physical address + control/length word) |
| Ping-pong buffers | 2 × 32 KB (each holds ~185 ms of 16-bit 44100 Hz stereo) |
| Loop mode | Last Valid Index wraps to ping-pong boundary |

### Initialization (`ac97_init`)

1. `pci_find_device(0x8086, 0x2415/0x2425/0x2445)` — bail if absent
2. Read NAM BAR (I/O, ~256 bytes) and NABM BAR (Bus Master, ~64 bytes)
3. Cold reset NAM, wait for codec ready
4. Set master volume and PCM out volume to max
5. Allocate BDL and ping-pong buffers via `pmm_alloc_pages`
6. Load BDL address into PCM Out Base Address register

### Hooks

`SYS_AUDIO_PLAY` dispatches to `sb16_play` first; if SB16 is absent, it tries `ac97_play`.

---

## VirtIO Devices

### Overview (`kernel/virtio.inc`)

VirtIO PCI legacy driver (spec 0.9.5).  Supports two device classes:

| Device | PCI IDs | Description |
| --- | --- | --- |
| **virtio-blk** | 1AF4:1001 / 1AF4:1042 | Virtqueue-based block device |
| **virtio-net** | 1AF4:1000 / 1AF4:1041 | Network device (MAC read + feature negotiation) |

### virtio-blk Read Path

1. Build a 3-descriptor chain in the virtqueue: header → data buffer → status byte
2. Post descriptor index to the Available Ring
3. Notify device via BAR0+`0x10` (Queue Notify)
4. Poll Used Ring for completion
5. Return data to caller via `virtio_blk_read(EAX=LBA, ECX=count, EDI=dest)`

### Initialization

`virtio_init` is called from `kernel_entry` after `atadma_init`.  Scans for virtio-blk
and virtio-net using `pci_find_device`; silently skips if neither is present.

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

## GDB Remote Stub

### Overview (`kernel/gdbstub.inc`)

A GDB RSP (Remote Serial Protocol) stub that allows remote debugging of the running
kernel over the serial port. Initialised by `gdbstub_init` near the end of the kernel
boot sequence.

- Listens on **COM1** (115200 8N1).
- Handles the standard RSP packet set: `?`, `g`/`G` (register read/write), `m`/`M`
  (memory read/write), `c` (continue), `s` (single-step via TF bit), `k` (kill), `z`/`Z`
  (software breakpoints via `INT 3`).
- The **Ctrl+Alt+G** hotkey (detected by the keyboard ISR) triggers an immediate software
  breakpoint, dropping control to a connected GDB client.
- All register state is saved/restored via the existing IDT fault frame — no additional
  stack frame is needed.

### Connecting

```text
(gdb) target remote /dev/ttyUSB0   # or COM port on host
(gdb) set architecture i386
(gdb) symbol-file kernel.elf       # if you have a debug ELF build
```

---

## Scheduler & Task Control

### Overview (`kernel/sched.inc`)

Mellivora OS uses a **preemptive priority-based round-robin scheduler**. The PIT fires at
100 Hz; on each ring-3 timer interrupt the scheduler picks the next runnable task.
**Ring-0 code (kernel / shell) is never preempted** — context switches only happen on
return from an interrupt that was taken in ring 3.

### Parameters

| Parameter | Value |
| --- | --- |
| Tick rate | 100 Hz (10 ms) |
| Maximum tasks | 128 (ring-3) + shell (ring-0) |
| Priority levels | 4 — HIGH (0), NORMAL (1), LOW (2), IDLE (3) |
| Scheduling policy | Priority-based round-robin within each level |
| Blocking operations | `waitpid` blocks the task in `TASK_BLOCKED` until the child exits |

### Task Control Block (TCB)

Each task occupies **136 bytes** in the static `task_table` array
(`MAX_TASKS × TCB_SIZE`). Layout:

| Offset | Size | Name | Description |
| --- | --- | --- | --- |
| 0 | 4 | `TCB_ESP` | Saved kernel-mode ESP (pushad + iretd frame) |
| 4 | 4 | `TCB_STATE` | 0=free, 1=ready, 2=running, 3=blocked, 4=stopped, 5=zombie |
| 8 | 4 | `TCB_KSTACK` | Top of this task's kernel stack page (TSS ESP0) |
| 12 | 4 | `TCB_USTACK` | User-mode stack top (randomised per launch; the exit trampoline sits at this address and ESP starts here) |
| 16 | 4 | `TCB_ENTRY` | Ring-3 entry point |
| 20 | 4 | `TCB_PID` | Task ID |
| 24 | 4 | `TCB_PRIORITY` | Priority level (0–3) |
| 28 | 4 | `TCB_WAKEUP` | Wakeup tick for `sys_sleep` |
| 32 | 16 | `TCB_NAME` | Task name (NUL-terminated, 15 chars max) |
| 48 | 4 | `TCB_SIG_PEND` | Pending signal bitmask |
| 52 | 4 | `TCB_SIG_MASK` | Blocked signal bitmask |
| 56 | 4 | `TCB_PGID` | Process group ID |
| 60 | 4 | `TCB_EXIT_CODE` | Exit status (set on `sys_exit`) |
| 64 | 4 | `TCB_BRK` | Program break for `sbrk` |
| 68 | 4 | `TCB_PPID` | Parent PID (`-1` = shell-launched, no parent) |
| 72 | 4 | `TCB_UID` | Real user ID |
| 76 | 4 | `TCB_GID` | Real group ID |
| 80 | 4 | `TCB_ALARM` | `alarm()` deadline in PIT ticks |
| 84 | 4 | `TCB_SIG_HAND` | Signal handler table pointer |
| 88 | 4 | `TCB_EUID` | Effective user ID |
| 92 | 4 | `TCB_EGID` | Effective group ID |
| 96 | 4 | `TCB_SID` | Session ID |
| 100 | 4 | `TCB_UMASK` | File creation umask |
| 104 | 4 | `TCB_ITIMER_VAL` | ITIMER_REAL countdown in ticks |
| 108 | 4 | `TCB_ITIMER_INT` | ITIMER_REAL reload interval in ticks |
| 112 | 4 | `TCB_SAVED_MASK` | Signal mask saved across `sigsuspend` |
| 116 | 4 | `TCB_WAIT_PID` | PID being waited on (0 = none) |
| 120 | 4 | `TCB_FD_TABLE_PTR` | Physical address of the per-task fd table page |
| 124 | 4 | `TCB_ERRNO` | Per-task errno value |
| 128 | 4 | `TCB_CR3` | Physical address of this task's page directory (0 = master) |
| **136** | — | `TCB_SIZE` | Total TCB size |

### Per-task File Descriptor Isolation (v12.1, extended v14)

Each task owns a private 4 KB page holding **both** fd views:

| Offset | Size | Contents |
| --- | --- | --- |
| 0 | 256 | Legacy HBFS `fd_table` (`FD_MAX × FD_ENTRY_SIZE`) |
| 256 | 2048 | VFS `vfs_fd_table` (`VFS_MAX_FDS × VFD_SIZE`) |

`sched_swap_fd_tables` is called at every context-switch point (scheduler tick,
`sys_yield`, shell→task start, signal delivery). It saves the live tables into the
outgoing task's page and loads the incoming task's copy into the globals, so one process
closing a descriptor cannot affect another's view. The shell has the same pair of
snapshots (`shell_fd_table`, `shell_vfd_table`).

- No cross-task fd aliasing — two tasks with the same fd number refer to independent
  descriptors (verified by `programs/fd_isolate_test.asm`).
- Inheritance — a new task's page starts as a copy of the launcher's view, so
  redirections active at launch are visible while later changes stay isolated.
- Cleanup on exit — the page is freed by `sched_exit_task` / `sched_deliver_signals`,
  independently of the kernel stack page.

`FD_PAGE_VFD_BYTES` in `kernel.asm` must equal `VFS_MAX_FDS * VFD_SIZE`; `kernel/vfs.inc`
asserts this at assembly time with `%if`/`%error`.

### Signal Delivery & W^X Trampoline (v11/v12.1)

When delivering a user-space signal, the kernel builds a signal frame on the task's
ring-3 stack:

| Slot | Content |
| --- | --- |
| `[esp+0]` | Return address → `SIGRETURN_TRAMPOLINE_VADDR` (0x1FFFF000) |
| `[esp+4]` | Signal number (EBX arg for handler) |
| `[esp+8..n]` | Saved caller registers (EFLAGS, EIP, …) |

`SIGRETURN_TRAMPOLINE_VADDR` (0x1FFFF000) is a **read-only, no-write, executable** page
mapped at boot (part of paging init). It contains an 8-byte stub:

```nasm
mov eax, SYS_SIGRETURN   ; 144
int 0x80
```

This design is **W^X compliant** — the trampoline page is never writable. `sys_sigreturn`
pops the saved register state from the signal frame and resumes user code.

### Job Control: `kill`, Process Groups, and Ctrl+C

`SYS_KILL` (53) is `kill(pid, sig)`:

| Register | Meaning |
| --- | --- |
| EBX | PID to signal, or a **negative** process-group id (`-pgid`) to signal a group |
| ECX | Signal number 1..31; anything else is treated as `SIGTERM` |
| EAX | 0 if at least one task was signalled, −1 otherwise |

`SYS_SETPGID` (75) sets a task's `TCB_PGID` (ECX = 0 means "use that task's own PID"), and
`SYS_GETPGID` reads it back — enough for a shell to put a pipeline in one group and signal
the whole job.

### The Terminal's Foreground Process Group

Keyboard signals belong to the console, not to "whichever task happens to be running":

| Syscall | Meaning |
| --- | --- |
| `SYS_TCSETPGRP` (183) | `tcsetpgrp(pgid)` — hand the console to a group; `-1` clears it. Fails if no live task belongs to that group |
| `SYS_TCGETPGRP` (184) | `tcgetpgrp()` — read it back (`-1` when no job owns the console) |

The value lives in `tty_fg_pgid` and starts at **-1**, not 0: PGIDs are task PIDs, so the
task in slot 0 genuinely has PGID 0 and cannot double as the "no owner" sentinel.

`sched_kbd_signal(EAX = signum)` decides where Ctrl+C/Ctrl+Z go: the whole foreground
group, or the running task when no group is set. It returns whether the interrupted context
survives — the keyboard handler returns to the program when it does, and otherwise hands
the console back to the shell (`SYS_EXIT`'s foreground path does the same, and clears
`tty_fg_pgid`).

Ctrl+Z is `SIGTSTP`: the job becomes `TASK_STOPPED` and keeps its TCB, fd view and address
space, so `fg <pid>` (`SIGCONT` + `tcsetpgrp`) or `bg <pid>` (`SIGCONT`) can resume it.

`sched_signal_task(EDI = TCB, EAX = signum)` is the single decision point:

| Target state | Result |
| --- | --- |
| Handler installed (not `SIG_DFL`/`SIG_IGN`) | `TCB_SIG_PEND` bit set; `TASK_BLOCKED` → `TASK_READY` so the next switch delivers it |
| `SIG_IGN` | Signal discarded |
| `SIG_DFL`, or no handler table at all | Task destroyed immediately (kstack, fd page, handler page, PD deferred-free) |
| `TASK_RUNNING` | Always queued — that is the caller, and its kernel stack must not be freed |

Delivery happens in `sched_deliver_signals(EBX = TCB, EDI = frame base)` and therefore needs
**the receiving task's address space installed first**: the signal frame is written to that
task's ring-3 stack. Every switch path (`sys_yield`, `sys_waitpid`, the timer) applies the
incoming task's CR3 and delivers **before committing anything** — before the fd-table swap,
the state change and `TCB_ESP` load. If a default action kills the candidate at that point,
nothing has been committed, so the candidate is simply skipped (waitpid keeps scanning) or
the caller resumes (yield). `sys_yield` also delivers for the running task when it stays
put, because a program that yields in a tight loop spends most of its time in ring 0, where
the timer never advances the scheduling quantum.

Four signals are handled before the handler lookup, because their semantics are not
"terminate by default". A RUNNING target (the caller itself) is always queued first —
tearing it down would free the kernel stack the syscall is standing on:

| Signal | Behaviour |
| --- | --- |
| `SIGKILL` (9) | Destroyed, ignoring any handler (it cannot be caught); queued first if it targets the caller |
| `SIGCONT` (25) | `TASK_STOPPED` → `TASK_READY`, otherwise no-op |
| `SIGTSTP` (20) | Stops the task unless it installed a handler, which then decides at delivery |
| `SIGCHLD` (17) | Ignored unless a handler is installed — `sched_exit_task` queues SIGCHLD on every parent, so "terminate" here would kill it the next time it ran |

`SYS_SIGNAL` (74) is an alias (`jmp sys_kill`): the shell's `kill`/`bg`/`fg` builtins use it,
and one implementation keeps handler dispatch, process groups and teardown identical.

Ctrl+C is `SIGINT` (2) and Ctrl+Z is `SIGTSTP` (20). Both go through `sched_kbd_signal`,
which signals the terminal's foreground process group (falling back to the running task):

| Outcome | Effect |
| --- | --- |
| A member has a handler | The signal is queued, EOI, `iret` — the job keeps running and the handler runs at the next delivery point |
| `SIG_IGN` | EOI, `iret` — nothing happens |
| `SIG_DFL` / no handler | Task torn down (Ctrl+C) or stopped (Ctrl+Z); the shell's fd view and master directory are restored and `tty_fg_pgid` cleared |

The handler table itself is a lazily allocated 4 KB page (`TCB_SIG_HAND`, indexed
`[table + signum*4]`) created by `SYS_SIGACTION` and freed on every teardown path.

**Pending signals are a bitmask, not a queue.** Sending the same signal twice before the
target next runs sets the same bit twice and is delivered **once** — the classic
non-realtime (`sigaction`-without-`SA_SIGINFO`) behaviour. Any protocol that counts events
must therefore use a *different* signal per event (`SIGUSR1` and `SIGUSR2`, one per child)
or a shared counter that the receiver reads after a single wakeup.

**Signal delivery clobbers `EAX`, `ECX`, `EDX` and `ESI`** (`sched_deliver_signals` uses
them as scratch while building the frame). Any kernel switch path that calls it and still
needs those registers must save them across the call — `.yld_switch` in `sys_yield` keeps
`ESI` (the destination slot index) for exactly this reason.

### Syscall Register Contract

`int 0x80` returns a value **only in `EAX`**; every other register is preserved. Bodies
implement this with `pushad`/`popad` and by writing the result to `[esp + 28]`, or with
explicit `push`/`pop` of the registers they use as scan variables. User code is entitled
to keep a value in `ECX`/`EDI` across a syscall.

`SYS_GETPID` and `SYS_GETPPID` used to break this: they walked `task_table` with `ECX` and
`EDI` and returned them to the caller. A program that had loaded a signal number into
`ECX` before `SYS_GETPPID` therefore called `SYS_KILL` with signal *1* (`SIGHUP`) instead —
and since the caller was RUNNING, the signal was queued and then terminated it at the next
switch point. Both syscalls now save and restore those registers.

---

## Process Execution

### Loading a Program

1. Shell parses command into program name (`prog_name_buf`, 256 bytes) and arguments
   (`program_args_buf`, 512 bytes)
2. Attempts to load file via `hbfs_read_file` to `PROGRAM_BASE` (0x300000)
3. If not found in current directory, searches PATH directories

### ELF vs Flat Binary Detection

The kernel checks for `ELF_MAGIC` (`0x464C457F`) at `PROGRAM_BASE`:

- **ELF:** `elf_load_program` validates ELF32 little-endian, iterates program headers,
  loads `PT_LOAD` segments (copies file data + zeroes BSS), returns entry point
- **Flat binary:** Entry point = `PROGRAM_BASE` directly

### Ring 3 Execution

1. Set `program_running = 1`, clear `ctrl_c_flag` and `program_exit_code`
2. Clear `[program_brk_init, PROGRAM_END_ADDR)` - the tail of the shell's window - unless
   this is an exec in place. A task's window is a copy of the shell's, so without this the
   next program could read the previous one's heap, `mmap` scratch and whole user stack.
   `program_brk_init` is the page-aligned end of the image the loader just wrote (and is
   clamped to `[PROGRAM_BASE + 4K, USER_STACK_BASE]`), so the image is never touched.
3. Pick this launch's stack top (`vm_random_stack_top`): `PROGRAM_EXIT_ADDR - 4` minus
    a page-granular offset of up to `STACK_ASLR_PAGES` (16) pages, stored in
    `program_stack_top` and, for a task, in `TCB_USTACK`. A task that is *already
    running* - an exec in place via `SYS_EXEC` - keeps its top instead: the pages
    outside its existing window are unmapped in its directory, so a new top could put
    the trampoline in a page that does not exist there. GUI launches always draw a new
    top because they always create a task.
4. Write **SYS_EXIT trampoline** at the chosen top + 4:

   ```nasm
   mov eax, 0    ; SYS_EXIT
   int 0x80
   ```

   The top itself holds the trampoline's address as the return address, so a program that
   ends with `RET` exits instead of running off the end of its code. Neither address is
   fixed any more: both move with the chosen top.
5. Create the task (`sched_create_task`) and switch to it with `sched_start_task`. The
   task's own kernel stack becomes TSS ESP0, so kernel work during its syscalls never
   touches the shell's stack. If task creation fails, the legacy direct-IRET path is
   used: TSS ESP0 stays `KERNEL_STACK` and the IRET frame is built by hand.
6. Set up the Ring 3 stack window below the chosen top (`paging_setup_user_stack` takes
   the TCB in `EAX`, reads `TCB_USTACK`): keep the `USER_STACK_INIT_PAGES` pages ending at
   the top page, unmap everything else in `[USER_STACK_BASE, PROGRAM_END_ADDR)` - the
   growth area below and the gap a lowered top leaves above - and release the frames that
   were private to the directory. `TCB_USTACK` is the trampoline's address, so the
   window's exclusive end is `TCB_USTACK + 4`.
7. Perform `IRETD` to Ring 3 (either from the task's saved frame or, on the legacy
   path, from a hand-built frame):
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

### Foreground Programs Run as Tasks

A program started from the prompt is a real scheduler task, not a one-off IRET:

1. `cmd_exec_program` loads the image, writes the exit trampoline, then calls
   `sched_create_task` (`EBX` = entry, `ECX` = user stack top).
2. `sched_start_task` installs the task's CR3, kernel stack and fd view and IRETs into
   ring 3 through the saved frame. It never returns.
3. On `SYS_EXIT` the task is torn down (`sched_exit_task`), the shell's fd view and the
   master directory are restored, and control returns to `shell_main`.
4. Ctrl+C performs the same teardown before jumping back to the shell. Without it, every
   interrupted program would leak a TCB, a kernel stack page, an fd page and a PD.

If `sched_create_task` fails (OOM) the launcher falls back to the legacy direct-IRET
path, which runs the program without a TCB. That path also keeps `parent_active` for
re-launching a parent whose `SYS_EXEC` needed it.

`TCB_PPID` uses **-1** as the "no parent" sentinel rather than 0, because PID 0 is a
valid task slot: a child forked by the task in slot 0 must still be treated as having a
parent (`waitpid`, zombie retention, `SIGCHLD`).

Both ways a task can die end in `sched_publish_death`, so a parent sees the same thing
either way: `SYS_EXIT` runs `sched_exit_task` in the dying task's own context, while a
signal with a terminating default action is torn down by whoever called `kill` and never
runs that path at all. It stores the status, marks the TCB `TASK_ZOMBIE`, counts the task
out, wakes waiters blocked in `waitpid` (writing the status into the EAX slot of their
saved frame) and queues `SIGCHLD`. A shell-launched task (`TCB_PPID` = -1) has nobody to
collect it and is reaped at once.

An exit status with the high bit set means the task did not exit: `0x80000000` alone is a
contained fault, and `0x80000000 | n` is "killed by signal n". `SYS_WAITPID` collects the
status and clears the slot to `TASK_FREE`; killing an existing zombie is a no-op rather
than a second teardown.

### Per-task Program Window (exec isolation)

All directories start as copies of the master, so `PROGRAM_BASE` normally maps the same
physical frames everywhere. Before a task execs, `sys_exec_call` calls
`paging_isolate_program_window`, which:

1. calls `paging_privatize_pde` for PDE 0 — copies the shared page table so remapping
   one directory cannot change another's mapping, then
2. `paging_copy_window_private` copies the 1 MB window at `PROGRAM_BASE` into fresh
   frames and repoints the PTEs.

The loader then writes the new image into frames only that task owns, so a forked child's
`exec` cannot overwrite the code its parent is still running. `sched_exit_task` releases
those frames again (only PTEs that differ from the master are freed, so shared identity
frames are never touched).

`sys_fork` clones the **parent's** page directory (`paging_clone_pd`), not the master, so
a child of an already-exec'd parent inherits that parent's private window.

### Copy-on-Write Fork

fork() no longer copies 64 KB of stack and cannot copy the program image, so the two
address spaces **share frames** until one of them writes:

1. `cow_fork_share` privatises the page tables that cover the shared regions in *both*
   directories (`paging_privatize_pde` per 4 MB PDE — without this a PTE write would leak
   into every task, since directories start as copies of the master).
2. It clears `PG_WRITABLE` on every present PTE in the parent's copy and in the child's,
   and records one extra owner per frame (`cow_share`).
3. The first write in either process takes `#PF` (present + write + user), which
   `cow_break_page` handles: with two or more owners it copies the frame into a private
   page and drops the old reference; with exactly one owner (the other side already
   copied) it simply re-enables writing; with no recorded owners it is a genuine
   protection fault and still crashes.

Shared regions are the program window (`PROGRAM_BASE`, which also holds a foreground
program's stack at its top) and a background task's separately allocated image and stack.
The last two are only shared when they are outside the window, and the stack is skipped
when `TCB_USTACK + 4` lies inside it: the window range already covered those pages, and a
second pass would count their frames twice (and, with a randomised top, walk below
`PROGRAM_BASE` into kernel identity pages).

Bookkeeping is `cow_ref` in `data.inc`: one byte per 4 KB frame the allocator can hand
out (`COW_MAX_PAGES` = `USER_DEMAND_BASE_MAX / 4096`, 448 MB worth; the allocator stops
below that ceiling, so no frame can fall outside the table). **0 means "not shared"**, so
a page that was never forked is freed by its owner directly; `cow_put` returns whether
the caller should free.
Every teardown path is refcount-aware (`paging_free_window_private`, `cow_release_range`
in `sched_exit_task`, the background-exit path), and exec isolation forces `PG_WRITABLE`
on the private copies it makes while releasing the references it replaces.

A task that is killed rather than exiting never runs `sched_exit_task` at all, so
`sched_signal_task` releases the same set itself before it drops the TCB: private window
frames, a separate user stack, and every mmap area with its page-cache references. The
page directory is then queued (`pd_deferred_free`) and released at the next `CR3` switch
by `paging_free_pd`, which frees the directory **and** the page tables it privatised — a
PDE whose table address differs from the master's for the same slot is this directory's
own, everything else is the shared template.


### Arguments and `execve`

`program_args_buf` only holds the launch currently being set up, so each task gets a
snapshot of it in `task_argv_table[PID]` (`sched_set_task_args`, 512 bytes per task).
`SYS_GETARGS` reads the calling task's own slot and falls back to the global buffer only
on the legacy no-TCB path. Without the snapshot a long-running background job would read
the arguments of whatever program the shell started most recently.

`SYS_EXECVE` (182) takes `EBX = path`, `ECX = argv`, `EDX = envp`:

1. the path is validated and copied into `program_args_buf`,
2. `argv[0]` is skipped (the loader already knows the program name) and `argv[1..]` are
   appended, space-separated, capped at 32 entries / 63 bytes each / 511 bytes total,
3. the flattened command line goes through `exec_kernel_line`, the same path `SYS_EXEC`
   uses, so isolation and parent bookkeeping happen exactly once.

Every task created by the shell is named from `prog_name_buf`
(`sched_set_create_name`), so `ps` shows the program instead of `-`.

---

## VBE/BGA Graphics Driver (`kernel/vbe.inc`)

### Overview

Mellivora uses the **Bochs VBE Extensions (BGA)** to set high-resolution linear
framebuffer (LFB) modes from 32-bit protected mode without requiring BIOS INT 10h.
QEMU exposes a compatible BGA adapter on the virtual VGA device.

### BGA I/O Ports

| Port | Name | Description |
| ---- | ---- | ----------- |
| `0x01CE` | `VBE_DISPI_IOPORT_INDEX` | Select BGA register index |
| `0x01CF` | `VBE_DISPI_IOPORT_DATA` | Read/write selected register |

### BGA Register Map

| Index | Name | Description |
| ----- | ---- | ----------- |
| 0 | `VBE_DISPI_INDEX_ID` | BGA version ID (`0xB0C0`–`0xB0C5`) |
| 1 | `VBE_DISPI_INDEX_XRES` | Horizontal resolution in pixels |
| 2 | `VBE_DISPI_INDEX_YRES` | Vertical resolution in pixels |
| 3 | `VBE_DISPI_INDEX_BPP` | Bits per pixel (8/15/16/24/32) |
| 4 | `VBE_DISPI_INDEX_ENABLE` | Enable/disable (`0x01` \| `0x40` for LFB) |
| 10 | `VBE_DISPI_INDEX_FB_ADDR` | LFB physical base address (Bochs ≥2.7) |

### LFB Address Discovery

1. Read BGA register 10 (`VBE_DISPI_INDEX_FB_ADDR`). If non-zero, that is the LFB base.
2. Otherwise, read PCI BAR 0 of the VGA device (Bus 0, Device 2, Function 0) via
   config address `0x80001010`. Mask off the lowest 4 bits.
3. Fallback: use `0xFD000000` (standard QEMU/Bochs mapping).

The LFB is identity-mapped by the paging subsystem so that kernel and user-mode pixel
writes go directly to video RAM. The mapping must exist in **both** the master
directory and the calling task's directory: `paging_map_page` only edits
`PAGE_DIR_ADDR`, so a task running on its own directory would otherwise fault on the
first framebuffer write. `paging_map_page_all` maps the page in the master (which every
future task copies) and in the active directory; demand paging instead uses
`paging_map_page_here` so a task's own pages never leak into other address spaces.

### Double Buffering — Shadow Buffer

To eliminate screen tearing, a **shadow buffer** the same size as the LFB is
allocated via `pmm_alloc_pages` and lives entirely within the first 128 MB of physical
memory (identity-mapped, accessible from Ring 3).

| Variable | Description |
| -------- | ----------- |
| `vbe_shadow_buf` | Physical address of shadow buffer (0 = not yet allocated) |
| `vbe_shadow_pages` | Page count of the current shadow allocation |

Programs obtain the shadow buffer address via `SYS_FRAMEBUF/0` and render all pixels
there. When the frame is complete, `SYS_FRAMEBUF/4` blits the shadow buffer to the real
LFB with a `rep movsd` (single contiguous copy, width×height×Bpp/4 dwords).

If a subsequent mode change requires more pages than currently allocated,
`vbe_set_mode` allocates a new, larger shadow buffer and updates `vbe_shadow_pages`.
This prevents buffer-overrun crashes when a small-mode program (e.g. 640×480) is
followed by a large-mode program (e.g. 1024×768).

### Key Routines (VBE)

| Routine | Description |
| ------- | ----------- |
| `vbe_init` | Detect BGA adapter, read LFB address, identity-map LFB pages |
| `vbe_set_mode` | Program BGA registers, allocate/resize shadow buffer, enable LFB mode |
| `vbe_restore_text` | Disable BGA, re-enable VGA Mode 3, restore VGA font from save buffer |
| `sys_framebuf` | Syscall handler for `SYS_FRAMEBUF` sub-functions 0–4 |

### `SYS_FRAMEBUF` Sub-Functions

| EBX | Sub-function | Input | Return |
| --- | ------------ | ----- | ------ |
| 0 | **get info** | — | EAX=shadow_buf addr, EBX=width, ECX=height, EDX=bpp |
| 1 | **set mode** | ECX=width, EDX=height, ESI=bpp | EAX=0/-1 |
| 2 | **restore text** | — | EAX=0 |
| 3 | **draw text** | ECX=x, EDX=y (PIXELS), ESI=str_ptr, EDI=fg_color | EAX=0 |
| 4 | **present frame** | — | EAX=0 (blits shadow→LFB) |

Sub-function 0 returns the shadow buffer address (not the raw LFB) so that programs
draw into the back buffer automatically. Direct LFB access is provided as fallback only
if the shadow buffer has not yet been allocated.

### Supported Modes

| Resolution | BPP | Shadow size | Pages |
| ---------- | --- | ----------- | ----- |
| 640×480 | 32 | 1.2 MB | 300 |
| 800×600 | 32 | 1.83 MB | 469 |
| 1024×768 | 32 | 3.0 MB | 768 |

### Drawing Primitives (v8.5+)

These syscalls operate directly on the shadow buffer.  Call `SYS_DIRTY_PRESENT` or
`SYS_FRAMEBUF/4` to flush the updated region to the LFB.

| Syscall | # | Arguments | Description |
| ------- | - | --------- | ----------- |
| `SYS_DRAW_LINE` | 95 | EBX=x0, ECX=y0, EDX=x1, ESI=y1, EDI=color | Bresenham line |
| `SYS_DRAW_TRIANGLE` | 96 | EBX=x0\|(y0<<16), ECX=x1\|(y1<<16), EDX=x2\|(y2<<16), ESI=color | Scanline-filled triangle |
| `SYS_BLIT` | 97 | EBX=src_ptr, ECX=dst_x, EDX=dst_y, ESI=w\|(h<<16), EDI=colorkey | Sprite blit with color key |
| `SYS_DIRTY_PRESENT` | 98 | — | Flush smallest bounding box of changed pixels to LFB |
| `SYS_PSF_LOAD` | 99 | EBX=filename_ptr | Load PSF2 bitmap font (≤256 KB) |
| `SYS_PSF_CHAR` | 100 | EBX=x, ECX=y, EDX=codepoint, ESI=fg_color | Render PSF2 glyph into shadow buffer |

**Dirty-rectangle tracking:** `vbe_dirty_present` maintains a bounding box of all
pixels modified since the last present and copies only that region to the LFB, saving
memory bandwidth for partial-update animations.

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
| 34 | `SYS_STDIN_READ` | EBX=buf | EAX=bytes read (-1 if no piped input) |
| 35 | `SYS_YIELD` | — | EAX=0 (cooperative yield) |
| 36 | `SYS_MOUSE` | — | EAX=x, EBX=y, ECX=buttons |
| 37 | `SYS_FRAMEBUF` | EBX=sub (0=info,1=set,2=restore,3=drawtext,4=present) | varies |
| 38 | `SYS_GUI` | EBX=sub-function | varies |
| 39 | `SYS_SOCKET` | EBX=type (1=TCP,2=UDP) | EAX=fd |
| 40 | `SYS_CONNECT` | EBX=fd, ECX=ip, EDX=port | EAX=0/-1 |
| 41 | `SYS_SEND` | EBX=fd, ECX=buf, EDX=len | EAX=bytes |
| 42 | `SYS_RECV` | EBX=fd, ECX=buf, EDX=maxlen | EAX=bytes |
| 43 | `SYS_BIND` | EBX=fd, ECX=port | EAX=0/-1 |
| 44 | `SYS_LISTEN` | EBX=fd | EAX=0/-1 |
| 45 | `SYS_ACCEPT` | EBX=fd | EAX=new_fd |
| 46 | `SYS_DNS` | EBX=hostname | EAX=ip |
| 47 | `SYS_SOCKCLOSE` | EBX=fd | EAX=0 |
| 48 | `SYS_PING` | EBX=ip | EAX=rtt/-1 |
| 49 | `SYS_SETDATE` | EBX=buf, ECX=century | EAX=0 |
| 50 | `SYS_AUDIO_PLAY` | EBX=buf, ECX=len, EDX=fmt | EAX=0/-1 |
| 51 | `SYS_AUDIO_STOP` | — | EAX=0 |
| 52 | `SYS_AUDIO_STATUS` | — | EAX=state, EBX=present |
| 53 | `SYS_KILL` | EBX=pid | EAX=0/-1 |
| 54 | `SYS_GETPID` | — | EAX=pid |
| 55 | `SYS_CLIPBOARD_COPY` | EBX=buf, ECX=len | EAX=0 |
| 56 | `SYS_CLIPBOARD_PASTE` | EBX=buf, ECX=maxlen | EAX=len |
| 57 | `SYS_NOTIFY` | EBX=text, EDX=color | EAX=0 |
| 58 | `SYS_FILE_OPEN_DLG` | EBX=title, EDX=filter | EAX=1/0, ECX=name |
| 59 | `SYS_FILE_SAVE_DLG` | EBX=title, EDX=filter | EAX=1/0, ECX=name |
| 60 | `SYS_PIPE_CREATE` | — | EAX=pipe_id |
| 61 | `SYS_PIPE_WRITE` | EBX=id, ECX=buf, EDX=len | EAX=written |
| 62 | `SYS_PIPE_READ` | EBX=id, ECX=buf, EDX=max | EAX=read |
| 63 | `SYS_PIPE_CLOSE` | EBX=id | EAX=0 |
| 64 | `SYS_SHMGET` | EBX=key, ECX=size | EAX=shm_id |
| 65 | `SYS_SHMADDR` | EBX=shm_id | EAX=ptr |
| 66 | `SYS_PROCLIST` | EBX=slot, ECX=buf (48 bytes) | EAX=0/-1 |
| 67 | `SYS_MEMINFO` | — | EAX=free_pages, EBX=total |
| 68 | `SYS_CHMOD` | EBX=filename, ECX=perms | EAX=0/-1 |
| 69 | `SYS_CHOWN` | EBX=filename, ECX=uid | EAX=0/-1 |
| 70 | `SYS_SYMLINK` | EBX=linkname, ECX=target | EAX=0/-1 |
| 71 | `SYS_READLINK` | EBX=linkname, ECX=buf | EAX=len/-1 |
| 72 | `SYS_SETPRIORITY` | EBX=pid (0=self), ECX=prio | EAX=0/-1 |
| 73 | `SYS_GETPRIORITY` | EBX=pid (0=self) | EAX=priority/-1 |
| 74 | `SYS_SIGNAL` | EBX=pid, ECX=signal | EAX=0/-1 |
| 75 | `SYS_SETPGID` | EBX=pid (0=self), ECX=pgid | EAX=0/-1 |
| 76 | `SYS_GETPGID` | EBX=pid (0=self) | EAX=pgid/-1 |
| 77 | `SYS_SIGMASK` | EBX=op (0-3), ECX=mask | EAX=old_mask/-1 |
| 78 | `SYS_TASKNAME` | EBX=name ptr (max 15 chars) | EAX=0 |
| 79 | `SYS_REALLOC` | EBX=ptr, ECX=new_size, EDX=old_size | EAX=new_ptr (0=fail) |
| 80 | `SYS_GETENV_SLOT` | EBX=index, ECX=buf (128 B) | EAX=0/-1 |
| 81 | `SYS_DMESG_WRITE` | EBX=msg_ptr | EAX=0 |
| 82 | `SYS_DMESG_READ` | EBX=index, ECX=buf (128 B) | EAX=0/-1 |
| 83 | `SYS_RENAME` | EBX=old_name, ECX=new_name | EAX=0/-1 |
| 84 | `SYS_RMDIR` | EBX=dirname | EAX=0/-1 |
| 85 | `SYS_TRUNCATE` | EBX=filename, ECX=new_size | EAX=0/-1 |
| 86 | `SYS_CONNECT_NB` | EBX=fd, ECX=ip, EDX=port | EAX=0/-2/-1 (non-blocking) |
| 87 | `SYS_POLL` | EBX=fd, ECX=events (1=in,2=out), EDX=timeout_ms | EAX=ready_mask |
| 88 | `SYS_SEM_CREATE` | EBX=initial_value | EAX=sem_id/-1 |
| 89 | `SYS_SEM_WAIT` | EBX=sem_id | EAX=0/-1 |
| 90 | `SYS_SEM_POST` | EBX=sem_id | EAX=0 |
| 91 | `SYS_SEM_CLOSE` | EBX=sem_id | EAX=0 |
| 92 | `SYS_WAITPID` | EBX=pid | EAX=exit_code/-1 |
| 93 | `SYS_GETMTIME` | EBX=filename | EAX=mtime, ECX=ctime |
| 94 | `SYS_SETMTIME` | EBX=filename, ECX=timestamp (0=now) | EAX=0/-1 |
| 95 | `SYS_DRAW_LINE` | EBX=x0, ECX=y0, EDX=x1, ESI=y1, EDI=color | EAX=0 |
| 96 | `SYS_DRAW_TRIANGLE` | EBX=x0\|(y0<<16), ECX=x1\|(y1<<16), EDX=x2\|(y2<<16), ESI=color | EAX=0 |
| 97 | `SYS_BLIT` | EBX=src_ptr, ECX=dst_x, EDX=dst_y, ESI=w\|(h<<16), EDI=colorkey | EAX=0 |
| 98 | `SYS_DIRTY_PRESENT` | — | EAX=0 (flush dirty rect to LFB) |
| 99 | `SYS_PSF_LOAD` | EBX=filename_ptr | EAX=0/-1 |
| 100 | `SYS_PSF_CHAR` | EBX=x, ECX=y, EDX=codepoint, ESI=fg_color | EAX=0 |
| 101 | `SYS_PCI_FIND` | EBX=vendor_id, ECX=device_id | EAX=bdf/-1 |
| 102 | `SYS_GETPPID` | — | EAX=parent PID |
| 103 | `SYS_FORK` | — | EAX=child PID (parent) / 0 (child) / -1 (error) |
| 104 | `SYS_DEFRAG` | — | EAX=0/-1 (defragment HBFS) |
| 105 | `SYS_TLS_CONNECT` | EBX=fd | EAX=0/-1 (TLS stub over existing TCP fd) |
| 106 | `SYS_AUDIO_REC_START` | EBX=buf, ECX=max | EAX=0/-1 |
| 107 | `SYS_AUDIO_REC_READ` | EBX=buf, ECX=max | EAX=bytes/-1 |
| 108 | `SYS_AUDIO_REC_STOP` | — | EAX=0 |
| 109 | `SYS_AUDIO_OPEN` | EBX=fmt, ECX=rate | EAX=channel/-1 |
| 110 | `SYS_AUDIO_WRITE` | EBX=channel, ECX=buf, EDX=len | EAX=bytes |
| 111 | `SYS_AUDIO_CLOSE_CHAN` | EBX=channel | EAX=0 |
| 112 | `SYS_MSGQ_CREATE` | EBX=key, ECX=max_msgs | EAX=qid/-1 |
| 113 | `SYS_MSGQ_SEND` | EBX=qid, ECX=buf, EDX=len | EAX=0/-1 |
| 114 | `SYS_MSGQ_RECV` | EBX=qid, ECX=buf, EDX=max | EAX=bytes/-1 |
| 115 | `SYS_MSGQ_CLOSE` | EBX=qid | EAX=0/-1 |
| 116 | `SYS_DUP` | EBX=fd | EAX=new_fd/-1 |
| 117 | `SYS_DUP2` | EBX=src, ECX=dst | EAX=dst/-1 |
| 118 | `SYS_FCNTL` | EBX=fd, ECX=cmd, EDX=arg | EAX=result/-1 |
| 119 | `SYS_IOCTL` | EBX=fd, ECX=req, EDX=arg | EAX=0/-1 |
| 120 | `SYS_MMAP` | EBX=addr, ECX=len, EDX=prot | EAX=addr/-1 |
| 121 | `SYS_MUNMAP` | EBX=addr, ECX=len | EAX=0/-1 |
| 122 | `SYS_MPROTECT` | EBX=addr, ECX=len, EDX=prot | EAX=0/-1 |
| 123 | `SYS_SELECT` | EBX=nfds, ECX=rfds, EDX=wfds, ESI=efds, EDI=tv | EAX=count/-1 |
| 124 | `SYS_CLOCK_GETTIME` | EBX=clk_id, ECX=timespec* | EAX=0/-1 |
| 125 | `SYS_NANOSLEEP` | EBX=rqtp, ECX=rmtp | EAX=0/-1 |
| 126 | `SYS_GETTIMEOFDAY` | EBX=timeval*, ECX=tz* | EAX=0 |
| 127 | `SYS_GETUID` | — | EAX=uid |
| 128 | `SYS_SETUID` | EBX=uid | EAX=0/-1 |
| 129 | `SYS_GETGID` | — | EAX=gid |
| 130 | `SYS_SETGID` | EBX=gid | EAX=0/-1 |
| 131 | `SYS_GETEUID` | — | EAX=euid |
| 132 | `SYS_GETEGID` | — | EAX=egid |
| 133 | `SYS_ACCESS` | EBX=path, ECX=mode | EAX=0/-1 |
| 134 | `SYS_PIPE2` | EBX=pipefd[2], ECX=flags | EAX=0/-1 |
| 135 | `SYS_SURFACE_CREATE` | EBX=w, ECX=h, EDX=x, ESI=y | EAX=surf_id/-1 |
| 136 | `SYS_SURFACE_COMMIT` | EBX=id, ECX=dx, EDX=dy, ESI=dw, EDI=dh | EAX=0/-1 |
| 137 | `SYS_SURFACE_DESTROY` | EBX=surf_id | EAX=0/-1 |
| 138 | `SYS_SURFACE_MOVE` | EBX=id, ECX=x, EDX=y | EAX=0/-1 |
| 139 | `SYS_SURFACE_RESIZE` | EBX=id, ECX=w, EDX=h | EAX=0/-1 |
| 140 | `SYS_ALARM` | EBX=seconds (0=cancel) | EAX=prev_secs |
| 141 | `SYS_GETXATTR` | EBX=file, ECX=key, EDX=val_buf, ESI=max | EAX=len/-1 |
| 142 | `SYS_SETXATTR` | EBX=file, ECX=key, EDX=val_ptr | EAX=0/-1 |
| 143 | `SYS_SIGACTION` | EBX=signum, ECX=handler, EDX=old_ptr | EAX=0/-1 |
| 144 | `SYS_SIGRETURN` | — | — (returns from signal handler) |
| 145 | `SYS_FSTAT` | EBX=fd, ECX=stat_buf | EAX=0/-1 |
| 146 | `SYS_FTRUNCATE` | EBX=fd, ECX=new_size | EAX=0/-1 |
| 147 | `SYS_FCHMOD` | EBX=fd, ECX=perms | EAX=0/-1 |
| 148 | `SYS_FCHOWN` | EBX=fd, ECX=uid | EAX=0/-1 |
| 149 | `SYS_FSYNC` | EBX=fd | EAX=0/-1 |
| 150 | `SYS_LINK` | EBX=target, ECX=linkname | EAX=0/-1 |
| 151 | `SYS_ISATTY` | EBX=fd | EAX=1/0 |
| 152 | `SYS_SETSID` | — | EAX=sid/-1 |
| 153 | `SYS_GETSID` | EBX=pid (0=self) | EAX=sid/-1 |
| 154 | `SYS_WAIT` | EBX=status_ptr | EAX=child_pid/-1 |
| 155 | `SYS_PAUSE` | — | EAX=-1 (EINTR on signal) |
| 156 | `SYS_UMASK` | EBX=mask (0xFFFFFFFF=query) | EAX=old_mask |
| 157 | `SYS_SIGPENDING` | EBX=sigset_ptr | EAX=0/-1 |
| 158 | `SYS_SIGSUSPEND` | EBX=mask_ptr | EAX=-1 (EINTR) |
| 159 | `SYS_SETITIMER` | EBX=which, ECX=new_val_ptr, EDX=old_val_ptr | EAX=0/-1 |
| 160 | `SYS_GETITIMER` | EBX=which, ECX=val_ptr | EAX=0/-1 |
| 161 | `SYS_SETENV` | EBX=name, ECX=value, EDX=overwrite | EAX=0/-1 |
| 162 | `SYS_UNSETENV` | EBX=name | EAX=0/-1 |
| 163 | `SYS_UNAME` | EBX=utsname_ptr | EAX=0 |
| 164 | `SYS_UTIME` | EBX=filename, ECX=utimbuf | EAX=0/-1 |
| 165 | `SYS_SYSCONF` | EBX=_SC_constant | EAX=value/-1 |
| 166 | `SYS_SETEUID` | EBX=euid | EAX=0/-1 |
| 167 | `SYS_SETEGID` | EBX=egid | EAX=0/-1 |
| 168 | `SYS_SETREUID` | EBX=ruid, ECX=euid (-1=keep) | EAX=0/-1 |
| 169 | `SYS_SETREGID` | EBX=rgid, ECX=egid (-1=keep) | EAX=0/-1 |
| 170 | `SYS_SENDTO` | EBX=fd, ECX=buf, EDX=len, ESI=flags, EDI=dest_addr | EAX=bytes/-1 |
| 171 | `SYS_RECVFROM` | EBX=fd, ECX=buf, EDX=maxlen, ESI=flags, EDI=from* | EAX=bytes/-1 |
| 172 | `SYS_SETSOCKOPT` | EBX=fd, ECX=level, EDX=optname, ESI=optval | EAX=0/-1 |
| 173 | `SYS_GETSOCKOPT` | EBX=fd, ECX=level, EDX=optname, ESI=optval | EAX=0/-1 |
| 174 | `SYS_GETSOCKNAME` | EBX=fd, ECX=addr_ptr | EAX=0/-1 |
| 175 | `SYS_GETPEERNAME` | EBX=fd, ECX=addr_ptr | EAX=0/-1 |
| 176 | `SYS_SHUTDOWN` | EBX=fd, ECX=how | EAX=0/-1 |
| 177 | `SYS_TCGETATTR` | EBX=fd, ECX=termios_ptr | EAX=0/-1 |
| 178 | `SYS_TCSETATTR` | EBX=fd, ECX=action, EDX=termios_ptr | EAX=0/-1 |
| 179 | `SYS_TCDRAIN` | EBX=fd | EAX=0 |
| 180 | `SYS_TCFLUSH` | EBX=fd, ECX=queue | EAX=0 |
| 181 | `SYS_GETERRNO` | — | EAX=per-task errno |
| 186 | `SYS_FAULTINJECT` | EBX=op, ECX=count | EAX=0/-1; see [Fault Injection](#fault-injection-p10) |

**Total: 186 syscalls defined (0–185, no gaps).**

---

## 64-bit Nucleus (P2)

The 32-bit tree is complete and green; the 64-bit tree is being built beside it, starting
from a nucleus that proves the boot path. `make 64bit` builds `mellivora64.img`, a
separate image with its own stage2 (`stage2-64.bin`, assembled with `-DKERNEL_64BIT=1`)
and nucleus; `make` still builds the 32-bit image and nothing in this port changes it.

### Toolchain

`tools/find_lm_toolchain.sh` decides what compiles the nucleus. It tries, in order,
`$LM_CC`, `x86_64-elf-gcc`, `clang --target=x86_64-elf` and the host `gcc`, and accepts a
candidate only after compiling a one-line object and reading `e_machine` out of its ELF
header - a native gcc on an ARM host compiles happily and produces the wrong
architecture, so "the command exists" is not enough. The linker is chosen the same way
(`x86_64-elf-ld`, `ld.lld -m elf_x86_64`, `ld -m elf_x86_64`). Compiler flags:

| Flag | Why |
| --- | --- |
| `-ffreestanding -fno-builtin` | no libc, no headers; `memcpy`/`memset` are provided by the nucleus |
| `-mno-red-zone` | an interrupt would otherwise clobber the 128 bytes below RSP |
| `-mcmodel=kernel` | every symbol reachable with a sign-extended 32-bit displacement, so code is position-dependent but small |
| `-mgeneral-regs-only` | no SSE: long mode starts with `CR4.OSFXSR` clear (see below) |
| `-fno-pic -fno-pie -fno-stack-protector` | nothing to relocate and no guard code at this stage |
| `-fno-asynchronous-unwind-tables -fno-unwind-tables` | no `.eh_frame` in a freestanding image |

### Higher half

`boot/lm64.ld` links the nucleus at `0xFFFFFFFF80100000` and `--oformat binary` emits the
sections in link order, so the flat image is exactly what stage2 places at physical
1 MB. stage2 then builds two mappings over the same pages:

| Table | Entry | Covers |
| --- | --- | --- |
| `PML4[0]` | `PDPT[0] -> PDT` | identity map, 0..1 GB (512 x 2 MB) |
| `PML4[511]` | `PDPT[510] -> PDH` | `0xFFFFFFFF80000000`..+1 GB (the same pages) |

and jumps to `KERNEL_LINK_ADDR` instead of `0x00100000`, so position-dependent code runs
where it was linked while low memory (the VGA buffer, stage2's stack) stays reachable.
Both PD pages are filled completely - one 2 MB page per entry costs no extra memory and
turns the higher half into a direct map for every frame the allocator hands out, which
is what lets the VMM zero a new page table it just allocated.
The two tables must stay in step: `KERNEL_LINK_ADDR` in `stage2.asm` and the `.` in
`boot/lm64.ld` are the same number.

### Entry and SYSCALL

`lm_start` (naked, placed in `.text.lm_start` so it lands at the image base) clears
`CR0.EM`/`TS`, sets `MP` and enables `CR4.OSFXSR`/`OSXMMEXCPT` - firmware leaves them
clear and the first SSE instruction would otherwise be an invalid-opcode fault. It then
calls `lm_main`, which zeroes `.bss` (it is `NOBITS`, so the loader carried no bytes for
it), switches from stage2's low stack to its own 16 KB stack *inside* `.bss` - which is
why the switch happens after the zeroing - initialises COM1 and reports.

`syscall_roundtrip` programs `EFER.SCE`, `STAR`, `LSTAR` (the entry point) and `FMASK`,
executes a 64-bit `syscall`, and the handler returns with `iretq` using `RCX`/`R11` as
SYSCALL left them. Two details come from the rings below:

- `STAR[47:32]` names the kernel CS (0x08, with SS 0x10 next to it) and `STAR[63:48]`
  is the base SYSRET adds 8 and 16 to, so it holds the kernel data selector 0x10:
  that names user data 0x18 and user code 0x20, which are also the selectors the IRETQ
  path returns to. Both return paths therefore agree on the same two descriptors.
- `FMASK` clears `IF` and `DF` on entry, because SYSCALL does not switch stacks and the
  kernel must not take an interrupt on the caller's stack before it has switched.

`IRETQ` pops **five** values in 64-bit mode - RIP, CS, RFLAGS, RSP, SS - even when it
returns to the same privilege level, so the frame always carries both. A three-value
frame returns with a garbage stack pointer (see the self-test notes below).

### Per-CPU state and the entry path

`boot/lm_cpu.c` keeps one block per processor rather than using a global: the entry path
has to find the CPU's kernel stack before it has a stack it can call anything on, so the
block is addressed as `%gs:<offset>` with no call, no relocation and no register
assumptions.

The two GS-base halves are set up for `SWAPGS`: `MSR_KERNEL_GS_BASE` holds the block and
the live GS base holds what a user program sees (zero here), because `SWAPGS` exchanges
them and the entry stub runs it unconditionally whichever ring the SYSCALL came from.
Ring 0 code therefore cannot use `%gs` to reach the block - it calls `cpu_this()`.

| Offset | Field | Use |
| --- | --- | --- |
| `0x00` | `self` | the block's own address (how `cpu_this()` would verify itself) |
| `0x08` | `id` | processor index |
| `0x10` | `kstack_top` | kernel stack the entry stub switches to (also TSS `RSP0`) |
| `0x18` | `saved_rsp` | caller's stack pointer, written by the stub |
| `0x20` | `syscall_count` | calls taken |
| `0x28` | `last_rsp` | stack the handler ran on |
| `0x30`/`0x38` | `last_nr`/`last_arg` | last call, for the self-test |
| `0x40` | `user_mode` | non-zero while the CPU runs a user program |
| `0x48` | `user_calls` | calls taken from ring 3 |
| `0x50`/`0x58` | `exit_code`/`exit_arg` | what `SYS_EXIT` was given |
| `0x60`/`0x68` | `last_user_rip`/`last_user_rsp` | where the last ring-3 call came from |

The stub (`lm_syscall_entry`, the address in `LSTAR`) does, in order: `SWAPGS`, save RSP
to `%gs:0x18`, load RSP from `%gs:0x10`, push the five-value IRETQ frame (SS, caller RSP,
RFLAGS from R11, CS, return RIP from RCX), push every general register the C dispatcher
may clobber, pass the frame pointer in RDI and call `lm_syscall_dispatch`. The dispatcher
writes the result into the frame's `rax` and the caller's selectors into `cs`/`ss`; the
stub pops everything back, `SWAPGS` and `iretq`s. The dispatcher therefore runs on the
per-CPU kernel stack, and the caller - ring 0 or ring 3 - finds its registers and stack
exactly as it left them.

A non-zero return from the dispatcher means the program is finished (`SYS_EXIT`): the
stub drops the syscall frame, reloads a clean kernel stack and calls `lm_user_exited`,
which reports and halts.

### Ring 3

`boot/lm_user.c` adds the user half:

| Piece | What it does |
| --- | --- |
| GDT | null, kernel code/data (0x08/0x10), user data/code (0x18/0x20, DPL 3), 16-byte TSS descriptor (0x28) |
| TSS | `RSP0` = the per-CPU kernel stack, so an interrupt or fault taken in ring 3 lands on a kernel stack; `iomap_base` past the segment, so user `in`/`out` faults |
| address space | two frames from the frame allocator at `0x40000000` (code, stack), `U/S` at every level of the walk |
| `lm_user_enter` | builds `SS`/`RSP`/`RFLAGS`/`CS`/`RIP` and `IRETQ`s to CPL 3 |

The user pages are mapped through the VMM: `0x40000000` is `PDPT[1]`, which stage2 never
filled, so no 2 MB kernel page has to be split, and `vmm_map` sets U/S on the shared
PML4 entry and on the tables it creates. The code page is mapped read-only and the stack
writable; `lm_user_run` then checks `vmm_user_mapped` for both, and checks that address
0, the identity map and the higher half are *not* user-mapped, before it enters ring 3.
Frames are reached through stage2's direct map (`phys + 0xFFFFFFFF80000000`, the first
1 GB).

`lm_user_enter` does **not** `SWAPGS`: the live GS base already holds the user value and
`KERNEL_GS_BASE` the block, which is the state the entry stub's `SWAPGS` expects to find.
The user program (`lm_user_program`, assembled into `.rodata` and copied into its frame)
starts with `INT3`, calls `PING(0x1234)`, pushes the result onto its own stack, calls
`WHOAMI`, pops the value back and exits with it - so the exit code proves both that the
return to ring 3 restored `RAX` and that the user stack is mapped writable, and the
breakpoint proves the fault path below. `lm_user_exited` checks the exit code, the
`WHOAMI` answer, the call count and that the last call came from the user code page with
the user stack, then prints `ring3=ok`.

### IDT and exceptions

`boot/lm_idt.c` installs the long-mode IDT before anything can fault. Vectors 0..21 get
stubs generated by two assembler macros - the CPU pushes an error code for 8, 10, 11,
12, 13, 14, 17 and 21, and the stub pushes a zero for the rest, so the C frame is the
same shape either way. Vectors above 21 are left absent: the CPU turns an attempt to use
one into `#GP`, which is vector 13 and lands in the handler that exists. The INT3 gate is
the only one with `DPL=3`, because that is the only vector software in ring 3 raises
itself; the CPU ignores `DPL` for the exceptions it delivers.

`lm_isr_common` does `SWAPGS`, pushes the fifteen general registers, passes the frame in
RDI and calls `lm_fault_dispatch`. The memory order matters and is pinned by
`_Static_assert`s: what the stub pushed last (R15) is nearest the stack pointer, and the
CPU's five values sit above the vector and error code.

| Field | Where it comes from |
| --- | --- |
| `vec`, `err` | the stub's two pushes (the error code, or a zero) |
| `rip`, `cs`, `rflags` | pushed by the CPU for every fault |
| `rsp`, `ss` | pushed by the CPU **only** for a privilege change - read only when `(cs & 3) == 3` |
| the fifteen registers | pushed by `lm_isr_common`, restored before `IRETQ` |

The dispatcher records the vector, error code, RIP and (for `#PF`) CR2, and checks two
things that decide whether the fault is the expected one: that the frame's CS says ring 3
and that its own RSP lies inside the per-CPU kernel stack. That second check is the proof
that `TSS.RSP0` did its job, because the CPU had to load it to get here from a user
stack. `#BP` is the only vector it resumes - INT3's saved RIP already points past the
instruction - and every other fault is reported and halts without the boot marker.

### Virtual memory

`boot/lm_vmm.c` sits between the frame allocator and everything that needs an address
space. `vmm_map` walks the four levels from CR3 and, when an intermediate table is
missing, allocates a frame, zeroes it through the direct map and links it in; the leaf
carries `VMM_W`/`VMM_U` as the caller asked. `invlpg` follows every map and unmap, and
`vmm_translate` resolves whatever the walk finds - including the 2 MB pages stage2 used,
so the identity map answers too.

| Call | Answers |
| --- | --- |
| `vmm_map(virt, phys, flags)` | maps one 4 KB page; -1 if already mapped, if a large page covers the address, or if the new table's frame is not reachable |
| `vmm_unmap(virt)` | clears the leaf and the TLB; -1 when nothing was there |
| `vmm_translate(virt)` | the physical address, or 0 |
| `vmm_user_mapped(virt)` | 1 only when **every** level, leaf included, has U/S |
| `vmm_phys(phys)` | the direct-map pointer for a frame below 1 GB |

`vmm_user_mapped` is the reason the walk upgrades intermediate entries: the `PML4[0]`
entry is shared by the identity map and the user area, so a user leaf under one of its
PDPT entries needs U/S on the levels above it. A kernel leaf under the same tables stays
out of reach because the check does not stop at the first level.

The self-test maps a frame at `0x7F0000000000`, writes through the virtual address and
reads the value back through the direct map (so the walk, the frame and the TLB all
have to agree), refuses a second map of the same address, unmaps, and then asks the MMU
whether it worked. Asking the MMU needs a fault that is survivable: the handler has a
kernel fix-up path - a ring-0 `#PF` at `lm_fault_fixup_cr2` resumes at
`lm_fault_fixup_rip` - and `vmm_probe()` arms it around one load, so the answer is 1
when the read faulted and 0 when it was served. The test finishes by checking that a
U/S leaf under the same tables is user-mapped while its kernel neighbour is not, and
that the identity map and the higher half still are not.

### Kernel heap

`boot/lm_kmem.c` is the slab allocator, the same design as the 32-bit tree's
`kernel/kmem.inc`: a cache is keyed by object size, free objects are threaded through
their own first eight bytes, and a cache takes a page only when its free list is empty.
Pages are never returned - that is the point of a slab - and every object is zeroed
before it is handed out, so a recycled slot cannot leak the previous owner's contents.

The 64-bit difference is where a page goes. The heap owns a 16 MB window at
`0xFFFFC00000000000` (`PML4[384]`, which stage2 never filled), and a new slab takes a
PMM frame and maps it there with `vmm_map`. The window is outside the identity map, only
the pages a cache has actually taken are mapped, and every object's address can be
resolved by `vmm_translate` - which the self-test checks, along with `vmm_user_mapped`
being false, because the heap is kernel-only.

| Call | Behaviour |
| --- | --- |
| `kmem_cache(size)` | id of an existing cache for that size, or a new one; -1 for sizes below 8 bytes, above a page, or when the table is full |
| `kmem_alloc(id)` | a zeroed object, taking and threading a new slab when needed; NULL on OOM |
| `kmem_free(id, obj)` | pushes the object back onto that cache's list; a pointer outside the heap window is ignored rather than linked in |
| `kmem_get_stats()` | caches, slabs, total slots and slots in use |

Nothing allocates from the heap yet beyond the self-test. The cache API takes an
explicit id - rather than inferring a cache from a pointer - because that is what the
32-bit tree does and what the task structures that arrive with the scheduler will use.

### Timers and interrupt delivery

`boot/lm_irq.c` puts the 8259 PIC and the 8254 PIT behind the IDT's vectors 32..47. The
PIC is remapped (master to vector 32, slave to 40) so hardware interrupts stop colliding
with the CPU's own exception vectors, everything but IRQ0 is masked, and channel 0 runs
in mode 2 at 100 Hz.

| Piece | Detail |
| --- | --- |
| IDT gates | vectors 32..47, the same stub shape as the exceptions (no error code, so the stub pushes the zero), `DPL=0`, interrupt gates |
| routing | `lm_fault_dispatch` sends vectors 32..47 to `lm_irq_dispatch` before it counts a fault |
| EOI | slave first when the IRQ came through it, then the master |
| accounting | `ticks`, `user_ticks` (arrived while ring 3 ran) and `tick_stack` (handler ran on the TSS stack) per CPU |

Interrupts are enabled in exactly one place: the `RFLAGS` of the ring-3 context
(`0x202`). The kernel runs with IF clear everywhere - the SYSCALL entry masks it and
every gate is an interrupt gate - so a tick cannot land in the middle of the kernel, and
every tick that happens at all is one that crossed from ring 3 to `TSS.RSP0` and back.
That is what `[timer] hz=100 ticks=3 user=3 stack=ok` asserts, and it is the delivery a
scheduler will preempt on: the pieces still missing are a task structure, a switch and
somewhere to return to when a tick arrives.

The user program waits for three ticks before its `PING`/`WHOAMI`/`EXIT` sequence,
spinning in user mode between polls of `SYS_TICKS`. If none arrives it exits with a code
the nucleus rejects, so a dead PIT fails the run instead of hanging it.

### Frame allocator

`boot/lm_pmm.c` is the first ported subsystem: a bitmap with one bit per 4 KB frame,
built from the E820 map stage2 leaves at `0x504` (count) / `0x508` (pointer).

| Step | What it does |
| --- | --- |
| pass 1 | totals the usable ranges and finds the highest usable address, capped at 8 GB |
| reserve | marks every frame used, then hands back the usable ranges above 1 MB |
| carve out | re-reserves `[1 MB, lm_image_end)` - the image, `.bss` and the stack |

`lm_image_end` comes from the linker script, so the allocator cannot hand out a frame it
is standing on. `pmm_alloc_frame` rotates a hint through the bitmap; `pmm_free_frame`
refuses a non-frame address, an address outside the managed range and a double free,
which is what makes the self-test meaningful.

The 64-bit specifics are the point of doing this first: E820 entries carry 64-bit
base/length, and a guest at or above 4 GB has usable memory above `0x100000000` on the
far side of the PCI hole, so every address and size is a `u64`. A 32-bit allocator reads
those entries, sees a base of 0 and reports the wrong total.

### Nucleus self-test

`report_memory` prints `usable=<MB> top=0x<addr> frames=<n> free=<n>`, allocates four
frames (they must be distinct, 4 KB aligned and counted), frees them, checks the free
count returned to its baseline, and checks that freeing an address that is not a frame
is refused. `pmm=ok` is the result.

Two long-mode details are worth knowing here because both were faults first:

- an `IRETQ` in 64-bit mode pops **five** values - RIP, CS, RFLAGS, RSP, SS - even when
  it returns to the same privilege level, so a SYSCALL handler that pushes only three
  returns with a garbage stack pointer (the first symptom was a `syscall=ok` line
  followed by a fault on the very next `call`, with RSP = 0);
- the nucleus stack is its own `NOLOAD` section *after* `.bss`, not part of it, because
  the nucleus zeroes `.bss` while running on that stack.

### What the test asserts

`tests/test_longmode.py` boots the image twice under `-cpu qemu64`. The 256 MB run
requires `MELLIVORA64_OK`, `base=0xFFFFFFFF80100000`, `syscall=ok`, `[cpu] id=0 calls=1`,
`stack=ok`, `pmm=ok`, `vmm=ok`, `kmem=ok`, `fault=ok`, `timer=ok` and `ring3=ok` on
COM1, so a
nucleus that booted in the wrong half, broke its SYSCALL path, ran its handler on the
wrong stack, mis-read the memory map, could not map and unmap a page while the MMU
agreed, handed out a slab object twice or unmapped, failed to deliver a ring-3 fault or a
timer tick to the TSS stack, or returned to the wrong stack in ring 3 fails rather than
printing a marker. The second run boots 8 GB (the roadmap's scale
gate) and requires at least 85% of it to be reported usable with a highest address above
4 GB. `--marker-only` relaxes the first run to the marker for the hand-written asm
nucleus (`make 64bit-asm`), which exists as a boot-path regression that needs no C
toolchain.

## Fault Injection (P10)

`kernel/faultinject.inc` makes the kernel fail one thing on purpose so the error paths
can be tested. Any task arms it with `SYS_FAULTINJECT`, does the operation it wants to
see fail, and checks both the error *and* that the system is unchanged afterwards.

| Fixture | Choke point | Injected failure |
| --- | --- | --- |
| `FI_OP_ALLOC` | `pmm_alloc_buddy_order` | returns 0 (out of memory) |
| `FI_OP_IO` | `ata_write_sectors` | returns `CF=1` (write failed) |
| `FI_OP_NET` | `net_send_frame` | the frame is dropped |

Each hook is a single `call`/`jc` in front of the one routine every allocation, write
and transmit goes through, and each gate (`fi_alloc_gate`, `fi_io_gate`, `fi_net_gate`)
preserves every register and reports in `CF`, so no caller needed a change. Counters
mean "inject this many more faults"; `FI_OP_CLEAR` (0) drops what is still armed and
`FI_OP_TOTAL` (5) reports how many were actually injected, so a test that armed nothing
fails instead of passing silently.

`programs/fault_inject_test.asm` is the conformance test: it forks with 1..12 page
allocations failing - one different failure point per prefix, from the child's kernel
stack through its area table, page directory and COW share - and requires the free-page
count to come back to its baseline each time; creates a file while every write fails and
requires `SYS_FWRITE` to return `-1` and the next create to work; and drops one frame
during a DNS lookup, which the resolver's retry has to absorb. `make check-faults`
repeats it, `check-runtime` runs it once after `dhcp`.

The facility deliberately has no privilege check: it exists to break the kernel in a
controlled way, and P6 is where a capability would gate it.

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
| Up/Down | Browse command history (128 entries) |
| Tab | Filename auto-complete |
| Ctrl+C | Cancel current input |
| Ctrl+A | Move cursor to beginning of line |
| Ctrl+E | Move cursor to end of line |
| Ctrl+U | Kill entire input line |
| Ctrl+W | Delete previous word |
| Ctrl+L | Clear screen, redraw prompt and input |
| Enter | Execute command |

---

## Environment Variables

### Storage

| Property | Value |
| --- | --- |
| Max variables | 32 (`ENV_MAX`) |
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
