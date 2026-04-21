; tictactoe.asm - Tic-Tac-Toe vs CPU
; Usage: tictactoe

%include "syscalls.inc"

EMPTY   equ 0
HUMAN   equ 1
COMP    equ 2

start:
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, title_str
        int 0x80

        ; Clear board
        mov edi, board
        mov ecx, 9
        xor eax, eax
        rep stosb

        ; Human goes first (X)
        mov byte [turn], HUMAN

game_loop:
        call draw_board

        cmp byte [turn], HUMAN
        je .human_turn

        ; CPU turn
        call cpu_move
        jmp .after_move

.human_turn:
        mov eax, SYS_PRINT
        mov ebx, prompt_str
        int 0x80

.get_input:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 'q'
        je quit
        cmp al, '1'
        jb .get_input
        cmp al, '9'
        ja .get_input

        ; Convert '1'-'9' to index 0-8
        sub al, '1'
        movzx esi, al

        ; Check empty
        cmp byte [board + esi], EMPTY
        jne .get_input

        mov byte [board + esi], HUMAN

.after_move:
        ; Check for winner
        call check_winner
        cmp eax, HUMAN
        je human_wins
        cmp eax, COMP
        je cpu_wins

        ; Check draw
        call check_draw
        cmp eax, 1
        je draw_game

        ; Toggle turn
        cmp byte [turn], HUMAN
        je .set_cpu
        mov byte [turn], HUMAN
        jmp game_loop
.set_cpu:
        mov byte [turn], COMP
        jmp game_loop

human_wins:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, win_str
        int 0x80
        jmp wait_exit

cpu_wins:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lose_str
        int 0x80
        jmp wait_exit

draw_game:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, draw_str
        int 0x80

wait_exit:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, again_str
        int 0x80
.wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je start
        cmp al, 'n'
        je quit
        cmp al, 'q'
        je quit
        jmp .wait

quit:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;--------------------------------------
; draw_board: Display the current board
;--------------------------------------
draw_board:
        pushad

        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        ; Draw rows
        xor ecx, ecx       ; cell index 0-8
.row:
        mov eax, SYS_PRINT
        mov ebx, pad_str
        int 0x80

        ; Cell 0 of row
        push ecx
        call .draw_cell
        pop ecx
        inc ecx

        mov eax, SYS_PRINT
        mov ebx, sep_str
        int 0x80

        ; Cell 1 of row
        push ecx
        call .draw_cell
        pop ecx
        inc ecx

        mov eax, SYS_PRINT
        mov ebx, sep_str
        int 0x80

        ; Cell 2 of row
        push ecx
        call .draw_cell
        pop ecx
        inc ecx

        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        cmp ecx, 9
        jge .done

        mov eax, SYS_PRINT
        mov ebx, line_str
        int 0x80

        jmp .row

.done:
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        popad
        ret

.draw_cell:
        ; ECX = cell index
        movzx esi, byte [board + ecx]
        cmp esi, HUMAN
        je .x
        cmp esi, COMP
        je .o
        ; Empty: show position number
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        lea eax, [ecx + '1']
        mov [char_buf], al
        mov byte [char_buf+1], 0
        mov eax, SYS_PRINT
        mov ebx, char_buf
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        ret
.x:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, x_str
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        ret
.o:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, o_str
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        ret

;--------------------------------------
; check_winner: returns HUMAN, COMP, or 0 in EAX
;--------------------------------------
check_winner:
        ; Check all 8 lines
        mov esi, win_lines
        mov ecx, 8
.check:
        movzx eax, byte [esi]
        movzx ebx, byte [esi+1]
        movzx edx, byte [esi+2]
        movzx eax, byte [board + eax]
        cmp eax, EMPTY
        je .next
        cmp al, [board + ebx]
        jne .next
        cmp al, [board + edx]
        jne .next
        ; Winner found
        ret
.next:
        add esi, 3
        dec ecx
        jnz .check
        xor eax, eax
        ret

;--------------------------------------
; check_draw: returns 1 if board full, 0 otherwise
;--------------------------------------
check_draw:
        xor ecx, ecx
