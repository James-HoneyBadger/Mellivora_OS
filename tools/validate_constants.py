#!/usr/bin/env python3
"""Validate shared storage/layout constants across kernel and tooling.

This script prevents silent drift between:
- kernel.asm
- populate.py
- tests/test_hbfs.py
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


def main() -> int:
    kernel = parse_kernel_symbols(KERNEL_PATH)
    populate = parse_python_assignments(POPULATE_PATH)
    test_hbfs = parse_python_assignments(TEST_HBFS_PATH)

    errors = []

    for kernel_name, (populate_name, test_name) in CONSTANT_MAP.items():
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

    return 0


if __name__ == "__main__":
    sys.exit(main())
