; connect4.asm - Connect Four game for Mellivora OS
; Drop discs to get 4 in a row. Play vs COMP.
;
; Controls: 1-7 to pick column. Q to quit.

%include "syscalls.inc"

COLS            equ 7
ROWS            equ 6
EMPTY           equ 0
PLAYER          equ 1
COMP             equ 2

start:
        call init_game

.turn_loop:
        call draw_board
        cmp byte [game_over], 1
        je .game_end

        cmp byte [current], PLAYER
        je .player_move

        ; COMP move
        call cpu_move
        jmp .after_move

.player_move:
        call get_player_move
        cmp eax, -1
        je .quit

.after_move:
        ; Check winner
        call check_win
        cmp eax, PLAYER
        je .player_wins
        cmp eax, COMP
        je .cpu_wins

        ; Check draw (board full)
        call check_full
        cmp eax, 1
        je .draw

        ; Switch turn
        cmp byte [current], PLAYER
        je .to_cpu
        mov byte [current], PLAYER
        jmp .turn_loop
.to_cpu:
        mov byte [current], COMP
        jmp .turn_loop

.player_wins:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_win
        int 0x80
        jmp .play_again

.cpu_wins:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_lose
        int 0x80
        jmp .play_again

.draw:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_draw
        int 0x80

.play_again:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_again
        int 0x80
.pa_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je start
        cmp al, 'Y'
        je start
        cmp al, 'n'
        je .quit
        cmp al, 'N'
        je .quit
        jmp .pa_key

.game_end:
.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
init_game:
        pushad
        ; Clear board
        mov edi, board
        mov ecx, COLS * ROWS
        xor eax, eax
        rep stosb
        mov byte [current], PLAYER
        mov byte [game_over], 0
        popad
        ret

;---------------------------------------
; Board access: board[row * COLS + col]
; Row 0 = top, Row 5 = bottom

get_cell:
        ; EAX = row, EBX = col, returns value in AL
        push edx
        push ecx
        imul eax, COLS
        add eax, ebx
        movzx eax, byte [board + eax]
        pop ecx
        pop edx
        ret

set_cell:
        ; EAX = row, EBX = col, CL = value
        push edx
        imul eax, COLS
        add eax, ebx
        mov [board + eax], cl
        pop edx
        ret

;---------------------------------------
drop_disc:
        ; EBX = column (0-6), CL = player
        ; Returns EAX = row placed, or -1 if full
        pushad
        mov eax, ROWS - 1
.dd_loop:
        cmp eax, 0
        jl .dd_full
        push eax
        push ecx
        call get_cell
        pop ecx
        cmp al, EMPTY
        pop eax
        je .dd_place
        dec eax
        jmp .dd_loop
.dd_place:
        call set_cell
        mov [esp + 28], eax     ; return row via pushad EAX
        popad
        ret
.dd_full:
        mov dword [esp + 28], -1
        popad
        ret

;---------------------------------------
get_player_move:
        ; Returns EAX = column chosen (0-6), or -1 = quit
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_pick
        int 0x80
.gpm_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'q'
        je .gpm_quit
        cmp al, 'Q'
        je .gpm_quit
        cmp al, '1'
        jl .gpm_key
        cmp al, '7'
        jg .gpm_key

        ; Valid column
        sub al, '1'
        movzx ebx, al
        push ebx
        mov cl, PLAYER
        call drop_disc
        pop ebx
        cmp eax, -1
        je .gpm_key             ; column full, try again
        ret

.gpm_quit:
        mov eax, -1
        ret

;---------------------------------------
cpu_move:
        ; Simple AI: try to win, then block, then center, then random
        pushad

        ; First: can we win?
        xor ebx, ebx
.cm_win_check:
        cmp ebx, COLS
        jge .cm_try_block
        push ebx
        mov cl, COMP
        call drop_disc
        cmp eax, -1
        je .cm_win_skip
        ; Check if this wins
        push ebx
        push eax
        call check_win
        pop edx                 ; row
        pop ebx                 ; col
        cmp eax, COMP
        je .cm_chosen           ; winning move!
        ; Undo
        mov eax, edx
        mov cl, EMPTY
        call set_cell
.cm_win_skip:
        pop ebx
        inc ebx
        jmp .cm_win_check

