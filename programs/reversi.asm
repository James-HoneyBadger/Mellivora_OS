; reversi.asm - Reversi/Othello (player vs greedy AI)
; 8x8 board, player=Black(B), AI=White(W)
; Greedy AI: picks move that flips the most pieces

%include "syscalls.inc"

EMPTY   equ 0
BLACK   equ 1
WHITE   equ 2

start:
        call init_board
        call game_loop
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

init_board:
        mov edi, board
        mov ecx, 64
        xor eax, eax
        rep stosb
        ; Standard starting position
        mov byte [board + 3*8 + 3], WHITE
        mov byte [board + 3*8 + 4], BLACK
        mov byte [board + 4*8 + 3], BLACK
        mov byte [board + 4*8 + 4], WHITE
        ret

game_loop:
.loop:
        ; Mark valid moves for black
        call mark_valid_moves
        call draw_board
        ; Check if black has moves
        call has_valid_moves
        test eax, eax
        jnz .black_turn
        ; Black has no moves — check if white has any
        mov byte [current_player], WHITE
        call mark_valid_moves
        call has_valid_moves
        test eax, eax
        jz .game_over
        ; White plays both turns — handled below
        jmp .white_turn

.black_turn:
        mov byte [current_player], BLACK
        call mark_valid_moves
        mov eax, SYS_PRINT
        mov ebx, msg_your_turn
        int 0x80
        call player_move
        call unmark_valid

.white_turn:
        ; Check if white has valid moves
        mov byte [current_player], WHITE
        call mark_valid_moves
        call has_valid_moves
        test eax, eax
        jz .skip_white
        call ai_move
        call unmark_valid
.skip_white:
        mov byte [current_player], BLACK
        jmp .loop

.game_over:
        call draw_board
        call print_score
        ret

mark_valid_moves:
        ; For each empty cell, check if placing current_player there is valid
        xor ebp, ebp
.mv_scan:
        cmp ebp, 64
        jge .mv_done
        movzx eax, byte [board + ebp]
        test eax, eax
        jnz .mv_next   ; not empty
        ; Try placing here
        push ebp
        call count_flips     ; EBP=pos → EAX=flip count
        pop ebp
        test eax, eax
        jz .mv_next
        mov byte [board + ebp], 3   ; mark as valid (3)
.mv_next:
        inc ebp
        jmp .mv_scan
.mv_done:
        ret

unmark_valid:
        xor ebp, ebp
.uv:    cmp ebp, 64
        jge .uv_done
        cmp byte [board + ebp], 3
        jne .uv_next
        mov byte [board + ebp], EMPTY
.uv_next:
        inc ebp
        jmp .uv
.uv_done:
        ret

has_valid_moves:
        xor ebp, ebp
.hv:    cmp ebp, 64
        jge .hv_no
        cmp byte [board + ebp], 3
        je .hv_yes
        inc ebp
        jmp .hv
.hv_yes:
        mov eax, 1
        ret
.hv_no:
        xor eax, eax
        ret

; count_flips: EBP = position, current_player in [current_player]
; Returns EAX = number of opponent pieces that would be flipped
count_flips:
        pushad
        xor edi, edi        ; total flip count
        ; Convert EBP to row/col
        mov eax, ebp
        xor edx, edx
        mov ecx, 8
        div ecx
        mov [cf_row], eax
        mov [cf_col], edx

        ; 8 directions: (-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1)
        mov esi, 0
.cf_dir:
        cmp esi, 8
        jge .cf_done

        movsx eax, byte [dir_dr + esi]
        movsx ecx, byte [dir_dc + esi]
        mov [cur_dr], eax
        mov [cur_dc], ecx

        ; Walk in direction
        mov eax, [cf_row]
        add eax, [cur_dr]
        mov [walk_r], eax
        mov eax, [cf_col]
        add eax, [cur_dc]
        mov [walk_c], eax
        xor ecx, ecx    ; opponent count in this direction

