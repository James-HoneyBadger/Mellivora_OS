#!/usr/bin/env python3
"""Validate shared storage/layout constants across kernel and tooling.

This script prevents silent drift between:
- kernel.asm
- populate.py
- tests/test_hbfs.py
- boot/lm_hbfs.c, the 64-bit nucleus's own copy of the on-disk layout

The 64-bit reader does not share code with the 32-bit kernel, so its constants
are a second copy of the format and are checked against populate.py the same way
the 32-bit kernel's are. A test that the comparison actually fails on a changed
number lives in tests/test_validate_constants.py - a validator only earns its
place if it has been seen to say no.
"""

from __future__ import annotations

import ast
import operator
import re
import sys
from pathlib import Path
from typing import Dict

ROOT = Path(__file__).resolve().parent.parent

KERNEL_PATH = ROOT / "kernel.asm"
POPULATE_PATH = ROOT / "populate.py"
TEST_HBFS_PATH = ROOT / "tests" / "test_hbfs.py"
LM_HBFS_PATH = ROOT / "boot" / "lm_hbfs.c"

# Kernel symbol -> (populate symbol, test_hbfs symbol)
CONSTANT_MAP = {
    "HBFS_MAGIC": ("HBFS_MAGIC", "HBFS_MAGIC"),
    "HBFS_BLOCK_SIZE": ("BLOCK_SIZE", "BLOCK_SIZE"),
    "HBFS_SUPERBLOCK_LBA": ("HBFS_SUPERBLOCK_LBA", "HBFS_SUPERBLOCK_LBA"),
    "HBFS_BITMAP_START": ("HBFS_BITMAP_START", "HBFS_BITMAP_START"),
    "HBFS_ROOT_DIR_START": ("HBFS_ROOT_DIR_START", "HBFS_ROOT_DIR_START"),
    "HBFS_DATA_START": ("HBFS_DATA_START", "HBFS_DATA_START"),
    "HBFS_DIR_ENTRY_SIZE": ("HBFS_DIR_ENTRY_SIZE", "HBFS_DIR_ENTRY_SIZE"),
    "HBFS_ROOT_DIR_BLOCKS": ("HBFS_ROOT_DIR_BLOCKS", "HBFS_ROOT_DIR_BLOCKS"),
    "HBFS_SUBDIR_BLOCKS": ("HBFS_SUBDIR_BLOCKS", "HBFS_SUBDIR_BLOCKS"),
}

# boot/lm_hbfs.c symbol -> populate.py symbol. The v4 entries are the layout the
# writer will use; they are checked from the day they are declared rather than
# from the day they are first written, because the whole point of a paired
# constant is that the pair cannot disagree.
LM_CONSTANT_MAP = {
    "HBFS_SUPERBLOCK_LBA": "HBFS_SUPERBLOCK_LBA",
    "HBFS_ROOT_DIR_BLOCKS": "HBFS_ROOT_DIR_BLOCKS",
    "HBFS_BLOCK_SIZE": "BLOCK_SIZE",
    "HBFS_SECTORS_PER_BLOCK": "SECTORS_PER_BLOCK",
    "HBFS_DATA_START": "HBFS_DATA_START",
    "HBFS_DIR_ENTRY_SIZE": "HBFS_DIR_ENTRY_SIZE",
    "HBFS_MAX_FILES": "HBFS_MAX_FILES",
    "HBFS_BITMAP_BLOCKS": "HBFS_BITMAP_BLOCKS",
    "HBFS_JOURNAL_BLOCKS": "HBFS_JOURNAL_BLOCKS",
    "HBFS_SUBDIR_BLOCKS": "HBFS_SUBDIR_BLOCKS",
    "HBFS_MAGIC": "HBFS_MAGIC",
    "HBFS4_MAGIC": "HBFS4_MAGIC",
    "HBFS4_OFF_FEATURES": "HBFS4_OFF_FEATURES",
    "HBFS4_OFF_GENERATION": "HBFS4_OFF_GENERATION",
    "HBFS4_OFF_BLOCKS_HI": "HBFS4_OFF_BLOCKS_HI",
    "HBFS4_OFF_DATA_HI": "HBFS4_OFF_DATA_HI",
    "HBFS4_OFF_CHECKSUM": "HBFS4_OFF_CHECKSUM",
    "HBFS4_FEATURE_EXTENTS": "HBFS4_FEATURE_EXTENTS",
    "HBFS4_FEATURE_JOURNAL": "HBFS4_FEATURE_JOURNAL",
    "HBFS4_FEATURE_SNAPSHOT": "HBFS4_FEATURE_SNAPSHOT",
}

ALLOWED_BINOPS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.floordiv,
    ast.FloorDiv: operator.floordiv,
    ast.Mod: operator.mod,
    ast.LShift: operator.lshift,
    ast.RShift: operator.rshift,
    ast.BitOr: operator.or_,
    ast.BitAnd: operator.and_,
    ast.BitXor: operator.xor,
}

ALLOWED_UNARY = {
    ast.UAdd: operator.pos,
    ast.USub: operator.neg,
    ast.Invert: operator.invert,
}


class EvalError(RuntimeError):
    pass


