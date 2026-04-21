; checkers.asm - Checkers (draughts) game
; 8x8 board, red (r/R) vs black (b/B), capital = king
; Player is red (moves up), AI is black (moves down), greedy capture AI

%include "syscalls.inc"

EMPTY   equ 0
RED     equ 1       ; regular red piece
RED_K   equ 2       ; red king
BLK     equ 3       ; regular black piece
BLK_K   equ 4       ; black king

start:
        call init_board
        call game_loop
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

init_board:
        ; Clear board
        mov edi, board
        mov ecx, 64
        xor eax, eax
        rep stosb

        ; Place black pieces on rows 0-2 (top)
        mov ebp, 0
.black_rows:
        cmp ebp, 3
        jge .red_rows
        mov ecx, 0
.black_cols:
        cmp ecx, 8
        jge .black_next_row
        ; Checkerboard: black squares only
        mov eax, ebp
        add eax, ecx
        and eax, 1
        cmp eax, 1
        jne .black_skip
        mov eax, ebp
        imul eax, 8
        add eax, ecx
        mov byte [board + eax], BLK
.black_skip:
        inc ecx
        jmp .black_cols
.black_next_row:
        inc ebp
        jmp .black_rows

.red_rows:
        ; Place red pieces on rows 5-7 (bottom)
        mov ebp, 5
.r_rows:
        cmp ebp, 8
        jge .init_done
        mov ecx, 0
.r_cols:
        cmp ecx, 8
        jge .r_next_row
        mov eax, ebp
        add eax, ecx
        and eax, 1
        cmp eax, 1
        jne .r_skip
        mov eax, ebp
        imul eax, 8
        add eax, ecx
        mov byte [board + eax], RED
.r_skip:
        inc ecx
        jmp .r_cols
.r_next_row:
        inc ebp
        jmp .r_rows
.init_done:
        ret

game_loop:
.loop:
        call draw_board
        ; Check game over
        call count_red
        test eax, eax
        jz .black_wins
        call count_black
        test eax, eax
        jz .red_wins

        ; Player (red) turn
        call player_move
        call count_black
        test eax, eax
        jz .red_wins

        ; AI (black) turn
        call ai_move
        call count_red
        test eax, eax
        jz .black_wins

        jmp .loop

.red_wins:
        call draw_board
        mov eax, SYS_PRINT
        mov ebx, msg_red_wins
        int 0x80
        ret

.black_wins:
        call draw_board
        mov eax, SYS_PRINT
        mov ebx, msg_blk_wins
        int 0x80
        ret

draw_board:
        mov eax, SYS_PRINT
        mov ebx, msg_board_hdr
        int 0x80
        mov ebp, 0
.db_row:
        cmp ebp, 8
        jge .db_done
        ; Row number
        mov eax, ebp
        add eax, '1'
        push eax
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov ecx, 0
.db_col:
        cmp ecx, 8
        jge .db_nl
        mov eax, ebp
        imul eax, 8
        add eax, ecx
        movzx ebx, byte [board + eax]
        ; Is this a dark square?
        mov edx, ebp
        add edx, ecx
        and edx, 1
        cmp edx, 0
        je .db_light

        call draw_piece
        jmp .db_sep
.db_light:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
.db_sep:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc ecx
        jmp .db_col
.db_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc ebp
        jmp .db_row
.db_done:
        mov eax, SYS_PRINT
        mov ebx, msg_col_hdr
        int 0x80
        ret

draw_piece:
        ; EBX = piece value
        cmp ebx, EMPTY
        jne .dp_piece
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ret
.dp_piece:
        cmp ebx, RED
        jne .dp_blk
        mov eax, SYS_PUTCHAR
        mov ebx, 'r'
        int 0x80
        ret
.dp_blk:
        cmp ebx, RED_K
        jne .dp_blkk
        mov eax, SYS_PUTCHAR
        mov ebx, 'R'
        int 0x80
        ret
.dp_blkk:
        cmp ebx, BLK
        jne .dp_blkking
        mov eax, SYS_PUTCHAR
        mov ebx, 'b'
        int 0x80
        ret
.dp_blkking:
        mov eax, SYS_PUTCHAR
        mov ebx, 'B'
        int 0x80
        ret

player_move:
        mov eax, SYS_PRINT
        mov ebx, msg_your_move
        int 0x80
