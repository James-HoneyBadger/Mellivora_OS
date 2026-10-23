#!/usr/bin/env python3
"""test_hbfs_checksum.py - the v4 checksums, and the tool that audits them.

Four things have to hold, and only the last is interesting on its own:

  * the CRC-32C in the builder and the one in tools/fsck_hbfs.py are the same
    function, pinned to the catalogue value for "123456789";
  * a v3 superblock is byte-for-byte what it has always been, so adding
    checksums to v4 did not quietly change the volume every image uses;
  * the checksum trailers fit in room the runs already had, so no allocation
    bit and no directory entry moved to make space for them;
  * a v4 image built by populate.py passes `fsck_hbfs.py --complain`, and the
    same image with one byte flipped in any region does not - a checker that has
    never been seen to complain is not a checker.

The images are built into a temporary directory and thrown away.
"""

from __future__ import annotations

import importlib.util
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


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build_image(directory, version, subdir=False):
    """A small v<version> volume with one file in it (and a directory, if asked)."""
    sys.path.insert(0, str(ROOT))
    import populate

    path = os.path.join(directory, "v%d%s.img" % (version, "-dir" if subdir else ""))
    with open(path, "wb") as f:
        f.truncate(IMAGE_BYTES)
    with redirect_stdout(io.StringIO()):
        fs = populate.FSImage(path, version=version)
        fs.add_file("hello.txt", b"hello\n", ftype=populate.FTYPE_TEXT)
        if subdir:
            fs.create_subdir("docs")
            fs.add_file("readme.txt", b"read me\n", directory="docs",
                        ftype=populate.FTYPE_TEXT)
        fs.finalize()
    return path, populate


def poke(image, offset, value=b"\xff"):
    """Flip one byte of the image, the way a torn write would."""
    with open(image, "r+b") as f:
        f.seek(offset)
        f.write(value)


