; fontsheet.asm - character sheet for the 5x7 user-space VBE font
;
; Draws every printable ASCII character plus mixed-case sentences straight from
; lib/font.inc, so a change to the font can be checked by eye. Run it as
; `fontsheet` from the Mellivora shell; press any key to return.

%include "syscalls.inc"
%include "lib/font.inc"

SCREEN_W equ 640
SCREEN_H equ 480

BG_COLOUR equ 0x00101418
FG_COLOUR equ 0x00E8F0F8
ACCENT    equ 0x0060C8FF
DIM       equ 0x008090A0

section .data
title       db "Mellivora 5x7 font", 0
subtitle    db "printable ASCII 0x20-0x7E from programs/lib/font.inc", 0

label_20    db "0x20-0x3F", 0
label_40    db "0x40-0x5F", 0
label_60    db "0x60-0x7E", 0

row_20      db ` !"#$%&'()*+,-./0123456789:;<=>?@`, 0
row_40      db '@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_', 0
row_60      db "`abcdefghijklmnopqrstuvwxyz{|}~", 0

mixed_head  db "mixed case:", 0
pangram1    db "The quick brown fox jumps", 0
pangram2    db "over the lazy dog. 0123456789", 0

footer      db "Press any key to return to the shell.", 0

section .text
start:
        ; Enter 640x480x32 and take the shadow buffer address from the kernel.
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCREEN_W
        mov edx, SCREEN_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .exit

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr], eax
        mov [fb_width], ebx
        mov [fb_height], ecx
        shl ebx, 2
        mov [fb_pitch], ebx

        mov edx, BG_COLOUR
        call vbe_clear_screen

        mov ebx, 16
        mov ecx, 16
        mov edx, title
        mov esi, ACCENT
        mov eax, 2
        call vbe_draw_str

        mov ebx, 16
        mov ecx, 42
        mov edx, subtitle
        mov esi, DIM
        mov eax, 1
        call vbe_draw_str

        ; Row 0x20-0x3F
        mov ebx, 16
        mov ecx, 64
        mov edx, label_20
        mov esi, DIM
        mov eax, 1
        call vbe_draw_str

        mov ebx, 16
        mov ecx, 76
        mov edx, row_20
        mov esi, FG_COLOUR
        mov eax, 2
        call vbe_draw_str

        ; Row 0x40-0x5F
        mov ebx, 16
        mov ecx, 110
        mov edx, label_40
        mov esi, DIM
        mov eax, 1
        call vbe_draw_str

        mov ebx, 16
        mov ecx, 122
        mov edx, row_40
        mov esi, FG_COLOUR
        mov eax, 2
        call vbe_draw_str

        ; Row 0x60-0x7E (lowercase lives here)
        mov ebx, 16
        mov ecx, 156
        mov edx, label_60
        mov esi, DIM
        mov eax, 1
        call vbe_draw_str

        mov ebx, 16
        mov ecx, 168
        mov edx, row_60
        mov esi, FG_COLOUR
        mov eax, 2
        call vbe_draw_str

        ; Mixed-case prose, which needs the lowercase glyphs to read correctly.
        mov ebx, 16
        mov ecx, 214
        mov edx, mixed_head
        mov esi, DIM
        mov eax, 1
        call vbe_draw_str

        mov ebx, 16
        mov ecx, 230
        mov edx, pangram1
        mov esi, FG_COLOUR
        mov eax, 2
        call vbe_draw_str

        mov ebx, 16
        mov ecx, 252
        mov edx, pangram2
        mov esi, FG_COLOUR
        mov eax, 2
        call vbe_draw_str

        mov ebx, 16
        mov ecx, 300
        mov edx, footer
        mov esi, DIM
        mov eax, 1
        call vbe_draw_str

        ; Flip shadow buffer to the screen and wait for a key.
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        mov eax, SYS_READ_KEY
        int 0x80

        ; Restore text mode before leaving, as the other VBE programs do.
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80

.exit:
        xor ebx, ebx
        xor eax, eax
        int 0x80
