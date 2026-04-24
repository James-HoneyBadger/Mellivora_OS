; life.asm - Conway's Game of Life — VBE pixel graphics
; 78x23 grid rendered as 8×20-pixel cells on 640×480 screen.
; Press q/ESC to quit, r to reset.
%include "syscalls.inc"

SCREEN_W        equ 640
SCREEN_H        equ 480
GRID_W          equ 78
GRID_H          equ 23
GRID_SZ         equ GRID_W * GRID_H
CELL_W          equ 8           ; pixels per cell horizontally
CELL_H          equ 20          ; pixels per cell vertically
BOARD_X         equ 8           ; left offset  (8 + 78*8 = 632, centred)
BOARD_Y         equ 10          ; top offset   (10 + 23*20 = 470)
STATUS_Y        equ 470         ; status bar y position
COLOR_ALIVE     equ 0x00FF44    ; bright green
COLOR_DEAD      equ 0x000000    ; black
COLOR_TEXT      equ 0xFFFF00    ; yellow status text

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

        call seed_grid

.main_loop:
        call draw_all

        call step_generation
        inc dword [generation]

        mov eax, SYS_SLEEP
        mov ebx, 8
        int 0x80

        mov eax, SYS_READ_KEY
        int 0x80
        cmp eax, -1
        je .main_loop
        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        cmp al, 27
        je .quit
        cmp al, 'r'
        je .reset
        cmp al, 'R'
        je .reset
        jmp .main_loop

.reset:
        call seed_grid
        mov dword [generation], 0
        jmp .main_loop

.quit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
.exit_novbe:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; draw_all — render entire grid + status
;---------------------------------------
draw_all:
        pushad
        xor edx, edx            ; row
.da_row:
        cmp edx, GRID_H
        jge .da_status
        xor ecx, ecx            ; col
.da_col:
        cmp ecx, GRID_W
        jge .da_next_row

        ; Cell address
        mov eax, edx
        imul eax, GRID_W
        add eax, ecx
        movzx eax, byte [grid_a + eax]

        ; Choose color
        test eax, eax
        jz .da_dead
        mov edi, COLOR_ALIVE
        jmp .da_draw
.da_dead:
        mov edi, COLOR_DEAD

.da_draw:
        push ecx
        push edx
        mov ebx, ecx
        imul ebx, CELL_W
        add ebx, BOARD_X
        mov ecx, edx
        imul ecx, CELL_H
        add ecx, BOARD_Y
        mov edx, CELL_W
        mov esi, CELL_H
        call fb_fill_rect
        pop edx
        pop ecx

        inc ecx
        jmp .da_col
.da_next_row:
        inc edx
        jmp .da_row

.da_status:
        ; Clear status bar
        xor ebx, ebx
        mov ecx, STATUS_Y
        mov edx, SCREEN_W
        mov esi, SCREEN_H - STATUS_Y
        xor edi, edi
        call fb_fill_rect

        ; "Gen: " label
        mov ebx, 8
        mov ecx, STATUS_Y + 2
        mov esi, str_gen
        mov edi, COLOR_TEXT
        call fb_draw_text

        ; Generation number
        mov eax, [generation]
        mov ebx, 8 + 8 * 5      ; after "Gen: " (5 chars × 8px)
        mov ecx, STATUS_Y + 2
        mov edi, COLOR_TEXT
        call fb_draw_num

        ; Controls hint
        mov ebx, 260
        mov ecx, STATUS_Y + 2
        mov esi, str_controls
        mov edi, 0x888888
        call fb_draw_text

        popad
        ret

;---------------------------------------
; seed_grid
;---------------------------------------
seed_grid:
        mov edi, grid_a
        mov ecx, GRID_SZ
        xor al, al
        rep stosb
        mov edi, grid_b
        mov ecx, GRID_SZ
        xor al, al
        rep stosb

        ; Glider at (2,2)
        mov byte [grid_a + 2*GRID_W + 3], 1
        mov byte [grid_a + 3*GRID_W + 4], 1
        mov byte [grid_a + 4*GRID_W + 2], 1
        mov byte [grid_a + 4*GRID_W + 3], 1
        mov byte [grid_a + 4*GRID_W + 4], 1

        ; Blinker at (10,10)
        mov byte [grid_a + 10*GRID_W + 10], 1
        mov byte [grid_a + 10*GRID_W + 11], 1
        mov byte [grid_a + 10*GRID_W + 12], 1

        ; R-pentomino at (11,35)
        mov byte [grid_a + 11*GRID_W + 36], 1
        mov byte [grid_a + 11*GRID_W + 37], 1
        mov byte [grid_a + 12*GRID_W + 35], 1
        mov byte [grid_a + 12*GRID_W + 36], 1
        mov byte [grid_a + 13*GRID_W + 36], 1

        ; Glider at (1,50)
        mov byte [grid_a + 1*GRID_W + 51], 1
        mov byte [grid_a + 2*GRID_W + 52], 1
        mov byte [grid_a + 3*GRID_W + 50], 1
        mov byte [grid_a + 3*GRID_W + 51], 1
        mov byte [grid_a + 3*GRID_W + 52], 1
        ret

