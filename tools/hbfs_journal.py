#!/usr/bin/env python3
"""hbfs_journal.py - the v4 write-ahead journal, as a reference implementation.

The protocol is the kernel tree's, with one addition the roadmap asks for: the
commit record carries a checksum, so a commit that was torn by a power cut
cannot be mistaken for one that completed.

    sector 0..            journal area, HBFS_JOURNAL_BLOCKS blocks at the first
                          data block (reserved by the builder, never allocated)
    PRE record            magic, type 1, target LBA, epoch, CRC-32C of the
                          pre-image; the next sector is the pre-image itself
    COMMIT record         magic, type 2, 0, epoch, record count, CRC-32C of the
                          commit sector with the checksum field zeroed

An undo journal: before a metadata sector is overwritten, its old contents are
written to the journal. `begin` sets the superblock's journal state to 1 and
bumps the epoch; `commit` writes the commit record and then clears the state.
Recovery at mount reads the log and either

  * finds a valid commit record - the transaction's writes were all applied, so
    it only has to clear the state, or
  * finds none - the pre-images are written back, newest first, and the state is
    cleared. A pre-image that fails its own CRC stops the replay: writing back a
    torn pre-image would trade a half-applied transaction for a corrupt volume.

The superblock carries the journal state, pointer and epoch at the same offsets
in v4 as in v3 (40, 44 and 56); the v4 header starts at 60 for that reason. Any
change to the superblock restamps its CRC-32C, which is what makes the state
transition itself part of the checksummed format rather than a loose field.

This is the reference the kernel port is checked against, and what
tests/test_hbfs_journal.py crashes on purpose.
"""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

import populate  # noqa: E402
from fsck_hbfs import crc32c  # noqa: E402  (one implementation, table-driven)

SECTOR = 512
JRNL_MAGIC = 0x4A524E4C          # 'JRNL', the same value the 32-bit tree uses
JRNL_T_PRE = 1
JRNL_T_COMMIT = 2
JRNL_LBA = populate.HBFS_DATA_START
JRNL_SECTS = populate.HBFS_JOURNAL_BLOCKS * populate.SECTORS_PER_BLOCK

SB_JRNL_STATE = 40               # dword: 0 clean, 1 transaction open
SB_JRNL_PTR = 44                 # dword: next free journal sector
SB_JRNL_EPOCH = 56               # dword: transaction counter
COMMIT_OFF_RECORDS = 20
COMMIT_OFF_CRC = 24
PRE_OFF_CRC = 16


class Crash(Exception):
    """The machine lost power here."""


class ReplayError(Exception):
    """The log cannot be replayed without risking a corrupt volume."""


@dataclass
class Replay:
    dirty: bool = False
    committed: bool = False
    records: int = 0
    undone: int = 0