def complain(image):
    """Run the offline checker's audit and return (status, output)."""
    proc = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"), image, "--complain"],
        capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def main():
    populate = load(ROOT / "populate.py", "populate")
    fsck = load(ROOT / "tools" / "fsck_hbfs.py", "fsck_hbfs")
    scratch = Path(tempfile.mkdtemp(prefix="hbfs-checksum-"))

    try:
        # One checksum, two implementations, one answer. The catalogue value is
        # the CRC-32C of the ASCII string "123456789" (RFC 3720, B.4).
        check("builder crc32c matches the catalogue",
              populate.crc32c(b"123456789") == 0xE3069283)
        check("checker crc32c matches the catalogue",
              fsck.crc32c(b"123456789") == 0xE3069283)

        # v3 is untouched: magic, version field 1, the geometry it always had,
        # and nothing written after it.
        v3 = populate.create_superblock(3)
        check("v3 magic and version field unchanged",
              struct.unpack_from("<I", v3, 0)[0] == 0x48424653
              and struct.unpack_from("<I", v3, 4)[0] == 1)
        check("v3 writes nothing past the block size",
              not any(v3[32:]))

        # v4 puts its header where the constants say, and nowhere v3 looks.
        v4 = populate.create_superblock(4, features=populate.HBFS4_FEATURE_EXTENTS,
                                        generation=7)
        magic, version, total, free = struct.unpack_from("<IIII", v4, 0)
        check("v4 magic and version field",
              magic == 0x34424653 and version == 4)
        check("v4 geometry is the v3 geometry",
              v4[8:32] == v3[8:32])
        check("v4 leaves the v3 journal and feature area alone",
              not any(v4[32:populate.HBFS4_OFF_FEATURES]))
        check("v4 features word",
              struct.unpack_from("<I", v4, populate.HBFS4_OFF_FEATURES)[0]
              == populate.HBFS4_FEATURE_EXTENTS)
        check("v4 generation counter",
              struct.unpack_from("<I", v4, populate.HBFS4_OFF_GENERATION)[0] == 7)
        check("v4 high halves are zero for a 2 GB volume",
              struct.unpack_from("<I", v4, populate.HBFS4_OFF_BLOCKS_HI)[0] == 0
              and struct.unpack_from("<I", v4, populate.HBFS4_OFF_DATA_HI)[0] == 0)
        stored = struct.unpack_from("<I", v4, populate.HBFS4_OFF_CHECKSUM)[0]
        check("v4 checksum is over the sector with the field zeroed",
              stored == populate.superblock_checksum(v4))
        check("a flipped byte breaks the checksum",
              populate.superblock_checksum(v4[:100] + b"\xff" + v4[101:]) != stored)

        # The trailers live in room the runs already had. If that ever stops
        # being true the checksum has taken a bit or an entry with it, and this
        # is where that is noticed.
        bitmap_used = (populate.TOTAL_BLOCKS + 7) // 8
        check("the bitmap keeps a trailer's worth of spare bytes",
              bitmap_used + populate.HBFS4_RUN_CHECKSUM_BYTES
              <= populate.HBFS_BITMAP_SIZE)
        check("the root directory keeps a trailer's worth of spare bytes",
              populate.HBFS_MAX_FILES * populate.HBFS_DIR_ENTRY_SIZE
              + populate.HBFS4_RUN_CHECKSUM_BYTES <= populate.HBFS_ROOT_DIR_SIZE)
        check("a subdirectory keeps a trailer's worth of spare bytes",
              populate.HBFS_SUBDIR_MAX_FILES * populate.HBFS_DIR_ENTRY_SIZE
              + populate.HBFS4_RUN_CHECKSUM_BYTES <= populate.HBFS_SUBDIR_SIZE)
        check("builder and checker agree on the trailer size",
              fsck.HBFS4_RUN_CHECKSUM_BYTES == populate.HBFS4_RUN_CHECKSUM_BYTES)

        # The checksum has to survive finalize(), which patches the total and
        # free counts after the superblock was created.
        v3_image, _ = build_image(scratch, 3)
        status, out = complain(v3_image)
        check("a v3 volume is reported as carrying no checksum",
              status == 0 and "carries no checksum" in out)

        v4_image, _ = build_image(scratch, 4, subdir=True)
        with open(v4_image, "rb") as f:
            f.seek(populate.HBFS_SUPERBLOCK_LBA * populate.SECTOR_SIZE)
            on_disk = f.read(populate.SECTOR_SIZE)
        check("finalize() refreshed the checksum after patching the counts",
              struct.unpack_from("<I", on_disk, populate.HBFS4_OFF_CHECKSUM)[0]
              == populate.superblock_checksum(on_disk))
        status, out = complain(v4_image)
        check("a clean v4 volume passes the audit", status == 0)
        check("the audit covers the superblock, the bitmap and both directories",
              "4 of 4 checksummed regions match" in out
              and "superblock" in out and "bitmap" in out
              and "/" in out and "/docs" in out)

        # One byte in each kind of region, one image each. A checksum that only
        # covers the region it was pointed at would pass three of these.
        regions = {
            "superblock": (populate.HBFS_SUPERBLOCK_LBA * populate.SECTOR_SIZE
                           + 12, "superblock"),
            "bitmap": (populate.HBFS_BITMAP_START * populate.SECTOR_SIZE + 100,
                       "bitmap"),
            "root": (populate.HBFS_ROOT_DIR_START * populate.SECTOR_SIZE + 1,
                     "/"),
            "subdir": (None, "/docs"),
        }
        for name, (offset, label) in regions.items():
            broken = os.path.join(scratch, "v4-broken-%s.img" % name)
            shutil.copyfile(v4_image, broken)
            if offset is None:
                # First byte of /docs' own run, found the way a reader would.
                with open(v4_image, "rb") as f:
                    f.seek(populate.HBFS_ROOT_DIR_START * populate.SECTOR_SIZE)
                    root = f.read(populate.HBFS_ROOT_DIR_SIZE)
                for i in range(populate.HBFS_MAX_FILES):
                    off = i * populate.HBFS_DIR_ENTRY_SIZE
                    if root[off:off + 4] == b"docs":
                        start, = struct.unpack_from("<I", root, off + 260)
                        offset = ((populate.HBFS_DATA_START
                                   + start * populate.SECTORS_PER_BLOCK)
                                  * populate.SECTOR_SIZE)
                        break
            poke(broken, offset)
            status, out = complain(broken)
            check("a corrupted %s fails the audit" % name,
                  status == 1 and ("ERROR  %s: checksum" % label) in out)
            check("the %s failure prints what it expected and found" % name,
                  "contents say" in out)
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
