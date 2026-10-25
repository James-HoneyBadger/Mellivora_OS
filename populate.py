#!/usr/bin/env python3
"""
populate.py - Populate a Mellivora OS disk image with sample files.

Writes files directly into the HBFS filesystem on the disk image.
This understands the on-disk HBFS layout:
  - Superblock at LBA 4096 (well past the kernel end, which is LBA 33 +
    KERNEL_SECTORS as generated into kernel_sectors.inc)
  - Block allocation bitmap at LBA 4097 (128 sectors = 16 blocks)
  - Root directory at LBA 4225 (256 sectors = 32 blocks)
  - Data area starts at LBA 4481

Each data block is 4096 bytes (8 sectors).
Each directory entry is 288 bytes.
That yields 14 entries per directory block, 455 entries in the root directory,
and 224 entries per subdirectory.
"""

import struct
import sys
import os
import json

# HBFS constants (must match kernel.asm)
SECTOR_SIZE = 512
BLOCK_SIZE = 4096
SECTORS_PER_BLOCK = BLOCK_SIZE // SECTOR_SIZE  # 8
HBFS_MAGIC = 0x48424653  # 'HBFS'
# HBFS on-disk layout — must match HBFS_SUPERBLOCK_LBA in kernel.asm.
# All derived constants are computed relative to HBFS_SUPERBLOCK_LBA so
# only ONE value needs updating if the layout ever changes.
HBFS_SUPERBLOCK_LBA = 4096
HBFS_BITMAP_BLOCKS = 16
HBFS_BITMAP_SIZE = HBFS_BITMAP_BLOCKS * BLOCK_SIZE   # 65536 bytes
HBFS_BITMAP_START = HBFS_SUPERBLOCK_LBA + 1
HBFS_ROOT_DIR_BLOCKS = 32
HBFS_ROOT_DIR_SECTS = HBFS_ROOT_DIR_BLOCKS * SECTORS_PER_BLOCK  # 256
HBFS_ROOT_DIR_SIZE = HBFS_ROOT_DIR_BLOCKS * BLOCK_SIZE          # 131072
HBFS_ROOT_DIR_START = HBFS_BITMAP_START + HBFS_BITMAP_BLOCKS * SECTORS_PER_BLOCK
HBFS_DATA_START = HBFS_ROOT_DIR_START + HBFS_ROOT_DIR_SECTS
HBFS_DIR_ENTRY_SIZE = 288
HBFS_MAX_FILES = HBFS_ROOT_DIR_SIZE // HBFS_DIR_ENTRY_SIZE      # 455
HBFS_MAX_FILENAME = 252
# The journal occupies the first HBFS_JOURNAL_BLOCKS data blocks and must never
# be handed to a file.
HBFS_JOURNAL_BLOCKS = 4

# HBFS v4 (roadmap P4) keeps every v3 field where it is and adds its own after
# them, inside the same 512-byte sector. "After them" is meant literally: v3
# uses the journal state and pointer at 40 and 44, the feature word at 48, the
# root uid/gid at 52 and the journal epoch at 56, so the v4 header starts at 60
# and a v3 reader looking at a v4 volume still finds its geometry where it
# expects it.
#
# Nothing here writes v4 by default - the image stays v3 until the writer can be
# boot-verified - but the layout and the checksum are exercised by
# `make check-hbfs-checksum`, and tools/fsck_hbfs.py --complain recomputes the
# checksum from the bytes on disk.
HBFS4_MAGIC = 0x34424653              # 'HBF4'
HBFS4_OFF_FEATURES = 60
HBFS4_OFF_GENERATION = 64
HBFS4_OFF_BLOCKS_HI = 68
HBFS4_OFF_DATA_HI = 72
HBFS4_OFF_CHECKSUM = 76
HBFS4_FEATURE_EXTENTS = 1 << 0
HBFS4_FEATURE_JOURNAL = 1 << 1
HBFS4_FEATURE_SNAPSHOT = 1 << 2
# Every metadata run carries its own CRC-32C in its last four bytes, read as
# zero while the checksum is computed. The runs have that room without moving a
# bit or an entry: the bitmap uses 65466 of its 65536 bytes for 523727 blocks,
# the root directory 131040 of its 131072 for 455 entries, and a subdirectory
# 64512 of its 65536 for 224. A run-level checksum cannot say *which* block is
# bad; per-block trailers need the bitmap to grow a block and entries to stop
# straddling, which changes both readers, so they belong with the v4 writer.
HBFS4_RUN_CHECKSUM_BYTES = 4

