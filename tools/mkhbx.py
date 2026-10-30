#!/usr/bin/env python3
"""mkhbx - build an HBX image from a flat Mellivora payload.

HBX is the native image format (docs/spec/EXEC.md section 2): a 96-byte header,
a section table, then the image bytes. A program is a payload assembled at the
base it wants to be loaded at; this tool prepends the header, describes the
payload's sections, computes the checksum, and leaves the kernel's
hbx_load_program to verify all of it.

Payloads that carry absolute addresses can describe their own base relocations.
If a payload contains the marker b"HBM1" followed by a u32 count and that many
u32 RVAs - assembled as `label - $$`, so one source works at any ORG - this tool
points the header's reloc_rva at the count and clears the marker, which is only
a build-time signal. See programs/hbx/hbx_reloc.asm.

Usage:
    mkhbx.py PAYLOAD -o IMAGE [options]

Options:
    --image-base ADDR     preferred load address (default 0x500000, PROGRAM_BASE)
    --entry RVA           entry point, relative to the image (default 0)
    --image-size BYTES    image size including the zero tail; default: payload
    --section NAME:RVA:SIZE:CHARS
                          repeatable; default: one .text over the whole payload
    --flags NAME[,NAME]   exec (default), dll, driver, 32only, 64bit
    --subsystem NAME      console (default), windows, driver
    --import MODULE:SYMBOL[@RVA]
                          repeatable; appended as an import table. With @RVA
                          the IAT slot is the payload's own, without it one is
                          allocated after the thunks.
    --export SYMBOL:RVA   repeatable; appended as an export table (a library)

A payload may also carry a TLS1 marker - `"TLS1"` followed by raw_size and the
RVAs of the template, the index slot and the base slot - which becomes the
image's thread-local storage directory (EXEC.md section 6).
    --stack-reserve N     bytes of stack to reserve (informational for now)
    --stack-commit N
    --heap-reserve N
    --no-reloc            ignore an HBM1 marker in the payload

CHARS is a combination of r, w, x, d (discardable) and s (shared).
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

MAGIC = 0x5842564D  # 'MVBX'
VERSION = 1
FIXED_HEADER = 96
SECTION_SIZE = 20
SECTION_NAME_LEN = 8
RELOC_MARKER = b"HBM1"
IMPORT_MARKER = b"HBI1"
EXPORT_MARKER = b"HBE1"
TLS_MARKER = b"TLS1"

FLAGS = {
    "exec": 0x01,
    "dll": 0x02,
    "driver": 0x04,
    "32only": 0x08,
    "64bit": 0x10,
}

CHARS = {"r": 0x01, "w": 0x02, "x": 0x04, "d": 0x08, "s": 0x10}

SUBSYSTEMS = {"console": 1, "windows": 2, "driver": 3}


def _crc32c_table() -> list[int]:
    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
        table.append(crc)
    return table


CRC32C_TABLE = _crc32c_table()


def crc32c(data: bytes) -> int:
    """CRC-32C (Castagnoli), the checksum the loader and HBFS both use."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc = (crc >> 8) ^ CRC32C_TABLE[(crc ^ byte) & 0xFF]
    return crc ^ 0xFFFFFFFF


class Section:
    def __init__(self, name: str, rva: int, size: int, chars: int):
        self.name = name
        self.rva = rva
        self.size = size
        self.chars = chars

    @classmethod
    def parse(cls, spec: str) -> "Section":
        parts = spec.split(":")
        if len(parts) != 4:
            raise ValueError(f"section {spec!r}: expected NAME:RVA:SIZE:CHARS")
        name, rva, size, chars = parts
        bits = 0
        for c in chars:
            if c not in CHARS:
                raise ValueError(f"section {spec!r}: unknown characteristic {c!r}")
            bits |= CHARS[c]
        return cls(name, int(rva, 0), int(size, 0), bits)

    def pack(self) -> bytes:
        name = self.name.encode("ascii")
        if len(name) > SECTION_NAME_LEN:
            raise ValueError(f"section name {self.name!r} is longer than {SECTION_NAME_LEN}")
        return struct.pack("<8sIII", name.ljust(SECTION_NAME_LEN, b"\0"), self.rva, self.size, self.chars)


