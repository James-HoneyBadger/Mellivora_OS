;=======================================================================
; utf8.inc - UTF-8 at the console, and a Unicode -> glyph map
;
; The ABI is UTF-8 everywhere and UTF-16 only at the boundary a program from
; the parity target sees (EXEC.md section 5.1); this file is the kernel half of
; that: it decodes what a string actually contains, and picks the glyph the
; hardware font can draw for it.
;
; Both consoles this tree has are byte cells:
;
;   * VGA text mode uses the adapter's own font, which is CP437 - 256 glyphs
;     including Latin-1 letters, Greek, box drawing, arrows and the arithmetic
;     signs;
;   * the desktop draws an 8x16 bitmap font, loaded from the same ROM at boot
;     (gui_font_load) so it has the same 256 glyphs instead of 95.
;
; So "Unicode support" here is honest and bounded: every code point the map
; below names renders, and anything else renders as '?'. A code point outside
; CP437 needs a scalable font, which is a later wave (MEDIA.md).
;
; The map is the interesting part: it is what makes a UTF-8 string of accented
; Latin, Greek or box-drawing characters render correctly instead of as
; mojibake, without touching the font at all.
;=======================================================================

U_FFFD          equ 0xFFFD              ; the replacement character
GLYPH_UNKNOWN   equ '?'

;-----------------------------------------------------------------------
; utf8_decode - decode one code point
;   In : ESI = pointer into a UTF-8 string
;   Out: EAX = code point, ESI advanced past it
;        CF clear on a well-formed sequence
;        CF set on a malformed one - EAX is U+FFFD and ESI advanced by one
;             byte, so a decoder cannot loop or run off the end
;
; Valid UTF-8 is 1 to 4 bytes, and every continuation byte starts with 10.
; Overlong forms, surrogates and code points past U+10FFFF are malformed: a
; decoder that accepted them would let two different byte strings mean one
; character, which is the ambiguity UTF-8 exists to remove.
;-----------------------------------------------------------------------
utf8_decode:
        push ebx
        push ecx
        push edx
        movzx eax, byte [esi]
        test al, al
        jz .ud_out                     ; NUL: EAX = 0, nothing consumed
        test al, 0x80
        jz .ud_one                     ; ASCII

        ; 110xxxxx: two bytes, U+0080..U+07FF
        mov ecx, 2
        and eax, 0xE0
        cmp eax, 0xC0
        je .ud_two
        ; 1110xxxx: three bytes, U+0800..U+FFFF
        movzx eax, byte [esi]
        and eax, 0xF0
        cmp eax, 0xE0
        je .ud_three
        ; 11110xxx: four bytes, U+10000..U+10FFFF
        movzx eax, byte [esi]
        and eax, 0xF8
        cmp eax, 0xF0
        je .ud_four
        jmp .ud_bad

.ud_one:
        inc esi
        clc
        jmp .ud_out

.ud_two:
        movzx eax, byte [esi]
        and eax, 0x1F
        call .ud_cont
        jc .ud_bad
        cmp eax, 0x80                  ; overlong
        jb .ud_bad
        clc
        jmp .ud_out

.ud_three:
        movzx eax, byte [esi]
        and eax, 0x0F
        call .ud_cont
        jc .ud_bad
        call .ud_cont
        jc .ud_bad
        cmp eax, 0x800                 ; overlong
        jb .ud_bad
        cmp eax, 0xD800                ; a surrogate is not a character
        jb .ud_three_ok
        cmp eax, 0xDFFF
        jbe .ud_bad
.ud_three_ok:
        clc
        jmp .ud_out

.ud_four:
        movzx eax, byte [esi]
        and eax, 0x07
        call .ud_cont
        jc .ud_bad
        call .ud_cont
        jc .ud_bad
        call .ud_cont
        jc .ud_bad
        cmp eax, 0x10000               ; overlong
        jb .ud_bad
        cmp eax, 0x10FFFF              ; past the last code point
        ja .ud_bad
        clc
        jmp .ud_out

        ; EAX = value so far, ESI = last byte read. Consumes one continuation.
