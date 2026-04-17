; hanoi.asm - Towers of Hanoi interactive puzzle
; Usage: hanoi [n]    - play with n disks (default 4, max 8)

%include "syscalls.inc"

MAX_DISKS   equ 8
DEF_DISKS   equ 4
PEG_HEIGHT  equ 10

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov dword [num_disks], DEF_DISKS
        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .init

        ; Parse disk count
        xor eax, eax
.parse:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jb .parsed
        cmp cl, '9'
        ja .parsed
        imul eax, 10
        sub cl, '0'
        add eax, ecx
        inc esi
        jmp .parse
.parsed:
        cmp eax, 1
        jl .init
        cmp eax, MAX_DISKS
        jg .clamp
        mov [num_disks], eax
        jmp .init
.clamp:
        mov dword [num_disks], MAX_DISKS

.init:
        ; Initialize pegs: peg A has all disks, B and C empty
        ; Disks are numbered 1..n (1=smallest)
        ; peg_X: [count, disk_n, disk_n-1, ..., disk_1] (bottom to top)
        mov edi, peg_a
        mov eax, [num_disks]
        mov [edi], eax          ; count
        lea edi, [edi + 4]
        mov ecx, eax
        ; Place disks largest first (bottom)
.place:
        mov [edi], ecx
        add edi, 4
        dec ecx
        jnz .place

        ; Clear peg B and C
        mov dword [peg_b], 0
        mov dword [peg_c], 0
        mov dword [move_count], 0

game_loop:
        call draw_state

        ; Check win: all disks on peg C
        mov eax, [peg_c]
        cmp eax, [num_disks]
        je you_win

        mov eax, SYS_PRINT
        mov ebx, prompt_str
        int 0x80

        ; Get source peg
        call get_peg
        cmp eax, -1
        je quit
        mov [src_peg], eax

        mov eax, SYS_PRINT
        mov ebx, to_str
        int 0x80

        ; Get dest peg
        call get_peg
        cmp eax, -1
        je quit
        mov [dst_peg], eax

        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Validate move
        call do_move
        cmp eax, 0
        je .invalid
        jmp game_loop

.invalid:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_move
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        jmp game_loop

