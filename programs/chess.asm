; chess.asm - Mellivora Chess (v9.0)
; VBE 1024x768x32bpp chess with greedy AI.
; Controls: Arrow keys=cursor, Enter=select/move, R=reset, ESC=quit.
; White = human (bottom), Black = AI (top).

%include "syscalls.inc"
%include "lib/vbe_game.inc"
%include "lib/font.inc"

EMPTY  equ 0
PAWN   equ 1
KNIGHT equ 2
BISHOP equ 3
ROOK   equ 4
QUEEN  equ 5
KING   equ 6
WHITE  equ 0x00
BLACK  equ 0x10

VAL_PAWN   equ 100
VAL_KNIGHT equ 320
VAL_BISHOP equ 330
VAL_ROOK   equ 500
VAL_QUEEN  equ 900
VAL_KING   equ 20000

CELL_SIZE  equ 80
BOARD_X    equ 112
BOARD_Y    equ 24

COL_LIGHT  equ 0x00F0D9B5
COL_DARK   equ 0x00B58863
COL_SELECT equ 0x0044FF44
COL_CURSOR equ 0x00FFFF00
COL_WHITE  equ 0x00FFFFFF
COL_BLACK  equ 0x00111111
COL_RED    equ 0x00FF4444
COL_BG     equ 0x00181818
COL_DIM    equ 0x00888888

start:
        VBE_GAME_INIT
        call chess_init_board
        mov byte [cursor_r], 6
        mov byte [cursor_f], 4
        mov byte [sel_r],    0xFF
        mov byte [sel_f],    0xFF
        mov byte [game_over], 0
        mov byte [turn],     0

.main_loop:
        call chess_draw
        VBE_GAME_PRESENT

        cmp byte [turn], 1
        jne .human_turn
        call chess_ai_move
        mov byte [turn], 0
        cmp byte [game_over], 1
        je .wait_endgame
        jmp .main_loop

.human_turn:
        VBE_GAME_POLL_KEY
        cmp eax, -1
        je .main_loop
        cmp al, KEY_ESC
        je .exit
        cmp al, 'r'
        je .reset
        cmp al, 'R'
        je .reset
        cmp al, KEY_UP
        je .cur_up
        cmp al, KEY_DOWN
        je .cur_down
        cmp al, KEY_LEFT
        je .cur_left
        cmp al, KEY_RIGHT
        je .cur_right
        cmp al, KEY_ENTER
        je .select
        jmp .main_loop
.cur_up:
        cmp byte [cursor_r], 0
        je .main_loop
        dec byte [cursor_r]
        jmp .main_loop
.cur_down:
        cmp byte [cursor_r], 7
        je .main_loop
        inc byte [cursor_r]
        jmp .main_loop
.cur_left:
        cmp byte [cursor_f], 0
        je .main_loop
        dec byte [cursor_f]
        jmp .main_loop
.cur_right:
        cmp byte [cursor_f], 7
        je .main_loop
        inc byte [cursor_f]
        jmp .main_loop

.select:
        cmp byte [sel_r], 0xFF
        jne .try_move
        ; Select: must be a white piece
        movzx eax, byte [cursor_r]
        imul eax, 8
        movzx ebx, byte [cursor_f]
        add eax, ebx
        movzx ecx, byte [board + eax]
        test ecx, ecx
        jz .main_loop
        test ecx, BLACK
        jnz .main_loop
        mov al, [cursor_r]
        mov [sel_r], al
        mov al, [cursor_f]
        mov [sel_f], al
        jmp .main_loop
.try_move:
        call chess_try_move
        cmp byte [game_over], 1
        je .wait_endgame
        jmp .main_loop

.wait_endgame:
        call chess_draw
        VBE_GAME_PRESENT
.we_k:  VBE_GAME_POLL_KEY
        cmp eax, -1
        je .we_k
        cmp al, 'r'
        je .reset
        cmp al, KEY_ESC
        je .exit
        jmp .we_k

.reset:
        call chess_init_board
        mov byte [cursor_r], 6
        mov byte [cursor_f], 4
        mov byte [sel_r], 0xFF
        mov byte [sel_f], 0xFF
        mov byte [game_over], 0
        mov byte [turn], 0
        jmp .main_loop

.exit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

chess_init_board:
        pushad
        mov edi, board
        xor eax, eax
        mov ecx, 64
        rep stosd
        mov byte [board + 0],  BLACK|ROOK
        mov byte [board + 1],  BLACK|KNIGHT
        mov byte [board + 2],  BLACK|BISHOP
        mov byte [board + 3],  BLACK|QUEEN
        mov byte [board + 4],  BLACK|KING
        mov byte [board + 5],  BLACK|BISHOP
        mov byte [board + 6],  BLACK|KNIGHT
        mov byte [board + 7],  BLACK|ROOK
        mov ecx, 8
        mov edi, board + 8
        mov al, BLACK|PAWN
        rep stosb
        mov ecx, 8
        mov edi, board + 48
        mov al, WHITE|PAWN
        rep stosb
        mov byte [board + 56], WHITE|ROOK
        mov byte [board + 57], WHITE|KNIGHT
        mov byte [board + 58], WHITE|BISHOP
        mov byte [board + 59], WHITE|QUEEN
        mov byte [board + 60], WHITE|KING
        mov byte [board + 61], WHITE|BISHOP
        mov byte [board + 62], WHITE|KNIGHT
        mov byte [board + 63], WHITE|ROOK
        popad
        ret

