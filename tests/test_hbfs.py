#!/usr/bin/env python3
"""
test_hbfs.py - Deep HBFS filesystem integrity checker.

Reads the Mellivora OS disk image and validates:
  - Superblock fields
  - Bitmap vs directory consistency
  - Directory entry structure
  - File data area bounds
  - Free block count accuracy

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
TOTAL_BLOCKS = 32768

# File types
FTYPE_FREE = 0
FTYPE_TEXT = 1
FTYPE_DIR = 2
FTYPE_EXEC = 3
FTYPE_BATCH = 4

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
        # Count allocated bits
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

        files = []
        free_entries = 0
        for i in range(HBFS_MAX_FILES):
            off = i * HBFS_DIR_ENTRY_SIZE
            entry = rd[off:off + HBFS_DIR_ENTRY_SIZE]
            ftype = entry[253]
            if ftype == FTYPE_FREE:
                free_entries += 1
                continue

            # Extract filename (null-terminated)
            name_bytes = entry[0:253]
            null_pos = name_bytes.find(b"\x00")
            if null_pos >= 0:
                name = name_bytes[:null_pos].decode("ascii", errors="replace")
            else:
                name = name_bytes.decode("ascii", errors="replace")

            size = struct.unpack_from("<I", entry, 256)[0]
            start_block = struct.unpack_from("<I", entry, 260)[0]
            block_count = struct.unpack_from("<I", entry, 264)[0]

            files.append({
                "index": i,
                "name": name,
                "type": ftype,
                "size": size,
                "start_block": start_block,
                "block_count": block_count,
            })

        file_count = len(files)
        check(f"Found {file_count} files in root directory", file_count > 0)
        check(f"Free entries ({free_entries}) + files ({file_count}) = {HBFS_MAX_FILES}",
              free_entries + file_count == HBFS_MAX_FILES)
        print()

        # ---- Per-file checks ----
        print(f"[File entries ({file_count} files)]")
        for fi in files:
            name = fi["name"]
            ftype = fi["type"]
            size = fi["size"]
            start = fi["start_block"]
            blocks = fi["block_count"]

            # Name should be non-empty and printable ASCII
            check(f"  '{name}' has valid name",
                  len(name) > 0 and all(32 <= ord(c) < 127 for c in name))

            # Type should be known
            check(f"  '{name}' has valid type ({ftype})",
                  ftype in (FTYPE_TEXT, FTYPE_DIR, FTYPE_EXEC, FTYPE_BATCH))

            # Block count should be enough for file size
            if ftype != FTYPE_DIR:
                expected_blocks = max(1, (size + BLOCK_SIZE - 1) // BLOCK_SIZE)
                check(f"  '{name}' block count ({blocks}) >= needed ({expected_blocks})",
                      blocks >= expected_blocks)

            # Start block should be in data area range
            max_block = TOTAL_BLOCKS
            check(f"  '{name}' start block ({start}) in range",
                  0 <= start < max_block)
            check(f"  '{name}' end block ({start + blocks}) in range",
                  start + blocks <= max_block)

            # Verify the corresponding bitmap bits are set
            for b in range(start, start + blocks):
                byte_idx = b // 8
                bit_idx = b % 8
                if byte_idx < len(bitmap):
                    is_set = (bitmap[byte_idx] >> bit_idx) & 1
                    if not is_set:
                        fail(f"  '{name}' block {b} not marked in bitmap")
                        break
            else:
                ok(f"  '{name}' all blocks marked in bitmap")

        print()

        # ---- Cross-check: no overlapping file allocations ----
        print("[Allocation overlap check]")
        block_map = {}
        overlaps = 0
        for fi in files:
            for b in range(fi["start_block"], fi["start_block"] + fi["block_count"]):
                if b in block_map:
                    fail(f"Block {b} claimed by both '{block_map[b]}' and '{fi['name']}'")
                    overlaps += 1
                else:
                    block_map[b] = fi["name"]
        if overlaps == 0:
            ok("No overlapping block allocations")
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
