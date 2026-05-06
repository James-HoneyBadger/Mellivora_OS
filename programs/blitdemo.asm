; blitdemo.asm  —  Sprite blit animation demo for Mellivora OS
;
; Demonstrates:
;   Direct-write sprite blitting with color-key transparency
;   Animated bouncing 16x16 diamond sprite on a 640x480 framebuffer.
;   Press any key to exit.

%include "syscalls.inc"

SPR_W   equ 16
SPR_H   equ 16
SCR_W   equ 640
SCR_H   equ 480
PITCH   equ SCR_W * 4
MAX_X   equ SCR_W - SPR_W      ; 624
MAX_Y   equ SCR_H - SPR_H      ; 464

; Color key: pixels with this value are transparent
COLORKEY equ 0x00000000

; Sprite pixel colors
SPR_T  equ 0x00000000           ; transparent (color key)
SPR_Y  equ 0x00FFFF00           ; yellow

start:
        ; ---- Set VBE mode 640x480x32 ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je  .novbe

        ; ---- Get shadow buffer ----
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [bl_shadow], eax

        ; ---- Initial sprite state ----
        mov dword [bl_x],  100
        mov dword [bl_y],  100
        mov dword [bl_dx],   4
        mov dword [bl_dy],   3

.frame:
        ; Non-blocking key check
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        ; ---- Clear shadow to dark navy ----
        mov edi, [bl_shadow]
        mov ecx, SCR_W * SCR_H
        mov eax, 0x00001830
        rep stosd

        ; ---- Draw sprite directly into shadow buffer ----
        ; For each row/col in the 16x16 sprite, write non-transparent
        ; pixels at (bl_x + col, bl_y + row) in the shadow buffer.
        xor esi, esi            ; row counter
.spr_row:
        cmp esi, SPR_H
        jge .spr_done
        ; dest_y = bl_y + esi; skip row if off-screen
        mov eax, [bl_y]
        add eax, esi
        cmp eax, SCR_H
        jge .spr_next_row
        cmp eax, 0
        jl  .spr_next_row
        ; row_base = [bl_shadow] + dest_y * PITCH
        push esi                ; save row counter (imul clobbers nothing, but be safe)
        imul eax, PITCH
        add eax, [bl_shadow]
        mov [bl_row_base], eax
        pop esi
        xor edi, edi            ; col counter
.spr_col:
        cmp edi, SPR_W
        jge .spr_next_row
        ; Load sprite pixel; skip transparent
        push edi
        mov eax, esi
        imul eax, SPR_W
        add eax, edi
        mov ecx, [spr_data + eax * 4]
        pop edi
        cmp ecx, COLORKEY
        je  .spr_next_col
        ; dest_x = bl_x + edi; skip if off-screen
        mov eax, [bl_x]
        add eax, edi
        cmp eax, 0
        jl  .spr_next_col
        cmp eax, SCR_W
        jge .spr_next_col
        ; write pixel to shadow: [row_base + dest_x * 4] = color
        shl eax, 2
        add eax, [bl_row_base]
        mov [eax], ecx
.spr_next_col:
        inc edi
        jmp .spr_col
.spr_next_row:
        inc esi
        jmp .spr_row
.spr_done:

        ; ---- Title text ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 8              ; x
        mov edx, 8              ; y
        mov esi, bl_title
        mov edi, 0x00FFFFFF
        int 0x80

        ; ---- Present: flip shadow -> LFB ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; ---- Delay (~16 ms) ----
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80

        ; ---- Update X position ----
        mov eax, [bl_x]
        mov ebx, [bl_dx]
        add eax, ebx
        ; Clamp low
        cmp eax, 0
        jge .x_lo_ok
        xor eax, eax
        neg dword [bl_dx]
        jmp .x_store
.x_lo_ok:
        ; Clamp high
        cmp eax, MAX_X
        jle .x_store
        mov eax, MAX_X
        neg dword [bl_dx]
.x_store:
        mov [bl_x], eax

        ; ---- Update Y position ----
        mov eax, [bl_y]
        mov ebx, [bl_dy]
        add eax, ebx
        cmp eax, 0
        jge .y_lo_ok
        xor eax, eax
        neg dword [bl_dy]
        jmp .y_store
.y_lo_ok:
        cmp eax, MAX_Y
        jle .y_store
        mov eax, MAX_Y
        neg dword [bl_dy]
.y_store:
        mov [bl_y], eax

        jmp .frame

.exit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
.novbe:
        mov eax, SYS_PRINT
        mov ebx, bl_msg_exit
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ---- Variables ----
bl_shadow:   dd 0
bl_row_base: dd 0
bl_x:        dd 0
bl_y:        dd 0
bl_dx:       dd 0
bl_dy:       dd 0

bl_title:    db "blitdemo - bouncing diamond", 0
bl_msg_exit: db "blitdemo: done.", 0x0A, 0

; ---- 16x16 sprite: yellow diamond on transparent background ----
; Each pixel is a 32-bit 0x00RRGGBB value.
; SPR_T = transparent (color key), SPR_Y = yellow fill
spr_data:
; row 0  (7T 2Y 7T)
dd SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_Y
dd SPR_Y, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T
; row 1  (6T 4Y 6T)
dd SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T
; row 2  (5T 6Y 5T)
dd SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T
; row 3  (4T 8Y 4T)
dd SPR_T, SPR_T, SPR_T, SPR_T, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_T, SPR_T, SPR_T, SPR_T
; row 4  (3T 10Y 3T)
dd SPR_T, SPR_T, SPR_T, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_T, SPR_T, SPR_T
; row 5  (2T 12Y 2T)
dd SPR_T, SPR_T, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_T, SPR_T
; row 6  (1T 14Y 1T)
dd SPR_T, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_T
; row 7  (all yellow)
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
; row 8  (all yellow)
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
; row 9  (1T 14Y 1T)
dd SPR_T, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_T
; row 10 (2T 12Y 2T)
dd SPR_T, SPR_T, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_T, SPR_T
; row 11 (3T 10Y 3T)
dd SPR_T, SPR_T, SPR_T, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_T, SPR_T, SPR_T
; row 12 (4T 8Y 4T)
dd SPR_T, SPR_T, SPR_T, SPR_T, SPR_Y, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_Y, SPR_T, SPR_T, SPR_T, SPR_T
; row 13 (5T 6Y 5T)
dd SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_Y, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_Y, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T
; row 14 (6T 4Y 6T)
dd SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_Y, SPR_Y
dd SPR_Y, SPR_Y, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T
; row 15 (7T 2Y 7T)
dd SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_Y
dd SPR_Y, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T, SPR_T
