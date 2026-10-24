#!/usr/bin/env python3
"""Check the 5x7 bitmap font used by VBE programs.

`programs/lib/font.inc` indexes glyphs as `font_data + (char - FONT_FIRST) * 7`,
so a missing or extra glyph silently shifts every character after it. This check
pins the geometry and the coverage:

* FONT_FIRST is 0x20, FONT_LAST is 0x7E and FONT_H is 7;
* the table holds exactly one 7-byte glyph per printable ASCII character;
* every row uses 5 bits (nothing above 0x1F);
* representative glyphs are non-empty, and lowercase is distinct from uppercase.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "programs" / "lib" / "font.inc"

GLYPH_ROWS = 7
FIRST = 0x20
LAST = 0x7E
EXPECTED_GLYPHS = LAST - FIRST + 1


def read_equ(text: str, name: str, errors: list[str]) -> int | None:
    match = re.search(rf"^{name}\s+equ\s+(0x[0-9A-Fa-f]+|\d+)", text, re.M)
    if not match:
        errors.append(f"{name} is not defined")
        return None
    return int(match.group(1), 0)


def glyph_rows(text: str) -> list[list[int]]:
    """Return the font_data table as a list of glyphs, each a list of row bytes."""
    start = text.index("font_data:")
    body = text[start:]
    body = body.split("%endif", 1)[0]
    rows: list[int] = []
    for line in body.splitlines():
        stripped = line.split(";", 1)[0].strip()
        if not stripped.startswith("db "):
            continue
        for item in stripped[3:].split(","):
            item = item.strip()
            if item:
                rows.append(int(item, 0))
    return [rows[i:i + GLYPH_ROWS] for i in range(0, len(rows), GLYPH_ROWS)]


def main() -> int:
    errors: list[str] = []
    if not FONT.is_file():
        print(f"[check-font] FAIL: {FONT} is missing")
        return 1

    text = FONT.read_text(encoding="utf-8")

    first = read_equ(text, "FONT_FIRST", errors)
    last = read_equ(text, "FONT_LAST", errors)
    width = read_equ(text, "FONT_W", errors)
    height = read_equ(text, "FONT_H", errors)
    if first != FIRST:
        errors.append(f"FONT_FIRST is {first}, expected 0x20")
    if last != LAST:
        errors.append(f"FONT_LAST is {last}, expected 0x7E")
    if width != 5:
        errors.append(f"FONT_W is {width}, expected 5")
    if height != GLYPH_ROWS:
        errors.append(f"FONT_H is {height}, expected {GLYPH_ROWS}")

    glyphs = glyph_rows(text)
    if len(glyphs) != EXPECTED_GLYPHS:
        errors.append(f"font_data holds {len(glyphs)} glyphs, expected {EXPECTED_GLYPHS}")
    for index, glyph in enumerate(glyphs):
        if len(glyph) != GLYPH_ROWS:
            errors.append(f"glyph 0x{FIRST + index:02X} has {len(glyph)} rows, expected {GLYPH_ROWS}")
            continue
        for row_index, value in enumerate(glyph):
            if value > 0x1F:
                errors.append(
                    f"glyph 0x{FIRST + index:02X} row {row_index} is 0x{value:02X}; a 5x7 row must fit in 5 bits"
                )

    if len(glyphs) == EXPECTED_GLYPHS:
        def glyph(char: str) -> list[int]:
            return glyphs[ord(char) - FIRST]

        if any(glyph(" ")):
            errors.append("space must be blank")
        for char in "Aa0g_~!?":
            if not any(glyph(char)):
                errors.append(f"glyph for {char!r} is blank")
        if glyph("a") == glyph("A"):
            errors.append("lowercase 'a' is identical to 'A' — lowercase glyphs are missing")
        if glyph("g") == glyph("q"):
            errors.append("'g' and 'q' are identical — descenders were probably copied")

    if errors:
        print("[check-font] FAIL")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("[check-font] PASS")
    print(f"  glyphs: {len(glyphs)} (0x{FIRST:02X}-0x{LAST:02X}), {width}x{height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