def find_relocations(payload: bytes) -> tuple[int | None, bytes]:
    """Locate an HBM1 table; return (reloc_rva, payload with the marker cleared)."""
    at = payload.find(RELOC_MARKER)
    if at < 0:
        return None, payload
    if at + 8 > len(payload):
        raise ValueError("HBM1 marker is not followed by a count")
    count = struct.unpack_from("<I", payload, at + 4)[0]
    end = at + 8 + 4 * count
    if end > len(payload):
        raise ValueError(f"HBM1 declares {count} relocations, which do not fit the payload")
    cleared = payload[:at] + b"\0\0\0\0" + payload[at + 4 :]
    return at + 4, cleared


def find_exports(payload: bytes) -> tuple[list[tuple[str, int]], bytes]:
    """Locate an HBE1 table; return ([(name, rva)], payload with it cleared).

    A payload that exports symbols describes them where it can see them - with
    `label - $$`, which is an RVA at any ORG - so the wrapper never has to parse
    an assembler listing. The marker is cleared afterwards so the table the tool
    builds is the only one in the image.
    """
    at = payload.find(EXPORT_MARKER)
    if at < 0:
        return [], payload
    if at + 8 > len(payload):
        raise ValueError("HBE1 marker is not followed by a count")
    count = struct.unpack_from("<I", payload, at + 4)[0]
    end = at + 8 + 8 * count
    if end > len(payload):
        raise ValueError(f"HBE1 declares {count} exports, which do not fit the payload")
    exports: list[tuple[str, int]] = []
    for i in range(count):
        name_rva, rva = struct.unpack_from("<II", payload, at + 8 + 8 * i)
        if name_rva >= len(payload):
            raise ValueError(f"export {i}: name RVA 0x{name_rva:x} is past the payload")
        raw = payload[name_rva : payload.find(b"\0", name_rva)]
        if not raw:
            raise ValueError(f"export {i}: empty name")
        exports.append((raw.decode("ascii"), rva))
    cleared = payload[:at] + b"\0\0\0\0" + payload[at + 4 :]
    return exports, cleared


def find_imports(payload: bytes) -> tuple[list[tuple[str, str, int]], bytes]:
    """Locate an HBI1 table; return ([(module, symbol, iat_rva)], payload cleared).

    Hand-written assembly cannot know where the wrapper will put an import
    address table, so an importer names its own slots: the marker carries the
    module name RVA, a count, and that many (symbol_name_rva, iat_rva) pairs.
    """
    at = payload.find(IMPORT_MARKER)
    if at < 0:
        return [], payload
    if at + 12 > len(payload):
        raise ValueError("HBI1 marker is not followed by a module name and a count")
    module_rva, count = struct.unpack_from("<II", payload, at + 4)
    end = at + 12 + 8 * count
    if end > len(payload):
        raise ValueError(f"HBI1 declares {count} imports, which do not fit the payload")
    if module_rva >= len(payload):
        raise ValueError("import module name is past the payload")
    module = payload[module_rva : payload.find(b"\0", module_rva)].decode("ascii")
    imports: list[tuple[str, str, int]] = []
    for i in range(count):
        name_rva, iat_rva = struct.unpack_from("<II", payload, at + 12 + 8 * i)
        if name_rva >= len(payload):
            raise ValueError(f"import {i}: name RVA 0x{name_rva:x} is past the payload")
        raw = payload[name_rva : payload.find(b"\0", name_rva)]
        if not raw:
            raise ValueError(f"import {i}: empty name")
        imports.append((module, raw.decode("ascii"), iat_rva))
    cleared = payload[:at] + b"\0\0\0\0" + payload[at + 4 :]
    return imports, cleared


def find_tls(payload: bytes) -> tuple[tuple[int, int, int, int] | None, bytes]:
    """Locate a TLS1 marker; return ((raw_size, data_rva, index_rva, base_rva), payload).

    A module that wants thread-local storage describes it where it can see it:
    the four numbers are `label - $$` RVAs into its own payload. The wrapper
    appends the directory the loader reads and clears the marker, so the image
    carries exactly one copy of the four numbers.
    """
    at = payload.find(TLS_MARKER)
    if at < 0:
        return None, payload
    if at + 20 > len(payload):
        raise ValueError("TLS1 marker is not followed by four dwords")
    values = struct.unpack_from("<IIII", payload, at + 4)
    cleared = payload[:at] + b"\0\0\0\0" + payload[at + 4 :]
    return values, cleared


