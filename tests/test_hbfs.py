#!/usr/bin/env python3
"""
test_hbfs.py - Deep HBFS filesystem integrity checker.

Reads the Mellivora OS disk image and validates:
  - Superblock fields
  - Bitmap vs directory consistency
  - Directory entry structure (root + all subdirectories)
  - File data area bounds
  - Free block count accuracy
  - Subdirectory structure and child file integrity
  - Program binary header validation
  - Global block allocation overlap (across all directories)

Usage: python3 tests/test_hbfs.py [mellivora.img]
"""

import struct
import sys
import os

# ---- HBFS constants (must match kernel.asm / populate.py) ----
SECTOR_SIZE = 512
BLOCK_SIZE = 4096
SECTORS_PER_BLOCK = BLOCK_SIZE // SECTOR_SIZE  # 8
HBFS_MAGIC = 0x48424653  # 'HBFS'
HBFS_SUPERBLOCK_LBA = 417
HBFS_BITMAP_START = 418
HBFS_ROOT_DIR_START = 426
HBFS_ROOT_DIR_BLOCKS = 16
HBFS_ROOT_DIR_SIZE = HBFS_ROOT_DIR_BLOCKS * BLOCK_SIZE
HBFS_DATA_START = 554
HBFS_DIR_ENTRY_SIZE = 288
HBFS_MAX_FILES = HBFS_ROOT_DIR_SIZE // HBFS_DIR_ENTRY_SIZE
HBFS_SUBDIR_BLOCKS = 4
HBFS_SUBDIR_SIZE = HBFS_SUBDIR_BLOCKS * BLOCK_SIZE
HBFS_SUBDIR_MAX_FILES = HBFS_SUBDIR_SIZE // HBFS_DIR_ENTRY_SIZE  # 56
TOTAL_BLOCKS = 32768

# Program load address (must match kernel.asm PROGRAM_BASE)
PROGRAM_BASE = 0x00200000

# File types
FTYPE_FREE = 0
FTYPE_TEXT = 1
FTYPE_DIR = 2
FTYPE_EXEC = 3
FTYPE_BATCH = 4

FTYPE_NAMES = {
    FTYPE_FREE: "free", FTYPE_TEXT: "text", FTYPE_DIR: "dir",
    FTYPE_EXEC: "exec", FTYPE_BATCH: "batch",
}

PASS = 0
FAIL = 0


def ok(msg):
    global PASS
    PASS += 1
    print(f"  \033[32mPASS\033[0m  {msg}")


def fail(msg):
    global FAIL
    FAIL += 1
    print(f"  \033[31mFAIL\033[0m  {msg}")


def check(desc, cond):
    if cond:
        ok(desc)
    else:
        fail(desc)


def read_at(f, offset, size):
    f.seek(offset)
    return f.read(size)


def lba_offset(lba):
    return lba * SECTOR_SIZE


def block_to_lba(block_num):
    """Convert data block number to LBA."""
    return HBFS_DATA_START + block_num * SECTORS_PER_BLOCK


def parse_dir_entries(dir_data, max_entries):
    """Parse directory entries from raw directory data. Returns (files, free_count)."""
    files = []
    free_count = 0
    for i in range(max_entries):
        off = i * HBFS_DIR_ENTRY_SIZE
        entry = dir_data[off:off + HBFS_DIR_ENTRY_SIZE]
        ftype = entry[253]
        if ftype == FTYPE_FREE:
            free_count += 1
            continue

        name_bytes = entry[0:253]
        null_pos = name_bytes.find(b"\x00")
        if null_pos >= 0:
            name = name_bytes[:null_pos].decode("ascii", errors="replace")
        else:
            name = name_bytes.decode("ascii", errors="replace")

        size = struct.unpack_from("<I", entry, 256)[0]
        start_block = struct.unpack_from("<I", entry, 260)[0]
        block_count = struct.unpack_from("<I", entry, 264)[0]
        created = struct.unpack_from("<I", entry, 268)[0]
        modified = struct.unpack_from("<I", entry, 272)[0]

        files.append({
            "index": i,
            "name": name,
            "type": ftype,
            "size": size,
            "start_block": start_block,
            "block_count": block_count,
            "created": created,
            "modified": modified,
        })

    return files, free_count


