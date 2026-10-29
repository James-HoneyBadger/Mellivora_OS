#!/usr/bin/env python3
"""Build the HBX loader fixtures.

Each fixture is one row of the refusal table in docs/spec/EXEC.md section 8,
plus the two images that must load: one at its preferred base and one that has
to be relocated. They are built here rather than by hand so the payload stays
readable and the differences between the images are visible in one place.

The payloads are assembled by the Makefile into .build/hbx/:
    ok.bin      prints HBX_OK and exits, built for 0x00500000
    reloc.bin   prints HBX_RELOC_OK through a relocated pointer, built for
                0x00400000 and carrying its own HBM1 relocation table
    aslr.bin    prints its own load address in hex, built the same way

Usage: hbx_fixtures.py [--build .build/hbx] [--out programs]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mkhbx import CHARS, FLAGS, Section, build_image  # noqa: E402

BASE = 0x00500000
OTHER_BASE = 0x00400000


def text(payload: bytes) -> Section:
    return Section(".text", 0, len(payload), CHARS["r"] | CHARS["x"])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build the HBX loader fixtures.")
    parser.add_argument("--build", type=Path, default=Path(".build/hbx"))
    parser.add_argument("--out", type=Path, default=Path("programs"))
    args = parser.parse_args(argv)

    ok = (args.build / "ok.bin").read_bytes()
    reloc = (args.build / "reloc.bin").read_bytes()
    aslr = (args.build / "aslr.bin").read_bytes()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    images: dict[str, bytes] = {}

    # A fixed-base image: nothing to relocate, the common case.
    images["hbx_ok.bin"] = build_image(ok, image_base=BASE, sections=[text(ok)])

    # Built for 0x00400000 and loaded at 0x00500000: only a loader that applies
    # the base relocations reaches the string it prints.
    images["hbx_reloc.bin"] = build_image(reloc, image_base=OTHER_BASE, sections=[text(reloc)])

    # Prints the address it was loaded at: two launches that print the same
    # number did not randomise anything.
    images["hbx_aslr.bin"] = build_image(aslr, image_base=OTHER_BASE, sections=[text(aslr)])

    # One byte changed after the checksum was stamped.
    damaged = bytearray(images["hbx_ok.bin"])
    damaged[-1] ^= 0xFF
    images["hbx_badsum.bin"] = bytes(damaged)

    # A section that is both writable and executable (SECURITY.md section 8).
    images["hbx_wx.bin"] = build_image(
        ok,
        image_base=BASE,
        image_size=len(ok) + 0x100,
        sections=[text(ok), Section(".rwx", len(ok), 0x100, CHARS["r"] | CHARS["w"] | CHARS["x"])],
    )

    # A 64-bit image: refused whatever else it says.
    images["hbx_64.bin"] = build_image(ok, image_base=BASE, flags=FLAGS["exec"] | FLAGS["64bit"])

    # Asks for a base it cannot be moved to and carries no relocations.
    images["hbx_noreloc.bin"] = build_image(ok, image_base=OTHER_BASE, relocations=False)

    # An entry point in a data section, which is not executable.
    images["hbx_entry.bin"] = build_image(
        ok,
        image_base=BASE,
        entry=len(ok) + 0x10,
        image_size=len(ok) + 0x100,
        sections=[text(ok), Section(".data", len(ok), 0x100, CHARS["r"] | CHARS["w"])],
    )

    # An import from a module that does not exist: the loader has to name both
    # the module and the symbol, which is all an image can be told today.
    images["hbx_import.bin"] = build_image(
        ok, image_base=BASE, imports=[("nosuch.dll", "missing")]
    )

    for name, image in images.items():
        (out / name).write_bytes(image)
        print(f"  hbx fixture {name:<20} {len(image):6d} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
