; nim.asm - Nim game (player vs AI)
; Three rows: 5, 4, 3 objects. Take any number from one row.
; Player who takes the last object LOSES (misère nim).

%include "syscalls.inc"

start:
        call init_game
        call game_loop

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

init_game:
        mov dword [row + 0], 5
        mov dword [row + 4], 4
        mov dword [row + 8], 3
        mov byte [player_turn], 1   ; 1 = human, 0 = AI
        ret

game_loop:
.loop:
        call draw_board
        ; Check game over: all rows = 0
        mov eax, [row]
        add eax, [row + 4]
        add eax, [row + 8]
        test eax, eax
        jz .game_over

        cmp byte [player_turn], 1
        je .human_turn
        call ai_move
        jmp .next_turn

.human_turn:
        call human_move

.next_turn:
        xor byte [player_turn], 1
        jmp .loop

.game_over:
        ; Last to take loses → previous player wins
        cmp byte [player_turn], 1
        je .ai_wins      ; human just moved (turn flipped), AI wins
        jmp .human_wins

.ai_wins:
        mov eax, SYS_PRINT
        mov ebx, msg_ai_wins
        int 0x80
        ret

.human_wins:
        mov eax, SYS_PRINT
        mov ebx, msg_you_win
        int 0x80
        ret

draw_board:
        mov eax, SYS_PRINT
        mov ebx, msg_board_hdr
        int 0x80
        ; Row 1
        mov eax, SYS_PRINT
        mov ebx, msg_row1
        int 0x80
        mov ecx, [row]
        call draw_objects
        ; Row 2
        mov eax, SYS_PRINT
        mov ebx, msg_row2
        int 0x80
        mov ecx, [row + 4]
        call draw_objects
        ; Row 3
        mov eax, SYS_PRINT
        mov ebx, msg_row3
        int 0x80
        mov ecx, [row + 8]
        call draw_objects
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

draw_objects:
        ; Print ECX '@' characters then newline
        push ecx
.do_loop:
        test ecx, ecx
        jz .do_done
        mov eax, SYS_PUTCHAR
        mov ebx, 'O'
        int 0x80
        dec ecx
        jmp .do_loop
.do_done:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop ecx
        ret

human_move:
        mov eax, SYS_PRINT
        mov ebx, msg_your_turn
        int 0x80
.ask_row:
        mov eax, SYS_PRINT
        mov ebx, msg_ask_row
        int 0x80
        mov eax, SYS_STDIN_READ
        mov ebx, input_buf
        mov ecx, 10
        int 0x80
        movzx eax, byte [input_buf]
        sub eax, '1'
        cmp eax, 2
        ja .ask_row
        mov [chosen_row], eax
        ; Check row is non-empty
        mov ecx, eax
        imul ecx, 4
        cmp dword [row + ecx], 0
        je .ask_row

.ask_count:
        mov eax, SYS_PRINT
        mov ebx, msg_ask_count
        int 0x80
        mov eax, SYS_STDIN_READ
        mov ebx, input_buf
        mov ecx, 10
        int 0x80
        ; Parse number
        xor eax, eax
        mov esi, input_buf
.parse_n:
        mov bl, [esi]
        cmp bl, '0'
        jb .n_done
        cmp bl, '9'
        ja .n_done
        sub bl, '0'
        imul eax, 10
        movzx ebx, bl
        add eax, ebx
        inc esi
        jmp .parse_n
.n_done:
        test eax, eax
        jz .ask_count
        mov ecx, [chosen_row]
        imul ecx, 4
        cmp [row + ecx], eax
        jl .ask_count
        ; Remove EAX objects from chosen row
        sub [row + ecx], eax
        ret

ai_move:
        ; Misère Nim AI:
        ; Compute XOR (nim-sum) of all rows
        ; If nim-sum = 0 → any move (take 1 from largest row)
        ; Else → find row where (row XOR nim-sum) < row
        mov eax, [row]
        xor eax, [row + 4]
        xor eax, [row + 8]
        mov [nim_sum], eax

        test eax, eax
        jz .ai_random

        ; Find a row to reduce
        xor ecx, ecx
.ai_find:
        cmp ecx, 3
        jge .ai_random
        mov edx, ecx
        imul edx, 4
        mov ebx, [row + edx]
        mov eax, ebx
        xor eax, [nim_sum]
        cmp eax, ebx
        jge .ai_next_row
        ; Take (row - (row XOR nim-sum)) objects from this row
        sub ebx, eax
        mov [row + edx], eax
        ; Print AI move
        push ecx
        push ebx
        mov eax, SYS_PRINT
        mov ebx, msg_ai_move
        int 0x80
        pop ebx
        mov eax, SYS_PRINT
        mov ebx, msg_ai_from
        int 0x80
        pop ecx
        push ecx
        push ebx
        inc ecx
        mov eax, ecx
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_ai_obj
        int 0x80
        pop ebx
        mov eax, ebx
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_ai_objs
        int 0x80
        pop ecx
        ret

.ai_next_row:
        inc ecx
        jmp .ai_find

.ai_random:
        ; Take 1 from the first non-empty row
        xor ecx, ecx
.ai_rnd:
        cmp ecx, 3
        jge .ai_done
        mov edx, ecx
        imul edx, 4
        cmp dword [row + edx], 0
        je .ai_rnd_next
        dec dword [row + edx]
        mov eax, SYS_PRINT
        mov ebx, msg_ai_1
        int 0x80
        inc ecx
        mov eax, ecx
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_ai_objs
        int 0x80
        ret
.ai_rnd_next:
        inc ecx
        jmp .ai_rnd
.ai_done:
        ret


msg_board_hdr:  db "=== NIM ===", 10, 0
msg_row1:       db "Row 1: ", 0
msg_row2:       db "Row 2: ", 0
msg_row3:       db "Row 3: ", 0
msg_your_turn:  db "Your turn.", 10, 0
msg_ask_row:    db "Choose row (1-3): ", 0
msg_ask_count:  db "How many to take: ", 0
msg_ai_move:    db "AI takes from row ", 0
msg_ai_from:    db ": ", 0
msg_ai_obj:     db " (", 0
msg_ai_objs:    db " objects)", 10, 0
msg_ai_1:       db "AI takes 1 from row ", 0
msg_you_win:    db "You win! Congratulations!", 10, 0
msg_ai_wins:    db "AI wins. Better luck next time.", 10, 0

row:            dd 5, 4, 3
nim_sum:        dd 0
chosen_row:     dd 0
player_turn:    db 1
input_buf:      times 32 db 0
