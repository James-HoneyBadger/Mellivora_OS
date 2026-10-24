#!/usr/bin/env python3
"""test_hbfs_journal.py - crash the journal at every point and require recovery.

The journal's whole claim is that a volume survives power loss between any two
writes. That is testable: `hbfs_journal.Journal(crash_after=N)` raises at write
N+1, so every boundary in the protocol can be visited on purpose instead of
hoped for, and the volume can then be replayed and audited.

For a crash before the commit record the test requires

  * replay to succeed,
  * the metadata to be byte-identical to the *before* state, apart from the
    superblock's journal bookkeeping - the epoch moves forward on every
    transaction and must not be rolled back, or the records it invalidates would
    be replayed a second time,
  * and `fsck_hbfs.py` to call the volume consistent, which for a v4 volume means
    the checksums hold again too.

A crash after the commit record but before the clean-state flush is the one case
that must end in the *after* state, and a torn commit record must not count as a
commit while a torn pre-image must stop the replay rather than be written back.

`--iterations N` (default 100) runs random operations at random crash points;
`make check-journal-crash` asks for the ten thousand the roadmap's gate names.
"""

from __future__ import annotations

import hashlib
import io
import os
import random
import shutil
import struct
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

import populate  # noqa: E402
import hbfs_journal as hj  # noqa: E402
import fsck_hbfs  # noqa: E402

# populate.py derives TOTAL_BLOCKS from the file size, so the volume has to be
# large enough to contain NEW_BLOCK (800) past the metadata area.
IMAGE_BYTES = 8 * 1024 * 1024
PASS = 0
FAIL = 0


def check(description, condition):
    global PASS, FAIL
    if condition:
        PASS += 1
        print("  PASS  %s" % description)
    else:
        FAIL += 1
        print("  FAIL  %s" % description)


def fresh_image(directory, name="journal.img"):
    """A small v4 volume with one file, built the normal way."""
    path = os.path.join(directory, name)
    with open(path, "wb") as f:
        f.truncate(IMAGE_BYTES)
    with redirect_stdout(io.StringIO()):
        fs = populate.FSImage(path, version=4)
        fs.add_file("hello.txt", b"hello\n", ftype=populate.FTYPE_TEXT)
        fs.finalize()
    return path


def content_digest(path):
    """What a rollback has to restore: the metadata, minus the journal's own
    bookkeeping fields in the superblock and the checksum that covers them."""
    with open(path, "rb") as f:
        f.seek(populate.HBFS_SUPERBLOCK_LBA * populate.SECTOR_SIZE)
        sb = bytearray(f.read(populate.SECTOR_SIZE))
        f.seek(populate.HBFS_BITMAP_START * populate.SECTOR_SIZE)
        bitmap = f.read(populate.HBFS_BITMAP_SIZE)
        f.seek(populate.HBFS_ROOT_DIR_START * populate.SECTOR_SIZE)
        root = f.read(populate.HBFS_ROOT_DIR_SIZE)
    for off in (hj.SB_JRNL_STATE, hj.SB_JRNL_PTR, hj.SB_JRNL_EPOCH,
                populate.HBFS4_OFF_CHECKSUM):
        struct.pack_into("<I", sb, off, 0)
    return hashlib.sha256(bytes(sb) + bitmap + root).hexdigest()


def read_superblock(path):
    with open(path, "rb") as f:
        f.seek(populate.HBFS_SUPERBLOCK_LBA * populate.SECTOR_SIZE)
        return f.read(populate.SECTOR_SIZE)


def journal_records(path):
    """How many PRE records an interrupted transaction left in the log."""
    epoch = struct.unpack_from("<I", read_superblock(path), hj.SB_JRNL_EPOCH)[0]
    count = 0
    with open(path, "rb") as f:
        for index in range(0, hj.JRNL_SECTS, 2):
            f.seek((hj.JRNL_LBA + index) * hj.SECTOR)
            magic, rtype, _target, rec_epoch = struct.unpack("<IIII", f.read(16))
            if (magic != hj.JRNL_MAGIC or rtype != hj.JRNL_T_PRE
                    or rec_epoch != epoch):
                break
            count += 1
    return count


def fsck_ok(path):
    """Run the whole checker in process. Too slow for every iteration of the
    crash gate, which uses `checksums_ok` instead."""
    checker = fsck_hbfs.Fsck(path)
    status = checker.run()
    return status == 0, "\n".join(checker.errors)


def checksums_ok(path):
    """The cheap half of consistency: every metadata run still agrees with its
    own checksum, which is the part a torn or half-undone write breaks first."""
    with open(path, "rb") as f:
        return all(stored == computed
                   for _label, stored, computed in fsck_hbfs.v4_checksum_report(f))


NEW_BLOCK = 800          # free in the test image, and far outside the journal
NEW_NAME = "journal.txt"