.loop:
        cmp byte [board + ecx], EMPTY
        je .not_full
        inc ecx
        cmp ecx, 9
        jb .loop
        mov eax, 1
        ret
.not_full:
        xor eax, eax
        ret

;--------------------------------------
; cpu_move: CPU plays optimally (minimax-lite)
;--------------------------------------
cpu_move:
        pushad

        ; 1. Try to win
        mov edx, COMP
        call try_complete
        cmp eax, -1
        jne .place

        ; 2. Block human
        mov edx, HUMAN
        call try_complete
        cmp eax, -1
        jne .place

        ; 3. Take center
        cmp byte [board + 4], EMPTY
        jne .try_corners
        mov eax, 4
        jmp .place

        ; 4. Take a corner
.try_corners:
        mov esi, corners
        mov ecx, 4
.corner_loop:
        movzx eax, byte [esi]
        cmp byte [board + eax], EMPTY
        je .place
        inc esi
        dec ecx
        jnz .corner_loop

        ; 5. Take any empty
        xor eax, eax
.any_loop:
        cmp byte [board + eax], EMPTY
        je .place
        inc eax
        cmp eax, 9
        jb .any_loop
        jmp .done_cpu        ; board full (shouldn't happen)

.place:
        mov byte [board + eax], COMP
.done_cpu:
        popad
        ret

;--------------------------------------
; try_complete: Try to find a line with 2 of player EDX and 1 empty
; Returns cell index in EAX, or -1
;--------------------------------------
try_complete:
        push esi
        push ecx
        push ebx
        mov esi, win_lines
        mov ecx, 8
.tc_loop:
        movzx eax, byte [esi]      ; a
        movzx ebx, byte [esi+1]    ; b
        push edx
        movzx edx, byte [esi+2]    ; c
        ; Count pieces
        xor edi, edi        ; count of player
        mov [.empty_cell], dword -1

        cmp byte [board + eax], dl
        jne .not_a
        inc edi
        jmp .check_b
.not_a:
        cmp byte [board + eax], EMPTY
        jne .check_b
        mov [.empty_cell], eax
.check_b:
        cmp byte [board + ebx], dl
        jne .not_b
        inc edi
        jmp .check_c
.not_b:
        cmp byte [board + ebx], EMPTY
        jne .check_c
        mov [.empty_cell], ebx
.check_c:
        cmp byte [board + edx], dl
        jne .not_c
        inc edi
        jmp .eval
.not_c:
        cmp byte [board + edx], EMPTY
        jne .eval
        push edx
        movzx edx, byte [esi+2]
        mov [.empty_cell], edx
        pop edx
.eval:
        pop edx
        cmp edi, 2
        jne .tc_next
        cmp dword [.empty_cell], -1
        je .tc_next
        mov eax, [.empty_cell]
        pop ebx
        pop ecx
        pop esi
        ret
.tc_next:
        add esi, 3
        dec ecx
        jnz .tc_loop
        mov eax, -1
        pop ebx
        pop ecx
        pop esi
        ret

.empty_cell: dd 0

;=======================================
; Data
;=======================================
title_str:  db "=== Tic-Tac-Toe ===", 10
            db "You are X, CPU is O", 10
            db "Enter 1-9 to place, Q to quit", 10, 0

prompt_str: db "Your move (1-9): ", 0
win_str:    db 10, "*** You win! ***", 10, 0
lose_str:   db 10, "CPU wins!", 10, 0
draw_str:   db 10, "It's a draw!", 10, 0
again_str:  db "Play again? (y/n) ", 0
newline:    db 10, 0
pad_str:    db "   ", 0
sep_str:    db " | ", 0
line_str:   db "  ---+---+---", 10, 0
x_str:      db "X", 0
o_str:      db "O", 0
char_buf:   db 0, 0

corners:    db 0, 2, 6, 8

; Winning lines: triplets of indices
win_lines:
        db 0,1,2,  3,4,5,  6,7,8  ; rows
        db 0,3,6,  1,4,7,  2,5,8  ; cols
        db 0,4,8,  2,4,6           ; diags

board:      times 9 db 0
turn:       db 0