.cf_walk:
        ; Check bounds
        cmp dword [walk_r], 0
        jl .cf_no_flip
        cmp dword [walk_r], 7
        jg .cf_no_flip
        cmp dword [walk_c], 0
        jl .cf_no_flip
        cmp dword [walk_c], 7
        jg .cf_no_flip

        mov eax, [walk_r]
        imul eax, 8
        add eax, [walk_c]
        movzx ebx, byte [board + eax]

        cmp ebx, EMPTY
        je .cf_no_flip
        cmp ebx, 3       ; valid marker = empty-equivalent
        je .cf_no_flip

        ; Is it opponent?
        movzx edx, byte [current_player]
        cmp ebx, edx
        je .cf_found_own

        ; It's opponent piece
        inc ecx
        mov eax, [walk_r]
        add eax, [cur_dr]
        mov [walk_r], eax
        mov eax, [walk_c]
        add eax, [cur_dc]
        mov [walk_c], eax
        jmp .cf_walk

.cf_found_own:
        ; Found own piece — ecx opponents in between
        add edi, ecx
        jmp .cf_next_dir

.cf_no_flip:
.cf_next_dir:
        inc esi
        jmp .cf_dir

.cf_done:
        mov [esp + 28], edi     ; return EAX via pushad frame
        popad
        ret

draw_board:
        mov eax, SYS_PRINT
        mov ebx, msg_hdr
        int 0x80
        mov ebp, 0
.db_row:
        cmp ebp, 8
        jge .db_done
        mov eax, ebp
        add eax, '1'
        push eax
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        xor ecx, ecx
.db_col:
        cmp ecx, 8
        jge .db_nl
        mov eax, ebp
        imul eax, 8
        add eax, ecx
        movzx ebx, byte [board + eax]
        call draw_cell
        inc ecx
        jmp .db_col
.db_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc ebp
        jmp .db_row
.db_done:
        ; Count and show score
        call print_score
        ret

draw_cell:
        cmp ebx, BLACK
        jne .dc_w
        mov eax, SYS_PRINT
        mov ebx, msg_black
        int 0x80
        ret
.dc_w:
        cmp ebx, WHITE
        jne .dc_v
        mov eax, SYS_PRINT
        mov ebx, msg_white
        int 0x80
        ret
.dc_v:
        cmp ebx, 3
        jne .dc_e
        mov eax, SYS_PRINT
        mov ebx, msg_valid
        int 0x80
        ret
.dc_e:
        mov eax, SYS_PRINT
        mov ebx, msg_empty
        int 0x80
        ret

print_score:
        xor ecx, ecx
        xor edx, edx
        xor ebp, ebp
.ps:
        cmp ebp, 64
        jge .ps_done
        movzx eax, byte [board + ebp]
        cmp eax, BLACK
        jne .ps_w
        inc ecx
        jmp .ps_next
.ps_w:
        cmp eax, WHITE
        jne .ps_next
        inc edx
.ps_next:
        inc ebp
        jmp .ps
.ps_done:
        mov eax, SYS_PRINT
        mov ebx, msg_score
        int 0x80
        mov eax, ecx
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_score2
        int 0x80
        mov eax, edx
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

player_move:
.ask:
        mov eax, SYS_PRINT
        mov ebx, msg_ask
        int 0x80
        mov eax, SYS_STDIN_READ
        mov ebx, input_buf
        mov ecx, 10
        int 0x80
        ; Parse "RC"
        movzx eax, byte [input_buf]
        sub eax, '1'
        cmp eax, 7
        ja .ask
        mov [pm_row], eax
        movzx eax, byte [input_buf + 1]
        sub eax, '1'
        cmp eax, 7
        ja .ask
        mov [pm_col], eax

        ; Validate: must be a marked valid cell
        mov eax, [pm_row]
        imul eax, 8
        add eax, [pm_col]
        cmp byte [board + eax], 3
        jne .ask

        ; Place and flip
        call do_place
        ret

ai_move:
        ; Find valid move (cell=3) with most flips
        mov dword [best_pos], -1
        mov dword [best_flips], -1
        xor ebp, ebp
.ai_scan:
        cmp ebp, 64
        jge .ai_done_scan
        cmp byte [board + ebp], 3
        jne .ai_next
        call count_flips
        cmp eax, [best_flips]
        jle .ai_next
        mov [best_flips], eax
        mov [best_pos], ebp
