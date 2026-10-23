#!/usr/bin/env python3
"""test_validate_constants.py - prove the constant validator catches drift.

tools/validate_constants.py compares the on-disk constants across kernel.asm,
populate.py, tests/test_hbfs.py, boot/lm_hbfs.c and tools/fsck_hbfs.py. A
validator that only ever prints PASS proves nothing, so this test changes one
number in a copy of each source in turn and requires the comparison to name it -
and requires the real files to come back clean, so a typo in the map itself
fails here rather than passing because both sides are missing.
"""

from __future__ import annotations

import importlib.util
import io
import re
import shutil
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VALIDATOR_PATH = ROOT / "tools" / "validate_constants.py"

PASS = 0
FAIL = 0


def check(description, condition):
    global PASS, FAIL
    if condition:
        PASS += 1
        print("  PASS  %s" % description)
    else:
        FAIL += 1
        print("  FAIL  %s" % description)


def load_validator():
    spec = importlib.util.spec_from_file_location("validate_constants", VALIDATOR_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def with_c_source(text):
    """Write `text` to a temporary lm_hbfs.c and return its path."""
    path = Path(tempfile.mkdtemp(prefix="validate-constants-")) / "lm_hbfs.c"
    path.write_text(text, encoding="utf-8")
    return path


def with_python_source(name, text):
    path = Path(tempfile.mkdtemp(prefix="validate-constants-")) / name
    path.write_text(text, encoding="utf-8")
    return path


def lm_check(validator, defines, populate, fsck):
    """The 64-bit comparison, over whichever copies the caller supplies."""
    return validator.check_sides(validator.LM_CONSTANT_MAP, [
        ("boot/lm_hbfs.c", defines),
        ("populate.py", populate),
        ("tools/fsck_hbfs.py", fsck),
    ])


def main():
    validator = load_validator()
    populate = validator.parse_python_assignments(validator.POPULATE_PATH)
    fsck = validator.parse_python_assignments(validator.FSCK_PATH)
    source = validator.LM_HBFS_PATH.read_text(encoding="utf-8")
    fsck_source = validator.FSCK_PATH.read_text(encoding="utf-8")
    defines = validator.parse_c_defines(validator.LM_HBFS_PATH)
    scratch = []

    # The parser has to read C literals the way the compiler does: the `U`
    # suffix is not part of the value, and a define written in terms of another
    # define is evaluated, not skipped.
    check("hex literal with a U suffix parses",
          defines.get("HBFS_MAGIC") == 0x48424653)
    check("a define derived from another one parses",
          defines.get("HBFS_SECTORS_PER_BLOCK") == 8)

    # The real files are clean, and every mapped name is present on every side
    # that claims to carry it - a rename cannot pass by being absent.
    errors = lm_check(validator, defines, populate, fsck)
    check("boot/lm_hbfs.c, populate.py and tools/fsck_hbfs.py agree", errors == [])
    for err in errors:
        print("        %s" % err)

    # A changed number in the C source is caught, and caught by name.
    mutated = re.sub(r"(#define\s+HBFS4_OFF_GENERATION\s+)64\b", r"\g<1>66",
                     source, count=1)
    check("the C mutation landed", mutated != source)
    path = with_c_source(mutated)
    scratch.append(path.parent)
    errors = lm_check(validator, validator.parse_c_defines(path), populate, fsck)
    # One line per pair that disagrees, so a change in the first file names
    # every other copy that now contradicts it.
    check("a changed C offset is reported",
          len(errors) == 2 and all("HBFS4_OFF_GENERATION" in err for err in errors))
    check("the report names both sides",
          bool(errors) and "boot/lm_hbfs.c" in errors[0] and "populate.py" in errors[0])

    # A deleted define is caught too, which is the case a drift check written as
    # "compare what both have" would silently pass.
    removed = re.sub(r"^[ \t]*#define[ \t]+HBFS4_OFF_DATA_HI\b[^\n]*\n", "",
                     source, count=1, flags=re.MULTILINE)
    check("the deletion landed", removed != source)
    path = with_c_source(removed)
    scratch.append(path.parent)
    errors = lm_check(validator, validator.parse_c_defines(path), populate, fsck)
    check("a missing define is reported",
          len(errors) == 1 and "HBFS4_OFF_DATA_HI" in errors[0])

    # The offline checker is a third implementation of the same numbers, and a
    # change there is just as much drift as a change in the kernel.
    path = with_python_source("fsck_hbfs.py", re.sub(
        r"(HBFS4_OFF_CHECKSUM\s*=\s*)76\b", r"\g<1>72", fsck_source, count=1))
    scratch.append(path.parent)
    errors = lm_check(validator, defines, populate,
                      validator.parse_python_assignments(path))
    check("a changed checker offset is reported",
          len(errors) == 1 and "tools/fsck_hbfs.py" in errors[0])

    # And the tool as a whole fails: a validator whose exit code ignores its own
    # findings is a validator that the Makefile cannot use.
    directory = Path(tempfile.mkdtemp(prefix="validate-constants-"))
    scratch.append(directory)
    shutil.copy(validator.KERNEL_PATH, directory / "kernel.asm")
    shutil.copy(validator.TEST_HBFS_PATH, directory / "test_hbfs.py")
    shutil.copy(validator.FSCK_PATH, directory / "fsck_hbfs.py")
    (directory / "populate.py").write_text(
        re.sub(r"(HBFS4_OFF_CHECKSUM\s*=\s*)76\b", r"\g<1>80",
               validator.POPULATE_PATH.read_text(encoding="utf-8"), count=1),
        encoding="utf-8")
    (directory / "lm_hbfs.c").write_text(source, encoding="utf-8")
    saved = (validator.KERNEL_PATH, validator.POPULATE_PATH,
             validator.TEST_HBFS_PATH, validator.LM_HBFS_PATH,
             validator.FSCK_PATH)
    validator.KERNEL_PATH = directory / "kernel.asm"
    validator.POPULATE_PATH = directory / "populate.py"
    validator.TEST_HBFS_PATH = directory / "test_hbfs.py"
    validator.LM_HBFS_PATH = directory / "lm_hbfs.c"
    validator.FSCK_PATH = directory / "fsck_hbfs.py"
    try:
        out = io.StringIO()
        with redirect_stdout(out):
            status = validator.main()
        check("main() exits non-zero on drift", status == 1)
        check("main() prints the offending constant",
              "HBFS4_OFF_CHECKSUM" in out.getvalue())
    finally:
        (validator.KERNEL_PATH, validator.POPULATE_PATH,
         validator.TEST_HBFS_PATH, validator.LM_HBFS_PATH,
         validator.FSCK_PATH) = saved
        for path in scratch:
            shutil.rmtree(path, ignore_errors=True)

    print()
    if FAIL:
        print("  %d passed, %d failed" % (PASS, FAIL))
        return 1
    print("  %d passed" % PASS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
