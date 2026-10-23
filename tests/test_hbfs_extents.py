#!/usr/bin/env python3
"""test_hbfs_extents.py - a v4 file whose data is more than one run.

An entry with HBFS4_DE_EXTENTS set does not point at its data: it points at a
metadata block of {block, count} records, and a reader has to walk it. That is
the one thing the format has to get right before a kernel can read such a file,
so this test pins all of it, offline:

  * the entry says what it should (flag, extent block, total data blocks),
  * the extent block parses, is inside the block's capacity, and its trailer
    checksum verifies,
  * the runs reassemble into exactly the bytes that went in,
  * the allocation bitmap marks the extent block and every run, and nothing
    else, so a fragmented file leaks nothing and double-claims nothing,
  * a file that is one run keeps the inline form, flags zero and `start`
    pointing straight at its data - the case that must not change,
  * and the offline audit covers the extent block, and names it when a byte in
    it is flipped.

Images are built in a temporary directory.
"""

from __future__ import annotations

import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
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


def build(directory, files):
    """Build a v4 image; `files` is a list of (name, data, fragments)."""
    sys.path.insert(0, str(ROOT))
    import populate

    path = os.path.join(directory, "v4.img")
    with open(path, "wb") as f:
        f.truncate(IMAGE_BYTES)
    with redirect_stdout(io.StringIO()):
        fs = populate.FSImage(path, version=4)
        for name, data, fragments in files:
            fs.add_file(name, data, ftype=populate.FTYPE_TEXT, fragments=fragments)
        fs.finalize()
    return path, populate


def entry_of(image, populate, index=0):
    with open(image, "rb") as f:
        f.seek(populate.HBFS_ROOT_DIR_START * populate.SECTOR_SIZE)
        root = f.read(populate.HBFS_ROOT_DIR_SIZE)
    off = index * populate.HBFS_DIR_ENTRY_SIZE
    name = root[off:off + populate.HBFS_MAX_FILENAME].split(b"\0")[0].decode()
    return {
        "name": name,
        "flags": struct.unpack_from("<H", root, off + 254)[0],
        "size": struct.unpack_from("<I", root, off + 256)[0],
        "start": struct.unpack_from("<I", root, off + 260)[0],
        "blocks": struct.unpack_from("<I", root, off + 264)[0],
    }


def extent_block_of(image, populate, entry):
    with open(image, "rb") as f:
        f.seek((populate.HBFS_DATA_START
                + entry["start"] * populate.SECTORS_PER_BLOCK)
               * populate.SECTOR_SIZE)
        raw = f.read(populate.BLOCK_SIZE)
    magic, count = struct.unpack_from("<II", raw, 0)
    runs = [struct.unpack_from("<II", raw,
                               populate.HBFS4_EXTENT_HDR
                               + i * populate.HBFS4_EXTENT_RECORD)
            for i in range(count)]
    return raw, magic, count, runs


def read_through(image, populate, runs):
    """The bytes a reader that honours the extent list would see."""
    out = b""
    with open(image, "rb") as f:
        for start, blocks in runs:
            f.seek((populate.HBFS_DATA_START
                    + start * populate.SECTORS_PER_BLOCK) * populate.SECTOR_SIZE)
            out += f.read(blocks * populate.BLOCK_SIZE)
    return out


def main():
    scratch = Path(tempfile.mkdtemp(prefix="hbfs-extents-"))
    big = bytes((i * 7) % 251 for i in range(4 * 4096 + 123))   # five blocks
    small = b"inline\n"

    try:
        path, populate = build(scratch, [("frag.bin", big, 3),
                                         ("inline.txt", small, None)])

        # Five blocks split into three runs: 2 + 2 + 1.
        frag = entry_of(path, populate)
        check("the fragmented file names three runs",
              frag["name"] == "frag.bin"
              and frag["flags"] & populate.HBFS4_DE_EXTENTS)
        check("the entry counts the data blocks, not the extent block",
              frag["blocks"] == 5 and frag["size"] == len(big))

        raw, magic, count, runs = extent_block_of(path, populate, frag)
        check("the extent block carries the extent magic",
              magic == populate.HBFS4_EXTENT_MAGIC)
        check("the extent block names three runs", count == 3)
        check("the runs add up to the entry's block count",
              sum(blocks for _start, blocks in runs) == frag["blocks"])
        check("no run is empty or absurd",
              all(0 < blocks <= frag["blocks"] for _start, blocks in runs))
        check("the extent block's trailer checksum verifies",
              struct.unpack_from("<I", raw, len(raw) - 4)[0]
              == populate.crc32c(bytes(raw[:-4]) + bytes(4)))
        check("the runs reassemble into the original bytes",
              read_through(path, populate, runs)[:len(big)] == big)

        # The inline case is untouched: no flag, and `start` is the data itself.
        inline = entry_of(path, populate, index=1)
        check("a one-run file stays inline",
              inline["name"] == "inline.txt" and inline["flags"] == 0
              and inline["blocks"] == 1
              and read_through(path, populate, [(inline["start"], 1)])
              .startswith(small))

        # Allocation: the extent block and the three runs are marked, and the
        # file's blocks are exactly what the entry claims.
        with open(path, "rb") as f:
            f.seek(populate.HBFS_BITMAP_START * populate.SECTOR_SIZE)
            bitmap = f.read(populate.HBFS_BITMAP_SIZE)

        def marked(block):
            return (bitmap[block // 8] >> (block % 8)) & 1

        check("the extent block is allocated", marked(frag["start"]))
        check("every run is allocated",
              all(marked(start + i)
                  for start, blocks in runs for i in range(blocks)))
        # Only the volume's own blocks are bits; the last four bytes of the run
        # are the checksum trailer, which sits past TOTAL_BLOCKS on purpose.
        used = sum(1 for block in range(populate.TOTAL_BLOCKS) if marked(block))
        check("the bitmap marks the journal, five data blocks, the extent block "
              "and the inline block - and nothing else",
              used == populate.HBFS_JOURNAL_BLOCKS + frag["blocks"] + 1 + 1)
        check("the trailer's bits are past the volume's last block",
              populate.TOTAL_BLOCKS
              <= (populate.HBFS_BITMAP_SIZE
                  - populate.HBFS4_RUN_CHECKSUM_BYTES) * 8)

        # And the audit reaches it.
        proc = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"), path,
             "--complain"], capture_output=True, text=True)
        check("the audit covers the extent block",
              proc.returncode == 0 and "/frag.bin" in proc.stdout)
        with open(path, "r+b") as f:
            f.seek((populate.HBFS_DATA_START
                    + frag["start"] * populate.SECTORS_PER_BLOCK)
                   * populate.SECTOR_SIZE + 100)
            f.write(b"\xff")
        proc = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"), path,
             "--complain"], capture_output=True, text=True)
        check("a corrupted extent block fails the audit",
              proc.returncode == 1 and "ERROR  /frag.bin: checksum" in proc.stdout)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    print()
    if FAIL:
        print("  %d passed, %d failed" % (PASS, FAIL))
        return 1
    print("  %d passed" % PASS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
