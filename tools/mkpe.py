#!/usr/bin/env python3
"""mkpe - wrap a flat payload in a 32-bit PE (PE32) image.

EXEC.md section 1 lists PE32 as the format for programs built for the parity
target's ecosystem, and the loader is meant to gain it as "one branch, not one
per caller". This tool builds the images that branch is tested with.

The subset the loader accepts is deliberately small and stated here so the
kernel and the fixtures cannot drift apart:

  * DOS header with `MZ`, and `e_lfanew` pointing at `PE\\0\\0`;
  * COFF header, Machine 0x014C (i386), at least one section;
  * optional header magic 0x010B (PE32). 0x020B is PE32+ and is refused;
  * ImageBase equal to PROGRAM_BASE, because nothing relocates a PE32 image
    yet - a different base is refused by name rather than loaded in the wrong
    place;
  * no import directory, because there is no library to resolve against yet;
  * sections whose raw data lies after the headers and whose virtual addresses
    do not overlap, each with its own W^X verdict;
  * an entry point inside an executable section.

Usage:
    mkpe.py PAYLOAD -o IMAGE [options]
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

MACHINE_I386 = 0x014C
MACHINE_AMD64 = 0x8664
PE32_MAGIC = 0x010B
PE32P_MAGIC = 0x020B

FILE_ALIGN = 0x200
SECTOR_ALIGN = 0x1000
SUBSYSTEM_CONSOLE = 3

SCN_CODE = 0x00000020
SCN_INITIALISED_DATA = 0x00000040
SCN_EXECUTE = 0x20000000
SCN_READ = 0x40000000
SCN_WRITE = 0x80000000


def align(value: int, to: int) -> int:
    return (value + to - 1) & ~(to - 1)


class Section:
    def __init__(self, name: str, va: int, raw: bytes, chars: int,
                 virtual_size: int | None = None):
        self.name = name
        self.va = va
        self.raw = raw
        self.chars = chars
        self.virtual_size = len(raw) if virtual_size is None else virtual_size


def build_image(
    payload: bytes,
    *,
    image_base: int,
    section_va: int = 0,
    entry: int = 0,
    machine: int = MACHINE_I386,
    magic: int = PE32_MAGIC,
    section_chars: int = SCN_CODE | SCN_EXECUTE | SCN_READ,
    image_base_field: int | None = None,
    extra_section: Section | None = None,
    import_rva: int = 0,
    subsystem: int = SUBSYSTEM_CONSOLE,
    size_of_image: int | None = None,
) -> bytes:
    """Build a PE32 image. Every keyword exists to make one refusal buildable."""
    sections = [Section(".text", section_va, payload, section_chars)]
    if extra_section is not None:
        sections.append(extra_section)

    # The DOS header is one page in every real linker's output; 0x40 keeps
    # e_lfanew readable by anything that only knows the first 0x40 bytes.
    lfanew = 0x40
    coff = 20
    opt_size = 0xE0                      # sizeof(IMAGE_OPTIONAL_HEADER32)
    sec_table = lfanew + 4 + coff + opt_size
    headers = align(sec_table + 40 * len(sections), FILE_ALIGN)

    raw_at = headers
    for section in sections:
        section.raw_at = raw_at
        raw_at += align(len(section.raw), FILE_ALIGN)

    body = bytearray(raw_at)
    # DOS header: MZ, and e_lfanew at 0x3C.
    body[0:2] = b"MZ"
    struct.pack_into("<H", body, 2, 0x90)          # e_cblp: bytes on last page
    struct.pack_into("<H", body, 4, 0x03)          # e_cp: pages
    struct.pack_into("<H", body, 6, 0x00)          # e_crlc: relocations
    struct.pack_into("<H", body, 8, 0x04)          # e_cparhdr: header paragraphs
    struct.pack_into("<H", body, 0x18, 0x40)       # e_lfarlc: relocation table
    struct.pack_into("<I", body, 0x3C, lfanew)

    body[lfanew:lfanew + 4] = b"PE\0\0"
    if size_of_image is None:
        size_of_image = align(
            max((s.va + align(s.virtual_size, SECTOR_ALIGN)) for s in sections),
            SECTOR_ALIGN)

    struct.pack_into("<HHIIIHH", body, lfanew + 4,
                     machine, len(sections), 0, 0, 0, opt_size, 0x0102)

    opt = lfanew + 4 + coff
    struct.pack_into("<H", body, opt + 0, magic)
    body[opt + 2] = 8                              # MajorLinkerVersion
    struct.pack_into("<III", body, opt + 4, 0, 0, 0)   # SizeOfCode/Data/BSS
    struct.pack_into("<I", body, opt + 16, entry)  # AddressOfEntryPoint
    struct.pack_into("<I", body, opt + 20, 0)      # BaseOfCode
    struct.pack_into("<I", body, opt + 24, 0)      # BaseOfData
    struct.pack_into("<I", body, opt + 28,
                     image_base if image_base_field is None else image_base_field)
    struct.pack_into("<II", body, opt + 32, SECTOR_ALIGN, FILE_ALIGN)
    struct.pack_into("<HH", body, opt + 40, 4, 0)  # OS version
    struct.pack_into("<HH", body, opt + 44, 0, 0)  # image version
    struct.pack_into("<HH", body, opt + 48, 4, 0)  # subsystem version
    struct.pack_into("<I", body, opt + 52, 0)      # Win32VersionValue
    struct.pack_into("<I", body, opt + 56, size_of_image)
    struct.pack_into("<I", body, opt + 60, headers)
    struct.pack_into("<I", body, opt + 64, 0)      # CheckSum
    struct.pack_into("<H", body, opt + 68, subsystem)
    struct.pack_into("<H", body, opt + 70, 0)      # DllCharacteristics
    struct.pack_into("<II", body, opt + 72, 0x100000, 0x1000)   # stack
    struct.pack_into("<II", body, opt + 80, 0x100000, 0x1000)   # heap
    struct.pack_into("<I", body, opt + 88, 0)      # LoaderFlags
    struct.pack_into("<I", body, opt + 92, 16)     # NumberOfRvaAndSizes
    # DataDirectory[1] is the import table.
    struct.pack_into("<II", body, opt + 96 + 8, import_rva, 0 if not import_rva else 20)

    for index, section in enumerate(sections):
        at = sec_table + 40 * index
        name = section.name.encode("ascii")[:8]
        struct.pack_into("<8sIIII", body, at, name.ljust(8, b"\0"), section.virtual_size,
                         section.va, align(len(section.raw), FILE_ALIGN),
                         section.raw_at)
        struct.pack_into("<I", body, at + 36, section.chars)
        body[section.raw_at:section.raw_at + len(section.raw)] = section.raw

    return bytes(body)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Wrap a flat payload in a PE32 image.")
    parser.add_argument("payload", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--image-base", default="0x500000")
    parser.add_argument("--entry", default="0")
    parser.add_argument("--machine", default=None)
    parser.add_argument("--pe32plus", action="store_true")
    args = parser.parse_args(argv)

    payload = args.payload.read_bytes()
    image = build_image(
        payload,
        image_base=int(args.image_base, 0),
        entry=int(args.entry, 0),
        machine=int(args.machine, 0) if args.machine else MACHINE_I386,
        magic=PE32P_MAGIC if args.pe32plus else PE32_MAGIC,
    )
    args.output.write_bytes(image)
    return 0


if __name__ == "__main__":
    sys.exit(main())
