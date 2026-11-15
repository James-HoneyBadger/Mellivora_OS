#!/usr/bin/env python3
"""test_doc_claims.py - prove the documentation-claims check catches drift.

tools/check_doc_claims.py compares the numbers and "there is no X" sentences in
the docs with the tree that produces them. A checker that only ever prints PASS
proves nothing, so this test mirrors the files it reads into a temporary tree,
changes one claim at a time, and requires the failure to name the file and the
real value. It also requires the real tree to come back clean, so a typo in the
claim list fails here instead of passing because both sides agree on nothing.
"""

from __future__ import annotations

import importlib.util
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHECKER_PATH = ROOT / "tools" / "check_doc_claims.py"

PASS = 0
FAIL = 0

# Files the checker reads as text; every one is copied verbatim.
COPY_FILES = [
    "Makefile",
    "README.md",
    "CONTRIBUTING.md",
    "docs/ROADMAP.md",
    "docs/TECHNICAL_REFERENCE.md",
    "docs/syscalls.json",
    "docs/spec/ABI.md",
    "docs/spec/EXEC.md",
    "kernel/cmdtable.inc",
    "kernel/syscall.inc",
    "kernel/object.inc",
    "programs/syscalls.inc",
]

# Directories it only counts, mirrored by name so the count matches the tree.
COUNTED = [
    ("programs", "*.asm"),
    ("programs/lib", "*.inc"),
    ("c/prog", "**/*.c"),
]


def check(description, condition, detail=""):
    global PASS, FAIL
    if condition:
        PASS += 1
        print("  PASS  %s" % description)
    else:
        FAIL += 1
        print("  FAIL  %s%s" % (description, "  " + detail if detail else ""))


def load_checker():
    spec = importlib.util.spec_from_file_location("check_doc_claims", CHECKER_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def mirror(root: Path) -> Path:
    for name in COPY_FILES:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)
    for directory, suffix in COUNTED:
        for source in sorted((ROOT / directory).glob(suffix)):
            target = root / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.touch()
    return root


def mutate(checker, name, edits):
    """Copy the tree, apply (path, old, new) edits, and return the errors."""
    root = mirror(Path(tempfile.mkdtemp(prefix="doc-claims-")))
    for path, old, new in edits:
        target = root / path
        text = target.read_text(encoding="utf-8")
        if old not in text:
            raise SystemExit(f"test_doc_claims: {path} has no {old!r} to change")
        target.write_text(text.replace(old, new, 1), encoding="utf-8")
    try:
        return checker.check(root)
    finally:
        shutil.rmtree(root, ignore_errors=True)


def main() -> int:
    checker = load_checker()

    clean = checker.check(ROOT)
    check("the real tree satisfies every claim", clean == [], "; ".join(clean))

    # The count the test mutates is the tree's own, so adding a program means
    # updating one document rather than this line as well.
    programs = len(list((ROOT / "programs").glob("*.asm")))
    stale = programs - 5
    errors = mutate(checker, "NASM count", [
        ("docs/ROADMAP.md", f"{programs} NASM programs",
         f"{stale} NASM programs")])
    check("a stale program count is refused",
          any("NASM programs" in e and str(stale) in e and str(programs) in e
              for e in errors),
          "; ".join(errors))

    errors = mutate(checker, "built-in count", [
        ("docs/ROADMAP.md", "135 shell built-ins", "97 shell built-ins")])
    check("a stale built-in count is refused",
          any("shell built-ins" in e and "97" in e for e in errors),
          "; ".join(errors))

    errors = mutate(checker, "kernel limit", [
        ("CONTRIBUTING.md", "under 8192 sectors (4 MB)", "under 2048 sectors (1 MB)")])
    check("a stale kernel-size limit is refused",
          any("kernel size limit" in e and "2048" in e for e in errors),
          "; ".join(errors))

    errors = mutate(checker, "library includes", [
        ("CONTRIBUTING.md", "18 reusable library includes", "17 reusable library includes")])
    check("a stale library-include count is refused",
          any("library includes" in e and "17" in e for e in errors),
          "; ".join(errors))

    errors = mutate(checker, "syscall count", [
        ("README.md", "210 syscalls", "209 syscalls")])
    check("a stale syscall count is refused",
          any("syscall count" in e and "209" in e for e in errors),
          "; ".join(errors))

    errors = mutate(checker, "absent claim", [
        ("docs/spec/EXEC.md", "threadmore` exercises all four",
         "threadmore` exercises all four. No detach, no cancellation")])
    check("a document denying an implemented feature is refused",
          any("docs/spec/EXEC.md" in e and "No detach" in e for e in errors),
          "; ".join(errors))

    print("  %d passed, %d failed" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
