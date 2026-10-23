#!/usr/bin/env python3
"""test_validate_constants.py - prove the constant validator catches drift.

tools/validate_constants.py pairs the on-disk constants between kernel.asm,
populate.py, tests/test_hbfs.py and boot/lm_hbfs.c. A validator that only ever
prints PASS proves nothing, so this test changes one number in a copy of the C
source and requires the comparison to name it - and requires the real pair to
come back clean, so a typo in the map itself fails here rather than passing
because both sides are missing.
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


def main():
    validator = load_validator()
    populate = validator.parse_python_assignments(validator.POPULATE_PATH)
    source = validator.LM_HBFS_PATH.read_text(encoding="utf-8")
    defines = validator.parse_c_defines(validator.LM_HBFS_PATH)
    scratch = []

    # The parser has to read C literals the way the compiler does: the `U`
    # suffix is not part of the value, and a define written in terms of another
    # define is evaluated, not skipped.
    check("hex literal with a U suffix parses",
          defines.get("HBFS_MAGIC") == 0x48424653)
    check("a define derived from another one parses",
          defines.get("HBFS_SECTORS_PER_BLOCK") == 8)

    # The real pair is clean, and every mapped name is present on both sides -
    # check_two_way reports a missing symbol, so a renamed constant cannot slip
    # through by being absent from one file.
    errors = validator.check_two_way(validator.LM_CONSTANT_MAP, defines, populate)
    check("boot/lm_hbfs.c and populate.py agree", errors == [])
    for err in errors:
        print("        %s" % err)

    # A changed number is caught, and caught by name. The substitutions are
    # whitespace-tolerant so that reformatting the C source does not turn this
    # test into a different test.
    mutated = re.sub(r"(#define\s+HBFS4_OFF_GENERATION\s+)36\b", r"\g<1>38",
                     source, count=1)
    check("the mutation landed", mutated != source)
    path = with_c_source(mutated)
    scratch.append(path.parent)
    errors = validator.check_two_way(
        validator.LM_CONSTANT_MAP, validator.parse_c_defines(path), populate)
    check("a changed offset is reported",
          len(errors) == 1 and "HBFS4_OFF_GENERATION" in errors[0])
    check("the report names both sides",
          bool(errors) and "lm_hbfs.c" in errors[0] and "populate.py" in errors[0])

    # A deleted define is caught too, which is the case a drift check written as
    # "compare what both have" would silently pass.
    removed = re.sub(r"^[ \t]*#define[ \t]+HBFS4_OFF_DATA_HI\b[^\n]*\n", "",
                     source, count=1, flags=re.MULTILINE)
    check("the deletion landed", removed != source)
    path = with_c_source(removed)
    scratch.append(path.parent)
    errors = validator.check_two_way(
        validator.LM_CONSTANT_MAP, validator.parse_c_defines(path), populate)
    check("a missing define is reported",
          len(errors) == 1 and "HBFS4_OFF_DATA_HI" in errors[0])

    # And the tool as a whole fails: a validator whose exit code ignores its own
    # findings is a validator that the Makefile cannot use.
    directory = Path(tempfile.mkdtemp(prefix="validate-constants-"))
    scratch.append(directory)
    shutil.copy(validator.KERNEL_PATH, directory / "kernel.asm")
    shutil.copy(validator.TEST_HBFS_PATH, directory / "test_hbfs.py")
    (directory / "populate.py").write_text(
        re.sub(r"(HBFS4_OFF_CHECKSUM\s*=\s*)48\b", r"\g<1>52",
               validator.POPULATE_PATH.read_text(encoding="utf-8"), count=1),
        encoding="utf-8")
    (directory / "lm_hbfs.c").write_text(source, encoding="utf-8")
    saved = (validator.KERNEL_PATH, validator.POPULATE_PATH,
             validator.TEST_HBFS_PATH, validator.LM_HBFS_PATH)
    validator.KERNEL_PATH = directory / "kernel.asm"
    validator.POPULATE_PATH = directory / "populate.py"
    validator.TEST_HBFS_PATH = directory / "test_hbfs.py"
    validator.LM_HBFS_PATH = directory / "lm_hbfs.c"
    try:
        out = io.StringIO()
        with redirect_stdout(out):
            status = validator.main()
        check("main() exits non-zero on drift", status == 1)
        check("main() prints the offending constant",
              "HBFS4_OFF_CHECKSUM" in out.getvalue())
    finally:
        (validator.KERNEL_PATH, validator.POPULATE_PATH,
         validator.TEST_HBFS_PATH, validator.LM_HBFS_PATH) = saved
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