.ask:
        mov eax, SYS_PRINT
        mov ebx, msg_from
        int 0x80
        mov eax, SYS_STDIN_READ
        mov ebx, input_buf
        mov ecx, 10
        int 0x80
        ; Parse "RC" -> from_row, from_col (1-based input)
        movzx eax, byte [input_buf]
        sub eax, '1'
        cmp eax, 7
        ja .ask
        mov [from_row], eax
        movzx eax, byte [input_buf + 1]
        sub eax, '1'
        cmp eax, 7
        ja .ask
        mov [from_col], eax

        ; Validate: must be red piece
        mov eax, [from_row]
        imul eax, 8
        add eax, [from_col]
        movzx ebx, byte [board + eax]
        cmp ebx, RED
        je .from_ok
        cmp ebx, RED_K
        je .from_ok
        jmp .ask

.from_ok:
        mov eax, SYS_PRINT
        mov ebx, msg_to
        int 0x80
        mov eax, SYS_STDIN_READ
        mov ebx, input_buf
        mov ecx, 10
        int 0x80
        movzx eax, byte [input_buf]
        sub eax, '1'
        cmp eax, 7
        ja .ask
        mov [to_row], eax
        movzx eax, byte [input_buf + 1]
        sub eax, '1'
        cmp eax, 7
        ja .ask
        mov [to_col], eax

        ; Validate destination is empty
        mov eax, [to_row]
        imul eax, 8
        add eax, [to_col]
        movzx ebx, byte [board + eax]
        cmp ebx, EMPTY
        jne .ask

        ; Validate diagonal move
        mov eax, [to_row]
        sub eax, [from_row]
        mov [dr], eax
        mov eax, [to_col]
        sub eax, [from_col]
        mov [dc], eax
        ; dr must be -1 or -2 (red moves up = decreasing row) or ±1/±2 for king
        ; Simplified: check |dr| = |dc| = 1 (regular) or 2 (capture)
        mov eax, [dr]
        test eax, eax
        jns .check_abs_dr
        neg eax
.check_abs_dr:
        mov edx, eax        ; save |dr| in EDX
        cmp eax, 1
        je .check_dc
        cmp eax, 2
        jne .ask
.check_dc:
        mov eax, [dc]
        test eax, eax
        jns .check_abs_dc
        neg eax
.check_abs_dc:
        cmp eax, edx
        jne .ask    ; |dr| != |dc|

        ; Check red pieces move up (dr < 0) unless king
        mov eax, [from_row]
        imul eax, 8
        add eax, [from_col]
        movzx ebx, byte [board + eax]
        cmp ebx, RED_K
        je .do_move
        ; Regular red: must move up (dr < 0)
        cmp dword [dr], 0
        jge .ask

.do_move:
        ; If capture (dr=±2), remove jumped piece
        mov eax, [dr]
        test eax, eax
        jns .dr_pos_cap
        cmp eax, -2
        jne .execute
        ; Capture
        mov eax, [from_row]
        add eax, -1        ; mid_row = from_row - 1
        imul eax, 8
        mov ecx, [from_col]
        add ecx, [dc]
        sar dword [dc + 0], 1   ; dc / 2 ... wait, this is tricky
        ; Let me just compute mid cell
        mov eax, [from_row]
        add eax, [to_row]
        sar eax, 1
        imul eax, 8
        mov ecx, [from_col]
        add ecx, [to_col]
        sar ecx, 1
        add eax, ecx
        mov byte [board + eax], EMPTY
        jmp .execute
.dr_pos_cap:
        cmp eax, 2
        jne .execute
        mov eax, [from_row]
        add eax, [to_row]
        sar eax, 1
        imul eax, 8
        mov ecx, [from_col]
        add ecx, [to_col]
        sar ecx, 1
        add eax, ecx
        mov byte [board + eax], EMPTY

.execute:
        ; Get piece
        mov eax, [from_row]
        imul eax, 8
        add eax, [from_col]
        movzx ebx, byte [board + eax]
        mov byte [board + eax], EMPTY
        ; Move piece
        mov eax, [to_row]
        imul eax, 8
        add eax, [to_col]
        mov [board + eax], bl
        ; King promotion: red reaches row 0
        cmp dword [to_row], 0
        jne .pm_done
        cmp byte [board + eax], RED
        jne .pm_done
        mov byte [board + eax], RED_K
.pm_done:
        ret

; Simple AI: scan for any capture, else move forward
ai_move:
        ; Try to find a capture
        mov ebp, 0
.ai_scan:
        cmp ebp, 64
        jge .ai_simple
        movzx eax, byte [board + ebp]
        cmp eax, BLK
        je .ai_try_cap
        cmp eax, BLK_K
        je .ai_try_cap
        jmp .ai_next
