#!/usr/bin/env python3
"""Write a minimal HBFS volume into the 64-bit image.

The 64-bit nucleus reads /init off the disk through its own HBFS reader, so the
image `make 64bit` produces needs a filesystem in it. The on-disk format is the
one the 32-bit tree already uses - populate.py owns the layout, so this imports
its constants and FSImage rather than duplicating 288-byte entry packing.

Usage: tools/mk_hbfs64.py <image> <file> [name]
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

import populate  # noqa: E402  (path set up above)


def main():
    if len(sys.argv) < 3:
        print("usage: mk_hbfs64.py <image> <file> [name]", file=sys.stderr)
        return 2

    image = sys.argv[1]
    path = sys.argv[2]
    name = sys.argv[3] if len(sys.argv) > 3 else os.path.basename(path)

    with open(path, "rb") as f:
        data = f.read()

    fs = populate.FSImage(image)
    fs.add_file(name, data, ftype=populate.FTYPE_EXEC)
    fs.finalize()
    print("HBFS: /%s, %d bytes" % (name, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
