#!/usr/bin/env python3
"""test_v4_mount.py - boot an HBFS v4 volume read-only.

v4 keeps the v3 layout and adds a second magic, a feature word and a CRC-32C
over the superblock and every metadata run. This build verifies those and
mounts the volume read-only. The test proves, in order:

  * the builder produces a v4 image whose checksums the offline checker agrees
    with (8 of 8 regions);
  * the kernel mounts it, lists the root and reads a file from it;
  * a write is refused and leaves every metadata region byte-identical - a
    read-only mount that quietly writes would fail this even if it "worked";
  * a corrupted superblock or root directory is refused instead of being read
    with rules the checksum just disproved.

Usage: tests/test_v4_mount.py [--keep]
Env:   V4_ALLOW_SKIP=1  exit 0 when QEMU or the build artefacts are missing.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE_MB = 256
SUPERBLOCK_LBA = 4096
BITMAP_BLOCKS = 16
ROOT_DIR_BLOCKS = 32
FAIL = 0


def check(description: str, condition: bool, detail: str = "") -> None:
    global FAIL
    if condition:
        print(f"  PASS  {description}")
    else:
        FAIL += 1
        print(f"  FAIL  {description}{(': ' + detail) if detail else ''}")


def region_digest(image: Path) -> str:
    """Digest of the metadata a read-only mount must not touch."""
    h = hashlib.sha256()
    with image.open("rb") as f:
        for lba, blocks in ((SUPERBLOCK_LBA, 1),
                            (SUPERBLOCK_LBA + 1, BITMAP_BLOCKS),
                            (SUPERBLOCK_LBA + 1 + BITMAP_BLOCKS * 8,
                             ROOT_DIR_BLOCKS)):
            f.seek(lba * 512)
            h.update(f.read(blocks * 512))
    return h.hexdigest()


def qemu_drive(image: Path, *commands: str) -> dict:
    args = [sys.executable, str(ROOT / "tools" / "qemu_drive.py"),
            str(image), *commands, "--wait", "15", "--json"]
    proc = subprocess.run(args, capture_output=True, text=True, timeout=300)
    try:
        return json.loads(proc.stdout[proc.stdout.index("{"):])
    except (ValueError, json.JSONDecodeError):
        return {"ok": False, "screen": "", "raw": proc.stdout[-400:]}


def build_image(directory: Path) -> Path:
    image = directory / "v4.img"
    with image.open("wb") as f:
        f.truncate(IMAGE_MB * 1024 * 1024)
    for name, seek in (("boot.bin", 0), ("stage2.bin", 1), ("kernel.bin", 33)):
        subprocess.run(["dd", f"if={ROOT / name}", f"of={image}", "bs=512",
                        f"seek={seek}", "conv=notrunc", "status=none"], check=True)
    proc = subprocess.run([sys.executable, str(ROOT / "populate.py"),
                           str(image), str(ROOT / "programs"), "--version", "4"],
                          capture_output=True, text=True)
    check("the v4 image populates", proc.returncode == 0,
          (proc.stderr or proc.stdout)[-300:])
    return image

def add_fragmented_file(image: Path) -> bytes:
    """Add a file whose data lives in three runs, and return what it should read."""
    sys.path.insert(0, str(ROOT))
    import populate

    payload = b"".join(
        (f"fragment run {i:03d} " + "." * 60 + "\n").encode()
        for i in range(1, 241))
    payload += b"LAST-FRAGMENT-MARKER\n"
    fs = populate.FSImage(str(image), version=4)
    fs.add_file("frag.txt", payload, ftype=populate.FTYPE_TEXT, fragments=3)
    # The same file as a program: loading it proves the executable loader walks
    # the run list, and running it drives fd_read over the same runs.
    program = (ROOT / "programs" / "fd_frag_test.bin").read_bytes()
    fs.add_file("fragload", program, directory="bin",
                ftype=populate.FTYPE_EXEC, fragments=2)
    fs.finalize()
    return payload


def check_extent_creation(work: Path) -> None:
    """Fill a small v4 volume so only isolated one-block holes remain, then let
    the kernel store a two-block file as two runs."""
    sys.path.insert(0, str(ROOT))
    import populate

    image = work / "frag.img"
    data_blocks = 400
    size = (populate.HBFS_DATA_START * populate.SECTOR_SIZE
            + data_blocks * populate.BLOCK_SIZE)
    with image.open("wb") as f:
        f.truncate(size)
    for name, seek in (("boot.bin", 0), ("stage2.bin", 1), ("kernel.bin", 33)):
        subprocess.run(["dd", f"if={ROOT / name}", f"of={image}", "bs=512",
                        f"seek={seek}", "conv=notrunc", "status=none"], check=True)

    program = (ROOT / "programs" / "fragmake.bin").read_bytes()
    fs = populate.FSImage(str(image), version=4)
    fs.create_subdir("bin")
    fs.add_file("fragmake", program, directory="bin", ftype=populate.FTYPE_EXEC)
    # The program, the /bin subdirectory and the fillers have to fill the data
    # area exactly, so no contiguous run is left when the test writes.
    used = (populate.HBFS_JOURNAL_BLOCKS
            + populate.HBFS_SUBDIR_BLOCKS
            + -(-len(program) // populate.BLOCK_SIZE))
    for i in range(data_blocks - used):
        fs.add_file("h%03d" % i, b"x", ftype=populate.FTYPE_TEXT)
    fs.finalize()

    screen = qemu_drive(image, "fragmake", "--expect", "FRAG_OK").get("screen", "")
    check("a file with no contiguous run is created as an extent",
          "FRAG_OK" in screen, screen[-200:])

    fsck = subprocess.run([sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"),
                           str(image), "--complain"],
                          capture_output=True, text=True)
    check("the new extent file keeps the volume consistent",
          fsck.returncode == 0, (fsck.stdout + fsck.stderr)[-300:])

    entry = None
    with image.open("rb") as f:
        f.seek(SUPERBLOCK_LBA * 512 + 129 * 512)
        root = f.read(ROOT_DIR_BLOCKS * 4096)
        for i in range(len(root) // 288):
            off = i * 288
            if root[off:off + 7] == b"two.bin":
                entry = struct.unpack_from("<HIII", root, off + 254)
                break
    check("its directory entry is an extent with two data blocks",
          entry is not None and (entry[0] & 1) == 1 and entry[3] == 2,
          repr(entry))
    runs = None
    if entry:
        with image.open("rb") as f:
            f.seek((populate.HBFS_DATA_START + entry[2] * 8) * 512)
            block = f.read(4096)
        count = struct.unpack_from("<I", block, 4)[0]
        runs = [struct.unpack_from("<II", block, 8 + j * 8) for j in range(count)]
    check("the run list holds two separate runs",
          runs is not None and len(runs) == 2 and all(n == 1 for _s, n in runs),
          repr(runs))

def main() -> int:
    keep = "--keep" in sys.argv[1:]
    if not (ROOT / "kernel.bin").is_file():
        if os.environ.get("V4_ALLOW_SKIP") == "1":
            print("  SKIP  v4 mount test: kernel.bin missing (run 'make full')")
            return 0
        print("  FAIL  kernel.bin missing (run 'make full')")
        return 1
    if not shutil.which("qemu-system-x86_64") and not shutil.which("qemu-system-i386"):
        if os.environ.get("V4_ALLOW_SKIP") == "1":
            print("  SKIP  v4 mount test: QEMU not installed")
            return 0
        print("  FAIL  v4 mount test: QEMU not installed")
        return 1

    work = Path(tempfile.mkdtemp(prefix="hbfs-v4-mount-"))
    try:
        image = build_image(work)

        fsck = subprocess.run([sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"),
                               str(image), "--complain"],
                              capture_output=True, text=True)
        check("the offline checker verifies every v4 checksum",
              fsck.returncode == 0 and "8 of 8" in fsck.stdout,
              (fsck.stdout + fsck.stderr)[-300:])

        payload = add_fragmented_file(image)
        fsck = subprocess.run([sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"),
                               str(image), "--complain"],
                              capture_output=True, text=True)
        check("a fragmented file keeps the volume consistent",
              fsck.returncode == 0, (fsck.stdout + fsck.stderr)[-300:])

        check("the v4 volume mounts and lists its root",
              "docs" in qemu_drive(image, "dir /").get("screen", ""))
        check("a file on it reads back",
              "The Silicon Sonnet" in
              qemu_drive(image, "cat /docs/poem.txt").get("screen", ""))
        frag = qemu_drive(image, "cat /frag.txt").get("screen", "")
        check("a file stored in several runs reads back",
              "LAST-FRAGMENT-MARKER" in frag)
        for name in ("fd_frag_test", "fragload"):
            check(f"{name} reads the fragmented file through fd_read",
                  "FD_FRAG_OK" in qemu_drive(image, name, "--expect", "FD_FRAG_OK").get("screen", ""))

        # The writer: create a file and a directory, then read the file back.
        screen = qemu_drive(image, "v4write_test", "--expect", "V4WRITE_OK").get("screen", "")
        check("the v4 writer creates a file and a directory", "V4WRITE_OK" in screen)
        fsck = subprocess.run([sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"),
                               str(image), "--complain"],
                              capture_output=True, text=True)
        check("the volume is still consistent after v4 writes",
              fsck.returncode == 0, (fsck.stdout + fsck.stderr)[-300:])
        listed = qemu_drive(image, "dir /").get("screen", "")
        check("the new entries are visible",
              "v4out.txt" in listed and "v4dir" in listed)
        check("the written file reads back",
              "written by the HBFS v4 writer" in
              qemu_drive(image, "cat v4out.txt").get("screen", ""))

        # Truncating an extent file rewrites and restamps its run list.
        trunc = work / "truncated.img"
        shutil.copyfile(image, trunc)
        screen = qemu_drive(trunc, "v4trunc_test", "--expect", "TRUNC_OK").get("screen", "")
        check("an extent file can be truncated", "TRUNC_OK" in screen)
        fsck = subprocess.run([sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"),
                               str(trunc), "--complain"],
                              capture_output=True, text=True)
        check("the volume is consistent after truncating an extent file",
              fsck.returncode == 0, (fsck.stdout + fsck.stderr)[-300:])
        with trunc.open("rb") as f:
            f.seek(SUPERBLOCK_LBA * 512 + 129 * 512)
            root = f.read(ROOT_DIR_BLOCKS * 4096)
        size = blocks = None
        for i in range(len(root) // 288):
            off = i * 288
            if root[off:off + 8] == b"frag.txt":
                size = struct.unpack_from("<I", root, off + 256)[0]
                blocks = struct.unpack_from("<I", root, off + 264)[0]
                break
        check("the truncated file reports 5000 bytes in two blocks",
              size == 5000 and blocks == 2, f"size={size} blocks={blocks}")

        # Deleting an extent file frees every run plus the extent block.
        screen = qemu_drive(image, "v4del_test", "--expect", "V4DEL_OK").get("screen", "")
        check("an extent file can be deleted", "V4DEL_OK" in screen)
        fsck = subprocess.run([sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"),
                               str(image), "--complain"],
                              capture_output=True, text=True)
        check("the volume is consistent after deleting an extent file",
              fsck.returncode == 0, (fsck.stdout + fsck.stderr)[-300:])
        check("the deleted file is gone",
              "frag.txt" not in qemu_drive(image, "dir /").get("screen", ""))

        # Reformatting produces a clean v4 volume, not the legacy v2 one.
        reformatted = work / "reformatted.img"
        shutil.copyfile(image, reformatted)
        qemu_drive(reformatted, "format", "--then", "y")
        fsck = subprocess.run([sys.executable, str(ROOT / "tools" / "fsck_hbfs.py"),
                               str(reformatted), "--complain"],
                              capture_output=True, text=True)
        check("format produces a v4 volume whose checksums verify",
              fsck.returncode == 0 and "PASS" in fsck.stdout,
              (fsck.stdout + fsck.stderr)[-300:])
        check("a formatted volume is writable",
              "Directory created." in
              qemu_drive(reformatted, "mkdir afterformat", "--expect", "Directory created.").get("screen", ""))

        # A v4 volume that does not claim the journal feature stays read-only.
        ro = work / "v4-ro.img"
        shutil.copyfile(image, ro)
        sys.path.insert(0, str(ROOT))
        import populate
        with ro.open("r+b") as f:
            f.seek(SUPERBLOCK_LBA * 512)
            sb = bytearray(f.read(512))
            features = struct.unpack_from("<I", sb, populate.HBFS4_OFF_FEATURES)[0]
            struct.pack_into("<I", sb, populate.HBFS4_OFF_FEATURES,
                             features & ~populate.HBFS4_FEATURE_JOURNAL)
            struct.pack_into("<I", sb, populate.HBFS4_OFF_CHECKSUM,
                             populate.superblock_checksum(bytes(sb)))
            f.seek(SUPERBLOCK_LBA * 512)
            f.write(sb)
        ro_before = region_digest(ro)
        screen = qemu_drive(ro, "mkdir roattempt").get("screen", "")
        check("a v4 volume without the journal feature is read-only",
              "Write error!" in screen)
        check("the read-only volume is unchanged", region_digest(ro) == ro_before)

        for name, offset in (("superblock", SUPERBLOCK_LBA * 512 + 80),
                             ("root directory",
                              (SUPERBLOCK_LBA + 1 + BITMAP_BLOCKS * 8) * 512 + 100)):
            broken = work / (name.replace(" ", "-") + ".img")
            shutil.copyfile(image, broken)
            with broken.open("r+b") as f:
                f.seek(offset)
                f.write(b"\xff")
            broken_before = region_digest(broken)
            result = qemu_drive(broken, "dir /")
            screen = result.get("screen", "")
            check(f"a corrupted {name} is refused",
                  result.get("ok") and "docs" not in screen)
            check(f"the corrupted {name} image is left alone",
                  region_digest(broken) == broken_before)
        check_extent_creation(work)

    finally:
        if keep:
            print(f"  kept {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)

    print()
    if FAIL:
        print(f"  FAIL  {FAIL} check(s) failed")
        return 1
    print("  PASS  all v4 mount checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