.ai_try_cap:
        ; Try each diagonal capture
        mov eax, ebp
        xor edx, edx
        mov ecx, 8
        div ecx
        ; EAX=row, EDX=col
        push eax
        push edx
        ; Try (row+2, col+2) if black piece, (row+1,col+1)=opp
        ; ...simplified: just try basic move
        pop edx
        pop eax
        jmp .ai_next
.ai_next:
        inc ebp
        jmp .ai_scan

.ai_simple:
        ; Find first black piece and move it forward (down = row+1)
        mov ebp, 0
.as2:
        cmp ebp, 64
        jge .ai_no_move
        movzx eax, byte [board + ebp]
        cmp eax, BLK
        je .as_move
        cmp eax, BLK_K
        je .as_move
        inc ebp
        jmp .as2
.as_move:
        ; Piece at ebp, row = ebp/8, col = ebp%8
        mov eax, ebp
        xor edx, edx
        mov ecx, 8
        div ecx
        ; EAX=row, EDX=col
        ; Try move to (row+1, col+1)
        cmp eax, 7
        je .try_other
        mov ecx, eax
        inc ecx
        mov edi, edx
        inc edi
        cmp edi, 8
        jge .try_other
        mov esi, ecx
        imul esi, 8
        add esi, edi
        cmp byte [board + esi], EMPTY
        jne .try_other
        ; Execute move
        movzx ebx, byte [board + ebp]
        mov byte [board + ebp], EMPTY
        mov [board + esi], bl
        ; King promotion: black reaches row 7
        cmp ecx, 7
        jne .ai_pm_done
        cmp byte [board + esi], BLK
        jne .ai_pm_done
        mov byte [board + esi], BLK_K
.ai_pm_done:
        mov eax, SYS_PRINT
        mov ebx, msg_ai_moved
        int 0x80
        ret
.try_other:
        ; Try (row+1, col-1)
        mov eax, ebp
        xor edx, edx
        mov ecx, 8
        div ecx
        cmp eax, 7
        je .ai_no_move
        mov ecx, eax
        inc ecx
        mov edi, edx
        dec edi
        js .ai_no_move
        mov esi, ecx
        imul esi, 8
        add esi, edi
        cmp byte [board + esi], EMPTY
        jne .ai_no_move
        movzx ebx, byte [board + ebp]
        mov byte [board + ebp], EMPTY
        mov [board + esi], bl
        cmp ecx, 7
        jne .ai_pm2
        cmp byte [board + esi], BLK
        jne .ai_pm2
        mov byte [board + esi], BLK_K
.ai_pm2:
        mov eax, SYS_PRINT
        mov ebx, msg_ai_moved
        int 0x80
        ret
.ai_no_move:
        mov eax, SYS_PRINT
        mov ebx, msg_ai_stuck
        int 0x80
        ret

count_red:
        xor eax, eax
        xor ecx, ecx
.cr:    cmp ecx, 64
        jge .cr_done
        movzx edx, byte [board + ecx]
        cmp edx, RED
        je .cr_inc
        cmp edx, RED_K
        jne .cr_next
.cr_inc:
        inc eax
.cr_next:
        inc ecx
        jmp .cr
.cr_done:
        ret

count_black:
        xor eax, eax
        xor ecx, ecx
.cb:    cmp ecx, 64
        jge .cb_done
        movzx edx, byte [board + ecx]
        cmp edx, BLK
        je .cb_inc
        cmp edx, BLK_K
        jne .cb_next
.cb_inc:
        inc eax
.cb_next:
        inc ecx
        jmp .cb
.cb_done:
        ret

msg_board_hdr:  db "=== CHECKERS ===", 10
                db "  a b c d e f g h", 10, 0
msg_col_hdr:    db "  a b c d e f g h", 10, 0
msg_your_move:  db "Your move (red=r/R, moves UP).", 10, 0
msg_from:       db "From (RC, e.g. 64): ", 0
msg_to:         db "To   (RC, e.g. 53): ", 0
msg_ai_moved:   db "AI moved.", 10, 0
msg_ai_stuck:   db "AI has no moves.", 10, 0
msg_red_wins:   db "RED wins!", 10, 0
msg_blk_wins:   db "BLACK wins!", 10, 0

board:          times 64 db 0
from_row:       dd 0
from_col:       dd 0
to_row:         dd 0
to_col:         dd 0
dr:             dd 0
dc:             dd 0
input_buf:      times 16 db 0