you_win:
        call draw_state
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, win_str
        int 0x80
        mov eax, [move_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, win_str2
        int 0x80

        ; Optimal moves
        mov ecx, [num_disks]
        mov eax, 1
        shl eax, cl
        dec eax
        push rax
        mov eax, SYS_PRINT
        mov ebx, opt_str
        int 0x80
        pop rax
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ')'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

quit:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;--------------------------------------
; get_peg: Read A/B/C from keyboard, return 0/1/2 or -1 for Q
;--------------------------------------
get_peg:
.loop:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        ; Uppercase
        cmp al, 'a'
        jb .check_upper
        cmp al, 'z'
        ja .loop
        sub al, 32
.check_upper:
        cmp al, 'A'
        je .peg_a
        cmp al, 'B'
        je .peg_b
        cmp al, 'C'
        je .peg_c
        jmp .loop
.peg_a:
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, 'A'
        int 0x80
        pop rax
        xor eax, eax
        ret
.peg_b:
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, 'B'
        int 0x80
        pop rax
        mov eax, 1
        ret
.peg_c:
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, 'C'
        int 0x80
        pop rax
        mov eax, 2
        ret
.quit:
        mov eax, -1
        ret

;--------------------------------------
; do_move: Move top disk from [src_peg] to [dst_peg]
; Returns 1 on success, 0 on failure
;--------------------------------------
do_move:
        mov eax, [src_peg]
        call get_peg_ptr
        mov esi, eax            ; source peg

        mov eax, [dst_peg]
        call get_peg_ptr
        mov edi, eax            ; dest peg

        ; Check source not empty
        mov eax, [esi]          ; source count
        cmp eax, 0
        je .fail

        ; Get top disk of source
        mov ecx, [esi]
        mov ebx, [esi + ecx * 4]   ; top disk value

        ; Check dest: if not empty, top must be larger
        mov eax, [edi]
        cmp eax, 0
        je .can_place
        mov edx, [edi + eax * 4]   ; dest top disk
        cmp ebx, edx
        jge .fail               ; can't place larger on smaller

.can_place:
        ; Remove from source
        dec dword [esi]

        ; Add to dest
        inc dword [edi]
        mov eax, [edi]
        mov [edi + eax * 4], ebx

        inc dword [move_count]
        mov eax, 1
        ret
.fail:
        xor eax, eax
        ret

;--------------------------------------
; get_peg_ptr: EAX=0/1/2, return pointer in EAX
;--------------------------------------
get_peg_ptr:
        cmp eax, 0
        je .a
        cmp eax, 1
        je .b
        mov eax, peg_c
        ret
.a:
        mov eax, peg_a
        ret
.b:
        mov eax, peg_b
        ret

;--------------------------------------
; draw_state: Draw all three pegs
;--------------------------------------
draw_state:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, title_str
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, moves_str
        int 0x80
        mov eax, [move_count]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Draw each level from top to bottom
        mov ecx, [num_disks]
        dec ecx                 ; top level index
.draw_level:
        cmp ecx, 0
        jl .draw_base

        ; Draw peg A at this level
        push rcx
        mov eax, 0
        call draw_peg_level
        mov eax, SYS_PRINT
        mov ebx, peg_gap
        int 0x80
        pop rcx

        ; Draw peg B at this level
        push rcx
        mov eax, 1
        call draw_peg_level
        mov eax, SYS_PRINT
        mov ebx, peg_gap
        int 0x80
        pop rcx

        ; Draw peg C at this level
        push rcx
        mov eax, 2
        call draw_peg_level
        pop rcx

        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop rcx

        dec ecx
        jmp .draw_level

.draw_base:
        ; Draw base line
        mov eax, SYS_SETCOLOR
        mov ebx, 0x06
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, base_str
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, label_str
        int 0x80

        POPALL
        ret

;--------------------------------------
; draw_peg_level: Draw one level of a peg
; EAX = peg (0/1/2), ECX = level (0=bottom)
;--------------------------------------
draw_peg_level:
        push rbx
        push rdx
        push rsi
        push rdi

        call get_peg_ptr
        mov esi, eax            ; peg ptr

        mov edx, [esi]          ; peg count
        ; Level ECX: if ecx < count, there's a disk at index ecx+1
        inc ecx                 ; 1-based index
        cmp ecx, edx
        jg .draw_pole           ; no disk at this level

        ; Get disk size at this level
        mov eax, [esi + ecx * 4]
        ; Draw disk: width = disk_size * 2 + 1
        ; Center in field of width (MAX_DISKS * 2 + 3)
        mov ebx, eax            ; disk size
        mov edx, [num_disks]
        sub edx, ebx            ; padding each side

        ; Print padding
        push rbx
        mov ecx, edx
.pad_left:
        cmp ecx, 0
        jle .disk_body
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rcx
        dec ecx
        jmp .pad_left

.disk_body:
        pop rbx
        ; Color based on disk size
        push rbx
        dec ebx
        and ebx, 7
        movzx ebx, byte [disk_colors + ebx]
        mov eax, SYS_SETCOLOR
        int 0x80
        pop rbx

        ; Draw disk: [=====]
        push rbx
        mov ecx, ebx
        shl ecx, 1
        inc ecx                 ; width = size*2+1
.draw_disk:
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB           ; block char
        int 0x80
        pop rcx
        dec ecx
        jnz .draw_disk
        pop rbx

        ; Reset color
        push rdx
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        pop rdx

        ; Right padding
        mov ecx, edx
.pad_right:
        cmp ecx, 0
        jle .peg_done
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rcx
        dec ecx
        jmp .pad_right

.draw_pole:
        ; No disk: just the pole character centered
        mov ecx, [num_disks]
.pole_left:
        cmp ecx, 0
        jle .pole_char
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rcx
        dec ecx
        jmp .pole_left
.pole_char:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov ecx, [num_disks]
.pole_right:
        cmp ecx, 0
        jle .peg_done
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rcx
        dec ecx
        jmp .pole_right

.peg_done:
        pop rdi
        pop rsi
        pop rdx
        pop rbx
        ret

;--------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

;=======================================
; Data
;=======================================
title_str:  db "=== Towers of Hanoi ===", 10
            db "Move all disks from A to C.", 10
            db "A larger disk cannot go on a smaller one.", 10
            db "Q to quit.", 10, 10, 0
prompt_str: db "Move from peg (A/B/C): ", 0
to_str:     db " to peg (A/B/C): ", 0
err_move:   db "Invalid move!", 10, 0
win_str:    db 10, "*** Congratulations! Solved in ", 0
win_str2:   db " moves! ***", 10, 0
opt_str:    db "(Optimal: ", 0
moves_str:  db "Moves: ", 0
peg_gap:    db "  ", 0
base_str:   db "=========  =========  =========", 10, 0
label_str:  db "    A          B          C", 10, 10, 0

; Colors for disks 1-8
disk_colors: db 0x0C, 0x0E, 0x0A, 0x0B, 0x09, 0x0D, 0x06, 0x0F

; Peg data: [count, disk1, disk2, ..., disk8]
peg_a:      times (MAX_DISKS + 1) dd 0
peg_b:      times (MAX_DISKS + 1) dd 0
peg_c:      times (MAX_DISKS + 1) dd 0

num_disks:  dd 0
move_count: dd 0
src_peg:    dd 0
dst_peg:    dd 0
arg_buf:    times 256 db 0