def restamp(run):
    """A v4 run's trailer, recomputed after its contents changed."""
    off = len(run) - populate.HBFS4_RUN_CHECKSUM_BYTES
    struct.pack_into("<I", run, off, 0)
    struct.pack_into("<I", run, off, populate.crc32c(bytes(run)))


def write_run(journal, lba, run, first, last):
    """Journal and write every sector the bytes first..last-1 fall in, plus the
    trailer's.

    Both halves matter: a 288-byte directory entry does not align to a 512-byte
    sector, so writing "the sector the entry starts in" loses its tail - the type
    byte, in this test - and the trailer lives in the run's last sector, so any
    change anywhere invalidates the checksum there too. That is the writer's
    whole obligation in v4.
    """
    restamp(run)
    trailer = (len(run) - populate.HBFS4_RUN_CHECKSUM_BYTES) // hj.SECTOR
    sectors = set(range(first // hj.SECTOR, (last - 1) // hj.SECTOR + 1))
    sectors.add(trailer)
    for sector in sorted(sectors):
        start = sector * hj.SECTOR
        journal.write(lba + sector, bytes(run[start:start + hj.SECTOR]))


def mutate(journal, tag):
    """Create a small file, the way the kernel will: allocate, link, account.

    The data block goes out unjournalled - nothing has to undo file content -
    and the three metadata runs are restamped and journalled sector by sector.
    """
    journal.begin()

    payload = (tag or b"hello")[:8].ljust(12, b".").ljust(hj.SECTOR, b"\0")
    journal.write_data(populate.HBFS_DATA_START
                       + NEW_BLOCK * populate.SECTORS_PER_BLOCK, payload)

    bitmap = bytearray(journal.read(populate.HBFS_BITMAP_START, populate.HBFS_BITMAP_SIZE))
    bitmap[NEW_BLOCK // 8] |= 1 << (NEW_BLOCK % 8)
    write_run(journal, populate.HBFS_BITMAP_START, bitmap,
              NEW_BLOCK // 8, NEW_BLOCK // 8 + 1)

    root = bytearray(journal.read(populate.HBFS_ROOT_DIR_START,
                                  populate.HBFS_ROOT_DIR_SIZE))
    slot = 1                      # the test image has one file, at slot 0
    entry = populate.create_dir_entry(NEW_NAME, populate.FTYPE_TEXT, 12,
                                      NEW_BLOCK, 1)
    root[slot * populate.HBFS_DIR_ENTRY_SIZE:
         (slot + 1) * populate.HBFS_DIR_ENTRY_SIZE] = entry
    write_run(journal, populate.HBFS_ROOT_DIR_START, root,
              slot * populate.HBFS_DIR_ENTRY_SIZE,
              (slot + 1) * populate.HBFS_DIR_ENTRY_SIZE)

    sb = bytearray(journal.read_sector(populate.HBFS_SUPERBLOCK_LBA))
    struct.pack_into("<I", sb, 12, struct.unpack_from("<I", sb, 12)[0] - 1)
    hj.restamp_superblock(sb)
    journal.write(populate.HBFS_SUPERBLOCK_LBA, bytes(sb))

    journal.commit()


def run_crashing(path, crash_after, tag=b"TAG1"):
    """Apply the transaction, letting the crash point stop it. Returns True if
    it was cut short."""
    try:
        with hj.Journal(path, crash_after=crash_after) as journal:
            mutate(journal, tag)
        return False
    except hj.Crash:
        return True


def main():
    iterations = 100
    if "--iterations" in sys.argv:
        iterations = int(sys.argv[sys.argv.index("--iterations") + 1])

    # The journal tool and the checker carry the same constants; before a kernel
    # third copy exists, this is the tripwire that keeps them equal.
    check("the journal tool and the checker agree on the format",
          hj.JRNL_MAGIC == fsck_hbfs.HBFS_JRNL_MAGIC
          and hj.JRNL_LBA == fsck_hbfs.HBFS_JOURNAL_LBA
          and hj.JRNL_SECTS == fsck_hbfs.HBFS_JOURNAL_SECTS
          and hj.COMMIT_OFF_RECORDS == fsck_hbfs.HBFS4_JRNL_OFF_RECORDS
          and hj.COMMIT_OFF_CRC == fsck_hbfs.HBFS4_JRNL_OFF_CRC
          and hj.SB_JRNL_STATE == 40 and hj.SB_JRNL_PTR == 44
          and hj.SB_JRNL_EPOCH == 56)

    scratch = tempfile.mkdtemp(prefix="hbfs-journal-")
    try:
        base = fresh_image(scratch)
        before = content_digest(base)

        # Count the protocol's writes so every boundary can be visited.
        probe = os.path.join(scratch, "probe.img")
        shutil.copyfile(base, probe)
        with hj.Journal(probe) as journal:
            mutate(journal, b"TAG1")
            writes = journal.writes
        after = content_digest(probe)

        check("a clean transaction changes the metadata", before != after)
        sb = read_superblock(probe)
        check("it commits, clears the dirty flag and keeps a valid checksum",
              struct.unpack_from("<I", sb, hj.SB_JRNL_STATE)[0] == 0
              and struct.unpack_from("<I", sb, populate.HBFS4_OFF_CHECKSUM)[0]
              == populate.superblock_checksum(sb))
        ok, out = fsck_ok(probe)
        check("the committed volume is consistent", ok)
        if not ok:
            print(out)
        check("the protocol is 22 sector writes, so 22 crash points to visit",
              writes == 22)

        # Every crash point but the last: roll back to exactly the old content.
        bad = []
        for point in range(writes - 1):
            path = os.path.join(scratch, "crash-%02d.img" % point)
            shutil.copyfile(base, path)
            cut = run_crashing(path, point)
            result = hj.replay(path)
            ok, out = fsck_ok(path)
            if not cut or content_digest(path) != before or not ok \
                    or result.committed:
                bad.append((point, cut, result, out.strip().splitlines()[-1:]))
        check("a crash at any of the first %d write points rolls back exactly"
              % (writes - 1), not bad)
        for point, cut, result, tail in bad:
            print("        crash-after=%d cut=%s committed=%s undone=%d %s"
                  % (point, cut, result.committed, result.undone, tail))

        # A crash between the commit record and the clean-state flush: the
        # writes landed, so replay must keep them.
        path = os.path.join(scratch, "after-commit.img")
        shutil.copyfile(base, path)
        cut = run_crashing(path, writes - 1, b"TAG2")
        result = hj.replay(path)
        ok, _out = fsck_ok(path)
        check("a crash after the commit record keeps the transaction",
              cut and result.dirty and result.committed and ok)
        check("and the content is the new one", content_digest(path) != before)

        # A commit record with a broken checksum must not count as a commit.
        path = os.path.join(scratch, "torn-commit.img")
        shutil.copyfile(base, path)
        run_crashing(path, writes - 2, b"TAG3")
        sb = read_superblock(path)
        epoch = struct.unpack_from("<I", sb, hj.SB_JRNL_EPOCH)[0]
        header = bytearray(hj.SECTOR)
        struct.pack_into("<IIII", header, 0, hj.JRNL_MAGIC, hj.JRNL_T_COMMIT, 0,
                         epoch)
        struct.pack_into("<I", header, hj.COMMIT_OFF_RECORDS, 3)
        struct.pack_into("<I", header, hj.COMMIT_OFF_CRC, 0xDEADBEEF)
        with open(path, "r+b") as f:
            f.seek((hj.JRNL_LBA + 2 * journal_records(path)) * hj.SECTOR)
            f.write(bytes(header))
        result = hj.replay(path)
        check("a commit record that fails its checksum is rolled back",
              result.dirty and not result.committed
              and content_digest(path) == before)

        # A torn pre-image must stop the replay rather than be written back.
        path = os.path.join(scratch, "torn-pre.img")
        shutil.copyfile(base, path)
        run_crashing(path, 5, b"TAG4")   # one record written, then cut
        with open(path, "r+b") as f:
            f.seek((hj.JRNL_LBA + 1) * hj.SECTOR + 32)
            f.write(b"\xde\xad\xbe\xef")
        try:
            hj.replay(path)
            refused = False
        except hj.ReplayError:
            refused = True
        check("a torn pre-image stops the replay", refused)

        # The roadmap's gate, at whatever count was asked for.
        rng = random.Random(20261022)
        failed = 0
        for i in range(iterations):
            path = os.path.join(scratch, "random.img")
            shutil.copyfile(base, path)
            point = rng.randrange(writes)
            cut = run_crashing(path, point, b"%04d" % (i % 10000))
            try:
                hj.replay(path)
                # Comparing against a known-good volume is the strong statement:
                # identical bytes mean the checksums are identical too. The full
                # structural check is still worth its cost, but not 10 000
                # times, so every fiftieth volume gets it as well.
                ok = True
                if i % 50 == 0:
                    ok = checksums_ok(path)
                    if ok:
                        ok, _out = fsck_ok(path)
            except hj.ReplayError:
                ok = False
            committed_case = cut and point == writes - 1
            expected = after if committed_case else before
            if not ok or content_digest(path) != expected:
                failed += 1
        check("%d random crash/replay iterations leave a consistent volume"
              % iterations, failed == 0)
        if failed:
            print("        %d of %d went wrong" % (failed, iterations))

        return 0 if FAIL == 0 else 1
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    status = main()
    print()
    print("  %d passed, %d failed" % (PASS, FAIL))
    sys.exit(status)