.ud_cont:
        inc esi
        movzx ebx, byte [esi]
        mov ecx, ebx
        and ecx, 0xC0
        cmp ecx, 0x80
        jne .udc_bad
        and ebx, 0x3F
        shl eax, 6
        or eax, ebx
        clc
        ret
.udc_bad:
        stc
        ret

.ud_bad:
        mov eax, U_FFFD
        inc esi                        ; one byte consumed, so decoding moves on
        stc
.ud_out:
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; unicode_to_glyph - the byte the font can draw for a code point
;   In : EAX = code point
;   Out: EAX = CP437 glyph byte (0..255)
;
; Anything the font cannot draw becomes '?'. Latin-1 and Latin Extended-A map
; to the CP437 letters where one exists; the rest of the table is the symbols
; a terminal program actually uses - arrows, box drawing, arithmetic.
;-----------------------------------------------------------------------
unicode_to_glyph:
        cmp eax, 0x7E
        ja .ug_high
        cmp eax, 0x20
        jb .ug_unknown                 ; control characters are not glyphs
        ret                            ; ASCII is itself
.ug_high:
        ; Code points CP437 also has, at the same byte value.
        cmp eax, 0xA0
        jb .ug_lookup
        cmp eax, 0xFF
        ja .ug_lookup
        ; Latin-1: the letters CP437 shares with ISO 8859-1 keep their value.
        mov ecx, utf8_latin1_shared
.ug_share:
        movzx edx, byte [ecx]
        test edx, edx
        jz .ug_lookup
        cmp eax, edx
        je .ug_identity
        inc ecx
        jmp .ug_share
.ug_identity:
        ret
.ug_lookup:
        mov ecx, unicode_glyph_map
.ug_loop:
        mov edx, [ecx]
        test edx, edx
        jz .ug_unknown
        cmp eax, edx
        je .ug_found
        add ecx, 8
        jmp .ug_loop
.ug_found:
        movzx eax, byte [ecx + 4]
        ret
.ug_unknown:
        mov eax, GLYPH_UNKNOWN
        ret

;-----------------------------------------------------------------------
; font_selftest - every code point the tree's own text uses has a glyph
;   Prints FONT_SELFTEST_OK or FONT_SELFTEST_FAIL on COM1. Runs once at boot.
;
; unicode_to_glyph is the whole of "does this character draw", and the tree had
; no check on it: a code point missing from the table renders as '?', which no
; other test can see because the bytes on the wire are still correct UTF-8. An
; em dash written in a banner was the one that showed it.
;-----------------------------------------------------------------------
font_selftest:
        push esi
        mov esi, font_selftest_points
.fst_loop:
        lodsd
        test eax, eax
        jz .fst_ok
        call unicode_to_glyph
        cmp eax, GLYPH_UNKNOWN
        je .fst_fail
        jmp .fst_loop
.fst_ok:
        mov esi, font_st_ok
        call serial_print
        pop esi
        ret
.fst_fail:
        mov esi, font_st_fail
        call serial_print
        pop esi
        ret

font_st_ok:     db "FONT_SELFTEST_OK", 10, 0
font_st_fail:   db "FONT_SELFTEST_FAIL", 10, 0

; The characters this system's own strings contain, plus the neighbours a
; terminal program reaches for: accents, Greek, arrows, box drawing, the
; typographic punctuation and the arithmetic signs.
font_selftest_points:
        dd 0x00E9, 0x00FC, 0x00E4, 0x00B7
        dd 0x03B1, 0x03C0, 0x03A3, 0x03A9
        dd 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026
        dd 0x2190, 0x2192, 0x2191, 0x2193
        dd 0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x2588
        dd 0x2260, 0x2264, 0x2265, 0x20AC
        dd 0

;-----------------------------------------------------------------------
; utf8_print - print a UTF-8 string
;   In : ESI = NUL-terminated UTF-8
;
; Every call site that already prints a C string keeps working: ASCII decodes
; one byte at a time, which is what it did before.
;-----------------------------------------------------------------------
utf8_print:
        push eax
        push ebx
        push esi
