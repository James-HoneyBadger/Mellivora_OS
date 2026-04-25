;=======================================================================
; spritetest.asm — Sprite library smoke test
; Draws four sprites using each sprite_draw variant, then exits on key.
;=======================================================================
%include "syscalls.inc"
%include "sprite.inc"

SCREEN_W        equ 640
SCREEN_H        equ 480

start:
        ; Enter 640x480x32 VBE mode
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCREEN_W
        mov edx, SCREEN_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .exit

        ; Get shadow buffer address
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr], eax
        mov dword [fb_pitch], SCREEN_W * 4

        ; Clear to dark grey
        mov ebx, 0
        mov ecx, 0
        mov edx, SCREEN_W
        mov esi, SCREEN_H
        mov edi, 0x202020
        call fb_fill_rect

        ; sprite_draw (alpha transparency) — top-left
        mov ebx, 40
        mov ecx, 40
        mov esi, spr_face
        call sprite_draw

        ; sprite_draw_opaque (no alpha) — top-right area
        mov ebx, 200
        mov ecx, 40
        mov esi, spr_face
        call sprite_draw_opaque

        ; sprite_draw_key (color-key: 0x00FF00FF = magenta) — middle
        mov ebx, 40
        mov ecx, 140
        mov esi, spr_keyed
        mov edi, 0x00FF00FF     ; magenta = transparent
        call sprite_draw_key

        ; sprite_draw_scaled 2x — right side
        mov ebx, 200
        mov ecx, 140
        mov esi, spr_face
        mov edx, 1              ; scale_shift=1 → 2x
        call sprite_draw_scaled

        ; sprite_draw_scaled 4x — lower area
        mov ebx, 40
        mov ecx, 260
        mov esi, spr_face
        mov edx, 2              ; scale_shift=2 → 4x
        call sprite_draw_scaled

        ; Present frame
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; Wait for any keypress
        mov eax, SYS_GETCHAR
        int 0x80

.exit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; fb_fill_rect — EBX=x ECX=y EDX=w ESI=h EDI=color
;=======================================================================
fb_fill_rect:
        pushad
        ; save color (EDI) — EDI gets clobbered by stosd
        mov [.clr], edi
        ; compute start address
        mov eax, [fb_pitch]
        imul eax, ecx           ; y * pitch
        add eax, [fb_addr]
        lea eax, [eax + ebx*4]  ; + x*4
        mov [.rp], eax
.fr_row:
        test esi, esi
        jz .fr_done
        mov edi, [.rp]
        mov ecx, edx
        mov eax, [.clr]
        cld
        rep stosd
        mov eax, [fb_pitch]
        add [.rp], eax
        dec esi
        jmp .fr_row
.fr_done:
        popad
        ret
.clr: dd 0
.rp:  dd 0

;=======================================================================
; Sprite data
;=======================================================================

; A simple 16x16 smiley face (alpha=FF=opaque, alpha=00=transparent)
SPRITE_BEGIN spr_face, 16, 16
        ; row 0
        dd 0x00000000, 0x00000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0x00000000, 0x00000000
        ; row 1
        dd 0x00000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0x00000000
        ; row 2
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 3
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFF000000, 0xFF000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFF000000, 0xFF000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 4
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFF000000, 0xFF000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFF000000, 0xFF000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 5
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 6
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 7
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 8 — start of smile
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFF000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFF000000, 0xFFFFDD44, 0xFFFFDD44
        ; row 9
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFF000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFF000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 10
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 11
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 12
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 13
        dd 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44
        ; row 14
        dd 0x00000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0x00000000
        ; row 15
        dd 0x00000000, 0x00000000, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0xFFFFDD44, 0x00000000, 0x00000000
SPRITE_END

; A 16x16 sprite with magenta color-key for use with sprite_draw_key
SPRITE_BEGIN spr_keyed, 16, 16
        ; row 0 — magenta border = transparent
        dd 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF
        ; row 1
        dd 0x00FF00FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0x00FF00FF
        ; rows 2-13 — blue rectangle with magenta corners
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        dd 0x00FF00FF, 0xFF4488FF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF88CCFF, 0xFF4488FF, 0x00FF00FF
        ; row 14
        dd 0x00FF00FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0xFF4488FF, 0x00FF00FF
        ; row 15
        dd 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF
SPRITE_END

;=======================================================================
; BSS
;=======================================================================
section .bss
fb_addr:    resd 1
fb_pitch:   resd 1
