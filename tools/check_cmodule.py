#!/usr/bin/env python3
"""Check that the C kernel module really landed in the kernel image.

The module is built as a standalone flat blob and assembled into kernel.bin at
an address measured by a probe assembly pass (see the kernel.bin rule in the
Makefile). If that measurement and the link ever disagree, the kernel calls
into whatever is at the address instead - a failure that shows up as a crash
somewhere else entirely. This reads the image the way kernel/cmod.inc does and
requires the header to be where and what it claims:

  - the slot address from the build is 16-byte aligned and inside the image;
  - the first word at that file offset is 'CMOD';
  - the version and header size are the ones kernel/cmod.inc expects;
  - the init and selftest pointers point inside the blob;
  - the blob fits the 32 KB slot the kernel asserts.

No build is needed beyond kernel.bin, so this is cheap enough for `make check`.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KERNEL = ROOT / "kernel.bin"
ADDR_FILE = ROOT / ".build" / "c" / "kmod.addr"
BLOB = ROOT / "c" / "build" / "kmod.bin"

KERNEL_BASE = 0x00100000
SLOT_BYTES = 0x8000           # kernel/data.inc
CMS_MAGIC = 0x444F4D43        # 'CMOD', little-endian
CMS_VERSION = 1
CMS_SIZE = 24                 # sizeof(cmod_header)
HEADER_FIELDS = 6


def main() -> int:
    for path in (KERNEL, ADDR_FILE, BLOB):
        if not path.is_file():
            print(f"[check-cmodule] FAIL: {path.relative_to(ROOT)} is missing")
            print("  run: make kernel.bin")
            return 1

    slot = int(ADDR_FILE.read_text(encoding="utf-8").strip(), 16)
    kernel = KERNEL.read_bytes()
    blob_size = BLOB.stat().st_size

    errors: list[str] = []
    if slot % 16:
        errors.append(f"the slot address 0x{slot:08X} is not 16-byte aligned")
    if slot + SLOT_BYTES > KERNEL_BASE + len(kernel) and blob_size > SLOT_BYTES:
        errors.append("the blob does not fit the slot")

    offset = slot - KERNEL_BASE
    if offset < 0 or offset + CMS_SIZE > len(kernel):
        print(f"[check-cmodule] FAIL: slot 0x{slot:08X} is outside the image")
        return 1

    magic, version, size, init, selftest, name = struct.unpack_from(
        "<6I", kernel, offset)

    if magic != CMS_MAGIC:
        errors.append(
            f"no 'CMOD' header at 0x{slot:08X} (found 0x{magic:08X})")
    if version != CMS_VERSION:
        errors.append(f"module version {version}, kernel expects {CMS_VERSION}")
    if size != CMS_SIZE:
        errors.append(f"header size {size}, kernel expects {CMS_SIZE}")
    if blob_size > SLOT_BYTES:
        errors.append(f"the module is {blob_size} bytes, the slot is {SLOT_BYTES}")
    for role, pointer in (("init", init), ("selftest", selftest)):
        if not (slot <= pointer < slot + blob_size):
            errors.append(
                f"{role} at 0x{pointer:08X} is outside the blob "
                f"(0x{slot:08X}..0x{slot + blob_size:08X})")
    if not (slot <= name < slot + blob_size):
        errors.append(f"the module name at 0x{name:08X} is outside the blob")

    if errors:
        print("[check-cmodule] FAIL")
        for error in errors:
            print(f"  - {error}")
        return 1

    print(f"[check-cmodule] PASS  'CMOD' v{version} at 0x{slot:08X}, "
          f"{blob_size} of {SLOT_BYTES} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
