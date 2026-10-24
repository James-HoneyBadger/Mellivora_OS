#!/usr/bin/env python3
"""test_validate_constants.py - prove the constant validator catches drift.

tools/validate_constants.py compares the on-disk constants across kernel.asm,
populate.py, tests/test_hbfs.py, tools/fsck_hbfs.py and tools/hbfs_journal.py. A
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


def with_python_source(name, text):
    path = Path(tempfile.mkdtemp(prefix="validate-constants-")) / name
    path.write_text(text, encoding="utf-8")
    return path


def fsck_check(validator, populate, fsck):
    """The builder/checker comparison, over whichever copies the caller supplies."""
    return validator.check_sides(validator.FSCK_CONSTANT_MAP, [
        ("populate.py", populate),
        ("tools/fsck_hbfs.py", fsck),
    ])


def journal_check(validator, fsck, journal):
    """The journal comparison between the checker and the reference reader."""
    return validator.check_sides(validator.JOURNAL_CONSTANT_MAP, [
        ("tools/fsck_hbfs.py", fsck),
        ("tools/hbfs_journal.py", journal),
    ])


def main():
    validator = load_validator()
    populate = validator.parse_python_assignments(validator.POPULATE_PATH)
    fsck_source = validator.FSCK_PATH.read_text(encoding="utf-8")
    fsck = validator.parse_python_assignments(validator.FSCK_PATH)
    journal_source = validator.JOURNAL_PATH.read_text(encoding="utf-8")
    journal = validator.parse_python_assignments(validator.JOURNAL_PATH)
    scratch = []

    # The real files are clean, and every mapped name is present on every side
    # that claims to carry it - a rename cannot pass by being absent.
    errors = fsck_check(validator, populate, fsck)
    check("populate.py and tools/fsck_hbfs.py agree", errors == [])
    for err in errors:
        print("        %s" % err)

    errors = journal_check(validator, fsck, journal)
    check("tools/fsck_hbfs.py and tools/hbfs_journal.py agree", errors == [])
    for err in errors:
        print("        %s" % err)

    # A changed number in the offline checker is caught, and caught by name.
    mutated = re.sub(r"(HBFS4_OFF_GENERATION\s*=\s*)64\b", r"\g<1>66",
                     fsck_source, count=1)
    check("the checker mutation landed", mutated != fsck_source)
    path = with_python_source("fsck_hbfs.py", mutated)
    scratch.append(path.parent)
    errors = fsck_check(validator, populate,
                        validator.parse_python_assignments(path))
    check("a changed checker offset is reported",
          len(errors) == 1 and "HBFS4_OFF_GENERATION" in errors[0])
    check("the report names both sides",
          bool(errors) and "populate.py" in errors[0]
          and "tools/fsck_hbfs.py" in errors[0])

    # A deleted constant is caught too, which is the case a drift check written
    # as "compare what both have" would silently pass.
    removed = re.sub(r"^HBFS4_OFF_DATA_HI\b[^\n]*\n", "", fsck_source,
                     count=1, flags=re.MULTILINE)
    check("the deletion landed", removed != fsck_source)
    path = with_python_source("fsck_hbfs.py", removed)
    scratch.append(path.parent)
    errors = fsck_check(validator, populate,
                        validator.parse_python_assignments(path))
    check("a missing constant is reported",
          len(errors) == 1 and "HBFS4_OFF_DATA_HI" in errors[0])

    # The journal reader is a second implementation of the same record layout,
    # and a change there is just as much drift as a change in the checker.
    path = with_python_source("hbfs_journal.py", re.sub(
        r"(COMMIT_OFF_CRC\s*=\s*)24\b", r"\g<1>22", journal_source, count=1))
    scratch.append(path.parent)
    errors = journal_check(validator, fsck,
                           validator.parse_python_assignments(path))
    check("a changed journal offset is reported",
          len(errors) == 1 and "tools/hbfs_journal.py" in errors[0])

    # And the tool as a whole fails: a validator whose exit code ignores its own
    # findings is a validator that the Makefile cannot use.
    directory = Path(tempfile.mkdtemp(prefix="validate-constants-"))
    scratch.append(directory)
    shutil.copy(validator.KERNEL_PATH, directory / "kernel.asm")
    shutil.copy(validator.TEST_HBFS_PATH, directory / "test_hbfs.py")
    shutil.copy(validator.FSCK_PATH, directory / "fsck_hbfs.py")
    shutil.copy(validator.JOURNAL_PATH, directory / "hbfs_journal.py")
    (directory / "populate.py").write_text(
        re.sub(r"(HBFS4_OFF_CHECKSUM\s*=\s*)76\b", r"\g<1>80",
               validator.POPULATE_PATH.read_text(encoding="utf-8"), count=1),
        encoding="utf-8")
    saved = (validator.KERNEL_PATH, validator.POPULATE_PATH,
             validator.TEST_HBFS_PATH, validator.FSCK_PATH,
             validator.JOURNAL_PATH)
    validator.KERNEL_PATH = directory / "kernel.asm"
    validator.POPULATE_PATH = directory / "populate.py"
    validator.TEST_HBFS_PATH = directory / "test_hbfs.py"
    validator.FSCK_PATH = directory / "fsck_hbfs.py"
    validator.JOURNAL_PATH = directory / "hbfs_journal.py"
    try:
        out = io.StringIO()
        with redirect_stdout(out):
            status = validator.main()
        check("main() exits non-zero on drift", status == 1)
        check("main() prints the offending constant",
              "HBFS4_OFF_CHECKSUM" in out.getvalue())
    finally:
        (validator.KERNEL_PATH, validator.POPULATE_PATH,
         validator.TEST_HBFS_PATH, validator.FSCK_PATH,
         validator.JOURNAL_PATH) = saved
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