chess_draw:
        pushad
        mov edx, COL_BG
        call vbe_clear_screen
        xor esi, esi            ; rank (row)
.cdr_rank:
        cmp esi, 8
        jge .cdr_done
        xor edi, edi            ; file (col)
.cdr_file:
        cmp edi, 8
        jge .cdr_nr
        ; Determine square highlight color
        mov eax, esi
        add eax, edi
        and eax, 1
        mov edx, COL_LIGHT
        jz .cdr_sq_col
        mov edx, COL_DARK
.cdr_sq_col:
        movzx eax, byte [sel_r]
        cmp eax, 0xFF
        je .cdr_chk_cursor
        cmp eax, esi
        jne .cdr_chk_cursor
        movzx eax, byte [sel_f]
        cmp eax, edi
        jne .cdr_chk_cursor
        mov edx, COL_SELECT
        jmp .cdr_draw_sq
.cdr_chk_cursor:
        movzx eax, byte [cursor_r]
        cmp eax, esi
        jne .cdr_draw_sq
        movzx eax, byte [cursor_f]
        cmp eax, edi
        jne .cdr_draw_sq
        mov edx, COL_CURSOR
.cdr_draw_sq:
        ; vbe_fill_rect: EBX=x ECX=y EDX=w ESI=h EDI=color
        ; Push loop vars (esi=rank, edi=file)
        push esi
        push edi
        push edx                ; save square color
        mov ebx, edi
        imul ebx, CELL_SIZE
        add ebx, BOARD_X
        mov ecx, esi
        imul ecx, CELL_SIZE
        add ecx, BOARD_Y
        pop edi                 ; EDI = color
        mov edx, CELL_SIZE
        mov esi, CELL_SIZE
        call vbe_fill_rect
        pop edi                 ; restore file
        pop esi                 ; restore rank
        ; Draw piece if present
        mov eax, esi
        imul eax, 8
        add eax, edi
        movzx eax, byte [board + eax]
        test eax, eax
        jz .cdr_np
        ; Compute pixel x,y for char center
        push eax                ; save piece byte
        mov ebx, edi
        imul ebx, CELL_SIZE
        add ebx, BOARD_X + CELL_SIZE/2 - 4
        mov ecx, esi
        imul ecx, CELL_SIZE
        add ecx, BOARD_Y + CELL_SIZE/2 - 4
        pop eax                 ; restore piece byte
        ; Choose piece color
        mov esi, COL_WHITE
        test eax, BLACK
        jz .cdr_pc
        mov esi, COL_BLACK
.cdr_pc:
        and eax, 0x0F
        dec eax
        jl .cdr_np
        cmp eax, 5
        jg .cdr_np
        movzx edx, byte [piece_chars + eax]
        mov eax, 1              ; scale=1
        call vbe_draw_char
.cdr_np:
        inc edi
        jmp .cdr_file
.cdr_nr:
        inc esi
        jmp .cdr_rank
.cdr_done:
        ; Status line at bottom of board
        mov ebx, BOARD_X
        mov ecx, BOARD_Y + 8 * CELL_SIZE + 12
        cmp byte [game_over], 1
        je .cdr_go
        cmp byte [turn], 0
        jne .cdr_ai
        mov edx, msg_white_turn
        jmp .cdr_str
.cdr_ai:
        mov edx, msg_ai_thinking
        jmp .cdr_str
.cdr_go:
        mov edx, msg_gameover
.cdr_str:
        mov esi, COL_WHITE
        mov eax, 1
        call vbe_draw_str
        popad
        ret

chess_try_move:
        pushad
        movzx eax, byte [sel_r]
        movzx ebx, byte [sel_f]
        movzx ecx, byte [cursor_r]
        movzx edx, byte [cursor_f]
        cmp eax, ecx
        jne .ctm_diff
        cmp ebx, edx
        jne .ctm_diff
        mov byte [sel_r], 0xFF
        mov byte [sel_f], 0xFF
        jmp .ctm_done
.ctm_diff:
        mov esi, ecx
        imul esi, 8
        add esi, edx
        movzx edi, byte [board + esi]
        test edi, edi
        jz .ctm_can
        test edi, BLACK
        jz .ctm_fail
.ctm_can:
        ; Check if white captures black king
        movzx edi, byte [board + esi]
        and edi, 0x0F
        cmp edi, KING
        jne .ctm_move
        mov byte [game_over], 1
