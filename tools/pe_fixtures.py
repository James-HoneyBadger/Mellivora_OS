#!/usr/bin/env python3
"""Build the PE32 loader fixtures.

The same arrangement as tools/hbx_fixtures.py: one payload, wrapped once per
row of the refusal table, so the difference between the images that must load
and the images that must be refused is visible in one place. The subset the
loader accepts is stated in tools/mkpe.py and in docs/spec/EXEC.md.

Usage: pe_fixtures.py [--build .build/hbx] [--out programs]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mkpe import (  # noqa: E402
    PE32P_MAGIC, SCN_EXECUTE, SCN_INITIALISED_DATA, SCN_READ, SCN_WRITE, Section,
    build_image,
)

BASE = 0x00500000
OTHER_BASE = 0x00400000
WINDOW_SIZE = 0x00C0000         # PROGRAM_BASE..USER_STACK_BASE


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build the PE32 loader fixtures.")
    parser.add_argument("--build", type=Path, default=Path(".build/hbx"))
    parser.add_argument("--out", type=Path, default=Path("programs"))
    args = parser.parse_args(argv)

    payload = (args.build / "pe.bin").read_bytes()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    images: dict[str, bytes] = {}

    # The image that must load: one code section at RVA 0, the base the window
    # is at, and the entry at the first byte.
    images["pe_ok.bin"] = build_image(payload, image_base=BASE)

    # PE32+: the loader must refuse the magic before it reads any field that
    # only exists in the other layout.
    images["pe_64.bin"] = build_image(payload, image_base=BASE, magic=PE32P_MAGIC)

    # An image built for a base this loader cannot move it to. Nothing
    # relocates a PE32 image yet, so it is refused by name.
    images["pe_base.bin"] = build_image(
        payload, image_base=BASE, image_base_field=OTHER_BASE)

    # An entry point in a data section: not executable, so not an entry.
    images["pe_entry.bin"] = build_image(
        payload,
        image_base=BASE,
        entry=0x2000 + 0x10,
        extra_section=Section(".data", 0x2000, b"\0" * 16,
                              SCN_INITIALISED_DATA | SCN_READ | SCN_WRITE,
                              virtual_size=0x1000),
    )

    # Section 8's rule is about sections, not about formats: writable and
    # executable is refused whatever the header calls itself.
    images["pe_wx.bin"] = build_image(
        payload, image_base=BASE, section_chars=SCN_EXECUTE | SCN_READ | SCN_WRITE)

    # An import directory, which the loader cannot resolve yet: refusing it is
    # better than starting a program whose IAT is full of zeroes.
    images["pe_import.bin"] = build_image(payload, image_base=BASE, import_rva=0x180)

    # An image that claims the whole window. Its file could be loaded, and its
    # image could be loaded, but not both at once: the copy has to stage the
    # file out of the image's way and there is nowhere left to put it.
    images["pe_nofit.bin"] = build_image(
        payload, image_base=BASE, size_of_image=WINDOW_SIZE)

    for name, image in images.items():
        (out / name).write_bytes(image)
        print(f"  pe fixture  {name:<20} {len(image):6d} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
