; fonttest.asm - render the kernel's VBE font as a character sheet
;
; SYS_FRAMEBUF/3 draws a string with the VGA font captured by the kernel at
; boot (vbe_save_vga_font). This program prints the printable ASCII range in
; rows so the glyphs can be compared against the expected characters; it also
; doubles as a quick "does the graphics path work" check.
;
; Press any key to quit.

%include "syscalls.inc"

SCREEN_W        equ 640
SCREEN_H        equ 480
COLS            equ (SCREEN_W / 8)      ; 80 characters
ROWS            equ (SCREEN_H / 16)     ; 30 text rows

start:
        ; Init VBE 640x480x32
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCREEN_W
        mov edx, SCREEN_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .exit

        call clear_screen

        ; Row 0..: "!\"#$%&'()*+,-./0123456789:;<=>?@"
        ; Row 1..: "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
        ; Row 2..: "abcdefghijklmnopqrstuvwxyz{|}~"
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        xor ecx, ecx            ; x
        mov edx, 16             ; y
        mov esi, line1
        mov edi, 0x00FF00
        int 0x80

        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        xor ecx, ecx
        mov edx, 48
        mov esi, line2
        mov edi, 0xFFFF00
        int 0x80

        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        xor ecx, ecx
        mov edx, 80
        mov esi, line3
        mov edi, 0x00FFFF
        int 0x80

        ; Blit the shadow buffer to the screen, then wait for a key
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

.wait:
        mov eax, SYS_READ_KEY
        int 0x80

.exit:
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;---------------------------------------
; clear_screen - fill the framebuffer with a dark colour
;---------------------------------------
clear_screen:
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx            ; 0 = get info -> EAX = shadow buffer
        int 0x80
        test eax, eax
        jz .cs_done
        mov edi, eax
        mov ecx, SCREEN_W * SCREEN_H     ; dwords: one per pixel (32bpp)
        mov eax, 0x101018
        cld
        rep stosd
.cs_done:
        ret

line1:  db `!"#$%&'()*+,-./0123456789:;<=>?@`, 0
line2:  db `ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_\``, 0
line3:  db `abcdefghijklmnopqrstuvwxyz{|}~`, 0
