#!/usr/bin/env python3
"""fsck_hbfs.py - validate an HBFS disk image without mounting it.

Checks performed:
  * superblock magic, version, block size and geometry
  * total_blocks against the real image size
  * bitmap vs directory agreement (referenced blocks are marked, and
    allocated blocks are referenced)
  * superblock free-block count
  * every root and subdirectory entry: name, type, size/block count, bounds
  * recursive directory walk with cycle detection and depth limit
  * duplicate block ownership across all files and directories
  * xattr block pointers and xattr entry counts
  * flags data placed in the reserved journal blocks

Exit status: 0 when no errors were found (warnings are allowed), 1 otherwise.
--strict turns warnings into failure.

Usage: tools/fsck_hbfs.py [image] [--strict] [--json] [--quiet]
"""

from __future__ import annotations

import json
import os
import struct
import sys

SECTOR_SIZE = 512
BLOCK_SIZE = 4096
SECTORS_PER_BLOCK = BLOCK_SIZE // SECTOR_SIZE

HBFS_MAGIC = 0x48424653
HBFS_SUPERBLOCK_LBA = 4096
HBFS_BITMAP_BLOCKS = 16
HBFS_BITMAP_START = HBFS_SUPERBLOCK_LBA + 1
HBFS_BITMAP_SIZE = HBFS_BITMAP_BLOCKS * BLOCK_SIZE
HBFS_ROOT_DIR_BLOCKS = 32
HBFS_ROOT_DIR_START = HBFS_BITMAP_START + HBFS_BITMAP_BLOCKS * SECTORS_PER_BLOCK
HBFS_DATA_START = HBFS_ROOT_DIR_START + HBFS_ROOT_DIR_BLOCKS * SECTORS_PER_BLOCK

HBFS_DIR_ENTRY_SIZE = 288
HBFS_MAX_FILENAME = 252
HBFS_JOURNAL_BLOCKS = 4
HBFS_JOURNAL_LBA = HBFS_DATA_START
HBFS_JOURNAL_SECTS = HBFS_JOURNAL_BLOCKS * SECTORS_PER_BLOCK
HBFS_JRNL_MAGIC = 0x4A524E4C
HBFS_JRNL_T_PRE = 1
HBFS_JRNL_T_COMMIT = 2
HBFS_MAX_DEPTH = 16

FTYPE_FREE = 0
FTYPE_TEXT = 1
FTYPE_DIR = 2
FTYPE_EXEC = 3
FTYPE_BATCH = 4
FTYPE_NAMES = {
    FTYPE_TEXT: "text",
    FTYPE_DIR: "dir",
    FTYPE_EXEC: "exec",
    FTYPE_BATCH: "batch",
}

# Directory entry offsets (must match kernel.asm).
DE_TYPE = 253
DE_SIZE = 256
DE_BLOCK = 260
DE_BLKCNT = 264
DE_MODE = 276
DE_UID = 278
DE_GID = 280
DE_XATTR_COUNT = 282
DE_XATTR_LBA = 284