.ctm_move:
        mov ebp, eax
        imul ebp, 8
        add ebp, ebx
        movzx edi, byte [board + ebp]
        mov eax, edi
        mov [ai_save_src], al
        mov ebp, ecx
        imul ebp, 8
        add ebp, edx
        movzx edi, byte [board + ebp]  ; captured
        mov eax, edi
        mov [ai_save_dst], al
        movzx eax, byte [ai_save_src]
        mov [board + ebp], al          ; apply move (src piece to dst sq)
        ; Restore cleanly
        movzx eax, byte [sel_r]
        imul eax, 8
        movzx ebx, byte [sel_f]
        add eax, ebx
        mov byte [board + eax], EMPTY
        ; Promotion
        movzx ecx, byte [cursor_r]
        cmp ecx, 0
        jne .ctm_np
        movzx esi, byte [board + ebp]
        cmp si, WHITE|PAWN
        jne .ctm_np
        mov byte [board + ebp], WHITE|QUEEN
.ctm_np:
        mov byte [sel_r], 0xFF
        mov byte [sel_f], 0xFF
        cmp byte [game_over], 1
        je .ctm_done
        mov byte [turn], 1
        jmp .ctm_done
.ctm_fail:
        mov byte [sel_r], 0xFF
        mov byte [sel_f], 0xFF
.ctm_done:
        popad
        ret

chess_ai_move:
        pushad
        mov dword [best_score], -32768
        mov dword [best_from],  -1
        mov dword [best_to],    -1
        xor esi, esi
.aim_src:
        cmp esi, 64
        jge .aim_apply
        movzx eax, byte [board + esi]
        test eax, eax
        jz .aim_ns
        test eax, BLACK
        jz .aim_ns
        xor edi, edi
.aim_dst:
        cmp edi, 64
        jge .aim_ns
        movzx ecx, byte [board + edi]
        test ecx, ecx
        jz .aim_try
        test ecx, BLACK
        jnz .aim_nd
.aim_try:
        movzx eax, byte [board + esi]
        mov [ai_save_src], al
        movzx ecx, byte [board + edi]
        mov [ai_save_dst], cl
        mov [board + edi], al
        mov byte [board + esi], EMPTY
        call chess_eval_board
        neg eax
        movzx ecx, byte [ai_save_src]
        mov [board + esi], cl
        movzx ecx, byte [ai_save_dst]
        mov [board + edi], cl
        cmp eax, [best_score]
        jle .aim_nd
        mov [best_score], eax
        mov [best_from], esi
        mov [best_to],   edi
.aim_nd:
        inc edi
        jmp .aim_dst
.aim_ns:
        inc esi
        jmp .aim_src
.aim_apply:
        cmp dword [best_from], -1
        je .aim_done
        mov esi, [best_from]
        mov edi, [best_to]
        movzx eax, byte [board + esi]
        mov [board + edi], al
        mov byte [board + esi], EMPTY
        movzx ecx, byte [board + edi]
        and ecx, 0xFF
        and ecx, 0x0F
        cmp ecx, KING
        jne .aim_nwk
        mov byte [game_over], 1
.aim_nwk:
        mov eax, edi
        xor edx, edx
        mov ecx, 8
        div ecx
        cmp eax, 7
        jne .aim_done
        movzx ecx, byte [board + edi]
        cmp cl, BLACK|PAWN
        jne .aim_done
        mov byte [board + edi], BLACK|QUEEN
.aim_done:
        popad
        ret

chess_eval_board:
        xor eax, eax
        xor ecx, ecx
.cev:
        cmp ecx, 64
        jge .cev_done
        movzx edx, byte [board + ecx]
        test edx, edx
        jz .cev_nxt
        push eax
        push ecx
        mov eax, edx
        and eax, 0x0F
        cmp eax, PAWN
        je .cvp
        cmp eax, KNIGHT
        je .cvn
        cmp eax, BISHOP
        je .cvb
        cmp eax, ROOK
        je .cvr
        cmp eax, QUEEN
        je .cvq
        mov ebx, VAL_KING
        jmp .cvdone
.cvp:   mov ebx, VAL_PAWN
        jmp .cvdone
.cvn:   mov ebx, VAL_KNIGHT
        jmp .cvdone
.cvb:   mov ebx, VAL_BISHOP
        jmp .cvdone
.cvr:   mov ebx, VAL_ROOK
        jmp .cvdone
.cvq:   mov ebx, VAL_QUEEN
.cvdone:
        test edx, BLACK
        pop ecx
        pop eax
        jnz .cev_sub
        add eax, ebx
        jmp .cev_nxt
.cev_sub:
        sub eax, ebx
.cev_nxt:
        inc ecx
        jmp .cev
.cev_done:
        ret

board:         times 64 db EMPTY
cursor_r:      db 6
cursor_f:      db 4
sel_r:         db 0xFF
sel_f:         db 0xFF
turn:          db 0
game_over:     db 0
best_score:    dd 0
best_from:     dd 0
best_to:       dd 0
ai_save_src:   db 0
ai_save_dst:   db 0
piece_chars:   db 'P', 'N', 'B', 'R', 'Q', 'K'
msg_white_turn:  db "Your turn (White). Arrows+Enter=move  R=reset  ESC=quit", 0
msg_ai_thinking: db "AI thinking...", 0
msg_gameover:    db "GAME OVER  R=restart  ESC=quit", 0