def validate_file_entry(fi, bitmap, prefix=""):
    """Validate a single file entry. Returns list of (block, owner) tuples for overlap check."""
    name = fi["name"]
    ftype = fi["type"]
    size = fi["size"]
    start = fi["start_block"]
    blocks = fi["block_count"]
    display = f"{prefix}{name}"

    check(f"  '{display}' has valid name",
          len(name) > 0 and all(32 <= ord(c) < 127 for c in name))

    check(f"  '{display}' has valid type ({FTYPE_NAMES.get(ftype, '?')})",
          ftype in (FTYPE_TEXT, FTYPE_DIR, FTYPE_EXEC, FTYPE_BATCH))

    if ftype != FTYPE_DIR:
        expected_blocks = max(1, (size + BLOCK_SIZE - 1) // BLOCK_SIZE)
        check(f"  '{display}' block count ({blocks}) >= needed ({expected_blocks})",
              blocks >= expected_blocks)

    max_block = TOTAL_BLOCKS
    check(f"  '{display}' start block ({start}) in range",
          0 <= start < max_block)
    check(f"  '{display}' end block ({start + blocks}) in range",
          start + blocks <= max_block)

    # Verify bitmap bits
    bitmap_ok = True
    for b in range(start, start + blocks):
        byte_idx = b // 8
        bit_idx = b % 8
        if byte_idx < len(bitmap):
            is_set = (bitmap[byte_idx] >> bit_idx) & 1
            if not is_set:
                fail(f"  '{display}' block {b} not marked in bitmap")
                bitmap_ok = False
                break
    if bitmap_ok:
        ok(f"  '{display}' all blocks marked in bitmap")

    return [(b, display) for b in range(start, start + blocks)]


def validate_program_binary(f, fi, prefix=""):
    """Validate that an executable binary has a reasonable header."""
    name = fi["name"]
    display = f"{prefix}{name}"
    start = fi["start_block"]
    size = fi["size"]

    if size < 4:
        fail(f"  '{display}' binary too small ({size} bytes)")
        return

    data_offset = lba_offset(block_to_lba(start))
    header = read_at(f, data_offset, min(size, 64))

    # Check for ELF magic or valid x86 code
    elf_magic = struct.unpack_from("<I", header, 0)[0]
    if elf_magic == 0x464C457F:
        ok(f"  '{display}' valid ELF header")
        return

    # For flat binaries: first bytes should be from syscalls.inc which has
    # [ORG 0x200000] followed by `jmp start` (0xE9 rel32) past the shared code.
    # The first instruction is the `jmp start` at offset 0.
    first_byte = header[0]
    # Accept: E9 (jmp near), EB (jmp short), or common x86 opcodes
    valid_opcodes = {
        0xE9,  # jmp rel32
        0xEB,  # jmp rel8
        0x55,  # push ebp
        0x53,  # push ebx
        0x50,  # push eax
        0x56,  # push esi
        0x57,  # push edi
        0x31,  # xor
        0x33,  # xor
        0x89,  # mov
        0x8B,  # mov
        0xB8,  # mov eax, imm
        0xB9,  # mov ecx, imm
        0xBA,  # mov edx, imm
        0xBB,  # mov ebx, imm
        0xBE,  # mov esi, imm
        0xBF,  # mov edi, imm
        0x83,  # sub/add/cmp r, imm8
        0x81,  # sub/add/cmp r, imm32
        0xE8,  # call rel32
        0x60,  # pushad
        0x90,  # nop
        0xCC,  # int3
    }
    check(f"  '{display}' starts with valid x86 opcode (0x{first_byte:02X})",
          first_byte in valid_opcodes)


def main():
    img_path = sys.argv[1] if len(sys.argv) > 1 else "mellivora.img"
    if not os.path.isfile(img_path):
        print(f"Error: '{img_path}' not found.")
        sys.exit(2)

    with open(img_path, "rb") as f:
        img_size = f.seek(0, 2)
        f.seek(0)

        print("=== HBFS Integrity Check ===")
        print(f"Image: {img_path} ({img_size} bytes)")
        print()

        # ---- Superblock ----
        print("[Superblock]")
        sb = read_at(f, lba_offset(HBFS_SUPERBLOCK_LBA), SECTOR_SIZE)
        magic = struct.unpack_from("<I", sb, 0)[0]
        version = struct.unpack_from("<I", sb, 4)[0]
        total_blocks = struct.unpack_from("<I", sb, 8)[0]
        free_blocks = struct.unpack_from("<I", sb, 12)[0]
        root_dir_blk = struct.unpack_from("<I", sb, 16)[0]
        bitmap_start = struct.unpack_from("<I", sb, 20)[0]
        data_start = struct.unpack_from("<I", sb, 24)[0]
        block_size = struct.unpack_from("<I", sb, 28)[0]

        check("Magic = 'HBFS'", magic == HBFS_MAGIC)
        check("Version = 1", version == 1)
        check(f"Total blocks = {TOTAL_BLOCKS}", total_blocks == TOTAL_BLOCKS)
        check(f"Free blocks <= total ({free_blocks} <= {total_blocks})",
              free_blocks <= total_blocks)
        check(f"Free blocks > 0 (not full)", free_blocks > 0)
        check(f"Root dir block = {HBFS_ROOT_DIR_START}", root_dir_blk == HBFS_ROOT_DIR_START)
        check(f"Bitmap start = {HBFS_BITMAP_START}", bitmap_start == HBFS_BITMAP_START)
        check(f"Data start = {HBFS_DATA_START}", data_start == HBFS_DATA_START)
        check(f"Block size = {BLOCK_SIZE}", block_size == BLOCK_SIZE)
        print()

        # ---- Bitmap ----
        print("[Bitmap]")
        bitmap = read_at(f, lba_offset(HBFS_BITMAP_START), BLOCK_SIZE)
        alloc_count = 0
        for byte in bitmap:
            alloc_count += bin(byte).count("1")
        used_blocks = alloc_count
        sb_free = free_blocks
        expected_used = total_blocks - sb_free

        check(f"Bitmap allocated bits ({used_blocks}) match superblock "
              f"(total - free = {expected_used})",
              used_blocks == expected_used)
        check("Bitmap has at least 1 allocated block", used_blocks > 0)
        print()

        # ---- Root directory ----
        print("[Root directory]")
        rd = read_at(f, lba_offset(HBFS_ROOT_DIR_START), HBFS_ROOT_DIR_SIZE)
        files, free_entries = parse_dir_entries(rd, HBFS_MAX_FILES)

        file_count = len(files)
        check(f"Found {file_count} files in root directory", file_count > 0)
        check(f"Free entries ({free_entries}) + files ({file_count}) = {HBFS_MAX_FILES}",
              free_entries + file_count == HBFS_MAX_FILES)
        print()

        # ---- Per-file checks (root) ----
        print(f"[Root file entries ({file_count} files)]")
        all_blocks = []  # Global block ownership list for overlap check
        for fi in files:
            blocks = validate_file_entry(fi, bitmap, prefix="/")
            all_blocks.extend(blocks)
        print()

        # ---- Subdirectory traversal ----
        dir_entries = [fi for fi in files if fi["type"] == FTYPE_DIR]
        total_subdir_files = 0

        for dent in dir_entries:
            dirname = dent["name"]
            start_block = dent["start_block"]
            block_count = dent["block_count"]

            subdir_lba = block_to_lba(start_block)
            subdir_size = block_count * BLOCK_SIZE
            subdir_max = subdir_size // HBFS_DIR_ENTRY_SIZE

            print(f"[Subdirectory /{dirname} ({subdir_max} max entries)]")

            subdir_data = read_at(f, lba_offset(subdir_lba), subdir_size)
            sub_files, sub_free = parse_dir_entries(subdir_data, subdir_max)

            sub_count = len(sub_files)
            total_subdir_files += sub_count
            check(f"  /{dirname}: found {sub_count} files", sub_count >= 0)
            check(f"  /{dirname}: free ({sub_free}) + files ({sub_count}) = {subdir_max}",
                  sub_free + sub_count == subdir_max)

            for fi in sub_files:
                blocks = validate_file_entry(fi, bitmap, prefix=f"/{dirname}/")
                all_blocks.extend(blocks)

            print()

        # ---- Summary of all files ----
        grand_total = file_count + total_subdir_files
        print(f"[File census]")
        check(f"Total files across all directories: {grand_total}",
              grand_total > 0)
        # We expect at least the number of .bin programs + text files + batch
        check(f"At least 60 files populated (found {grand_total})",
              grand_total >= 60)
        print()

        # ---- Program binary header validation ----
        print("[Program binary validation]")
        exec_files = []
        for fi in files:
            if fi["type"] == FTYPE_EXEC:
                exec_files.append((fi, "/"))
        for dent in dir_entries:
            dirname = dent["name"]
            subdir_lba = block_to_lba(dent["start_block"])
            subdir_size = dent["block_count"] * BLOCK_SIZE
            subdir_max = subdir_size // HBFS_DIR_ENTRY_SIZE
            subdir_data = read_at(f, lba_offset(subdir_lba), subdir_size)
            sub_files, _ = parse_dir_entries(subdir_data, subdir_max)
            for fi in sub_files:
                if fi["type"] == FTYPE_EXEC:
                    exec_files.append((fi, f"/{dirname}/"))

        for fi, prefix in exec_files:
            validate_program_binary(f, fi, prefix=prefix)
        check(f"Validated {len(exec_files)} executable binaries", len(exec_files) > 0)
        print()

        # ---- Global allocation overlap check (all directories) ----
        print("[Global allocation overlap check]")
        block_map = {}
        overlaps = 0
        for block_num, owner in all_blocks:
            if block_num in block_map:
                fail(f"Block {block_num} claimed by both "
                     f"'{block_map[block_num]}' and '{owner}'")
                overlaps += 1
            else:
                block_map[block_num] = owner
        if overlaps == 0:
            ok(f"No overlapping block allocations ({len(block_map)} blocks checked)")

        # Verify total allocated blocks match bitmap
        check(f"File blocks ({len(block_map)}) match bitmap allocated ({used_blocks})",
              len(block_map) == used_blocks)
        print()

        # ---- Bitmap unused region check ----
        print("[Bitmap unused region]")
        # All bits beyond next_block should be 0 (unused)
        highest_alloc = max(block_map.keys()) if block_map else 0
        stray_bits = 0
        for b in range(highest_alloc + 1, min(TOTAL_BLOCKS, (len(bitmap)) * 8)):
            byte_idx = b // 8
            bit_idx = b % 8
            if byte_idx < len(bitmap):
                if (bitmap[byte_idx] >> bit_idx) & 1:
                    stray_bits += 1
        check(f"No stray bitmap bits beyond allocated range "
              f"(found {stray_bits})", stray_bits == 0)
        print()

    # ---- Summary ----
    total = PASS + FAIL
    print(f"=== Results: {PASS}/{total} passed ===")
    if FAIL > 0:
        print(f"{FAIL} test(s) FAILED")
        sys.exit(1)
    else:
        print("All tests passed!")
        sys.exit(0)


if __name__ == "__main__":
    main()
