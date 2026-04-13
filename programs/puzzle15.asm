; puzzle15.asm - Sliding 15-puzzle game
; Usage: puzzle15
; Slide tiles using arrow keys to order 1-15

%include "syscalls.inc"

BLANK   equ 0
BSIZE   equ 4

start:
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, title_str
        int 0x80

        ; Initialize solved board: 1,2,3,...,15,0
        mov edi, board
        mov ecx, 1
.init:
        mov [edi], cl
        inc edi
        inc ecx
        cmp ecx, 16
        jb .init
        mov byte [edi - 1], BLANK
        mov byte [blank_pos], 15    ; bottom-right

        ; Shuffle by making random moves
        call shuffle

        mov dword [moves], 0

game_loop:
        call draw_board

        ; Check win
        call check_win
        cmp eax, 1
        je you_win

        mov eax, SYS_PRINT
        mov ebx, move_str
        int 0x80

        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 'q'
        je quit
        cmp al, 0x80          ; KEY_UP
        je move_up
        cmp al, 0x81          ; KEY_DOWN
        je move_down
        cmp al, 0x82          ; KEY_LEFT
        je move_left
        cmp al, 0x83          ; KEY_RIGHT
        je move_right
        ; WASD support
        cmp al, 'w'
        je move_up
        cmp al, 's'
        je move_down
        cmp al, 'a'
        je move_left
        cmp al, 'd'
        je move_right
        jmp game_loop

; Move tile from below blank UP into blank
move_up:
        movzx eax, byte [blank_pos]
        add eax, BSIZE
        cmp eax, 16
        jge game_loop
        call swap_blank
        jmp game_loop

; Move tile from above blank DOWN into blank
move_down:
        movzx eax, byte [blank_pos]
        sub eax, BSIZE
        js game_loop
        call swap_blank
        jmp game_loop

; Move tile from right of blank LEFT into blank
move_left:
        movzx eax, byte [blank_pos]
        mov ecx, eax
        and ecx, 3          ; column
        cmp ecx, 3
        je game_loop
        lea eax, [eax + 1]
        call swap_blank
        jmp game_loop

; Move tile from left of blank RIGHT into blank
move_right:
        movzx eax, byte [blank_pos]
        mov ecx, eax
        and ecx, 3
        cmp ecx, 0
        je game_loop
        lea eax, [eax - 1]
        call swap_blank
        jmp game_loop

;--------------------------------------
; swap_blank: Swap board[blank_pos] with board[eax]
;--------------------------------------
swap_blank:
        movzx ebx, byte [blank_pos]
        mov cl, [board + eax]
        mov [board + ebx], cl
        mov byte [board + eax], BLANK
        mov [blank_pos], al
        inc dword [moves]
        ret

;--------------------------------------
; shuffle: Make 200 random moves to shuffle
;--------------------------------------
shuffle:
        pusha
        mov ebp, 200
.shuf_loop:
        mov eax, SYS_GETTIME
        int 0x80
        ; Use low bits for direction
        and eax, 3
        ; 0=up, 1=down, 2=left, 3=right
        cmp eax, 0
        je .try_up
        cmp eax, 1
        je .try_down
        cmp eax, 2
        je .try_left
        jmp .try_right

.try_up:
        movzx eax, byte [blank_pos]
        add eax, BSIZE
        cmp eax, 16
        jge .skip
        jmp .do_swap
.try_down:
        movzx eax, byte [blank_pos]
        sub eax, BSIZE
        js .skip
        jmp .do_swap
.try_left:
        movzx eax, byte [blank_pos]
        mov ecx, eax
        and ecx, 3
        cmp ecx, 3
        je .skip
        lea eax, [eax + 1]
        jmp .do_swap
.try_right:
        movzx eax, byte [blank_pos]
        mov ecx, eax
        and ecx, 3
        cmp ecx, 0
        je .skip
        lea eax, [eax - 1]
.do_swap:
        call swap_blank
        dec dword [moves]       ; don't count shuffle moves
.skip:
        dec ebp
        jnz .shuf_loop

        mov dword [moves], 0
        popa
        ret

;--------------------------------------
; draw_board
;--------------------------------------
draw_board:
        pusha
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, title_str
        int 0x80

        ; Print moves count
        mov eax, SYS_PRINT
        mov ebx, moves_lbl
        int 0x80
        mov eax, [moves]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, border_top
        int 0x80

        xor esi, esi        ; cell index
.row:
        mov eax, SYS_PRINT
        mov ebx, bar_str
        int 0x80

        mov ecx, BSIZE
.cell:
        movzx eax, byte [board + esi]
        cmp eax, BLANK
        je .blank_cell

        ; Colored tile
        push ecx
        push esi
        mov ebx, 0x0B
        cmp eax, [solved + esi]
        jne .wrong_pos
        mov ebx, 0x0A        ; green if in correct position
.wrong_pos:
        push eax
        mov eax, SYS_SETCOLOR
        int 0x80
        pop eax

        ; Print number with padding
        cmp eax, 10
        jge .two_digit
        mov ebx, SYS_PUTCHAR
        xchg eax, ebx
        push ebx
        mov ebx, ' '
        int 0x80
        pop ebx
        add ebx, '0'
        push ebx
        mov eax, SYS_PUTCHAR
        mov ebx, [esp]
        int 0x80
        add esp, 4
        jmp .after_num
.two_digit:
        call print_dec
.after_num:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        pop esi
        pop ecx
        jmp .sep

.blank_cell:
        mov eax, SYS_PRINT
        mov ebx, blank_str
        int 0x80

.sep:
        inc esi
        mov eax, SYS_PRINT
        mov ebx, sep_str
        int 0x80

        dec ecx
        jnz .cell

        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        cmp esi, 16
        jge .board_done

        mov eax, SYS_PRINT
        mov ebx, border_mid
        int 0x80
        jmp .row

.board_done:
        mov eax, SYS_PRINT
        mov ebx, border_bot
        int 0x80
        popa
        ret

;--------------------------------------
; check_win
;--------------------------------------
check_win:
        xor ecx, ecx
.loop:
        movzx eax, byte [board + ecx]
        cmp eax, [solved + ecx]
        jne .no
        inc ecx
        cmp ecx, 16
        jb .loop
        mov eax, 1
        ret
.no:
        xor eax, eax
        ret

you_win:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, win_str
        int 0x80
        mov eax, [moves]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, win_str2
        int 0x80

quit:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================
; Data
;=======================================
title_str:  db "=== 15 Puzzle ===", 10
            db "Arrow keys/WASD to slide, Q to quit", 10, 10, 0
moves_lbl:  db "Moves: ", 0
move_str:   db 10, "Move: ", 0
newline:    db 10, 0
blank_str:  db "  ", 0
sep_str:    db " |", 0
bar_str:    db "| ", 0
border_top: db "+----+----+----+----+", 10, 0
border_mid: db "+----+----+----+----+", 10, 0
border_bot: db "+----+----+----+----+", 10, 0
win_str:    db 10, "*** SOLVED in ", 0
win_str2:   db " moves! ***", 10, 0

; Solved state for comparison (stored as dwords for cmp)
solved:     dd 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0

board:      times 16 db 0
blank_pos:  db 0
moves:      dd 0