class Journal:
    """One open transaction at a time, with a crash point for the harness.

    `crash_after` is a count of sector writes: the (crash_after + 1)th write
    raises Crash, which is the only way to make "power failed between these two
    writes" a test rather than a hope.
    """

    def __init__(self, image_path, crash_after=None):
        self.image_path = image_path
        self.crash_after = crash_after
        self.writes = 0
        self.epoch = 0
        self.ptr = 0
        self.logged = {}
        self.open = False
        self.f = open(image_path, "r+b")

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *_exc):
        self.close()

    # -- sectors ------------------------------------------------------
    def read(self, lba, size):
        self.f.seek(lba * SECTOR)
        return self.f.read(size)

    def read_sector(self, lba):
        return self.read(lba, SECTOR)

    def _write_sector(self, lba, data):
        if len(data) != SECTOR:
            raise ValueError("a journal write is one sector, got %d bytes"
                             % len(data))
        if self.crash_after is not None and self.writes >= self.crash_after:
            raise Crash("power lost before write %d (LBA %d)"
                        % (self.writes + 1, lba))
        self.f.seek(lba * SECTOR)
        self.f.write(data)
        self.f.flush()
        self.writes += 1

    # -- superblock ---------------------------------------------------
    def superblock(self):
        """The superblock as bytes, with the journal fields filled in."""
        sb = bytearray(self.read_sector(populate.HBFS_SUPERBLOCK_LBA))
        struct.pack_into("<I", sb, SB_JRNL_STATE, 1 if self.open else 0)
        struct.pack_into("<I", sb, SB_JRNL_PTR, self.ptr)
        struct.pack_into("<I", sb, SB_JRNL_EPOCH, self.epoch)
        restamp_superblock(sb)
        return bytes(sb)

    def _flush_superblock(self):
        """The state word is the journal's own marker, so it is never logged."""
        self._write_sector(populate.HBFS_SUPERBLOCK_LBA, self.superblock())

    # -- transaction --------------------------------------------------
    def begin(self):
        if self.open:
            raise RuntimeError("a transaction is already open")
        sb = self.read_sector(populate.HBFS_SUPERBLOCK_LBA)
        if struct.unpack_from("<I", sb, SB_JRNL_STATE)[0] != 0:
            # Bumping the epoch here would orphan the records of the
            # transaction that was interrupted, and replay would then clear the
            # state without undoing them.
            raise RuntimeError("the journal is dirty; replay before writing")
        self.epoch = (struct.unpack_from("<I", sb, SB_JRNL_EPOCH)[0] + 1) & 0xFFFFFFFF
        self.ptr = 0
        self.logged = {}
        self.open = True
        self._flush_superblock()

    def log(self, target_lba):
        """Capture a sector's pre-image; a second log of the same sector is a
        no-op, because the first one already holds the pre-transaction bytes."""
        if not self.open:
            raise RuntimeError("no transaction is open")
        if target_lba in self.logged:
            return
        if self.ptr + 2 >= JRNL_SECTS:
            raise RuntimeError("journal full at %d sectors" % self.ptr)
        pre = self.read_sector(target_lba)
        header = bytearray(SECTOR)
        struct.pack_into("<IIII", header, 0, JRNL_MAGIC, JRNL_T_PRE,
                         target_lba, self.epoch)
        struct.pack_into("<I", header, PRE_OFF_CRC, crc32c(pre))
        # Payload first: a crash between the two writes leaves a record whose
        # header is not valid yet, and replay ignores those.
        self._write_sector(JRNL_LBA + self.ptr + 1, pre)
        self._write_sector(JRNL_LBA + self.ptr, bytes(header))
        self.logged[target_lba] = pre
        self.ptr += 2

    def write(self, target_lba, data):
        """Journal a metadata sector and then write it."""
        self.log(target_lba)
        self._write_sector(target_lba, data)

    def write_data(self, lba, data):
        """A data sector: content nobody has to undo, so not journalled. A
        rollback frees the block the caller was about to link into a file, and
        whatever was written here becomes unreachable with it."""
        self._write_sector(lba, data)

    def commit(self):
        """Seal the transaction: the record that says every write above landed."""
        if not self.open:
            return
        header = bytearray(SECTOR)
        struct.pack_into("<IIII", header, 0, JRNL_MAGIC, JRNL_T_COMMIT, 0,
                         self.epoch)
        struct.pack_into("<I", header, COMMIT_OFF_RECORDS, len(self.logged))
        struct.pack_into("<I", header, COMMIT_OFF_CRC, 0)
        struct.pack_into("<I", header, COMMIT_OFF_CRC,
                         crc32c(bytes(header)))
        self._write_sector(JRNL_LBA + self.ptr, bytes(header))
        self.ptr = 0
        self.open = False
        self._flush_superblock()

    def abort(self):
        """Roll back in this process; the same code path replay uses."""
        if not self.open:
            return
        for target_lba, pre in reversed(list(self.logged.items())):
            self._write_sector(target_lba, pre)
        self.ptr = 0
        self.open = False
        self._flush_superblock()


