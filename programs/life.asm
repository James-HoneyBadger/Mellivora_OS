; life.asm - Conway's Game of Life for Mellivora OS
; 78x23 grid, runs until Ctrl+C / 'q'
%include "syscalls.inc"

GRID_W  equ 78
GRID_H  equ 23
GRID_SZ equ GRID_W * GRID_H

start:
        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Seed with a few patterns
        call seed_grid

.main_loop:
        ; Draw grid
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80

        mov esi, grid_a
        xor edx, edx           ; row counter
.draw_row:
        xor ecx, ecx           ; col counter
.draw_col:
        mov al, [esi]
        cmp al, 1
        je .draw_alive
        mov ebx, ' '
        jmp .draw_put
.draw_alive:
        mov ebx, 0xDB          ; block char
.draw_put:
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        inc ecx
        cmp ecx, GRID_W
        jl .draw_col
        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        inc edx
        cmp edx, GRID_H
        jl .draw_row

        ; Status line
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_gen
        int 0x80
        mov eax, [generation]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_quit
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Compute next generation
        call step_generation
        inc dword [generation]

        ; Delay
        mov eax, SYS_SLEEP
        mov ebx, 8              ; ~80ms
        int 0x80

        ; Check for key press
        mov eax, SYS_READ_KEY
        int 0x80
        cmp eax, -1
        je .main_loop
        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        cmp al, 'r'
        je .reset
        cmp al, 27              ; ESC
        je .quit
        jmp .main_loop

.reset:
        call seed_grid
        mov dword [generation], 0
        jmp .main_loop

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;---------------------------------------
; Seed grid with glider, blinker, etc.
;---------------------------------------
seed_grid:
        ; Clear both grids
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
; Step one generation: grid_a -> grid_b, then copy back
;---------------------------------------
step_generation:
        PUSHALL
        ; Clear grid_b
        mov edi, grid_b
        mov ecx, GRID_SZ
        xor al, al
        rep stosb

        xor edx, edx           ; row
.sg_row:
        xor ecx, ecx           ; col
.sg_col:
        ; Count neighbors of (edx, ecx)
        xor ebx, ebx           ; neighbor count
        ; Check all 8 directions
        mov eax, edx
        dec eax                 ; row-1
        js .sg_skip_top
        ; top-left
        mov esi, ecx
        dec esi
        js .sg_no_tl
        push rax
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
        pop rax
.sg_no_tl:
        ; top
        push rax
        imul eax, GRID_W
        add eax, ecx
        add bl, [grid_a + eax]
        pop rax
        ; top-right
        mov esi, ecx
        inc esi
        cmp esi, GRID_W
        jge .sg_skip_top
        push rax
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
        pop rax
.sg_skip_top:
        ; left
        mov esi, ecx
        dec esi
        js .sg_no_l
        mov eax, edx
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
.sg_no_l:
        ; right
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
        ; bottom-left
        mov esi, ecx
        dec esi
        js .sg_no_bl
        push rax
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
        pop rax
.sg_no_bl:
        ; bottom
        push rax
        imul eax, GRID_W
        add eax, ecx
        add bl, [grid_a + eax]
        pop rax
        ; bottom-right
        mov esi, ecx
        inc esi
        cmp esi, GRID_W
        jge .sg_skip_bot
        push rax
        imul eax, GRID_W
        add eax, esi
        add bl, [grid_a + eax]
        pop rax
.sg_skip_bot:
        ; Apply rules
        mov eax, edx
        imul eax, GRID_W
        add eax, ecx
        cmp byte [grid_a + eax], 1
        je .sg_alive
        ; Dead cell: birth if 3 neighbors
        cmp bl, 3
        jne .sg_next
        mov byte [grid_b + eax], 1
        jmp .sg_next
.sg_alive:
        ; Alive: survive if 2 or 3
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

        ; Copy grid_b -> grid_a
        mov esi, grid_b
        mov edi, grid_a
        mov ecx, GRID_SZ
        rep movsb
        POPALL
        ret

;---------------------------------------
; Data
;---------------------------------------
generation: dd 0
msg_gen:    db " Gen: ", 0
msg_quit:   db "  [q]uit [r]eset", 0

;---------------------------------------
; BSS
;---------------------------------------
section .bss
grid_a: resb GRID_SZ
grid_b: resb GRID_SZ
