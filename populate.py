#!/usr/bin/env python3
"""
populate.py - Populate a Mellivora OS disk image with sample files.

Writes files directly into the HBFS filesystem on the disk image.
This understands the on-disk HBFS layout:
  - Superblock at LBA 417
  - Block allocation bitmap at LBA 418 (128 sectors = 16 blocks)
  - Root directory at LBA 426 (128 sectors = 16 blocks)
  - Data area starts at LBA 554

Each data block is 4096 bytes (8 sectors).
Each directory entry is 288 bytes.
14 entries per directory block, 227 entries total.
"""

import struct
import sys
import os

# HBFS constants (must match kernel.asm)
SECTOR_SIZE = 512
BLOCK_SIZE = 4096
SECTORS_PER_BLOCK = BLOCK_SIZE // SECTOR_SIZE  # 8
HBFS_MAGIC = 0x48424653  # 'HBFS'
HBFS_SUPERBLOCK_LBA = 417
HBFS_BITMAP_START = 418
HBFS_ROOT_DIR_START = 546
HBFS_ROOT_DIR_BLOCKS = 32
HBFS_ROOT_DIR_SECTS = HBFS_ROOT_DIR_BLOCKS * SECTORS_PER_BLOCK
HBFS_ROOT_DIR_SIZE = HBFS_ROOT_DIR_BLOCKS * BLOCK_SIZE
HBFS_DATA_START = 802
HBFS_DIR_ENTRY_SIZE = 288
HBFS_MAX_FILES = HBFS_ROOT_DIR_SIZE // HBFS_DIR_ENTRY_SIZE
HBFS_MAX_FILENAME = 252
TOTAL_BLOCKS = 524288

# File types
FTYPE_FREE = 0
FTYPE_TEXT = 1
FTYPE_FILE = 1  # backward compat alias
FTYPE_DIR = 2
FTYPE_EXEC = 3
FTYPE_BATCH = 4


def lba_to_offset(lba):
    """Convert LBA sector number to byte offset in image."""
    return lba * SECTOR_SIZE


def read_sectors(img, lba, count):
    """Read sectors from disk image."""
    img.seek(lba_to_offset(lba))
    return img.read(count * SECTOR_SIZE)


def write_sectors(img, lba, data):
    """Write data at the given LBA, padding to sector boundary."""
    img.seek(lba_to_offset(lba))
    # Pad to sector boundary
    if len(data) % SECTOR_SIZE:
        pad_len = SECTOR_SIZE - len(data) % SECTOR_SIZE
        padded = data + b'\x00' * pad_len
    else:
        padded = data
    img.write(padded)


def create_superblock():
    """Create a fresh HBFS superblock."""
    sb = bytearray(SECTOR_SIZE)
    struct.pack_into('<I', sb, 0, HBFS_MAGIC)  # Magic
    struct.pack_into('<I', sb, 4, 1)  # Version
    struct.pack_into('<I', sb, 8, TOTAL_BLOCKS)  # Total blocks
    struct.pack_into('<I', sb, 12, TOTAL_BLOCKS)  # Free (update)
    struct.pack_into('<I', sb, 16, HBFS_ROOT_DIR_START)
    struct.pack_into('<I', sb, 20, HBFS_BITMAP_START)
    struct.pack_into('<I', sb, 24, HBFS_DATA_START)
    struct.pack_into('<I', sb, 28, BLOCK_SIZE)
    return sb


def create_dir_entry(filename, ftype, size, start_block,
                     block_count, timestamp=1000):
    """Create a single HBFS directory entry (288 bytes)."""
    entry = bytearray(HBFS_DIR_ENTRY_SIZE)

    # Filename: bytes 0-252 (null-terminated, max 252 chars)
    name_bytes = filename.encode('ascii')[:HBFS_MAX_FILENAME]
    entry[0:len(name_bytes)] = name_bytes
    entry[len(name_bytes)] = 0  # Null terminator

    # Type: byte 253
    entry[253] = ftype

    # Flags: bytes 254-255
    struct.pack_into('<H', entry, 254, 0)

    # File size: bytes 256-259
    struct.pack_into('<I', entry, 256, size)

    # Start block: bytes 260-263
    struct.pack_into('<I', entry, 260, start_block)

    # Block count: bytes 264-267
    struct.pack_into('<I', entry, 264, block_count)

    # Created timestamp: bytes 268-271
    struct.pack_into('<I', entry, 268, timestamp)

    # Modified timestamp: bytes 272-275
    struct.pack_into('<I', entry, 272, timestamp)

    # Reserved: bytes 276-287 (already zeroed)

    return entry