def restamp_superblock(sb):
    """Keep a v4 superblock's checksum true after a field is patched."""
    if struct.unpack_from("<I", sb, 0)[0] != populate.HBFS4_MAGIC:
        return sb
    struct.pack_into("<I", sb, populate.HBFS4_OFF_CHECKSUM, 0)
    struct.pack_into("<I", sb, populate.HBFS4_OFF_CHECKSUM, crc32c(bytes(sb)))
    return sb


def replay(image_path):
    """Recover a volume whose last transaction may not have finished."""
    with open(image_path, "r+b") as f:
        f.seek(populate.HBFS_SUPERBLOCK_LBA * SECTOR)
        sb = bytearray(f.read(SECTOR))
        state = struct.unpack_from("<I", sb, SB_JRNL_STATE)[0]
        epoch = struct.unpack_from("<I", sb, SB_JRNL_EPOCH)[0]
        if state == 0:
            return Replay(dirty=False)

        def read(lba):
            f.seek(lba * SECTOR)
            return f.read(SECTOR)

        def write(lba, data):
            f.seek(lba * SECTOR)
            f.write(data)
            f.flush()

        undo = []
        committed = False
        index = 0
        while index < JRNL_SECTS:
            header = read(JRNL_LBA + index)
            if len(header) < SECTOR:
                break
            magic, rtype, target, rec_epoch = struct.unpack_from("<IIII", header, 0)
            if magic != JRNL_MAGIC or rec_epoch != epoch:
                break
            if rtype == JRNL_T_COMMIT:
                declared = struct.unpack_from("<I", header, COMMIT_OFF_RECORDS)[0]
                crc = struct.unpack_from("<I", header, COMMIT_OFF_CRC)[0]
                view = bytearray(header)
                struct.pack_into("<I", view, COMMIT_OFF_CRC, 0)
                # A zero checksum is what pre-checksum volumes wrote; accept it
                # only when the record also declares nothing, which is what they
                # wrote there too.
                checksum_ok = crc == 0 or crc32c(bytes(view)) == crc
                valid = checksum_ok and declared == (len(undo) if crc else 0)
                committed = valid
                break
            if rtype != JRNL_T_PRE:
                break
            pre = read(JRNL_LBA + index + 1)
            crc = struct.unpack_from("<I", header, PRE_OFF_CRC)[0]
            if crc and (len(pre) != SECTOR or crc32c(pre) != crc):
                raise ReplayError(
                    "record at sector %d has a torn pre-image; refusing to "
                    "write it back" % index)
            undo.append((target, pre))
            index += 2

        result = Replay(dirty=True, committed=committed, records=len(undo))
        if not committed:
            # Nothing said the writes landed, so put the old bytes back -
            # newest first, though each target is logged once.
            for target, pre in reversed(undo):
                write(target, pre)
            result.undone = len(undo)

        # Clearing the state is the last step either way, and it is restamped:
        # a v4 superblock's checksum covers the marker that says "dirty".
        #
        # Re-read it first: if the superblock was one of the sectors rolled back,
        # the copy read at entry holds the *modified* free count, and patching
        # that copy would quietly undo the undo.
        sb = bytearray(read(populate.HBFS_SUPERBLOCK_LBA))
        struct.pack_into("<I", sb, SB_JRNL_STATE, 0)
        struct.pack_into("<I", sb, SB_JRNL_PTR, 0)
        restamp_superblock(sb)
        write(populate.HBFS_SUPERBLOCK_LBA, bytes(sb))
        return result


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print("usage: hbfs_journal.py <image>", file=sys.stderr)
        return 2
    try:
        result = replay(args[0])
    except ReplayError as exc:
        print(f"  ERROR  {exc}", file=sys.stderr)
        return 1
    if not result.dirty:
        print("  PASS   the journal is clean; nothing to replay")
        return 0
    if result.committed:
        print(f"  PASS   a committed transaction of {result.records} "
              "record(s); state cleared")
        return 0
    print(f"  PASS   rolled back {result.undone} record(s) from an unfinished "
          "transaction")
    return 0


if __name__ == "__main__":
    sys.exit(main())