;---------------------------------------
; step_generation - grid_a → grid_b, copy back
;---------------------------------------
step_generation:
        pushad
        mov edi, grid_b
        mov ecx, GRID_SZ
        xor al, al
        rep stosb

        xor edx, edx            ; row
.sg_row:
        xor ecx, ecx            ; col
.sg_col:
        xor ebx, ebx            ; neighbor count
        ; top row
        mov eax, edx
        dec eax
        js .sg_skip_top
        mov esi, ecx
        dec esi
        js .sg_no_tl
        push eax
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
        pop eax
.sg_no_tl:
        push eax
        imul eax, GRID_W
        add eax, ecx
        add bl, [grid_a + eax]
        pop eax
        mov esi, ecx
        inc esi
        cmp esi, GRID_W
        jge .sg_skip_top
        push eax
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
        pop eax
.sg_skip_top:
        ; middle row (left + right)
        mov esi, ecx
        dec esi
        js .sg_no_l
        mov eax, edx
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
.sg_no_l:
        mov esi, ecx
        inc esi
        cmp esi, GRID_W
        jge .sg_no_r
        mov eax, edx
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
.sg_no_r:
        ; bottom row
        mov eax, edx
        inc eax
        cmp eax, GRID_H
        jge .sg_skip_bot
        mov esi, ecx
        dec esi
        js .sg_no_bl
        push eax
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
        pop eax
.sg_no_bl:
        push eax
        imul eax, GRID_W
        add eax, ecx
        add bl, [grid_a + eax]
        pop eax
        mov esi, ecx
        inc esi
        cmp esi, GRID_W
        jge .sg_skip_bot
        push eax
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
        pop eax
.sg_skip_bot:
        ; Apply rules
        mov eax, edx
        imul eax, GRID_W
        add eax, ecx
        cmp byte [grid_a + eax], 1
        je .sg_alive
        cmp bl, 3
        jne .sg_next
        mov byte [grid_b + eax], 1
        jmp .sg_next
.sg_alive:
        cmp bl, 2
        je .sg_survive
        cmp bl, 3
        je .sg_survive
        jmp .sg_next
.sg_survive:
        mov byte [grid_b + eax], 1
.sg_next:
        inc ecx
        cmp ecx, GRID_W
        jl .sg_col
        inc edx
        cmp edx, GRID_H
        jl .sg_row

        mov esi, grid_b
        mov edi, grid_a
        mov ecx, GRID_SZ
        rep movsb
        popad
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

; fb_draw_text: EBX=x, ECX=y, ESI=str_ptr, EDI=color
fb_draw_text:
        pushad
        mov edx, ecx
        mov ecx, ebx
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        int 0x80
        popad
        ret

; itoa: EAX=number → null-terminated decimal in num_buf
itoa:
        pushad
        mov edi, num_buf + 11
        mov byte [edi], 0
        dec edi
        test eax, eax
        jnz .itoa_digits
        mov byte [edi], '0'
        dec edi
        jmp .itoa_copy
.itoa_digits:
        mov ecx, 10
.itoa_lp:
        test eax, eax
        jz .itoa_copy
        xor edx, edx
        div ecx
        add dl, '0'
        mov [edi], dl
        dec edi
        jmp .itoa_lp
.itoa_copy:
        inc edi
        mov esi, edi
        mov edi, num_buf
.itoa_cp:
        mov al, [esi]
        mov [edi], al
        inc esi
        inc edi
        test al, al
        jnz .itoa_cp
        popad
        ret

; fb_draw_num: EAX=number, EBX=x, ECX=y, EDI=color
fb_draw_num:
        push esi
        push ebx
        push ecx
        push edi
        call itoa
        pop edi
        pop ecx
        pop ebx
        mov esi, num_buf
        call fb_draw_text
        pop esi
        ret

;=======================================================================
; DATA
;=======================================================================
generation:     dd 0
str_gen:        db "Gen: ", 0
str_controls:   db "[q]uit  [r]eset", 0

;=======================================================================
; BSS
;=======================================================================
section .bss
grid_a:         resb GRID_SZ
grid_b:         resb GRID_SZ
fb_addr:        resd 1
fb_pitch:       resd 1
num_buf:        resb 12