.ufp_loop:
        lodsb
        test al, al
        jz .ufp_done
        test al, 0x80
        jz .ufp_ascii
        dec esi
        movzx ebx, byte [esi]          ; the byte itself, for the CP437 fallback
        call utf8_decode               ; EAX = code point, ESI advanced
        jc .ufp_cp437
        call unicode_to_glyph
        call vga_putchar
        jmp .ufp_loop
.ufp_cp437:
        ; Not UTF-8. The console's own font is 256 glyphs wide, and this tree
        ; printed raw CP437 for years before it spoke UTF-8 - a line drawn with
        ; 0xC4 is a line, not a mistake. One high byte keeps its glyph; a
        ; string that really is UTF-8 never reaches this path.
        mov eax, ebx
        call vga_putchar
        jmp .ufp_loop
.ufp_ascii:
        call vga_putchar
        jmp .ufp_loop
.ufp_done:
        pop esi
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; utf16_print - print a UTF-16LE string, the ABI boundary's encoding
;   In : ESI = NUL-terminated UTF-16LE
;
; A surrogate pair is combined into its code point, as a terminal that only
; knows BMP glyphs still has to consume both halves.
;-----------------------------------------------------------------------
utf16_print:
        push eax
        push ebx
        push esi
.uwp_loop:
        movzx eax, word [esi]
        test eax, eax
        jz .uwp_done
        add esi, 2
        cmp eax, 0xD800
        jb .uwp_have
        cmp eax, 0xDBFF
        ja .uwp_have
        ; High surrogate: combine with the low half if it is there.
        movzx ebx, word [esi]
        cmp ebx, 0xDC00
        jb .uwp_lone
        cmp ebx, 0xDFFF
        ja .uwp_lone
        add esi, 2
        sub eax, 0xD800
        shl eax, 10
        sub ebx, 0xDC00
        add eax, ebx
        add eax, 0x10000
        jmp .uwp_have
.uwp_lone:
        mov eax, U_FFFD
.uwp_have:
        call unicode_to_glyph
        call vga_putchar
        jmp .uwp_loop
.uwp_done:
        pop esi
        pop ebx
        pop eax
        ret

; Latin-1 code points CP437 already has at the same byte, so no mapping is
; needed for them. Everything else goes through the table below.
utf8_latin1_shared:
        db 0xA0, 0xA1, 0xA2, 0xA3, 0xA5, 0xA7, 0xAA, 0xAB
        db 0xB0, 0xB1, 0xB2, 0xB5, 0xB6, 0xB7, 0xBA, 0xBB
        db 0xBC, 0xBD, 0xBF, 0xC4, 0xC5, 0xC6, 0xC7, 0xC9
        db 0xD1, 0xD6, 0xDC, 0xDF, 0xE1, 0xE4, 0xE5, 0xE6
        db 0xE7, 0xE9, 0xED, 0xF1, 0xF3, 0xF6, 0xFA, 0xFC
        db 0, 0

