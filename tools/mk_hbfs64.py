#!/usr/bin/env python3
"""Write a minimal HBFS volume into the 64-bit image.

The 64-bit nucleus reads /init off the disk through its own HBFS reader, so the
image `make 64bit` produces needs a filesystem in it. The on-disk format is the
one the 32-bit tree already uses - populate.py owns the layout, so this imports
its constants and FSImage rather than duplicating 288-byte entry packing.

Usage: tools/mk_hbfs64.py <image> <name>=<path> [<name>=<path> ...]
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

import populate  # noqa: E402  (path set up above)


def main():
    if len(sys.argv) < 3:
        print("usage: mk_hbfs64.py <image> <name>=<path> ...", file=sys.stderr)
        return 2

    image = sys.argv[1]
    fs = populate.FSImage(image)
    for spec in sys.argv[2:]:
        name, _, path = spec.partition("=")
        if not name or not path:
            print("bad file spec: %s" % spec, file=sys.stderr)
            return 2
        with open(path, "rb") as f:
            data = f.read()
        text = name.endswith(".txt") or name == "motd" or name == "readme"
        ftype = populate.FTYPE_TEXT if text else populate.FTYPE_EXEC
        directory = None
        if "/" in name:
            # "docs/readme=..." puts the file in /docs, creating that
            # directory if the volume does not have it yet.
            directory, _, name = name.rpartition("/")
            fs.create_subdir(directory)
        fs.add_file(name, data, directory=directory, ftype=ftype)
    fs.finalize()
    return 0


if __name__ == "__main__":
    sys.exit(main())