HBFS_SUBDIR_BLOCKS = 8                 # Blocks per subdirectory
HBFS_SUBDIR_SIZE = HBFS_SUBDIR_BLOCKS * BLOCK_SIZE
HBFS_SUBDIR_MAX_FILES = HBFS_SUBDIR_SIZE // HBFS_DIR_ENTRY_SIZE  # 112


class FSImage:
    """Manages an HBFS filesystem image with subdirectory support."""

    def __init__(self, image_path):
        self.image_path = image_path
        self.img = open(image_path, 'r+b')
        self.bitmap = bytearray(TOTAL_BLOCKS // 8)  # 64KB bitmap
        self.root_dir = bytearray(HBFS_ROOT_DIR_SIZE)
        self.next_block = 0
        self.total_files = 0
        # Track subdirectories:
        # name -> (start_block, dir_data bytearray, entry_count)
        self.subdirs = {}

        # Write fresh superblock
        self.sb = create_superblock()
        self.img.seek(lba_to_offset(HBFS_SUPERBLOCK_LBA))
        self.img.write(self.sb)

    def _alloc_blocks(self, count):
        """Allocate contiguous blocks, returns start block number."""
        start = self.next_block
        for b in range(start, start + count):
            byte_idx = b // 8
            bit_idx = b % 8
            self.bitmap[byte_idx] |= (1 << bit_idx)
        self.next_block += count
        return start

    def _write_data(self, block_num, data, blocks_needed):
        """Write data to disk at the given block."""
        data_lba = HBFS_DATA_START + block_num * SECTORS_PER_BLOCK
        pad_sz = BLOCK_SIZE * blocks_needed - len(data)
        padded = data + b'\x00' * pad_sz
        self.img.seek(lba_to_offset(data_lba))
        self.img.write(padded)
        return data_lba

    def create_subdir(self, dirname):
        """Create a subdirectory in the root directory."""
        block_num = self._alloc_blocks(HBFS_SUBDIR_BLOCKS)
        data_lba = HBFS_DATA_START + block_num * SECTORS_PER_BLOCK

        # Write zeroed directory data to disk
        zeroed = b'\x00' * HBFS_SUBDIR_SIZE
        self.img.seek(lba_to_offset(data_lba))
        self.img.write(zeroed)

        # Create directory entry in root
        root_entry_count = 0
        for i in range(HBFS_MAX_FILES):
            off = i * HBFS_DIR_ENTRY_SIZE
            if (self.root_dir[off + 253] == FTYPE_FREE
                    and self.root_dir[off] == 0):
                root_entry_count = i
                break

        entry = create_dir_entry(
            filename=dirname,
            ftype=FTYPE_DIR,
            size=0,
            start_block=block_num,
            block_count=HBFS_SUBDIR_BLOCKS,
            timestamp=900
        )
        off = root_entry_count * HBFS_DIR_ENTRY_SIZE
        self.root_dir[off:off + HBFS_DIR_ENTRY_SIZE] = entry

        self.subdirs[dirname] = {
            'start_block': block_num,
            'data': bytearray(HBFS_SUBDIR_SIZE),
            'entry_count': 0,
            'data_lba': data_lba,
        }
        print(f"  [DIR] /{dirname:18s}         "
              f"  -> block {block_num} (LBA {data_lba})")
        return block_num

    def add_file(self, filename, data, directory=None, ftype=None):
        """Add a file to root or a subdirectory."""
        if ftype is None:
            if filename.endswith('.txt') or filename.endswith('.c'):
                ftype = FTYPE_TEXT
            elif filename.endswith('.bat'):
                ftype = FTYPE_BATCH
            else:
                ftype = FTYPE_EXEC

        blocks_needed = max(1, (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE)
        block_num = self._alloc_blocks(blocks_needed)
        data_lba = self._write_data(block_num, data, blocks_needed)

        entry = create_dir_entry(
            filename=filename,
            ftype=ftype,
            size=len(data),
            start_block=block_num,
            block_count=blocks_needed,
            timestamp=1000 + self.total_files * 100
        )

        if directory and directory in self.subdirs:
            sd = self.subdirs[directory]
            if sd['entry_count'] >= HBFS_SUBDIR_MAX_FILES:
                print(f"  WARNING: /{directory} full, skipping {filename}")
                return
            off = sd['entry_count'] * HBFS_DIR_ENTRY_SIZE
            sd['data'][off:off + HBFS_DIR_ENTRY_SIZE] = entry
            sd['entry_count'] += 1
            path_display = f"/{directory}/{filename}"
        else:
            # Find free slot in root
            root_slot = None
            for i in range(HBFS_MAX_FILES):
                off = i * HBFS_DIR_ENTRY_SIZE
                if (self.root_dir[off + 253] == FTYPE_FREE
                        and self.root_dir[off] == 0):
                    root_slot = i
                    break
            if root_slot is None:
                print(f"  WARNING: Root full, skipping {filename}")
                return
            off = root_slot * HBFS_DIR_ENTRY_SIZE
            self.root_dir[off:off + HBFS_DIR_ENTRY_SIZE] = entry
            path_display = f"/{filename}"

        self.total_files += 1
        print(
            f"  [{self.total_files:2d}] {path_display:28s}"
            f" {len(data):6d} bytes"
            f"  -> block {block_num} (LBA {data_lba})")

    def finalize(self):
        """Write all directory structures and bitmap to disk."""
        # Write subdirectory data to disk
        for _dirname, sd in self.subdirs.items():
            self.img.seek(lba_to_offset(sd['data_lba']))
            self.img.write(sd['data'])

        # Update superblock free count
        used = self.next_block
        free = TOTAL_BLOCKS - used
        struct.pack_into('<I', self.sb, 12, free)
        self.img.seek(lba_to_offset(HBFS_SUPERBLOCK_LBA))
        self.img.write(self.sb)

        # Write bitmap
        self.img.seek(lba_to_offset(HBFS_BITMAP_START))
        self.img.write(self.bitmap)

        # Write root directory
        self.img.seek(lba_to_offset(HBFS_ROOT_DIR_START))
        self.img.write(self.root_dir)

        self.img.close()
        print(
            f"\nFilesystem populated: {self.total_files} files,"
            f" {used} blocks used,"
            f" {free} blocks free.")


# ============================================================
# Sample data files (embedded)
# ============================================================

TEXT_FILES = {
    "readme.txt": """\
Mellivora OS - 32-bit Operating System
==========================================

Welcome to Mellivora OS, a 32-bit protected mode operating system
built from scratch in x86 assembly language.

Features:
  * i486+ 32-bit protected mode
  * Flat 4 GB memory model with bitmap page allocator
  * ATA PIO disk driver with LBA48 (up to 128 PB)
  * HBFS filesystem with 252-character filenames
  * VGA 80x25 text mode with 16 colors and scrolling
  * PS/2 keyboard with scancode translation
  * PIT timer at 100 Hz
  * Syscall interface via INT 0x80 (34 system calls)
  * Interactive command shell with 50+ commands
  * Program execution (flat 32-bit & ELF binaries)
  * Ring 3 user mode with TSS
  * PATH-based program search across directories
  * Command-line argument passing
  * Serial console output (COM1)
  * File descriptor I/O (open/read/write/close)
  * Tab completion, command history, Ctrl+C abort
  * Batch file / script execution
  * Full-screen text editor (edit)
  * Calendar, calculator, and games

Directory Structure:
  /bin      - Utility programs (edit, calc, sort, grep, etc.)
  /games    - Games (snake, tetris, 2048, life, etc.)
  /samples  - C source code samples for the TCC compiler
  /docs     - Documentation and text files

Type 'help' for a list of commands.
Type 'set PATH /bin:/games' to configure program search path.

Serial Console (COM1):
  Mellivora includes a bidirectional serial port driver on COM1
  at 115200 baud, 8N1.  This enables host communication from
  inside the OS, which is useful for logging, debugging, file
  transfer, and remote control.

  QEMU quick-start:
    qemu-system-i386 ... -serial tcp::4555,server=on,wait=off
  Then on the host:  nc localhost 4555
  Or for a PTY:      qemu-system-i386 ... -serial pty

  Shell utility (in /bin):
    serial              Interactive serial terminal (Esc to quit)
    serial send <text>  Send a line of text out the serial port

  Syscalls for programs:
    SYS_SERIAL    (28) - Write string to COM1  (EBX = string ptr)
    SYS_SERIAL_IN (33) - Non-blocking read     (EAX = char or -1)

  Use cases:
    * Debug logging  - print trace messages from programs
    * Remote shell   - pipe serial to a terminal on the host
    * File transfer  - send/receive data to/from host tools
    * Automated test - script QEMU + nc to drive the OS
    * Data export    - dump computation results to the host

Mellivora don't care - it just runs!
""",

    "license.txt": """\
MIT License

Copyright (c) 2024 Honey Badger Universe

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
""",

    "notes.txt": """\
Mellivora OS Developer Notes
============================

Memory Map:
  0x00000000 - 0x000003FF  Real mode IVT (unused in pmode)
  0x00000400 - 0x000004FF  BIOS Data Area
  0x00000500 - 0x0000050F  Boot info from stage 2
  0x00007C00 - 0x00007DFF  Stage 1 boot sector
  0x00007E00 - 0x0000BDFF  Stage 2 loader (16KB)
  0x0009FC00 - 0x0009FFFF  Kernel stack (grows down)
  0x000A0000 - 0x000BFFFF  VGA memory
  0x000B8000 - 0x000B8F9F  VGA text buffer (80x25x2)
  0x000C0000 - 0x000FFFFF  ROM area
  0x00100000 - 0x001FFFFF  Kernel (loaded at 1MB)
  0x00200000 - 0x002FFFFF  Program load area (1MB)
  0x00300000 - 0x0031FFFF  Physical memory bitmap (128KB)
  0x00400000 - onwards     Free memory (managed by PMM)

Disk Layout:
  LBA 0:       Boot sector (512 bytes)
  LBA 1-32:    Stage 2 loader (16KB)
  LBA 33-416:  Kernel (384 sectors, 192KB)
  LBA 417:     HBFS Superblock
  LBA 418-545: Block allocation bitmap (64KB, 16 blocks)
  LBA 546-801: Root directory (128KB, 32 blocks, 455 entries)
  LBA 802+:    Data blocks (4KB each)

Syscall Interface (INT 0x80):
  EAX = syscall number
  EBX, ECX, EDX, ESI, EDI = arguments
  Return value in EAX

  0  exit        - Return to shell
  1  putchar     - Print character (EBX=char)
  2  getchar     - Read character (blocking)
  3  print       - Print string (EBX=ptr)
  4  read_key    - Poll key (non-blocking)
  5  open        - Open file
  6  read        - Read from file
  7  write       - Write to file
  8  close       - Close file
  9  delete      - Delete file
  10 seek        - Seek in file
  11 stat        - File info
  12 mkdir       - Create directory
  13 readdir     - Read directory
  14 setcursor   - Set cursor pos (EBX=x, ECX=y)
  15 gettime     - Get tick count
  16 sleep       - Sleep (EBX=ticks)
  17 clear       - Clear screen
  18 setcolor    - Set VGA color (EBX=attr)
  19 malloc      - Allocate memory
  20 free        - Free memory
  21 exec        - Execute program
  22 disk_read   - Raw disk read (kernel only)
  23 disk_write  - Raw disk write (kernel only)
  24 beep        - Play tone (EBX=freq, ECX=ticks)
  25 date        - Read RTC date/time
  26 chdir       - Change directory
  27 getcwd      - Get current directory
  28 serial      - Write string to COM1 serial port
                   EBX = pointer to null-terminated string
                   Returns: EAX = 0
                   No-op if serial hardware not detected.
  29 getenv      - Get environment variable
  30 fread       - Read entire file to buffer
  31 fwrite      - Write buffer to file
  32 getargs     - Get command-line arguments
  33 serial_in   - Non-blocking read from COM1 serial port
                   Returns: EAX = character (0-254) if data ready,
                            EAX = -1 (0xFFFFFFFF) if no data.
                   Programs should poll in a loop with SYS_SLEEP
                   between calls to avoid busy-spinning.

Serial Port Notes:
  - COM1 at I/O 0x3F8, configured to 115200 baud, 8N1.
  - Hardware is probed at boot via the scratch register;
    if no UART is detected, serial_present is set to 0 and
    all serial I/O becomes a no-op (safe to call always).
  - SYS_SERIAL_IN is non-blocking: returns immediately with
    -1 if the receive buffer is empty.
  - In QEMU, use  -serial tcp::PORT,server=on,wait=off  or
    -serial pty  to expose the virtual COM1 to the host.
""",

    "todo.txt": """\
Mellivora OS TODO List
=====================

[x] Stage 1 boot loader with A20 gate
[x] Stage 2 with E820 memory detection
[x] Protected mode switch with flat GDT
[x] VGA text mode driver (80x25, 16 colors)
[x] PIC initialization and IRQ remapping
[x] IDT with ISR/IRQ handlers
[x] PIT timer at 100 Hz
[x] PS/2 keyboard driver with scancode table
[x] Physical memory manager (bitmap, 4KB pages)
[x] ATA PIO driver with LBA48
[x] HBFS filesystem (create, read, delete)
[x] Command shell with 34+ commands
[x] INT 0x80 syscall interface (34 syscalls)
[x] Program loading and execution
[x] User mode (ring 3) with TSS
[x] ELF binary loader (minimal)
[x] Serial port driver (COM1 console)
[x] PC speaker beep support
[x] Shell tab completion
[x] Ctrl+C program abort (hard-kill from IRQ)
[x] Batch file / script execution
[x] Environment variables
[x] File descriptor abstraction (open/read/write/close)
[x] Basic subdirectory support (mkdir, cd, pwd)
[x] Full-screen text editor (edit)
[x] Boot splash screen
[x] Command-line argument passing (SYS_GETARGS)
[x] FD write implementation (block read-modify-write)
[x] Raw disk syscall restriction (ring 3 denied)
[x] Calendar program (cal)
[x] Calculator program (calc)
[x] Virtual memory / paging
[x] Preemptive multitasking
[x] PCI bus enumeration
[x] Network stack (NIC driver + stub)
[x] Mouse driver (PS/2, IRQ12)
[x] GUI / framebuffer mode (VBE/BGA)
[x] Pipes and I/O redirection
[x] Wildcard expansion (*.txt)
""",

    "poem.txt": """\
The Silicon Sonnet

In circuits deep where electrons flow,
Through gates of logic, high and low,
A kernel wakes from metal sleep,
Its promises to fetch and keep.

The bootstrap loads with careful hand,
From disk to RAM, as specs demand,
Protected mode, the GDT set right,
Four gigabytes within its sight.

The interrupt, the timer's call,
The keyboard press that starts it all,
Each syscall answered, swift and true,
In thirty-two bits, born anew.

So boot, dear OS, take your stand,
A world of data, yours to command.
""",

    "script.bat": """\
echo Hello from batch file!
echo This is a test batch script.
echo
echo Listing directory:
dir
echo
echo Done.
""",
}

# Classify programs into categories for subdirectories
GAME_PROGRAMS = {
    '2048', 'galaga', 'guess', 'life', 'maze', 'mine',
    'snake', 'sokoban', 'tetris', 'piano',
}

# Everything else in programs/ goes to /bin


def main():
    """Parse arguments and populate disk image with files."""
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <disk_image> [programs_dir]")
        sys.exit(1)

    image_path = sys.argv[1]
    programs_dir = sys.argv[2] if len(sys.argv) > 2 else "programs"

    if not os.path.exists(image_path):
        print(f"Error: Image file '{image_path}' not found.")
        sys.exit(1)

    print(f"Populating {image_path} with organized directory structure...")

    fs = FSImage(image_path)

    # Create subdirectories
    fs.create_subdir("bin")
    fs.create_subdir("games")
    fs.create_subdir("samples")
    fs.create_subdir("docs")

    # Add text files to appropriate directories
    doc_files = {'readme.txt', 'license.txt', 'notes.txt', 'todo.txt'}
    for filename, content in TEXT_FILES.items():
        data = content.encode('ascii')
        if filename in doc_files:
            fs.add_file(filename, data, directory="docs")
        elif filename.endswith('.bat'):
            # Batch files go in root for easy access
            fs.add_file(filename, data)
        else:
            # Other text files (poem.txt etc.) in docs
            fs.add_file(filename, data, directory="docs")

    # Add binary programs
    if os.path.isdir(programs_dir):
        for fname in sorted(os.listdir(programs_dir)):
            if fname.endswith('.bin'):
                fpath = os.path.join(programs_dir, fname)
                with open(fpath, 'rb') as f:
                    data = f.read()
                prog_name = fname[:-4]
                if prog_name in GAME_PROGRAMS:
                    fs.add_file(prog_name, data, directory="games")
                else:
                    fs.add_file(prog_name, data, directory="bin")

    # Add C sample source files
    samples_dir = "samples"
    if os.path.isdir(samples_dir):
        for fname in sorted(os.listdir(samples_dir)):
            if fname.endswith('.c'):
                fpath = os.path.join(samples_dir, fname)
                with open(fpath, 'r', encoding='ascii') as f:
                    data = f.read()
                fs.add_file(fname, data.encode('ascii'),
                            directory="samples")

    fs.finalize()


if __name__ == '__main__':
    main()