def build_imports(
    payload_len: int, imports: list[tuple[str, str, int | None]]
) -> tuple[bytes, int, int]:
    """Append an import table; return (bytes, import_rva, count_of_entries_used).

    An import with an IAT RVA writes into a slot the payload already declared;
    one without gets a slot allocated here, after the thunks.
    """
    modules: list[str] = []
    symbols: dict[str, list[tuple[str, int | None]]] = {}
    for module, symbol, iat in imports:
        if module not in symbols:
            modules.append(module)
            symbols[module] = []
        symbols[module].append((symbol, iat))

    strings = bytearray()
    module_rva: dict[str, int] = {}
    for module in modules:
        module_rva[module] = payload_len + len(strings)
        strings += module.encode("ascii") + b"\0"
    symbol_rva: dict[str, list[int]] = {}
    for module in modules:
        symbol_rva[module] = []
        for symbol, _iat in symbols[module]:
            symbol_rva[module].append(payload_len + len(strings))
            strings += symbol.encode("ascii") + b"\0"

    thunk_base = payload_len + len(strings)
    thunks: dict[str, int] = {}
    at = thunk_base
    for module in modules:
        thunks[module] = at
        at += 8 * len(symbols[module])
    iat_base = at
    allocated = sum(1 for m in modules for _s, iat in symbols[m] if iat is None)
    at += 4 * allocated
    desc_base = at
    at += 12 * len(modules)

    blob = bytearray(strings)
    iat = iat_base
    for module in modules:
        for (symbol, slot), name_rva in zip(symbols[module], symbol_rva[module]):
            if slot is None:
                slot = iat
                iat += 4
            blob += struct.pack("<II", name_rva, slot)
    blob += b"\0" * (4 * allocated)
    for module in modules:
        blob += struct.pack("<III", module_rva[module], thunks[module], len(symbols[module]))
    return bytes(blob), desc_base, len(modules)


def build_exports(payload_len: int, exports: list[tuple[str, int]]) -> tuple[bytes, int]:
    """Append an export table; return (bytes, export_rva).

    The table is a u32 count followed by that many {name_rva, rva} entries, the
    same pair the import table's thunks use, so a resolver reads one shape.
    """
    strings = bytearray()
    names: list[int] = []
    for name, _rva in exports:
        names.append(payload_len + len(strings))
        strings += name.encode("ascii") + b"\0"
    table_at = payload_len + len(strings)
    blob = bytearray(strings)
    blob += struct.pack("<I", len(exports))
    for (name, rva), name_rva in zip(exports, names):
        blob += struct.pack("<II", name_rva, rva)
    return bytes(blob), table_at


