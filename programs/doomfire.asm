; doomfire.asm - Doom fire effect demo — VBE pixel graphics
; Press any key to exit.

%include "syscalls.inc"

SCREEN_W        equ 640
SCREEN_H        equ 480
FIRE_W          equ 80
FIRE_H          equ 48         ; 48 * 10 = 480
FIRE_SIZE       equ FIRE_W * FIRE_H
CELL_W          equ 8          ; pixels per fire cell (80*8=640)
CELL_H          equ 10         ; pixels per fire cell (48*10=480)
TICK_DELAY      equ 2
NUM_SHADES      equ 37         ; 0=cold black, 36=white-hot

start:
        ; Set VBE mode 640x480x32
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCREEN_W
        mov edx, SCREEN_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .exit_novbe

        ; Get framebuffer info
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr], eax
        mov dword [fb_pitch], SCREEN_W * 4

        ; Seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_state], eax

        ; Clear fire buffer to zero
        mov edi, fire_buf
        mov ecx, FIRE_SIZE
        xor eax, eax
        rep stosb

        ; Set bottom row to max intensity
        mov edi, fire_buf + (FIRE_H - 1) * FIRE_W
        mov ecx, FIRE_W
        mov al, NUM_SHADES - 1
        rep stosb

        ; Black out screen
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCREEN_W
        mov esi, SCREEN_H
        xor edi, edi
        call fb_fill_rect

.main_loop:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        call fire_spread
        call fire_render

        mov eax, SYS_SLEEP
        mov ebx, TICK_DELAY
        int 0x80
        jmp .main_loop

.exit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
.exit_novbe:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; fire_spread - propagate fire upward
;---------------------------------------
fire_spread:
        pushad
        mov edx, 0
.fs_row:
        cmp edx, FIRE_H - 1
        jge .fs_done
        xor ecx, ecx
.fs_col:
        cmp ecx, FIRE_W
        jge .fs_next_row

        ; Source pixel below
        mov eax, edx
        inc eax
        imul eax, FIRE_W
        add eax, ecx
        movzx ebx, byte [fire_buf + eax]

        ; Random decay
        call rand_small
        sub ebx, eax
        jns .fs_clamp_ok
        xor ebx, ebx
.fs_clamp_ok:

        ; Random wind
        call rand_small
        mov edi, eax
        call rand_small
        sub edi, eax
        mov esi, ecx
        add esi, edi
        jns .fs_xok
        xor esi, esi
.fs_xok:
        cmp esi, FIRE_W
        jb .fs_xok2
        mov esi, FIRE_W - 1
.fs_xok2:

        mov eax, edx
        imul eax, FIRE_W
        add eax, esi
        mov byte [fire_buf + eax], bl

        inc ecx
        jmp .fs_col
.fs_next_row:
        inc edx
        jmp .fs_row
.fs_done:
        popad
        ret

;---------------------------------------
; fire_render - draw fire to VBE framebuffer
;---------------------------------------
fire_render:
        pushad
        xor edx, edx            ; row
.fr_row:
        cmp edx, FIRE_H
        jge .fr_done
        xor ecx, ecx            ; col
.fr_col:
        cmp ecx, FIRE_W
        jge .fr_next_row

        ; Intensity → palette color
        mov eax, edx
        imul eax, FIRE_W
        add eax, ecx
        movzx eax, byte [fire_buf + eax]
        mov edi, [fire_palette + eax * 4]

        ; Draw CELL_W × CELL_H block
        push ecx
        push edx
        mov ebx, ecx
        imul ebx, CELL_W
        mov ecx, edx
        imul ecx, CELL_H
        mov edx, CELL_W
        mov esi, CELL_H
        call fb_fill_rect
        pop edx
        pop ecx

        inc ecx
        jmp .fr_col
.fr_next_row:
        inc edx
        jmp .fr_row
.fr_done:
        popad
        ret

;---------------------------------------
; rand_small — returns 0 or 1 in EAX
;---------------------------------------
rand_small:
        push ebx
        mov eax, [rand_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        shr eax, 16
        and eax, 1
        pop ebx
        ret

;=======================================================================
; VBE HELPERS
;=======================================================================

; fb_fill_rect: EBX=x, ECX=y, EDX=w, ESI=h, EDI=color
fb_fill_rect:
        pushad
        test edx, edx
        jz .ffr_done
        test esi, esi
        jz .ffr_done
        mov eax, ecx
        imul eax, [fb_pitch]
        add eax, [fb_addr]
        lea eax, [eax + ebx*4]
.ffr_row:
        push eax
        push edx
        mov ecx, edx
.ffr_col:
        mov [eax], edi
        add eax, 4
        dec ecx
        jnz .ffr_col
        pop edx
        pop eax
        add eax, [fb_pitch]
        dec esi
        jnz .ffr_row
.ffr_done:
        popad
        ret

;=======================================================================
; DATA
;=======================================================================

; 37-shade fire palette: black → red → orange → yellow → white
fire_palette:
        dd 0x000000, 0x0D0000, 0x1A0000, 0x260000, 0x330000
        dd 0x400000, 0x4D0000, 0x5A0000, 0x660000, 0x730000
        dd 0x800000, 0x8C0000, 0x990000, 0xA60000, 0xB30000
        dd 0xBF0000, 0xCC0000, 0xD90000, 0xE60000, 0xFF0000
        dd 0xFF1A00, 0xFF3300, 0xFF4D00, 0xFF6600, 0xFF8000
        dd 0xFF9900, 0xFFB300, 0xFFCC00, 0xFFE600, 0xFFFF00
        dd 0xFFFF33, 0xFFFF66, 0xFFFF99, 0xFFFFCC, 0xFFFFE6
        dd 0xFFFFF0, 0xFFFFFF

rand_state:     dd 0
fire_buf:       times FIRE_SIZE db 0

section .bss
fb_addr:        resd 1
fb_pitch:       resd 1
