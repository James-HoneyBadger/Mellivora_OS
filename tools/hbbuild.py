#!/usr/bin/env python3
"""hbbuild.py - build a multi-source HBX program from a directory of C sources.

This is the SDK's link step. It exists because a program that uses a shared
library cannot be produced by the one-source pattern rule the Makefile uses: the
import slots have to be named in the image's import table, and their addresses
are only known after the link.

The three steps, and why each one is needed:

  1. Link the objects against the runtime into an ELF at PROGRAM_BASE, keeping
     the symbol table. That table is the only record of where an import slot
     landed.
  2. Ask the symbol table for each import slot's address, and turn it into an
     RVA relative to PROGRAM_BASE. The HBX header describes the image in RVAs
     because the loader may move it.
  3. Strip the ELF to a flat payload and wrap it with tools/mkhbx.py, whose
     --import table is what the loader resolves and patches.

Usage:
  hbbuild.py --name mvapp --src c/prog/mvapp --out programs/mvapp.bin \
             --import mvlib.hbl:mv_add=mvlib_add [--import ...]
"""
from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROGRAM_BASE = 0x00500000


def run(cmd: list[str], what: str) -> str:
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(f"[hbbuild] {what} failed:\n  {' '.join(cmd)}\n")
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(1)
    return result.stdout


def symbol_address(elf: Path, name: str) -> int:
    """The address of a symbol in the linked image."""
    nm = os.environ.get("NM", "nm")
    out = run([nm, str(elf)], "symbol lookup")
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == name:
            return int(parts[0], 16)
        # nm on some hosts prints "0000000000500123 T name"
    raise SystemExit(f"[hbbuild] {name} is not in {elf}: an import slot must be "
                     f"a defined object, so the loader has somewhere to write")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--src", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--import", action="append", default=[], dest="imports",
                    help="MODULE:SYMBOL=SLOT, repeatable")
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    sources = sorted(args.src.glob("*.c"))
    if not sources:
        raise SystemExit(f"[hbbuild] no C sources in {args.src}")

    cc = shlex.split(os.environ.get("CC", "clang"))
    ld = shlex.split(os.environ.get("LD", "ld.lld"))
    cflags = shlex.split(os.environ.get("C_CFLAGS", ""))

    with tempfile.TemporaryDirectory(prefix="hbbuild-") as tmpdir:
        tmp = Path(tmpdir)
        objects: list[Path] = []
        for src in sources:
            obj = tmp / (src.stem + ".o")
            run([*cc, *cflags, "-I", str(args.src), "-o", str(obj), str(src)],
                f"compile {src.name}")
            objects.append(obj)

        runtime = os.environ.get("C_USER_RT_OBJS", "").split()
        elf = tmp / f"{args.name}.elf"
        run([*ld, "-m", "elf_i386", "-T", str(ROOT / "c/prog/prog.ld"),
             "-o", str(elf), *(str(o) for o in objects), *runtime],
            "link")

        imports: list[str] = []
        for spec in args.imports:
            target, _, slot = spec.partition("=")
            module, _, symbol = target.partition(":")
            if not (module and symbol):
                raise SystemExit(f"[hbbuild] bad --import '{spec}'")
            if slot:
                addr = symbol_address(elf, slot)
                imports.append(f"{module}:{symbol}@{addr - PROGRAM_BASE:#x}")
            else:
                imports.append(f"{module}:{symbol}")

        payload = tmp / f"{args.name}.payload"
        run([*ld, "-m", "elf_i386", "-T", str(ROOT / "c/prog/prog.ld"),
             "--oformat", "binary", "-o", str(payload), *(str(o) for o in objects),
             *runtime], "strip to a flat payload")

        args.out.parent.mkdir(parents=True, exist_ok=True)
        cmd = [sys.executable, str(ROOT / "tools/mkhbx.py"), str(payload),
               "-o", str(args.out), "--entry", "0"]
        for imp in imports:
            cmd += ["--import", imp]
        run(cmd, "wrap the image")
        if args.keep:
            shutil.copy(elf, args.out.with_suffix(".elf"))

    print(f"  [hbbuild] {args.out.name}: {len(sources)} sources, "
          f"{len(args.imports)} imports")
    return 0


if __name__ == "__main__":
    sys.exit(main())
