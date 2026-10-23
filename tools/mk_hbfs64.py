#!/usr/bin/env python3
"""Write a minimal HBFS volume into the 64-bit image.

The 64-bit nucleus reads /init off the disk through its own HBFS reader, so the
image `make 64bit` produces needs a filesystem in it. The on-disk format is the
one the 32-bit tree already uses - populate.py owns the layout, so this imports
its constants and FSImage rather than duplicating 288-byte entry packing.

Usage: tools/mk_hbfs64.py [--version 3|4] [--fragment NAME] <image> <name>=<path> [...]

--fragment writes that file as three runs described by an extent block, which
is the only way to put a fragmented file in an image before there is a kernel
writer to fragment one by growing it.
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

import populate  # noqa: E402  (path set up above)


def main():
    args = sys.argv[1:]
    version = 3
    fragmented = set()
    while args[:1] in (["--version"], ["--fragment"]):
        if args[0] == "--version":
            if len(args) < 2 or args[1] not in ("3", "4"):
                print("--version takes 3 or 4", file=sys.stderr)
                return 2
            version = int(args[1])
        else:
            if len(args) < 2:
                print("--fragment takes a file name", file=sys.stderr)
                return 2
            fragmented.add(args[1])
        args = args[2:]

    if len(args) < 2:
        print("usage: mk_hbfs64.py [--version 3|4] <image> <name>=<path> ...",
              file=sys.stderr)
        return 2

    image = args[0]
    fs = populate.FSImage(image, version=version)
    for spec in args[1:]:
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
        fragments = 3 if name in fragmented else None
        fs.add_file(name, data, directory=directory, ftype=ftype,
                    fragments=fragments)
    fs.finalize()
    return 0


if __name__ == "__main__":
    sys.exit(main())
