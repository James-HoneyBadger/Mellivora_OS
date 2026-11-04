#!/usr/bin/env python3
"""Generate the shell's icon glyph table (kernel/glyphs.inc).

Every icon in Burrows - the desktop tile, the title-bar mark, the taskbar chip
and the Start-menu row - is one 16x16 glyph from this table, scaled by the
compositor at draw time (2x in a 40px tile, 1x in a 16px mark). Drawing glyphs
from rasterised data rather than from hand-placed rectangles is what keeps the
four places an application appears identical, and it makes a new glyph a
drawing job rather than an assembly job.

The output is a flat table of 32 bytes per glyph: two bytes per row, most
significant bit first, row 0 at the top.

Usage:
    python3 tools/gen_glyphs.py            # write kernel/glyphs.inc
    python3 tools/gen_glyphs.py --check    # fail if the file is stale
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "kernel" / "glyphs.inc"

SIZE = 16


class Canvas:
    """A 16x16 one-bit canvas with the handful of primitives the icons need."""

    def __init__(self) -> None:
        self.px = [[0] * SIZE for _ in range(SIZE)]

    def dot(self, x: int, y: int) -> None:
        if 0 <= x < SIZE and 0 <= y < SIZE:
            self.px[y][x] = 1

    def fill(self, x: int, y: int, w: int, h: int) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.dot(xx, yy)

    def rect(self, x: int, y: int, w: int, h: int) -> None:
        self.fill(x, y, w, 1)
        self.fill(x, y + h - 1, w, 1)
        self.fill(x, y, 1, h)
        self.fill(x + w - 1, y, 1, h)

    def round_rect(self, x: int, y: int, w: int, h: int, r: int = 2) -> None:
        for row in range(h):
            inset = 0
            if row < r:
                inset = r - 1 - row
            elif row >= h - r:
                inset = r - (h - row)
            self.fill(x + inset, y + row, w - 2 * inset, 1)

    def outline_round(self, x: int, y: int, w: int, h: int, r: int = 2) -> None:
        body = Canvas()
        body.round_rect(x, y, w, h, r)
        inner = Canvas()
        inner.round_rect(x + 1, y + 1, w - 2, h - 2, max(0, r - 1))
        for yy in range(SIZE):
            for xx in range(SIZE):
                if body.px[yy][xx] and not inner.px[yy][xx]:
                    self.px[yy][xx] = 1

    def line(self, x0: int, y0: int, x1: int, y1: int) -> None:
        dx = abs(x1 - x0)
        dy = -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.dot(x0, y0)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def circle(self, cx: int, cy: int, r: int) -> None:
        # A midpoint circle, drawn as one-pixel dots.
        x, y, d = r, 0, 1 - r
        while x >= y:
            for sx, sy in ((x, y), (y, x), (-y, x), (-x, y),
                           (-x, -y), (-y, -x), (y, -x), (x, -y)):
                self.dot(cx + sx, cy + sy)
            y += 1
            if d < 0:
                d += 2 * y + 1
            else:
                x -= 1
                d += 2 * (y - x) + 1

    def rows(self) -> list[int]:
        out = []
        for row in self.px:
            value = 0
            for x, on in enumerate(row):
                if on:
                    value |= 1 << (15 - x)
            out.append(value)
        return out


def terminal() -> Canvas:
    c = Canvas()
    c.line(4, 4, 7, 7)
    c.line(7, 7, 4, 10)
    c.fill(9, 10, 5, 2)
    return c


def folder() -> Canvas:
    c = Canvas()
    c.fill(2, 4, 6, 2)
    c.outline_round(2, 5, 12, 9, 2)
    return c


def listing() -> Canvas:
    c = Canvas()
    for row in (4, 7, 10):
        c.fill(2, row, 2, 2)
        c.fill(6, row, 8, 1)
    return c


def document() -> Canvas:
    c = Canvas()
    c.rect(4, 2, 8, 12)
    c.line(9, 2, 11, 4)
    c.fill(6, 6, 4, 1)
    c.fill(6, 8, 4, 1)
    c.fill(6, 10, 3, 1)
    return c


def calculator() -> Canvas:
    c = Canvas()
    c.outline_round(2, 2, 12, 12, 2)
    c.fill(4, 4, 8, 3)
    for row in (9, 12):
        for col in (4, 7, 10):
            c.fill(col, row, 2, 1)
    return c


def palette() -> Canvas:
    c = Canvas()
    c.outline_round(2, 3, 12, 10, 3)
    c.fill(4, 5, 2, 2)
    c.fill(8, 5, 2, 2)
    c.fill(6, 9, 4, 2)
    return c


def clock() -> Canvas:
    c = Canvas()
    c.circle(8, 8, 6)
    c.fill(8, 5, 1, 4)
    c.fill(8, 8, 4, 1)
    return c


def pulse() -> Canvas:
    c = Canvas()
    c.outline_round(1, 4, 14, 9, 2)
    c.fill(3, 8, 2, 1)
    c.line(5, 8, 7, 5)
    c.line(7, 5, 9, 11)
    c.line(9, 11, 11, 8)
    c.fill(11, 8, 2, 1)
    return c


def apps() -> Canvas:
    c = Canvas()
    c.fill(2, 2, 5, 5)
    c.outline_round(9, 2, 5, 5, 1)
    c.outline_round(2, 9, 5, 5, 1)
    c.fill(9, 9, 5, 5)
    return c


def sliders() -> Canvas:
    c = Canvas()
    for y, knob in ((4, 9), (8, 5), (12, 11)):
        c.fill(2, y, 12, 1)
        c.fill(knob, y - 1, 2, 3)
    return c


def info() -> Canvas:
    c = Canvas()
    c.circle(8, 8, 6)
    c.fill(7, 5, 2, 2)
    c.fill(7, 8, 2, 4)
    return c


def exit_door() -> Canvas:
    c = Canvas()
    c.rect(4, 2, 8, 12)
    c.fill(9, 8, 2, 1)
    return c


def window() -> Canvas:
    c = Canvas()
    c.outline_round(1, 3, 14, 11, 2)
    c.fill(3, 5, 10, 1)
    return c


GLYPHS = [
    ("ICON_TERM", terminal),
    ("ICON_FOLDER", folder),
    ("ICON_LIST", listing),
    ("ICON_DOC", document),
    ("ICON_CALC", calculator),
    ("ICON_PAINT", palette),
    ("ICON_CLOCK", clock),
    ("ICON_PULSE", pulse),
    ("ICON_APPS", apps),
    ("ICON_SETTINGS", sliders),
    ("ICON_INFO", info),
    ("ICON_EXIT", exit_door),
    ("ICON_WINDOW", window),
]


def render() -> str:
    lines = [
        "; Generated by tools/gen_glyphs.py - do not edit.",
        "; 16x16 one-bit icons, two bytes per row, MSB first, scaled at draw time.",
        "",
        f"GLYPH_COUNT equ {len(GLYPHS)}",
        "",
        "gui_glyph_table:",
    ]
    for name, maker in GLYPHS:
        rows = maker().rows()
        lines.append(f"        ; {name}")
        for row in rows:
            lines.append(f"        dw 0x{row:04X}")
    lines.append("")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    text = render()
    if "--check" in argv:
        current = OUT.read_text(encoding="utf-8") if OUT.is_file() else ""
        if current != text:
            print("[glyphs] FAIL: kernel/glyphs.inc is stale; run 'make glyphs'")
            return 1
        print(f"[glyphs] PASS  {len(GLYPHS)} glyphs in kernel/glyphs.inc")
        return 0
    OUT.write_text(text, encoding="utf-8")
    print(f"[glyphs] wrote {len(GLYPHS)} glyphs to {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