def build_image(
    payload: bytes,
    *,
    image_base: int = 0x00500000,
    entry: int = 0,
    image_size: int | None = None,
    sections: list[Section] | None = None,
    flags: int = FLAGS["exec"],
    subsystem: int = 1,
    imports: list[tuple[str, str, int | None]] | None = None,
    exports: list[tuple[str, int]] | None = None,
    stack_reserve: int = 0,
    stack_commit: int = 0,
    heap_reserve: int = 0,
    relocations: bool = True,
) -> bytes:
    if sections is None:
        sections = [Section(".text", 0, len(payload), CHARS["r"] | CHARS["x"])]

    reloc_rva, body = find_relocations(payload) if relocations else (None, payload)
    if reloc_rva is not None and reloc_rva >= len(body):
        raise ValueError("relocation table starts past the end of the payload")

    # The payload's own declaration of what it imports and exports wins over
    # the command line: hand-written assembly names its IAT slots and its entry
    # points with `label - $$`, and only the payload knows those numbers.
    marked_imports, body = find_imports(body)
    if marked_imports:
        imports = marked_imports
    marked_exports, body = find_exports(body)
    if marked_exports and not exports:
        exports = marked_exports

    import_rva = 0
    import_count = 0
    if imports:
        table, import_rva, import_count = build_imports(len(body), imports)
        body += table

    export_rva = 0
    export_count = 0
    if exports:
        table, export_rva = build_exports(len(body), exports)
        export_count = len(exports)
        body += table

    marked_tls, body = find_tls(body)
    tls_rva = 0
    if marked_tls is not None:
        raw_size, data_rva, index_rva, base_rva = marked_tls
        if raw_size == 0:
            raise ValueError("TLS1 declares no initialised data")
        if data_rva + raw_size > len(body):
            raise ValueError("TLS1 template runs past the payload")
        tls_rva = len(body)
        body += struct.pack("<IIII", raw_size, data_rva, index_rva, base_rva)

    if image_size is None:
        image_size = len(body)
    if image_size < len(body):
        raise ValueError(f"image size {image_size} is smaller than the {len(body)} payload bytes")

    header_size = FIXED_HEADER + SECTION_SIZE * len(sections)
    header_size = (header_size + 15) & ~15

    for section in sections:
        if section.rva + section.size > image_size:
            raise ValueError(
                f"section {section.name} covers 0x{section.rva:x}+{section.size} "
                f"past the image size 0x{image_size:x}"
            )

    header = bytearray(FIXED_HEADER)

    def put32(off: int, value: int) -> None:
        struct.pack_into("<I", header, off, value & 0xFFFFFFFF)

    def put64(off: int, value: int) -> None:
        struct.pack_into("<Q", header, off, value)

    put32(0, MAGIC)
    put32(4, VERSION)
    put32(8, header_size)
    put32(12, flags)
    put64(16, entry)
    put64(24, image_base)
    put64(32, image_size)
    put32(40, len(sections))
    put32(44, import_count)
    put32(48, export_count)
    put32(52, import_rva)
    put32(56, export_rva)
    put32(60, tls_rva)
    put32(64, reloc_rva or 0)
    put32(68, 0)  # resource_rva
    put64(72, 0)  # checksum
    put32(80, subsystem)
    put32(84, stack_reserve)
    put32(88, stack_commit)
    put32(92, heap_reserve)

    table = b"".join(section.pack() for section in sections)
    image = bytes(header) + table
    image += b"\0" * (header_size - len(image))
    image += body

    if len(image) != header_size + len(body):
        raise ValueError("internal error: header and image lengths disagree")

    checksum = crc32c(image)
    image = image[:72] + struct.pack("<Q", checksum) + image[80:]
    return image


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build an HBX image from a flat payload.")
    parser.add_argument("payload", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--image-base", default="0x500000")
    parser.add_argument("--entry", default="0")
    parser.add_argument("--image-size", default=None)
    parser.add_argument("--section", action="append", default=[])
    parser.add_argument("--flags", default="exec")
    parser.add_argument("--subsystem", default="console")
    parser.add_argument("--import", dest="imports", action="append", default=[])
    parser.add_argument("--export", dest="exports", action="append", default=[])
    parser.add_argument("--stack-reserve", default="0")
    parser.add_argument("--stack-commit", default="0")
    parser.add_argument("--heap-reserve", default="0")
    parser.add_argument("--no-reloc", action="store_true")
    args = parser.parse_args(argv)

    flags = 0
    for name in args.flags.split(","):
        if name not in FLAGS:
            parser.error(f"unknown flag {name!r}")
        flags |= FLAGS[name]
    if args.subsystem not in SUBSYSTEMS:
        parser.error(f"unknown subsystem {args.subsystem!r}")

    imports = []
    for spec in args.imports:
        if ":" not in spec:
            parser.error(f"import {spec!r}: expected MODULE:SYMBOL[@IAT_RVA]")
        module, symbol = spec.split(":", 1)
        slot: int | None = None
        if "@" in symbol:
            symbol, at = symbol.split("@", 1)
            slot = int(at, 0)
        imports.append((module, symbol, slot))

    exports = []
    for spec in args.exports:
        if ":" not in spec:
            parser.error(f"export {spec!r}: expected SYMBOL:RVA")
        symbol, rva = spec.split(":", 1)
        exports.append((symbol, int(rva, 0)))

    payload = args.payload.read_bytes()
    image = build_image(
        payload,
        image_base=int(args.image_base, 0),
        entry=int(args.entry, 0),
        image_size=int(args.image_size, 0) if args.image_size else None,
        sections=[Section.parse(spec) for spec in args.section],
        flags=flags,
        subsystem=SUBSYSTEMS[args.subsystem],
        imports=imports,
        exports=exports,
        stack_reserve=int(args.stack_reserve, 0),
        stack_commit=int(args.stack_commit, 0),
        heap_reserve=int(args.heap_reserve, 0),
        relocations=not args.no_reloc,
    )
    args.output.write_bytes(image)
    return 0


if __name__ == "__main__":
    sys.exit(main())