def safe_eval_expr(expr: str, symbols: Dict[str, int]) -> int:
    """Evaluate simple arithmetic expressions with previously known symbols."""

    def _eval(node: ast.AST) -> int:
        if isinstance(node, ast.Expression):
            return _eval(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return int(node.value)
        if isinstance(node, ast.Name):
            if node.id not in symbols:
                raise EvalError(f"unknown symbol '{node.id}'")
            return symbols[node.id]
        if isinstance(node, ast.UnaryOp) and type(node.op) in ALLOWED_UNARY:
            return ALLOWED_UNARY[type(node.op)](_eval(node.operand))
        if isinstance(node, ast.BinOp) and type(node.op) in ALLOWED_BINOPS:
            left = _eval(node.left)
            right = _eval(node.right)
            if right == 0 and type(node.op) in (ast.Div, ast.FloorDiv, ast.Mod):
                raise EvalError("division by zero")
            return ALLOWED_BINOPS[type(node.op)](left, right)
        raise EvalError(f"unsupported expression node: {ast.dump(node, include_attributes=False)}")

    try:
        parsed = ast.parse(expr, mode="eval")
    except SyntaxError as exc:
        raise EvalError(str(exc)) from exc
    return int(_eval(parsed))


def strip_asm_comment(line: str) -> str:
    return line.split(";", 1)[0].strip()


def parse_kernel_symbols(path: Path) -> Dict[str, int]:
    symbols: Dict[str, int] = {}
    pattern = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s+equ\s+(.+)$")

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = strip_asm_comment(raw)
        if not line:
            continue
        m = pattern.match(line)
        if not m:
            continue
        name, expr = m.group(1), m.group(2).strip()
        if name in symbols:
            continue
        try:
            symbols[name] = safe_eval_expr(expr, symbols)
        except EvalError:
            # Ignore symbols requiring unavailable context; we only compare mapped keys.
            continue

    return symbols


def parse_python_assignments(path: Path) -> Dict[str, int]:
    symbols: Dict[str, int] = {}
    pattern = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$")

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or line.startswith("def ") or line.startswith("class "):
            continue
        m = pattern.match(line)
        if not m:
            continue
        name, expr = m.group(1), m.group(2).strip()
        if name in symbols:
            continue
        try:
            symbols[name] = safe_eval_expr(expr, symbols)
        except EvalError:
            continue

    return symbols


CPP_INT_SUFFIX = re.compile(r"\b(0[xX][0-9A-Fa-f]+|\d+)[uUlL]+\b")
CPP_DEFINE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+(.+)$", re.MULTILINE
)


def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def parse_c_defines(path: Path) -> Dict[str, int]:
    """Read the object-like `#define NAME value` lines out of a C source file.

    Only literal expressions over other defines are evaluated; anything else is
    skipped, because LM_CONSTANT_MAP above names the constants worth comparing
    and an unexpected one is not an error.
    """
    symbols: Dict[str, int] = {}
    for m in CPP_DEFINE.finditer(strip_c_comments(path.read_text(encoding="utf-8"))):
        name, expr = m.group(1), m.group(2).strip()
        if name in symbols:
            continue
        expr = CPP_INT_SUFFIX.sub(lambda suffix: suffix.group(1), expr)
        try:
            symbols[name] = safe_eval_expr(expr, symbols)
        except EvalError:
            continue
    return symbols


def check_three_way(
    mapping: Dict[str, tuple],
    kernel: Dict[str, int],
    populate: Dict[str, int],
    test_hbfs: Dict[str, int],
) -> list:
    errors = []
    for kernel_name, (populate_name, test_name) in mapping.items():
        kv = kernel.get(kernel_name)
        pv = populate.get(populate_name)
        tv = test_hbfs.get(test_name)

        if kv is None:
            errors.append(f"Missing kernel symbol: {kernel_name}")
            continue
        if pv is None:
            errors.append(f"Missing populate.py symbol: {populate_name}")
            continue
        if tv is None:
            errors.append(f"Missing tests/test_hbfs.py symbol: {test_name}")
            continue

        if kv != pv:
            errors.append(
                f"Mismatch {kernel_name} vs {populate_name}: kernel={kv}, populate={pv}"
            )
        if kv != tv:
            errors.append(
                f"Mismatch {kernel_name} vs {test_name}: kernel={kv}, test_hbfs={tv}"
            )
    return errors


def check_two_way(
    mapping: Dict[str, str],
    c_defines: Dict[str, int],
    populate: Dict[str, int],
) -> list:
    errors = []
    for c_name, populate_name in mapping.items():
        cv = c_defines.get(c_name)
        pv = populate.get(populate_name)

        if cv is None:
            errors.append(f"Missing define in boot/lm_hbfs.c: {c_name}")
            continue
        if pv is None:
            errors.append(f"Missing populate.py symbol: {populate_name}")
            continue
        if cv != pv:
            errors.append(
                f"Mismatch {c_name} in boot/lm_hbfs.c vs {populate_name} "
                f"in populate.py: kernel={cv}, populate={pv}"
            )
    return errors


def main() -> int:
    kernel = parse_kernel_symbols(KERNEL_PATH)
    populate = parse_python_assignments(POPULATE_PATH)
    test_hbfs = parse_python_assignments(TEST_HBFS_PATH)
    lm_hbfs = parse_c_defines(LM_HBFS_PATH)

    errors = check_three_way(CONSTANT_MAP, kernel, populate, test_hbfs)
    errors += check_two_way(LM_CONSTANT_MAP, lm_hbfs, populate)

    if errors:
        print("[validate-constants] FAIL")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("[validate-constants] PASS")
    for kernel_name, (populate_name, test_name) in CONSTANT_MAP.items():
        print(
            f"  {kernel_name}: {kernel[kernel_name]} "
            f"(populate:{populate_name}, test:{test_name})"
        )
    for c_name, populate_name in LM_CONSTANT_MAP.items():
        print(f"  {c_name}: {lm_hbfs[c_name]} (lm_hbfs.c, populate:{populate_name})")

    return 0


if __name__ == "__main__":
    sys.exit(main())