# Extents (roadmap P4). An entry that sets HBFS4_DE_EXTENTS in its flags word -
# the two bytes at 254 that v3 has always written as zero - keeps its data in
# several runs instead of one. `start` then names a metadata block of
# {block, count} records and `blocks` is still the number of data blocks the file
# occupies. A file that is one run stays inline and is read exactly as before.
HBFS4_DE_EXTENTS = 1 << 0
HBFS4_EXTENT_MAGIC = 0x34584248          # on disk: 'H','B','X','4'
HBFS4_EXTENT_HDR = 8                     # magic, record count
HBFS4_EXTENT_RECORD = 8                  # start block, block count
HBFS4_EXTENT_MAX = ((BLOCK_SIZE - HBFS4_EXTENT_HDR
                     - HBFS4_RUN_CHECKSUM_BYTES) // HBFS4_EXTENT_RECORD)

# A 2048 MiB image has 523727 complete 4 KB data blocks after the metadata
# (data starts at LBA 4481). Declaring 524288 made the last 561 blocks point
# past the end of the image.
MIN_BLOCKS = 64
TOTAL_BLOCKS = 523727

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


# ---------------------------------------------------------------------------
# Incremental populate cache
# ---------------------------------------------------------------------------
_CACHE_FILE = ".populate_cache"


def _load_cache():
    """Load mtime cache from disk.  Returns (files_dict, tombstones_set)."""
    try:
        with open(_CACHE_FILE, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError):
        return {}, set()
    if isinstance(data, dict):
        # v2 schema: {"_v": 2, "files": {...}, "tombstones": [...]}
        if data.get("_v") == 2:
            files = data.get("files", {}) or {}
            tombs = set(data.get("tombstones", []) or [])
            if isinstance(files, dict):
                return files, tombs
        # v1 schema: flat {fname: mtime}
        else:
            return data, set()
    return {}, set()


def _save_cache(files, tombstones):
    """Persist mtime cache and tombstones to disk."""
    payload = {
        "_v": 2,
        "files": files,
        "tombstones": sorted(tombstones),
    }
    try:
        with open(_CACHE_FILE, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
    except OSError as exc:
        print(f"  WARNING: could not write cache file: {exc}")


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


def crc32c(data):
    """CRC-32C (Castagnoli), the same polynomial the HBFS journal uses.

    Table-free on purpose: the builder runs once per image, and a table is one
    more thing that can be transcribed wrongly. The catalogue value for
    "123456789" is 0xE3069283 and the test pins both copies of this to it.
    """
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def superblock_checksum(sector):
    """CRC-32C over a superblock with its own checksum field read as zero."""
    view = bytearray(sector)
    struct.pack_into('<I', view, HBFS4_OFF_CHECKSUM, 0)
    return crc32c(bytes(view))


def stamp_run_checksum(run):
    """Write a metadata run's checksum into its trailer, in place.

    The trailer is read as zero while the checksum is computed, so stamping an
    unchanged run twice gives the same value and a reader can recompute it
    without knowing what was there before.
    """
    offset = len(run) - HBFS4_RUN_CHECKSUM_BYTES
    struct.pack_into('<I', run, offset, 0)
    value = crc32c(bytes(run))
    struct.pack_into('<I', run, offset, value)
    return value


def create_superblock(version=3, features=None, generation=0):
    """Create an HBFS superblock: v3 for the images, v4 on request.

    v3 writes the version field as 1 - every v3 image ever built says 1 there,
    and the magic is what actually identifies the version - so this stays
    byte-for-byte what it has always produced. v4 writes 4, the feature and
    generation words, the high halves of the 64-bit block counters and a
    CRC-32C over the sector with the checksum field read as zero.

    `features` defaults to what this builder can actually produce: extents and a
    journal, for v4. A volume that says it has no extents while its entries name
    runs would be lying to the checker, and one that claims no journal is one the
    kernel mounts read-only - which is exactly what a feature word is for.
    """
    if version not in (3, 4):
        raise ValueError("HBFS version %r is not supported" % (version,))
    if features is None:
        features = (HBFS4_FEATURE_EXTENTS | HBFS4_FEATURE_JOURNAL
                    if version == 4 else 0)
    sb = bytearray(SECTOR_SIZE)
    struct.pack_into('<I', sb, 0,
                     HBFS_MAGIC if version == 3 else HBFS4_MAGIC)  # Magic
    struct.pack_into('<I', sb, 4, 1 if version == 3 else 4)  # Version
    struct.pack_into('<I', sb, 8, TOTAL_BLOCKS)  # Total blocks
    struct.pack_into('<I', sb, 12, TOTAL_BLOCKS)  # Free (update)
    struct.pack_into('<I', sb, 16, HBFS_ROOT_DIR_START)
    struct.pack_into('<I', sb, 20, HBFS_BITMAP_START)
    struct.pack_into('<I', sb, 24, HBFS_DATA_START)
    struct.pack_into('<I', sb, 28, BLOCK_SIZE)
    if version == 4:
        struct.pack_into('<I', sb, HBFS4_OFF_FEATURES, features)
        struct.pack_into('<I', sb, HBFS4_OFF_GENERATION, generation)
        struct.pack_into('<I', sb, HBFS4_OFF_BLOCKS_HI, TOTAL_BLOCKS >> 32)
        struct.pack_into('<I', sb, HBFS4_OFF_DATA_HI, HBFS_DATA_START >> 32)
        struct.pack_into('<I', sb, HBFS4_OFF_CHECKSUM, 0)
        struct.pack_into('<I', sb, HBFS4_OFF_CHECKSUM,
                         superblock_checksum(sb))
    return sb


def create_dir_entry(filename, ftype, size, start_block,
                     block_count, timestamp=1000, flags=0):
    """Create a single HBFS directory entry (288 bytes)."""
    entry = bytearray(HBFS_DIR_ENTRY_SIZE)

    # Filename: bytes 0-252 (null-terminated, max 252 chars)
    raw = filename.encode('ascii')
    if len(raw) > HBFS_MAX_FILENAME:
        # Phase 3.2: surface the truncation instead of silently shortening.
        print(f"  WARNING: filename '{filename}' is {len(raw)} bytes, "
              f"truncating to {HBFS_MAX_FILENAME}")
    name_bytes = raw[:HBFS_MAX_FILENAME]
    entry[0:len(name_bytes)] = name_bytes
    entry[len(name_bytes)] = 0  # Null terminator

    # Type: byte 253
    entry[253] = ftype

    # Flags: bytes 254-255. v3 has always written zero; v4 uses bit 0 for
    # "this entry's data is a list of extents".
    struct.pack_into('<H', entry, 254, flags)

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

    # Permission metadata (v3): mode/uid/gid. Explicit defaults matter because a
    # zero mode makes every entry a legacy entry that permission checks reject.
    if ftype in (FTYPE_DIR, FTYPE_EXEC):
        mode = 0o755
    else:
        mode = 0o644
    struct.pack_into('<H', entry, 276, mode)
    struct.pack_into('<H', entry, 278, 0)      # uid = root
    struct.pack_into('<H', entry, 280, 0)      # gid = root

    return entry


HBFS_SUBDIR_BLOCKS = 16                 # Blocks per subdirectory
HBFS_SUBDIR_SIZE = HBFS_SUBDIR_BLOCKS * BLOCK_SIZE
HBFS_SUBDIR_MAX_FILES = HBFS_SUBDIR_SIZE // HBFS_DIR_ENTRY_SIZE  # 224
HBFS_BITMAP_SECTS = HBFS_BITMAP_BLOCKS * SECTORS_PER_BLOCK


class FSImage:
    """Manages an HBFS filesystem image with subdirectory support."""

    def __init__(self, image_path, version=3):
        self.image_path = image_path
        self.version = version
        # The volume may only claim the blocks the image can actually hold, so
        # a smaller image stays consistent instead of pointing past its end.
        global TOTAL_BLOCKS
        image_bytes = os.path.getsize(image_path)
        usable = image_bytes - HBFS_DATA_START * SECTOR_SIZE
        if usable < BLOCK_SIZE * MIN_BLOCKS:
            raise SystemExit(
                f"Error: image '{image_path}' is too small for HBFS "
                f"(need at least {HBFS_DATA_START * SECTOR_SIZE + BLOCK_SIZE * MIN_BLOCKS} bytes)"
            )
        TOTAL_BLOCKS = usable // BLOCK_SIZE
        self.img = open(image_path, 'r+b')
        self.bitmap = bytearray(HBFS_BITMAP_SIZE)
        self.root_dir = bytearray(HBFS_ROOT_DIR_SIZE)
        self.next_block = 0
        self.total_files = 0
        self.preserve_mode = False
        # name -> {start_block, data, entry_count, data_lba}
        self.subdirs = {}

        # If a valid HBFS already exists, preserve its contents.
        self.img.seek(lba_to_offset(HBFS_SUPERBLOCK_LBA))
        existing_sb = self.img.read(SECTOR_SIZE)
        if len(existing_sb) == SECTOR_SIZE:
            magic = struct.unpack_from('<I', existing_sb, 0)[0]
            version = struct.unpack_from('<I', existing_sb, 4)[0]
            if magic == self._magic and version >= 1:
                self.preserve_mode = True
                self.sb = bytearray(existing_sb)

                self.img.seek(lba_to_offset(HBFS_BITMAP_START))
                self.bitmap = bytearray(self.img.read(HBFS_BITMAP_SIZE))

                self.img.seek(lba_to_offset(HBFS_ROOT_DIR_START))
                self.root_dir = bytearray(self.img.read(HBFS_ROOT_DIR_SIZE))
                # Bring pre-v3 entries (mode 0) up to explicit defaults.
                self._migrate_modes(self.root_dir, HBFS_MAX_FILES)

                self.next_block = self._find_next_block()
                return

        # No filesystem yet: initialize a fresh one.
        self.sb = create_superblock(self.version)
        self.next_block = HBFS_JOURNAL_BLOCKS
        self.img.seek(lba_to_offset(HBFS_SUPERBLOCK_LBA))
        self.img.write(self.sb)

    @property
    def _magic(self):
        return HBFS_MAGIC if self.version == 3 else HBFS4_MAGIC

    @staticmethod
    def _entry_name(directory_buf, off):
        end = off
        limit = off + HBFS_MAX_FILENAME + 1
        while end < limit and directory_buf[end] != 0:
            end += 1
        return bytes(directory_buf[off:end]).decode('ascii', errors='ignore')

    @staticmethod
    def _entry_used(directory_buf, off):
        return (
            directory_buf[off + 253] != FTYPE_FREE
            and directory_buf[off] != 0
        )

    @staticmethod
    def _migrate_modes(directory_buf, max_entries):
        """Give legacy mode-0 entries explicit default permissions.

        Images created before v3 have mode/uid/gid zeroed. Those entries are
        rejected by permission checks and look like "legacy" entries to fsck.
        Nothing else about the entry changes.
        """
        for i in range(max_entries):
            off = i * HBFS_DIR_ENTRY_SIZE
            if directory_buf[off + 253] == FTYPE_FREE:
                continue
            mode = struct.unpack_from('<H', directory_buf, off + 276)[0]
            if mode == 0:
                ftype = directory_buf[off + 253]
                mode = 0o755 if ftype in (FTYPE_DIR, FTYPE_EXEC) else 0o644
                struct.pack_into('<H', directory_buf, off + 276, mode)

    def _find_next_block(self):
        """Return the next free block after the highest used one."""
        for block in range(TOTAL_BLOCKS - 1, -1, -1):
            byte_idx = block // 8
            bit_idx = block % 8
            if self.bitmap[byte_idx] & (1 << bit_idx):
                return block + 1
        return 0

    def find_entry_slot(self, directory_buf, max_entries, name):
        """Find an existing filename in a directory buffer."""
        for i in range(max_entries):
            off = i * HBFS_DIR_ENTRY_SIZE
            if self._entry_used(directory_buf, off):
                if self._entry_name(directory_buf, off) == name:
                    return i
        return None

    def _find_free_slot(self, directory_buf, max_entries):
        """Find a free slot in a directory buffer."""
        for i in range(max_entries):
            off = i * HBFS_DIR_ENTRY_SIZE
            if (directory_buf[off + 253] == FTYPE_FREE
                    and directory_buf[off] == 0):
                return i
        return None

    def _count_entries(self, directory_buf, max_entries):
        """Count live directory entries."""
        count = 0
        for i in range(max_entries):
            off = i * HBFS_DIR_ENTRY_SIZE
            if self._entry_used(directory_buf, off):
                count += 1
        return count

    def _mark_block_range(self, bitmap, start, count):
        """Mark a block range as allocated in the provided bitmap."""
        for b in range(start, start + count):
            byte_idx = b // 8
            bit_idx = b % 8
            bitmap[byte_idx] |= (1 << bit_idx)

    def _alloc_blocks(self, count):
        """Allocate contiguous blocks, returns start block number."""
        start = self.next_block
        self._mark_block_range(self.bitmap, start, count)
        self.next_block += count
        return start

    def _rebuild_bitmap(self):
        """Rebuild the allocation bitmap from live directory entries."""
        fresh = bytearray(HBFS_BITMAP_SIZE)

        # The journal is reserved even when no transaction is active.
        self._mark_block_range(fresh, 0, HBFS_JOURNAL_BLOCKS)

        def mark_directory(directory_buf, max_entries):
            entries = []
            for i in range(max_entries):
                off = i * HBFS_DIR_ENTRY_SIZE
                if not self._entry_used(directory_buf, off):
                    continue
                name = self._entry_name(directory_buf, off)
                ftype = directory_buf[off + 253]
                start_block = struct.unpack_from(
                    '<I', directory_buf, off + 260)[0]
                block_count = struct.unpack_from(
                    '<I', directory_buf, off + 264)[0]
                flags = struct.unpack_from('<H', directory_buf, off + 254)[0]
                if flags & HBFS4_DE_EXTENTS:
                    # The entry names an extent block; the data blocks are the
                    # runs inside it, and they need not be contiguous.
                    self._mark_block_range(fresh, start_block, 1)
                    for run_start, run_blocks in self._read_extents(start_block):
                        self._mark_block_range(fresh, run_start, run_blocks)
                elif block_count > 0:
                    self._mark_block_range(fresh, start_block, block_count)
                entries.append((name, ftype, start_block, block_count))
            return entries

        root_entries = mark_directory(self.root_dir, HBFS_MAX_FILES)
        for name, ftype, start_block, block_count in root_entries:
            if ftype != FTYPE_DIR or block_count <= 0:
                continue
            if name in self.subdirs:
                subdir_data = self.subdirs[name]['data']
            else:
                data_lba = HBFS_DATA_START + start_block * SECTORS_PER_BLOCK
                self.img.seek(lba_to_offset(data_lba))
                subdir_data = self.img.read(block_count * BLOCK_SIZE)
            subdir_max = (block_count * BLOCK_SIZE) // HBFS_DIR_ENTRY_SIZE
            mark_directory(subdir_data, subdir_max)

        self.bitmap = fresh
        self.next_block = self._find_next_block()
        return sum(bin(byte).count('1') for byte in self.bitmap)

    def _read_extents(self, extent_block):
        """Read an extent block back into a list of (start, blocks) runs.

        A block that does not parse reads as no runs, because the builder is not
        the checker: fsck_hbfs.py is, and it is the one that has to complain.
        """
        lba = HBFS_DATA_START + extent_block * SECTORS_PER_BLOCK
        self.img.seek(lba_to_offset(lba))
        raw = self.img.read(BLOCK_SIZE)
        if len(raw) < BLOCK_SIZE:
            return []
        if struct.unpack_from('<I', raw, 0)[0] != HBFS4_EXTENT_MAGIC:
            return []
        count = struct.unpack_from('<I', raw, 4)[0]
        if count > HBFS4_EXTENT_MAX:
            return []
        return [struct.unpack_from('<II', raw,
                                   HBFS4_EXTENT_HDR + i * HBFS4_EXTENT_RECORD)
                for i in range(count)]

    def _write_fragments(self, data, blocks_needed, fragments):
        """Write a file as several runs, described by one extent block.

        The runs are allocated one after another, which is not how a fragmented
        file comes about in practice - growth does that - but it is the only way
        to put one in an image without a writer, and it exercises exactly what
        the reader has to do: map a block number through a list that is not
        monotonic in anything but its own order.
        """
        count = max(1, min(fragments, blocks_needed))
        per = (blocks_needed + count - 1) // count

        runs = []
        remaining = blocks_needed
        offset = 0
        while remaining:
            take = min(per, remaining)
            start = self._alloc_blocks(take)
            self._write_data(start, data[offset:offset + take * BLOCK_SIZE], take)
            runs.append((start, take))
            offset += take * BLOCK_SIZE
            remaining -= take
        if len(runs) > HBFS4_EXTENT_MAX:
            raise ValueError("%d extents is more than a block holds" % len(runs))

        block = self._alloc_blocks(1)
        buf = bytearray(BLOCK_SIZE)
        struct.pack_into('<I', buf, 0, HBFS4_EXTENT_MAGIC)
        struct.pack_into('<I', buf, 4, len(runs))
        for i, (start, take) in enumerate(runs):
            struct.pack_into('<II', buf,
                             HBFS4_EXTENT_HDR + i * HBFS4_EXTENT_RECORD,
                             start, take)
        stamp_run_checksum(buf)
        lba = HBFS_DATA_START + block * SECTORS_PER_BLOCK
        self.img.seek(lba_to_offset(lba))
        self.img.write(buf)
        return block, sum(take for _start, take in runs), HBFS4_DE_EXTENTS

    def _write_data(self, block_num, data, blocks_needed):
        """Write data to disk at the given block."""
        data_lba = HBFS_DATA_START + block_num * SECTORS_PER_BLOCK
        pad_sz = BLOCK_SIZE * blocks_needed - len(data)
        padded = data + b'\x00' * pad_sz
        self.img.seek(lba_to_offset(data_lba))
        self.img.write(padded)
        return data_lba

    def create_subdir(self, dirname):
        """Create a subdirectory in the root directory if missing."""
        existing_slot = self.find_entry_slot(self.root_dir, HBFS_MAX_FILES,
                                              dirname)
        if existing_slot is not None:
            off = existing_slot * HBFS_DIR_ENTRY_SIZE
            if self.root_dir[off + 253] == FTYPE_DIR:
                block_num = struct.unpack_from('<I', self.root_dir,
                                               off + 260)[0]
                block_count = struct.unpack_from('<I', self.root_dir,
                                                 off + 264)[0]
                data_lba = HBFS_DATA_START + block_num * SECTORS_PER_BLOCK
                self.img.seek(lba_to_offset(data_lba))
                raw = self.img.read(block_count * BLOCK_SIZE)
                data = bytearray(raw[:HBFS_SUBDIR_SIZE].ljust(
                    HBFS_SUBDIR_SIZE, b'\x00'))
                self._migrate_modes(data, HBFS_SUBDIR_MAX_FILES)
                self.subdirs[dirname] = {
                    'start_block': block_num,
                    'data': data,
                    'entry_count': self._count_entries(
                        data, HBFS_SUBDIR_MAX_FILES),
                    'data_lba': data_lba,
                }
                print(f"  [DIR] /{dirname:18s}         preserved")
                return block_num
            else:
                # Stale non-directory entry with the same name — clear it so
                # the directory entry can be created cleanly below.
                self.root_dir[off:off + HBFS_DIR_ENTRY_SIZE] = \
                    b'\x00' * HBFS_DIR_ENTRY_SIZE
                print(f"  [CLEAN] cleared stale root entry: {dirname}")

        block_num = self._alloc_blocks(HBFS_SUBDIR_BLOCKS)
        data_lba = HBFS_DATA_START + block_num * SECTORS_PER_BLOCK

        zeroed = b'\x00' * HBFS_SUBDIR_SIZE
        self.img.seek(lba_to_offset(data_lba))
        self.img.write(zeroed)

        root_slot = self._find_free_slot(self.root_dir, HBFS_MAX_FILES)
        if root_slot is None:
            raise RuntimeError(
                f"Root directory full, cannot create /{dirname}"
            )

        entry = create_dir_entry(
            filename=dirname,
            ftype=FTYPE_DIR,
            size=0,
            start_block=block_num,
            block_count=HBFS_SUBDIR_BLOCKS,
            timestamp=900
        )
        off = root_slot * HBFS_DIR_ENTRY_SIZE
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

    def add_file(self, filename, data, directory=None, ftype=None,
                 fragments=None):
        """Add or update a file in root or a subdirectory."""
        if ftype is None:
            if filename.endswith('.txt') or filename.endswith('.c'):
                ftype = FTYPE_TEXT
            elif filename.endswith('.bat'):
                ftype = FTYPE_BATCH
            else:
                ftype = FTYPE_EXEC

        blocks_needed = max(1, (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE)
        flags = 0
        if fragments and fragments > 1 and self.version != 4:
            raise ValueError("extents are a v4 entry flag; a v3 volume has none")
        if fragments and fragments > 1:
            block_num, entry_blocks, flags = self._write_fragments(
                data, blocks_needed, fragments)
            data_lba = HBFS_DATA_START + block_num * SECTORS_PER_BLOCK
        else:
            block_num = self._alloc_blocks(blocks_needed)
            data_lba = self._write_data(block_num, data, blocks_needed)
            entry_blocks = blocks_needed

        entry = create_dir_entry(
            filename=filename,
            ftype=ftype,
            size=len(data),
            start_block=block_num,
            block_count=entry_blocks,
            timestamp=1000 + self.total_files * 100,
            flags=flags
        )

        if directory and directory in self.subdirs:
            sd = self.subdirs[directory]
            slot = self.find_entry_slot(sd['data'], HBFS_SUBDIR_MAX_FILES,
                                         filename)
            if slot is None:
                slot = self._find_free_slot(sd['data'], HBFS_SUBDIR_MAX_FILES)
            if slot is None:
                print(f"  WARNING: /{directory} full, skipping {filename}")
                return
            off = slot * HBFS_DIR_ENTRY_SIZE
            sd['data'][off:off + HBFS_DIR_ENTRY_SIZE] = entry
            sd['entry_count'] = max(sd['entry_count'], slot + 1)
            path_display = f"/{directory}/{filename}"
        else:
            slot = self.find_entry_slot(self.root_dir, HBFS_MAX_FILES,
                                         filename)
            if slot is None:
                slot = self._find_free_slot(self.root_dir, HBFS_MAX_FILES)
            if slot is None:
                print(f"  WARNING: Root full, skipping {filename}")
                return
            off = slot * HBFS_DIR_ENTRY_SIZE
            self.root_dir[off:off + HBFS_DIR_ENTRY_SIZE] = entry
            path_display = f"/{filename}"

        self.total_files += 1
        print(
            f"  [{self.total_files:2d}] {path_display:28s}"
            f" {len(data):6d} bytes"
            f"  -> block {block_num} (LBA {data_lba})")

    def cleanup_stale_root_entries(self):
        """Remove root-level file entries that have been migrated to a subdir.

        When populate.py previously stored programs directly in root and the
        layout has since changed to use subdirectories, the old root entries
        persist across incremental runs.  This pass removes them so programs
        appear only in their canonical subdirectory.
        """
        removed = 0
        for i in range(HBFS_MAX_FILES):
            off = i * HBFS_DIR_ENTRY_SIZE
            if not self._entry_used(self.root_dir, off):
                continue
            if self.root_dir[off + 253] == FTYPE_DIR:
                continue  # keep directory entries
            name = self._entry_name(self.root_dir, off)
            for sd in self.subdirs.values():
                if self.find_entry_slot(
                        sd['data'], HBFS_SUBDIR_MAX_FILES, name) is not None:
                    self.root_dir[off:off + HBFS_DIR_ENTRY_SIZE] = \
                        b'\x00' * HBFS_DIR_ENTRY_SIZE
                    print(f"  [CLEAN] removed stale root entry: {name}")
                    removed += 1
                    break
        return removed

    def cleanup_stale_subdir_entries(self, dirname, managed_names,
                                     expected_names):
        """Remove stale managed file entries from a preserved subdirectory.

        This prunes files previously installed by populate.py when they are
        no longer present in the source tree or no longer belong in this
        directory. Guest-created files are preserved because only names from
        the managed cache are eligible for removal.
        """
        sd = self.subdirs.get(dirname)
        if sd is None:
            return 0

        removed = 0
        for i in range(HBFS_SUBDIR_MAX_FILES):
            off = i * HBFS_DIR_ENTRY_SIZE
            if not self._entry_used(sd['data'], off):
                continue
            name = self._entry_name(sd['data'], off)
            if name in managed_names and name not in expected_names:
                sd['data'][off:off + HBFS_DIR_ENTRY_SIZE] = \
                    b'\x00' * HBFS_DIR_ENTRY_SIZE
                print(f"  [CLEAN] removed stale /{dirname} entry: {name}")
                removed += 1

        sd['entry_count'] = self._count_entries(sd['data'],
                                                HBFS_SUBDIR_MAX_FILES)
        return removed

    def finalize(self):
        """Write all directory structures and bitmap to disk."""
        if self.version == 4:
            for sd in self.subdirs.values():
                stamp_run_checksum(sd['data'])

        for _dirname, sd in self.subdirs.items():
            self.img.seek(lba_to_offset(sd['data_lba']))
            self.img.write(sd['data'])

        used = self._rebuild_bitmap()
        free = TOTAL_BLOCKS - used
        # Keep the on-disk geometry in sync with the constants. This also
        # migrates an image created when TOTAL_BLOCKS overran the image size.
        struct.pack_into('<I', self.sb, 8, TOTAL_BLOCKS)
        struct.pack_into('<I', self.sb, 12, free)
        if self.version == 4:
            # Both of these cover bytes that are only final now: the superblock
            # its free count, the bitmap the allocation the rebuild just did. A
            # checksum stamped before the last write is a checksum of the wrong
            # bytes.
            struct.pack_into('<I', self.sb, HBFS4_OFF_CHECKSUM, 0)
            struct.pack_into('<I', self.sb, HBFS4_OFF_CHECKSUM,
                             superblock_checksum(self.sb))
            stamp_run_checksum(self.bitmap)
            stamp_run_checksum(self.root_dir)

        self.img.seek(lba_to_offset(HBFS_SUPERBLOCK_LBA))
        self.img.write(self.sb)

        self.img.seek(lba_to_offset(HBFS_BITMAP_START))
        self.img.write(self.bitmap)

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
Mellivora OS v8.5.0 - Expanded Graphics, Audio & I/O
==========================================

Welcome to Mellivora OS, a 32-bit protected mode operating system
built from scratch in x86 assembly language.

Features:
  * 32-bit protected mode with flat memory model
  * Bitmap page allocator (malloc/free/realloc)
  * ATA PIO disk driver with LBA48 (up to 128 PB)
  * HBFS filesystem with 252-character filenames
  * VGA 80x25 text mode with 16 colors and scrolling
  * PS/2 keyboard with scancode translation
  * PIT timer at 100 Hz with preemptive multitasking
  * 80 syscalls via INT 0x80
  * Priority-based scheduler (4 levels, 64 tasks)
  * POSIX-style signals (SIGINT, SIGKILL, SIGTERM, etc.)
  * Process groups and job control
  * Interactive shell with 50+ commands
  * Enhanced line editing (Ctrl+A/E/U/W/L)
  * Tab completion, 128-entry history, Ctrl+C abort
  * Program execution (flat 32-bit & ELF binaries)
  * Ring 0/Ring 3 privilege separation with TSS
  * Full TCP/IP networking stack (RTL8139)
  * Burrows desktop GUI (640x480x32)
  * TCC C compiler (compile & run C programs)
  * Batch file / script execution
  * Full-screen text editor (edit)
  * Calendar, calculator, and games

Directory Structure:
  /bin      - Utility programs (edit, calc, sort, grep, etc.)
  /games    - Games (snake, tetris, 2048, life, etc.)
  /Burrows  - Burrows GUI desktop applications
  /samples  - C source code samples for the TCC compiler
  /docs     - Documentation and text files

Type 'help' for a list of commands.
Type 'set PATH /bin:/games:/Burrows' to configure program search path.

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
  0x00100000 - 0x002FFFFF  Kernel (loaded at 1MB, ~1.3MB)
  0x00300000 - 0x003FFFFF  Program load area (1MB)
  0x00410000 - 0x00490FFF  Page directory + page tables
  0x00500000 - 0x0051FFFF  Physical memory bitmap (128KB)
  0x00520000 - 0x00520FFF  PMM buddy tags (unused)
  0x00700000 - 0x007FFFFF  Heap window (unused)
  0x00800000 - onwards     Free memory (managed by PMM), KERNEL_RESERVED_END

Disk Layout:
  LBA 0:         Boot sector (512 bytes)
  LBA 1-32:      Stage 2 loader (16KB)
  LBA 33+:       Kernel (KERNEL_SECTORS from kernel_sectors.inc)
  LBA 4096:      HBFS Superblock
  LBA 4097-4224: Block allocation bitmap (128 sectors = 16 blocks)
  LBA 4225-4480: Root directory (256 sectors = 32 blocks, 455 entries)
  LBA 4481+:     Data blocks (4KB each)

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
  34 stdin_read  - Read piped stdin (EBX=buf) -> EAX=bytes or -1
  35 yield       - Cooperative task yield (for future multitasking)

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
[x] 32-bit protected mode switch with flat GDT
[x] VGA text mode driver (80x25, 16 colors)
[x] PIC initialization and IRQ remapping
[x] IDT with ISR/IRQ handlers
[x] PIT timer at 100 Hz
[x] PS/2 keyboard driver with scancode table
[x] Physical memory manager (bitmap, 4KB pages)
[x] ATA PIO driver with LBA48
[x] HBFS filesystem (create, read, delete)
[x] Command shell with 34+ commands
[x] INT 0x80 syscall interface (36 syscalls)
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
[ ] Virtual memory / paging
[ ] Preemptive multitasking
[ ] PCI bus enumeration
[ ] Network stack
[ ] Mouse driver
[ ] GUI / framebuffer mode
[ ] Pipes and I/O redirection
[ ] Wildcard expansion (*.txt)
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

# Burrows GUI applications — go into /Burrows subdirectory
# Programs installed at the root of the volume rather than in /bin.
# /bin is at the 227-entry limit of an HBFS subdirectory (roadmap M3
# grows it); root holds 455 entries and is searched before PATH.
ROOT_PROGRAMS = {'perm_test', 'hostile_test', 'handle_test'}

BURROWS_PROGRAMS = {
    'bcalc', 'bedit', 'bforager', 'bhive', 'bnotes', 'bpaint',
    'bplayer', 'bsettings', 'bsheet', 'bsysmon', 'bterm', 'bview',
}

# Classify programs into categories for subdirectories
GAME_PROGRAMS = {
    'adventure', 'blackjack', 'breakout', 'connect4', 'doomfire',
    'galaga', 'guess', 'hangman', 'kingdom', 'life',
    'lunar', 'mastermind', 'matrix', 'maze', 'mine',
    'neurovault', 'outbreak', 'pacman', 'iago', 'rain', 'raycaster', 'rogue',
    'simon', 'snake', 'solitaire', 'starfield', 'tetris', 'tictactoe',
    'robotown',
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

    # Load mtime cache + tombstone set. Tombstones are program names the user
    # `del`'d inside the OS; we must NOT re-add them on `make full` unless the
    # host source file's mtime changes (which is treated as an explicit
    # "please re-add this" signal from the developer).
    cache, tombstones = _load_cache()
    new_cache = {}
    skipped = 0
    tombstoned_now = 0
    revived = 0

    # Create subdirectories
    fs.create_subdir("bin")
    fs.create_subdir("games")
    fs.create_subdir("Burrows")
    fs.create_subdir("samples")
    fs.create_subdir("docs")

    managed_names = {
        fname[:-4] for fname in cache
        if fname.endswith('.bin')
    }
    for prog_name in sorted(ROOT_PROGRAMS):
        root_bin = os.path.join(programs_dir, prog_name + '.bin')
        if os.path.isfile(root_bin):
            with open(root_bin, 'rb') as f:
                fs.add_file(prog_name, f.read())

    expected_programs = {
        'bin': set(),
        'games': set(),
        'Burrows': set(),
    }

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
                mtime = os.path.getmtime(fpath)
                src_path = os.path.join(programs_dir, fname[:-4] + '.asm')
                if not os.path.exists(src_path):
                    print(f"  [SKIP] orphaned binary without source: {fname}")
                    continue
                new_cache[fname] = mtime

                prog_name = fname[:-4]
                if prog_name in ROOT_PROGRAMS:
                    continue            # installed at the root above
                # Determine target directory (used for skip check below)
                if prog_name in BURROWS_PROGRAMS:
                    target_dir = "Burrows"
                elif prog_name in GAME_PROGRAMS:
                    target_dir = "games"
                else:
                    target_dir = "bin"

                expected_programs[target_dir].add(prog_name)

                # Tombstone handling: if the user `del`'d this program inside
                # the OS, honor that across `make full` runs UNLESS the host
                # source has been touched since we recorded it (mtime change
                # = explicit "re-add" signal from the developer).
                if fname in tombstones:
                    if fs.preserve_mode and cache.get(fname) == mtime:
                        # User deleted, source unchanged → keep deleted.
                        del new_cache[fname]
                        skipped += 1
                        continue
                    # Source changed → revive it.
                    tombstones.discard(fname)
                    revived += 1

                # Incremental skip: if the image already has a valid HBFS,
                # the file hasn't changed since the last populate, and the
                # entry is already present in the correct subdirectory, we
                # can skip the (expensive) block-allocation + write.
                if fs.preserve_mode and cache.get(fname) == mtime:
                    sd = fs.subdirs.get(target_dir)
                    if sd is not None:
                        slot = fs.find_entry_slot(
                            sd['data'], HBFS_SUBDIR_MAX_FILES, prog_name
                        )
                        if slot is not None:
                            skipped += 1
                            continue
                    # Image lost the entry (user `del`'d it). If we still hold
                    # a cache hit, that means this is the FIRST `make full`
                    # since the deletion — record a tombstone and skip.
                    if fs.preserve_mode:
                        tombstones.add(fname)
                        del new_cache[fname]
                        tombstoned_now += 1
                        continue

                with open(fpath, 'rb') as f:
                    data = f.read()
                if prog_name in BURROWS_PROGRAMS:
                    fs.add_file(prog_name, data, directory="Burrows")
                elif prog_name in GAME_PROGRAMS:
                    fs.add_file(prog_name, data, directory="games")
                else:
                    fs.add_file(prog_name, data, directory="bin")

    if fs.preserve_mode:
        fs.cleanup_stale_subdir_entries(
            "bin", managed_names, expected_programs["bin"])
        fs.cleanup_stale_subdir_entries(
            "games", managed_names, expected_programs["games"])
        fs.cleanup_stale_subdir_entries(
            "Burrows", managed_names, expected_programs["Burrows"])

    # Sample sources: everything else in samples/ is a text file, except
    # .bat scripts, which the shell types as batch so they are easy to find.
    samples_dir = "samples"
    sample_types = {
        '.c': FTYPE_TEXT, '.bas': FTYPE_TEXT, '.pl': FTYPE_TEXT,
        '.asm': FTYPE_TEXT, '.bat': FTYPE_BATCH,
    }
    if os.path.isdir(samples_dir):
        for fname in sorted(os.listdir(samples_dir)):
            ext = os.path.splitext(fname)[1].lower()
            if ext not in sample_types:
                continue
            fpath = os.path.join(samples_dir, fname)
            with open(fpath, 'r', encoding='ascii') as f:
                data = f.read()
            fs.add_file(fname, data.encode('ascii'),
                        directory="samples", ftype=sample_types[ext])

    # Remove any root-level entries that have since moved to subdirectories.
    fs.cleanup_stale_root_entries()

    fs.finalize()

    # Drop tombstones whose source binary no longer exists on the host so the
    # cache doesn't grow unboundedly.
    if os.path.isdir(programs_dir):
        existing_bins = {
            n for n in os.listdir(programs_dir) if n.endswith('.bin')
        }
        tombstones = {t for t in tombstones if t in existing_bins}

    # Persist mtime cache for next run
    _save_cache(new_cache, tombstones)
    if skipped:
        print(f"  ({skipped} unchanged binaries skipped — incremental mode)")
    if tombstoned_now:
        print(f"  ({tombstoned_now} user-deleted binaries tombstoned — "
              f"will not be re-added; rebuild the .asm to revive)")
    if revived:
        print(f"  ({revived} tombstoned binaries revived from source change)")


if __name__ == '__main__':
    main()