; Code point, glyph. Packed as two dwords so a scan is one compare and one
; load; the list ends with a zero.
unicode_glyph_map:
        dd 0x0192, 0x9F                ; ƒ
        dd 0x0393, 0xE2                ; Γ
        dd 0x0398, 0xE9                ; Θ
        dd 0x03A3, 0xE4                ; Σ
        dd 0x03A6, 0xE8                ; Φ
        dd 0x03A9, 0xEA                ; Ω
        dd 0x03B1, 0xE1                ; α
        dd 0x03B4, 0xEB                ; δ
        dd 0x03B5, 0xF5                ; ε  (CP437 has no ε: use the closest)
        dd 0x03C0, 0xE3                ; π
        dd 0x03C3, 0xE5                ; σ
        dd 0x03C4, 0xF5                ; τ  (as above)
        dd 0x03C6, 0xED                ; φ
        dd 0x2010, 0x2D                ; hyphen
        dd 0x2011, 0x2D                ; non-breaking hyphen
        dd 0x2012, 0xC4                ; figure dash: the box line is the
        dd 0x2013, 0xC4                ;   closest full-width dash CP437 has
        dd 0x2014, 0xC4                ; em dash
        dd 0x2015, 0xC4                ; horizontal bar
        dd 0x2018, 0x91                ; '
        dd 0x2019, 0x92                ; '
        dd 0x201C, 0x93                ; "
        dd 0x201D, 0x94                ; "
        dd 0x2022, 0x07                ; •
        dd 0x2026, 0x85                ; …
        dd 0x2030, 0x89                ; ‰
        dd 0x2039, 0x8B                ; ‹
        dd 0x203A, 0x9B                ; ›
        dd 0x20AC, 0x9D                ; €  (CP437 0x9D is the cent sign's slot)
        dd 0x2190, 0x1B                ; ←
        dd 0x2191, 0x18                ; ↑
        dd 0x2192, 0x1A                ; →
        dd 0x2193, 0x19                ; ↓
        dd 0x2194, 0x1D                ; ↔
        dd 0x2195, 0x12                ; ↕
        dd 0x21A8, 0x17                ; ↨
        dd 0x2212, 0x2D                ; −  (minus sign, drawn as a hyphen)
        dd 0x2219, 0x07                ; ∙
        dd 0x221A, 0xFB                ; √
        dd 0x221E, 0xEC                ; ∞
        dd 0x2229, 0xEF                ; ∩
        dd 0x2248, 0xF7                ; ≈
        dd 0x2260, 0xF0                ; ≠
        dd 0x2261, 0xF0                ; ≡  (as above)
        dd 0x2264, 0xF3                ; ≤
        dd 0x2265, 0xF2                ; ≥
        dd 0x2302, 0x7F                ; ⌂
        dd 0x2500, 0xC4                ; ─
        dd 0x2502, 0xB3                ; │
        dd 0x250C, 0xDA                ; ┌
        dd 0x2510, 0xBF                ; ┐
        dd 0x2514, 0xC0                ; └
        dd 0x2518, 0xD9                ; ┘
        dd 0x251C, 0xC3                ; ├
        dd 0x2524, 0xB4                ; ┤
        dd 0x252C, 0xC2                ; ┬
        dd 0x2534, 0xC1                ; ┴
        dd 0x253C, 0xC5                ; ┼
        dd 0x2550, 0xCD                ; ═
        dd 0x2551, 0xBA                ; ║
        dd 0x2554, 0xC9                ; ╔
        dd 0x2557, 0xBB                ; ╗
        dd 0x255A, 0xC8                ; ╚
        dd 0x255D, 0xBC                ; ╝
        dd 0x2560, 0xCC                ; ╠
        dd 0x2563, 0xB9                ; ╣
        dd 0x2566, 0xCB                ; ╦
        dd 0x2569, 0xCA                ; ╩
        dd 0x256C, 0xCE                ; ╬
        dd 0x2580, 0xDF                ; ▀
        dd 0x2584, 0xDC                ; ▄
        dd 0x2588, 0xDB                ; █
        dd 0x258C, 0xDD                ; ▌
        dd 0x2590, 0xDE                ; ▐
        dd 0x2591, 0xB0                ; ░
        dd 0x2592, 0xB1                ; ▒
        dd 0x2593, 0xB2                ; ▓
        dd 0x25A0, 0xFE                ; ■
        dd 0x25B2, 0x1E                ; ▲
        dd 0x25BA, 0x10                ; ►
        dd 0x25BC, 0x1F                ; ▼
        dd 0x25C4, 0x11                ; ◄
        dd 0x25CB, 0x09                ; ○
        dd 0x25D8, 0x08                ; ◘
        dd 0x25D9, 0x0A                ; ◙
        dd 0x263A, 0x01                ; ☺
        dd 0x263B, 0x02                ; ☻
        dd 0x263C, 0x0F                ; ☼
        dd 0x2640, 0x0C                ; ♀
        dd 0x2642, 0x0B                ; ♂
        dd 0x2660, 0x06                ; ♠
        dd 0x2663, 0x05                ; ♣
        dd 0x2665, 0x03                ; ♥
        dd 0x2666, 0x04                ; ♦
        dd 0x266A, 0x0D                ; ♪
        dd 0x266B, 0x0E                ; ♫
        dd 0, 0