def crc32c(data: bytes) -> int:
    """CRC-32C (Castagnoli), the checksum used by the HBFS journal."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


class Fsck:
    def __init__(self, image: str):
        self.image = image
        self.errors: list[str] = []
        self.warnings: list[str] = []
        self.stats = {
            "entries": 0,
            "files": 0,
            "dirs": 0,
            "blocks_claimed": 0,
            "blocks_allocated": 0,
            "legacy_mode_entries": 0,
            "journal_overlaps": 0,
            "journal_records": 0,
        }
        self.total_blocks = 0
        self.data_lba = HBFS_DATA_START
        self.bitmap = b""
        self.claims: dict[int, str] = {}
        self.visited_dirs: set[int] = set()
        self.file_size = os.path.getsize(image)

    # -- reporting ------------------------------------------------------
    def error(self, msg: str) -> None:
        self.errors.append(msg)

    def warn(self, msg: str) -> None:
        self.warnings.append(msg)

    # -- low-level reads ------------------------------------------------
    def read(self, offset: int, size: int) -> bytes:
        with open(self.image, "rb") as f:
            f.seek(offset)
            return f.read(size)

    def read_lba(self, lba: int, size: int) -> bytes:
        return self.read(lba * SECTOR_SIZE, size)

    def block_lba(self, block: int) -> int:
        return self.data_lba + block * SECTORS_PER_BLOCK

    def bitmap_bit(self, block: int) -> int:
        if block < 0 or block // 8 >= len(self.bitmap):
            return 0
        return (self.bitmap[block // 8] >> (block % 8)) & 1

    # -- superblock -----------------------------------------------------
    def check_superblock(self) -> int:
        sb = self.read_lba(HBFS_SUPERBLOCK_LBA, SECTOR_SIZE)
        if len(sb) < 56:
            self.error("cannot read the HBFS superblock at LBA 4096")
            return HBFS_ROOT_DIR_START

        magic, version, total, free = struct.unpack_from("<IIII", sb, 0)
        root_lba, bitmap_lba, data_lba, block_size = struct.unpack_from(
            "<IIII", sb, 16)
        jrnl_state, jrnl_ptr = struct.unpack_from("<II", sb, 40)
        jrnl_epoch = struct.unpack_from("<I", sb, 56)[0]
        features = struct.unpack_from("<I", sb, 48)[0]
        root_uid, root_gid = struct.unpack_from("<HH", sb, 52)

        if magic != HBFS_MAGIC:
            self.error(f"bad superblock magic 0x{magic:08X} (expected 'HBFS')")
            return root_lba
        if version not in (1, 2, 3):
            self.warn(f"unexpected HBFS version {version} (expected 1..3)")
        if block_size != BLOCK_SIZE:
            self.error(f"block size is {block_size}, expected {BLOCK_SIZE}")
        if bitmap_lba != HBFS_BITMAP_START:
            self.warn(f"bitmap LBA is {bitmap_lba}, expected {HBFS_BITMAP_START}")
        if root_lba != HBFS_ROOT_DIR_START:
            self.warn(f"root directory LBA is {root_lba}, "
                      f"expected {HBFS_ROOT_DIR_START}")
        if data_lba < HBFS_DATA_START:
            self.error(f"data area LBA {data_lba} overlaps the bitmap/root "
                       f"directory (minimum {HBFS_DATA_START})")

        # A block is addressable only if it fits inside the image.
        capacity = max(0, (self.file_size - data_lba * SECTOR_SIZE) // BLOCK_SIZE)
        if total > capacity:
            self.warn(f"superblock declares {total} blocks but the image holds "
                      f"{capacity} data blocks ({total - capacity} past the end)")
        if total == 0:
            self.error("superblock declares zero total blocks")

        if version >= 2 and jrnl_state != 0:
            self.warn(f"journal is dirty (state {jrnl_state}, pointer {jrnl_ptr}); "
                      "mount will replay it")
        if version >= 3 and features & 1:
            pass  # permissions feature flag
        if root_uid != 0 or root_gid != 0:
            self.warn(f"root directory uid/gid is {root_uid}/{root_gid}")

        self.total_blocks = total
        self.data_lba = data_lba
        self.root_lba = root_lba
        self.sb_free = free
        self.version = version
        self.jrnl_state = jrnl_state
        self.jrnl_epoch = jrnl_epoch
        return root_lba

    # -- write-ahead journal -------------------------------------------
    def check_journal(self) -> None:
        """Validate the undo journal.

        A pre-image record is a header sector followed by a copy of the sector
        it protects. Replay stops at the first commit marker, so records past
        it are stale leftovers and are ignored.
        """
        if getattr(self, "version", 1) < 2:
            return
        index = 0
        records = 0
        committed = False
        low = HBFS_SUPERBLOCK_LBA
        high = self.data_lba + self.total_blocks * SECTORS_PER_BLOCK
        while index < HBFS_JOURNAL_SECTS:
            hdr = self.read_lba(HBFS_JOURNAL_LBA + index, SECTOR_SIZE)
            if len(hdr) < 16:
                break
            magic, rtype, target, epoch = struct.unpack_from("<IIII", hdr, 0)
            crc = struct.unpack_from("<I", hdr, 16)[0]
            if magic != HBFS_JRNL_MAGIC:
                break
            if epoch != getattr(self, "jrnl_epoch", 0):
                # Records from an older transaction; replay stops here too.
                break
            if rtype == HBFS_JRNL_T_COMMIT:
                committed = True
                break
            if rtype != HBFS_JRNL_T_PRE:
                self.warn(f"journal record at sector {index} has unknown "
                          f"type {rtype}")
                index += 1
                continue
            records += 1
            if not (low <= target < high):
                self.error(f"journal record at sector {index} targets LBA "
                           f"{target}, outside the filesystem")
            payload = self.read_lba(HBFS_JOURNAL_LBA + index + 1, SECTOR_SIZE)
            # A zero checksum means the record was written before checksums
            # existed (v2/v3 images); accept it rather than crying wolf.
            if (crc and len(payload) == SECTOR_SIZE
                    and crc32c(payload) != crc):
                self.error(f"journal record at sector {index} fails its "
                           "CRC32C check (torn write); replay would stop here")
            index += 2
        self.stats["journal_records"] = records
        if self.jrnl_state and not records and not committed:
            self.warn("superblock says the journal is dirty but the log is empty")
        if self.jrnl_state and committed:
            self.warn("journal is marked dirty but already holds a commit "
                      "marker (crash between commit and superblock update); "
                      "replay is a no-op")

    # -- bitmap ---------------------------------------------------------
    def load_bitmap(self) -> None:
        self.bitmap = self.read_lba(HBFS_BITMAP_START, HBFS_BITMAP_SIZE)
        if len(self.bitmap) != HBFS_BITMAP_SIZE:
            self.error("short read of the allocation bitmap")
            return
        self.stats["blocks_allocated"] = sum(
            bin(byte).count("1") for byte in self.bitmap)

    # -- ownership ------------------------------------------------------
    def claim(self, start: int, count: int, owner: str) -> None:
        if count < 0 or start < 0:
            self.error(f"{owner}: negative block range {start}+{count}")
            return
        if start + count > self.total_blocks:
            self.error(f"{owner}: blocks {start}..{start + count - 1} "
                       f"past total_blocks {self.total_blocks}")
            return
        for b in range(start, start + count):
            if b < HBFS_JOURNAL_BLOCKS:
                self.stats["journal_overlaps"] += 1
            prev = self.claims.get(b)
            if prev is not None and prev != owner:
                self.error(f"block {b} owned by both {prev!r} and {owner!r}")
                continue
            self.claims[b] = owner

    # -- directory walk -------------------------------------------------
    def walk(self, lba: int, blocks: int, prefix: str, depth: int) -> None:
        if depth > HBFS_MAX_DEPTH:
            self.error(f"{prefix or '/'}: directory nesting exceeds "
                       f"{HBFS_MAX_DEPTH} levels")
            return
        if blocks <= 0:
            self.error(f"{prefix or '/'}: directory has {blocks} blocks")
            return
        data = self.read_lba(lba, blocks * BLOCK_SIZE)
        if len(data) != blocks * BLOCK_SIZE:
            self.error(f"{prefix or '/'}: short read of directory")
            return

        max_entries = len(data) // HBFS_DIR_ENTRY_SIZE
        for i in range(max_entries):
            off = i * HBFS_DIR_ENTRY_SIZE
            entry = data[off:off + HBFS_DIR_ENTRY_SIZE]
            ftype = entry[DE_TYPE]
            if ftype == FTYPE_FREE:
                continue

            raw_name = entry[0:HBFS_MAX_FILENAME + 1]
            nul = raw_name.find(b"\x00")
            if nul < 0:
                self.error(f"{prefix or '/'}: entry {i} has no NUL-terminated name")
                name = raw_name.decode("ascii", "replace")
            else:
                name = raw_name[:nul].decode("ascii", "replace")
            if not name:
                self.error(f"{prefix or '/'}: entry {i} has an empty name")
                continue
            if any(ord(c) < 32 or ord(c) > 126 for c in name):
                self.warn(f"{prefix}{name!r}: non-printable characters in name")
            if "/" in name:
                self.warn(f"{prefix}{name!r}: '/' in a directory entry name")

            display = f"{prefix}{name}"
            self.stats["entries"] += 1

            if ftype not in FTYPE_NAMES:
                self.error(f"{display}: invalid entry type {ftype}")
                continue

            size, start, count = struct.unpack_from("<III", entry, DE_SIZE)
            mode = struct.unpack_from("<H", entry, DE_MODE)[0]
            if mode == 0:
                self.stats["legacy_mode_entries"] += 1

            if ftype == FTYPE_DIR:
                self.stats["dirs"] += 1
                if start in self.visited_dirs:
                    self.error(f"{display}: directory cycle (block {start} "
                               "already visited)")
                    continue
                self.visited_dirs.add(start)
                self.claim(start, count, display + "/")
                self.walk(self.block_lba(start), count, display + "/", depth + 1)
            else:
                self.stats["files"] += 1
                if count == 0 and size > 0:
                    self.error(f"{display}: {size} bytes in zero blocks")
                needed = (size + BLOCK_SIZE - 1) // BLOCK_SIZE
                if count < needed:
                    self.error(f"{display}: {size} bytes need {needed} blocks, "
                               f"entry claims {count}")
                if count > 0:
                    self.claim(start, count, display)
                elif size == 0:
                    pass  # empty file

            # Extended attributes live in their own block.
            xcount = struct.unpack_from("<H", entry, DE_XATTR_COUNT)[0]
            xlba = struct.unpack_from("<I", entry, DE_XATTR_LBA)[0]
            if xcount or xlba:
                if xcount > 32:
                    self.error(f"{display}: xattr count {xcount} > 32")
                if xlba < self.data_lba or xlba % SECTORS_PER_BLOCK:
                    self.error(f"{display}: xattr LBA {xlba} is not a data block")
                else:
                    block = (xlba - self.data_lba) // SECTORS_PER_BLOCK
                    self.claim(block, 1, display + " [xattr]")

    # -- cross checks ---------------------------------------------------
    def cross_check(self) -> None:
        # Every referenced block must be marked in the bitmap.
        for block, owner in sorted(self.claims.items()):
            if not self.bitmap_bit(block):
                self.error(f"block {block} ({owner}) is referenced but not "
                           "marked allocated")
        self.stats["blocks_claimed"] = len(self.claims)

        # Every allocated bit should belong to something. The first
        # HBFS_JOURNAL_BLOCKS blocks are reserved for the write-ahead journal
        # and are intentionally allocated-but-unreferenced.
        for block in range(self.total_blocks):
            if block < HBFS_JOURNAL_BLOCKS:
                continue
            if self.bitmap_bit(block) and block not in self.claims:
                self.warn(f"block {block} is marked allocated but unreferenced")

        allocated = self.stats["blocks_allocated"]
        expected_free = self.total_blocks - allocated
        if expected_free != self.sb_free:
            self.warn(f"superblock free count is {self.sb_free}, "
                      f"bitmap implies {expected_free}")

        if self.stats["journal_overlaps"]:
            self.warn(f"{self.stats['journal_overlaps']} block(s) of live file "
                      "data occupy the reserved journal area (blocks 0..3)")
        if self.stats["legacy_mode_entries"]:
            self.warn(f"{self.stats['legacy_mode_entries']} entries have mode 0 "
                      "(legacy entries written without permission bits)")

    # -- driver ---------------------------------------------------------
    def run(self) -> int:
        root = self.check_superblock()
        if self.errors:
            return 1
        self.check_journal()
        self.load_bitmap()
        if not self.bitmap:
            # Without a bitmap every ownership check would report the whole
            # filesystem as unallocated; stop here.
            return 1
        self.walk(root, HBFS_ROOT_DIR_BLOCKS, "/", 1)
        self.cross_check()
        return 0 if not self.errors else 1


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    image = args[0] if args else "mellivora.img"
    quiet = "--quiet" in flags
    strict = "--strict" in flags
    as_json = "--json" in flags

    if not os.path.exists(image):
        print(f"fsck_hbfs: no such image: {image}", file=sys.stderr)
        return 1

    fs = Fsck(image)
    status = fs.run()
    if strict and fs.warnings:
        status = 1

    if as_json:
        print(json.dumps({
            "image": image,
            "ok": status == 0 and not fs.warnings if strict else status == 0,
            "errors": fs.errors,
            "warnings": fs.warnings,
            "stats": fs.stats,
        }, indent=2))
    else:
        if not quiet or fs.errors:
            for msg in fs.errors:
                print(f"  ERROR  {msg}")
        if not quiet:
            for msg in fs.warnings:
                print(f"  WARN   {msg}")
        print(
            f"  {fs.stats['entries']} entries "
            f"({fs.stats['files']} files, {fs.stats['dirs']} dirs), "
            f"{fs.stats['blocks_claimed']} blocks referenced, "
            f"{fs.stats['blocks_allocated']} allocated"
        )
        if fs.errors:
            print(f"  FAIL   {len(fs.errors)} error(s), "
                  f"{len(fs.warnings)} warning(s)")
        elif fs.warnings:
            print(f"  PASS   {len(fs.warnings)} warning(s)"
                  + (" (strict: FAIL)" if strict else ""))
        else:
            print("  PASS   filesystem is consistent")

    return 0 if status == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
