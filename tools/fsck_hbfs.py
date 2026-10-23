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

--complain audits metadata checksums instead: the superblock, the bitmap and
every directory carry one, and it recomputes each from the bytes on disk and
prints the ones that disagree. A v3 volume carries none, and it says so rather
than reporting a pass it did not earn.

Usage: tools/fsck_hbfs.py [image] [--strict] [--json] [--quiet] [--complain]
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
# HBFS v4 (roadmap P4): a second magic in the same superblock sector and a
# header at 60, past every field v3 uses, so the two versions can be told apart
# and a v3 reader still finds the geometry where it expects it.
HBFS4_MAGIC = 0x34424653
HBFS4_OFF_FEATURES = 60
HBFS4_OFF_GENERATION = 64
HBFS4_OFF_BLOCKS_HI = 68
HBFS4_OFF_DATA_HI = 72
HBFS4_OFF_CHECKSUM = 76
# Each metadata run keeps a CRC-32C in its last four bytes: the bitmap, the root
# directory and every subdirectory. The runs have the room without moving a bit
# or an entry, which is why the checksum is per run rather than per block.
HBFS4_RUN_CHECKSUM_BYTES = 4
# The v4 commit record: how many pre-images the transaction wrote, and a CRC-32C
# over the record with this field zeroed, so a torn commit is not a commit.
HBFS4_JRNL_OFF_RECORDS = 20
HBFS4_JRNL_OFF_CRC = 24
# Extents: an entry whose flags word sets bit 0 keeps its data in several runs,
# named by {block, count} records in a metadata block `start` points at.
HBFS4_FEATURE_EXTENTS = 1 << 0
HBFS4_FEATURE_JOURNAL = 1 << 1
HBFS4_FEATURE_SNAPSHOT = 1 << 2
HBFS4_DE_EXTENTS = 1 << 0
HBFS4_EXTENT_MAGIC = 0x34584248      # on disk: 'H','B','X','4'
HBFS4_EXTENT_HDR = 8                 # magic, record count
HBFS4_EXTENT_RECORD = 8              # start block, block count
HBFS4_EXTENT_MAX = ((BLOCK_SIZE - HBFS4_EXTENT_HDR
                     - HBFS4_RUN_CHECKSUM_BYTES) // HBFS4_EXTENT_RECORD)
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
DE_FLAGS = 254
DE_SIZE = 256
DE_BLOCK = 260
DE_BLKCNT = 264
DE_MODE = 276
DE_UID = 278
DE_GID = 280
DE_XATTR_COUNT = 282
DE_XATTR_LBA = 284


def _crc32c_table() -> list:
    table = []
    for value in range(256):
        crc = value
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
        table.append(crc)
    return table


CRC32C_TABLE = _crc32c_table()


def crc32c(data: bytes) -> int:
    """CRC-32C (Castagnoli), the checksum the journal and every v4 metadata
    trailer use.

    Table-driven because this runs over whole metadata runs - the root directory
    is 128 KB - and a bit-at-a-time loop makes an audit of one volume cost more
    than the rest of the check put together.
    """
    crc = 0xFFFFFFFF
    table = CRC32C_TABLE
    for byte in data:
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


def run_checksum(run: bytes) -> int:
    """CRC-32C over a metadata run with its own trailer read as zero."""
    offset = len(run) - HBFS4_RUN_CHECKSUM_BYTES
    view = bytearray(run)
    struct.pack_into("<I", view, offset, 0)
    return crc32c(bytes(view))


def v4_checksum_report(f):
    """Yield (label, stored, computed) for every checksummed region of a v4 volume.

    The superblock's checksum lives at a fixed offset of its sector; every other
    region keeps one in its last four bytes. The directories are walked rather
    than taken from the superblock's word, because a checksum on a directory
    nothing reaches protects nothing, and an extent block is metadata like any
    other, so it is checked where the entry that names it is found.
    """

    def compare(label, run, field=None):
        offset = (len(run) - HBFS4_RUN_CHECKSUM_BYTES
                  if field is None else field)
        view = bytearray(run)
        stored = struct.unpack_from("<I", view, offset)[0]
        struct.pack_into("<I", view, offset, 0)
        return label, stored, crc32c(bytes(view))

    f.seek(HBFS_SUPERBLOCK_LBA * SECTOR_SIZE)
    sb = f.read(SECTOR_SIZE)
    if len(sb) < SECTOR_SIZE:
        return
    yield compare("superblock", sb, HBFS4_OFF_CHECKSUM)

    f.seek(HBFS_BITMAP_START * SECTOR_SIZE)
    bitmap = f.read(HBFS_BITMAP_SIZE)
    if len(bitmap) == HBFS_BITMAP_SIZE:
        yield compare("bitmap", bitmap)

    def walk(lba, blocks, label, depth, seen):
        if depth > HBFS_MAX_DEPTH or blocks <= 0 or lba in seen:
            return
        seen.add(lba)
        f.seek(lba * SECTOR_SIZE)
        run = f.read(blocks * BLOCK_SIZE)
        if len(run) < blocks * BLOCK_SIZE:
            return
        yield compare(label, run)
        for i in range(len(run) // HBFS_DIR_ENTRY_SIZE):
            off = i * HBFS_DIR_ENTRY_SIZE
            if run[off + DE_TYPE] == FTYPE_FREE or run[off] == 0:
                continue
            end = run.find(b"\x00", off, off + HBFS_MAX_FILENAME + 1)
            if end < 0:
                end = off + HBFS_MAX_FILENAME
            name = run[off:end].decode("ascii", errors="replace")
            ftype = run[off + DE_TYPE]
            start_block, count = struct.unpack_from("<II", run, off + DE_BLOCK)
            flags = struct.unpack_from("<H", run, off + DE_FLAGS)[0]
            here = label.rstrip("/") + "/" + name
            if flags & HBFS4_DE_EXTENTS:
                f.seek((HBFS_DATA_START
                        + start_block * SECTORS_PER_BLOCK) * SECTOR_SIZE)
                block_bytes = f.read(BLOCK_SIZE)
                if len(block_bytes) == BLOCK_SIZE:
                    yield compare(here, block_bytes)
            elif ftype == FTYPE_DIR and count > 0:
                yield from walk(HBFS_DATA_START + start_block * SECTORS_PER_BLOCK,
                                count, here, depth + 1, seen)

    yield from walk(HBFS_ROOT_DIR_START, HBFS_ROOT_DIR_BLOCKS, "/", 1, set())


def complain_checksums(image: str) -> int:
    """Recompute every metadata checksum and print the ones that disagree.

    On a v3 volume there is nothing to check and it says so. On a v4 volume the
    superblock, the bitmap, every directory and every extent block carry one.
    """
    with open(image, "rb") as f:
        f.seek(HBFS_SUPERBLOCK_LBA * SECTOR_SIZE)
        sb = f.read(SECTOR_SIZE)
        if len(sb) < SECTOR_SIZE:
            print(f"  ERROR  cannot read the superblock at LBA {HBFS_SUPERBLOCK_LBA}")
            return 1
        magic = struct.unpack_from("<I", sb, 0)[0]
        if magic == HBFS_MAGIC:
            print("  NOTE   v3 volume: the superblock carries no checksum")
            print("  PASS   0 of 0 checksummed regions checked")
            return 0
        if magic != HBFS4_MAGIC:
            print(f"  ERROR  bad superblock magic 0x{magic:08X}")
            return 1

        labels = []
        bad = []
        for label, stored, computed in v4_checksum_report(f):
            labels.append(label)
            if stored != computed:
                bad.append((label, stored, computed))

    for label, stored, computed in bad:
        print(f"  ERROR  {label}: checksum 0x{stored:08X}, "
              f"contents say 0x{computed:08X}")
    if bad:
        print(f"  FAIL   {len(bad)} of {len(labels)} checksummed regions disagree")
        return 1
    print(f"  PASS   {len(labels)} of {len(labels)} checksummed regions match "
          f"({', '.join(labels)})")
    return 0


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
        self.v4 = False
        self.features = 0
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

        if magic not in (HBFS_MAGIC, HBFS4_MAGIC):
            self.error(f"bad superblock magic 0x{magic:08X} "
                       "(expected 'HBFS' or 'HBF4')")
            return root_lba
        self.v4 = magic == HBFS4_MAGIC
        if self.v4:
            if version != 4:
                self.warn(f"v4 volume says version {version}, expected 4")
            features = struct.unpack_from("<I", sb, HBFS4_OFF_FEATURES)[0]
            known = (HBFS4_FEATURE_EXTENTS | HBFS4_FEATURE_JOURNAL
                     | HBFS4_FEATURE_SNAPSHOT)
            if features & ~known:
                self.warn(f"feature word 0x{features:08X} has unknown bits "
                          f"0x{features & ~known:08X}")
            self.features = features
            stored = struct.unpack_from("<I", sb, HBFS4_OFF_CHECKSUM)[0]
            view = bytearray(sb)
            struct.pack_into("<I", view, HBFS4_OFF_CHECKSUM, 0)
            if stored != crc32c(bytes(view)):
                self.error(f"superblock checksum 0x{stored:08X}, contents say "
                           f"0x{crc32c(bytes(view)):08X}")
            # The high halves exist so a volume can outgrow 2 TiB; this checker
            # reads the low half only, and says so rather than pretending.
            if (struct.unpack_from("<I", sb, HBFS4_OFF_BLOCKS_HI)[0]
                    or struct.unpack_from("<I", sb, HBFS4_OFF_DATA_HI)[0]):
                self.warn("the volume uses 64-bit block numbers; this checker "
                          "validates the low halves only")
        elif version not in (1, 2, 3):
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
                if self.v4:
                    declared = struct.unpack_from(
                        "<I", hdr, HBFS4_JRNL_OFF_RECORDS)[0]
                    commit_crc = struct.unpack_from(
                        "<I", hdr, HBFS4_JRNL_OFF_CRC)[0]
                    if commit_crc:
                        view = bytearray(hdr[:SECTOR_SIZE])
                        struct.pack_into("<I", view, HBFS4_JRNL_OFF_CRC, 0)
                        if crc32c(bytes(view)) != commit_crc:
                            self.error("commit record fails its CRC32C (a torn "
                                       "commit must not count as one)")
                    if declared != records:
                        self.error(f"commit record says {declared} records, the "
                                   f"log holds {records}")
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
        # A v4 bitmap ends in a four-byte checksum trailer, which is past the
        # volume's last block; its bits are not allocations.
        self.stats["blocks_allocated"] = sum(
            1 for block in range(self.total_blocks) if self.bitmap_bit(block))

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
            flags = struct.unpack_from("<H", entry, DE_FLAGS)[0]
            if flags & ~HBFS4_DE_EXTENTS:
                self.warn(f"{display}: unknown entry flags 0x{flags:04X}")
            if flags & HBFS4_DE_EXTENTS and ftype == FTYPE_DIR:
                self.error(f"{display}: a directory cannot be extent-mapped")
                flags = 0
            if flags & HBFS4_DE_EXTENTS and not self.v4:
                self.error(f"{display}: extents are a v4 entry flag")
                flags = 0
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
            elif flags & HBFS4_DE_EXTENTS:
                self.stats["files"] += 1
                self.claim_extents(display, entry, size, start, count)
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

    def claim_extents(self, display: str, entry: bytes, size: int,
                      extent_block: int, count: int) -> None:
        """An entry whose data is runs: the extent block is metadata, the runs
        are data, and the two have to add up to what the entry says."""
        self.claim(extent_block, 1, display + " [extents]")
        block = self.read_lba(self.block_lba(extent_block), BLOCK_SIZE)
        if len(block) != BLOCK_SIZE:
            self.error(f"{display}: short read of its extent block")
            return
        magic, records = struct.unpack_from("<II", block, 0)
        if magic != HBFS4_EXTENT_MAGIC:
            self.error(f"{display}: block {extent_block} is not an extent block")
            return
        if records == 0 or records > HBFS4_EXTENT_MAX:
            self.error(f"{display}: extent block names {records} runs")
            return
        total = 0
        for i in range(records):
            run_start, run_blocks = struct.unpack_from(
                "<II", block, HBFS4_EXTENT_HDR + i * HBFS4_EXTENT_RECORD)
            if run_blocks == 0:
                self.error(f"{display}: run {i} is empty")
                continue
            total += run_blocks
            self.claim(run_start, run_blocks, display)
        if total != count:
            self.error(f"{display}: runs add up to {total} blocks, the entry "
                       f"claims {count}")
        needed = (size + BLOCK_SIZE - 1) // BLOCK_SIZE
        if count < needed:
            self.error(f"{display}: {size} bytes need {needed} blocks, "
                       f"entry claims {count}")

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
        if self.v4:
            self.check_checksums()
        return 0 if not self.errors else 1

    def check_checksums(self) -> None:
        """Every metadata region's checksum, as an error rather than a report."""
        with open(self.image, "rb") as f:
            for label, stored, computed in v4_checksum_report(f):
                if stored != computed:
                    self.error(f"{label}: checksum 0x{stored:08X}, contents "
                               f"say 0x{computed:08X}")


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    image = args[0] if args else "mellivora.img"
    quiet = "--quiet" in flags
    strict = "--strict" in flags
    as_json = "--json" in flags
    complain = "--complain" in flags

    if not os.path.exists(image):
        print(f"fsck_hbfs: no such image: {image}", file=sys.stderr)
        return 1

    if complain:
        return complain_checksums(image)

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