.cm_try_block:
        ; Can player win next?
        xor ebx, ebx
.cm_block_check:
        cmp ebx, COLS
        jge .cm_center
        push ebx
        mov cl, PLAYER
        call drop_disc
        cmp eax, -1
        je .cm_block_skip
        push ebx
        push eax
        call check_win
        pop edx
        pop ebx
        cmp eax, PLAYER
        je .cm_block_found
        ; Undo
        mov eax, edx
        mov cl, EMPTY
        call set_cell
.cm_block_skip:
        pop ebx
        inc ebx
        jmp .cm_block_check

.cm_block_found:
        ; Undo player disc, place COMP disc
        mov eax, edx
        mov cl, EMPTY
        call set_cell
        pop ebx                 ; discard saved ebx
        push ebx
        mov cl, COMP
        call drop_disc
        pop ebx
        jmp .cm_done

.cm_center:
        ; Try center column
        mov ebx, 3
        mov cl, COMP
        call drop_disc
        cmp eax, -1
        jne .cm_done

        ; Random column
        mov eax, SYS_GETTIME
        int 0x80
.cm_rand:
        imul eax, eax, 1103515245
        add eax, 12345
        push eax
        xor edx, edx
        mov ecx, COLS
        div ecx
        mov ebx, edx
        pop eax
        push eax
        mov cl, COMP
        call drop_disc
        pop eax
        cmp eax, -1
        je .cm_rand
        jmp .cm_done

.cm_chosen:
        ; Winning move is already placed. Clean up stack.
        pop ebx                 ; discard saved ebx

.cm_done:
        popad
        ret

;---------------------------------------
check_win:
        ; Returns EAX = winning player (1 or 2), or 0
        pushad
        ; Check all possible 4-in-a-row
        ; Horizontal
        xor esi, esi            ; row
.cw_hrow:
        cmp esi, ROWS
        jge .cw_vert
        xor edi, edi            ; col
.cw_hcol:
        mov eax, COLS - 3
        cmp edi, eax
        jge .cw_hrow_next
        ; Check board[r][c], [r][c+1], [r][c+2], [r][c+3]
        mov eax, esi
        mov ebx, edi
        call get_cell
        cmp al, EMPTY
        je .cw_hnext
        mov dl, al
        ; Check next 3
        mov eax, esi
        lea ebx, [edi + 1]
        call get_cell
        cmp al, dl
        jne .cw_hnext
        mov eax, esi
        lea ebx, [edi + 2]
        call get_cell
        cmp al, dl
        jne .cw_hnext
        mov eax, esi
        lea ebx, [edi + 3]
        call get_cell
        cmp al, dl
        jne .cw_hnext
        ; Win!
        movzx eax, dl
        mov [esp + 28], eax
        popad
        ret
.cw_hnext:
        inc edi
        jmp .cw_hcol
.cw_hrow_next:
        inc esi
        jmp .cw_hrow

.cw_vert:
        ; Vertical check
        xor esi, esi
.cw_vrow:
        mov eax, ROWS - 3
        cmp esi, eax
        jge .cw_diag1
        xor edi, edi
.cw_vcol:
        cmp edi, COLS
        jge .cw_vrow_next
        mov eax, esi
        mov ebx, edi
        call get_cell
        cmp al, EMPTY
        je .cw_vnext
        mov dl, al
        lea eax, [esi + 1]
        mov ebx, edi
        call get_cell
        cmp al, dl
        jne .cw_vnext
        lea eax, [esi + 2]
        mov ebx, edi
        call get_cell
        cmp al, dl
        jne .cw_vnext
        lea eax, [esi + 3]
        mov ebx, edi
        call get_cell
        cmp al, dl
        jne .cw_vnext
        movzx eax, dl
        mov [esp + 28], eax
        popad
        ret
.cw_vnext:
        inc edi
        jmp .cw_vcol
.cw_vrow_next:
        inc esi
        jmp .cw_vrow

.cw_diag1:
        ; Diagonal (down-right) check
        xor esi, esi
.cw_d1row:
        mov eax, ROWS - 3
        cmp esi, eax
        jge .cw_diag2
        xor edi, edi