.ai_next:
        inc ebp
        jmp .ai_scan
.ai_done_scan:
        cmp dword [best_pos], -1
        je .ai_no_move
        mov ebp, [best_pos]
        ; Convert to row/col for do_place
        mov eax, ebp
        xor edx, edx
        mov ecx, 8
        div ecx
        mov [pm_row], eax
        mov [pm_col], edx
        call do_place
        ; Print AI move
        mov eax, SYS_PRINT
        mov ebx, msg_ai
        int 0x80
        mov eax, [pm_row]
        add eax, '1'
        push eax
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        mov eax, [pm_col]
        add eax, '1'
        push eax
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
.ai_no_move:
        ret

do_place:
        ; Place current_player at (pm_row, pm_col) and flip pieces
        mov eax, [pm_row]
        imul eax, 8
        add eax, [pm_col]
        movzx ecx, byte [current_player]
        mov [board + eax], cl

        ; Walk all 8 directions and flip
        mov esi, 0
.dp_dir:
        cmp esi, 8
        jge .dp_done
        movsx eax, byte [dir_dr + esi]
        movsx ecx, byte [dir_dc + esi]
        mov [cur_dr], eax
        mov [cur_dc], ecx

        mov eax, [pm_row]
        add eax, [cur_dr]
        mov [walk_r], eax
        mov eax, [pm_col]
        add eax, [cur_dc]
        mov [walk_c], eax

        ; Collect opponents in a temp array
        mov dword [flip_count], 0
.dp_walk:
        cmp dword [walk_r], 0
        jl .dp_no_flip
        cmp dword [walk_r], 7
        jg .dp_no_flip
        cmp dword [walk_c], 0
        jl .dp_no_flip
        cmp dword [walk_c], 7
        jg .dp_no_flip

        mov eax, [walk_r]
        imul eax, 8
        add eax, [walk_c]
        movzx ebx, byte [board + eax]
        cmp ebx, EMPTY
        je .dp_no_flip
        cmp ebx, 3
        je .dp_no_flip

        movzx edx, byte [current_player]
        cmp ebx, edx
        je .dp_do_flip

        ; Opponent: record position
        mov ecx, [flip_count]
        mov [flip_buf + ecx*4], eax
        inc dword [flip_count]
        mov eax, [walk_r]
        add eax, [cur_dr]
        mov [walk_r], eax
        mov eax, [walk_c]
        add eax, [cur_dc]
        mov [walk_c], eax
        jmp .dp_walk

.dp_do_flip:
        ; Own piece found — flip all recorded opponents
        mov ecx, [flip_count]
        test ecx, ecx
        jz .dp_no_flip
        movzx edx, byte [current_player]
.dp_flip_loop:
        dec ecx
        js .dp_next_dir
        mov eax, [flip_buf + ecx*4]
        mov [board + eax], dl
        jmp .dp_flip_loop
.dp_no_flip:
.dp_next_dir:
        inc esi
        jmp .dp_dir
.dp_done:
        ret


msg_hdr:        db "=== REVERSI ===", 10
                db "  12345678", 10, 0
msg_black:      db "B", 0
msg_white:      db "W", 0
msg_valid:      db "*", 0
msg_empty:      db ".", 0
msg_score:      db "Score - B:", 0
msg_score2:     db " W:", 0
msg_your_turn:  db "Your turn (Black=B)", 10, 0
msg_ask:        db "Enter move (RC, e.g. 34): ", 0
msg_ai:         db "AI (White) plays: ", 0

; Direction tables (dr, dc for 8 directions)
dir_dr:         db -1, -1, -1,  0,  0,  1,  1,  1
dir_dc:         db -1,  0,  1, -1,  1, -1,  0,  1

current_player: db BLACK
cf_row:         dd 0
cf_col:         dd 0
cur_dr:         dd 0
cur_dc:         dd 0
walk_r:         dd 0
walk_c:         dd 0
pm_row:         dd 0
pm_col:         dd 0
best_pos:       dd -1
best_flips:     dd -1
flip_count:     dd 0
flip_buf:       times 8 dd 0    ; max 7 pieces in one direction
input_buf:      times 16 db 0
board:          times 64 db 0