.cw_d1col:
        mov eax, COLS - 3
        cmp edi, eax
        jge .cw_d1row_next
        mov eax, esi
        mov ebx, edi
        call get_cell
        cmp al, EMPTY
        je .cw_d1next
        mov dl, al
        lea eax, [esi + 1]
        lea ebx, [edi + 1]
        call get_cell
        cmp al, dl
        jne .cw_d1next
        lea eax, [esi + 2]
        lea ebx, [edi + 2]
        call get_cell
        cmp al, dl
        jne .cw_d1next
        lea eax, [esi + 3]
        lea ebx, [edi + 3]
        call get_cell
        cmp al, dl
        jne .cw_d1next
        movzx eax, dl
        mov [esp + 28], eax
        popad
        ret
.cw_d1next:
        inc edi
        jmp .cw_d1col
.cw_d1row_next:
        inc esi
        jmp .cw_d1row

.cw_diag2:
        ; Diagonal (down-left) check
        xor esi, esi
.cw_d2row:
        mov eax, ROWS - 3
        cmp esi, eax
        jge .cw_none
        mov edi, 3
.cw_d2col:
        cmp edi, COLS
        jge .cw_d2row_next
        mov eax, esi
        mov ebx, edi
        call get_cell
        cmp al, EMPTY
        je .cw_d2next
        mov dl, al
        lea eax, [esi + 1]
        lea ebx, [edi - 1]
        call get_cell
        cmp al, dl
        jne .cw_d2next
        lea eax, [esi + 2]
        lea ebx, [edi - 2]
        call get_cell
        cmp al, dl
        jne .cw_d2next
        lea eax, [esi + 3]
        lea ebx, [edi - 3]
        call get_cell
        cmp al, dl
        jne .cw_d2next
        movzx eax, dl
        mov [esp + 28], eax
        popad
        ret
.cw_d2next:
        inc edi
        jmp .cw_d2col
.cw_d2row_next:
        inc esi
        jmp .cw_d2row

.cw_none:
        mov dword [esp + 28], 0
        popad
        ret

;---------------------------------------
check_full:
        ; Returns EAX = 1 if board full, 0 otherwise
        pushad
        xor ecx, ecx
.cf_loop:
        cmp ecx, COLS * ROWS
        jge .cf_full
        cmp byte [board + ecx], EMPTY
        je .cf_not
        inc ecx
        jmp .cf_loop
.cf_not:
        mov dword [esp + 28], 0
        popad
        ret
.cf_full:
        mov dword [esp + 28], 1
        popad
        ret

;---------------------------------------
draw_board:
        pushad
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80

        ; Title
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        ; Column numbers
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_cols
        int 0x80

        ; Board rows
        xor esi, esi
.db_row:
        cmp esi, ROWS
        jge .db_bot

        mov eax, SYS_SETCOLOR
        mov ebx, 0x09           ; blue for frame
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_bar
        int 0x80

        xor edi, edi
.db_col:
        cmp edi, COLS
        jge .db_eol

        ; Get cell
        mov eax, esi
        mov ebx, edi
        call get_cell

        cmp al, PLAYER
        je .db_player
        cmp al, COMP
        je .db_cpu

        ; Empty
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        jmp .db_sep

.db_player:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; red
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'X'
        int 0x80
        jmp .db_sep

.db_cpu:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; yellow
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'O'
        int 0x80

.db_sep:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x09
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80

        inc edi
        jmp .db_col

.db_eol:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc esi
        jmp .db_row

.db_bot:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x09
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_bottom
        int 0x80

        ; Legend
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_legend
        int 0x80

        popad
        ret

;=======================================
; Data
;=======================================
msg_title:      db "  === CONNECT FOUR ===", 10, 10, 0
msg_cols:       db "   1 2 3 4 5 6 7", 10, 0
str_bar:        db "  |", 0
msg_bottom:     db "  +-------------+", 10, 0
msg_legend:     db 10, "  X=You  O=COMP", 10, 0
msg_pick:       db "  Pick column (1-7): ", 0
msg_win:        db 10, "  You WIN! Four in a row!", 10, 0
msg_lose:       db 10, "  COMP wins. Better luck next time!", 10, 0
msg_draw:       db 10, "  Draw! Board is full.", 10, 0
msg_again:      db "  Play again? (Y/N) ", 0

; Game state
board:          times COLS * ROWS db 0
current:        db PLAYER
game_over:      db 0
