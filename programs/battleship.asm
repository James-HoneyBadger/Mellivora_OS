; battleship.asm - Classic Battleship game (player vs AI)
; 10x10 grid. Ships: Carrier(5), Battleship(4), Cruiser(3), Sub(3), Destroyer(2)

%include "syscalls.inc"

GRID_SIZE   equ 10
; Cell values
WATER   equ 0
SHIP    equ 1
HIT     equ 2
MISS    equ 3

start:
        call init_grids
        call place_ships
        call game_loop
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ---------- Init ----------
init_grids:
        ; Zero both grids
        mov edi, player_grid
        mov ecx, 100
        xor eax, eax
        rep stosd
        mov edi, ai_grid
        mov ecx, 100
        rep stosd
        ; AI tracking grid for player's display (what player knows of AI grid)
        mov edi, ai_view
        mov ecx, 100
        xor eax, eax
        rep stosd
        ret

; ---------- Ship placement ----------
; Ships: lengths 5,4,3,3,2
place_ships:
        ; Hardcode AI ship placement (deterministic for simplicity)
        ; Carrier(5): row 0, col 0-4 horizontal
        mov ebp, 0*10 + 0
        mov ecx, 5
        call place_h_ai

        ; Battleship(4): row 2, col 3-6
        mov ebp, 2*10 + 3
        mov ecx, 4
        call place_h_ai

        ; Cruiser(3): row 5, col 7-9
        mov ebp, 5*10 + 7
        mov ecx, 3
        call place_h_ai

        ; Sub(3): row 7, col 0-2
        mov ebp, 7*10 + 0
        mov ecx, 3
        call place_h_ai

        ; Destroyer(2): row 9, col 5-6
        mov ebp, 9*10 + 5
        mov ecx, 2
        call place_h_ai

        ; Place player ships interactively
        mov eax, SYS_PRINT
        mov ebx, msg_place_hdr
        int 0x80
        call place_player_ships
        ret

place_h_ai:
        ; Mark ECX cells starting at ai_grid[EBP] as SHIP
.ph:    mov byte [ai_grid + ebp], SHIP
        inc ebp
        dec ecx
        jnz .ph
        ret

place_player_ships:
        ; Auto-place for player (symmetric to AI for demo)
        mov ebp, 1*10 + 0
        mov ecx, 5
        call place_h_player

        mov ebp, 3*10 + 3
        mov ecx, 4
        call place_h_player

        mov ebp, 6*10 + 7
        mov ecx, 3
        call place_h_player

        mov ebp, 8*10 + 0
        mov ecx, 3
        call place_h_player

        mov ebp, 0*10 + 8
        mov ecx, 2
        call place_h_player

        mov eax, SYS_PRINT
        mov ebx, msg_ships_placed
        int 0x80
        ret

place_h_player:
.php:   mov byte [player_grid + ebp], SHIP
        inc ebp
        dec ecx
        jnz .php
        ret

; ---------- Game loop ----------
game_loop:
.loop:
        call draw_boards
        ; Check win/loss
        call check_win
        cmp eax, 1
        je .player_wins
        cmp eax, 2
        je .ai_wins

        ; Player's turn
        mov eax, SYS_PRINT
        mov ebx, msg_your_turn
        int 0x80
        call player_shot
        call check_win
        cmp eax, 1
        je .player_wins

        ; AI's turn
        call ai_shot
        call check_win
        cmp eax, 2
        je .ai_wins

        jmp .loop

.player_wins:
        call draw_boards
        mov eax, SYS_PRINT
        mov ebx, msg_p_win
        int 0x80
        ret

.ai_wins:
        call draw_boards
        mov eax, SYS_PRINT
        mov ebx, msg_a_win
        int 0x80
        ret

; ---------- Draw ----------
draw_boards:
        mov eax, SYS_PRINT
        mov ebx, msg_header
        int 0x80
        ; Column headers
        mov eax, SYS_PRINT
        mov ebx, msg_col_hdr
        int 0x80

        mov ebp, 0          ; row counter
.draw_row:
        cmp ebp, GRID_SIZE
        jge .draw_done

        ; Row label
        mov eax, ebp
        add eax, 'A'
        push eax
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Player grid (show ships)
        mov ecx, 0
.pdraw_col:
        cmp ecx, GRID_SIZE
        jge .pdraw_sep
        mov eax, ebp
        imul eax, GRID_SIZE
        add eax, ecx
        movzx ebx, byte [player_grid + eax]
        call draw_cell_player
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc ecx
        jmp .pdraw_col
.pdraw_sep:
        mov eax, SYS_PRINT
        mov ebx, msg_sep
        int 0x80

        ; AI view grid (hide ships)
        mov ecx, 0
.adraw_col:
        cmp ecx, GRID_SIZE
        jge .adraw_nl
        mov eax, ebp
        imul eax, GRID_SIZE
        add eax, ecx
        movzx ebx, byte [ai_view + eax]
        call draw_cell_ai
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc ecx
        jmp .adraw_col
.adraw_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc ebp
        jmp .draw_row

.draw_done:
        ret

draw_cell_player:
        ; EBX = cell value
        cmp ebx, WATER
        je .w
        cmp ebx, SHIP
        je .s
        cmp ebx, HIT
        je .h
        ; MISS
        mov eax, SYS_PUTCHAR
        mov ebx, 'o'
        int 0x80
        ret
.w:     mov eax, SYS_PUTCHAR
        mov ebx, '~'
        int 0x80
        ret
.s:     mov eax, SYS_PUTCHAR
        mov ebx, '#'
        int 0x80
        ret
.h:     mov eax, SYS_PUTCHAR
        mov ebx, 'X'
        int 0x80
        ret

draw_cell_ai:
        ; EBX = cell value in ai_view
        cmp ebx, WATER
        je .w
        cmp ebx, HIT
        je .h
        ; MISS
        mov eax, SYS_PUTCHAR
        mov ebx, 'o'
        int 0x80
        ret
.w:     mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ret
.h:     mov eax, SYS_PUTCHAR
        mov ebx, 'X'
        int 0x80
        ret

; ---------- Player shot ----------
player_shot:
.ask:
        mov eax, SYS_PRINT
        mov ebx, msg_ask_shot
        int 0x80
        mov eax, SYS_STDIN_READ
        mov ebx, input_buf
        mov ecx, 10
        int 0x80

        ; Parse "A5" format
        movzx eax, byte [input_buf]
        cmp eax, 'a'
        jl .up
        cmp eax, 'z'
        jg .up
        sub eax, 32     ; to uppercase
.up:
        cmp eax, 'A'
        jb .ask
        cmp eax, 'J'
        ja .ask
        sub eax, 'A'    ; row 0-9
        mov [shot_row], eax

        movzx eax, byte [input_buf + 1]
        ; Check for "10" first (two-char column, e.g. "A10")
        cmp eax, '1'
        jne .not_col10
        cmp byte [input_buf + 2], '0'
        jne .not_col10
        mov eax, 9              ; column 10 -> 0-based index 9
        jmp .col_ok
.not_col10:
        cmp eax, '1'
        jb .ask
        cmp eax, '9'
        ja .ask
        sub eax, '1'            ; '1'-'9' -> 0-8
.col_ok:
        cmp eax, GRID_SIZE
        jge .ask
        mov [shot_col], eax

        ; Check not already shot
        mov eax, [shot_row]
        imul eax, GRID_SIZE
        add eax, [shot_col]
        movzx ebx, byte [ai_view + eax]
        cmp ebx, WATER
        jne .ask

        ; Check actual AI grid
        movzx ebx, byte [ai_grid + eax]
        cmp ebx, SHIP
        je .phit
        ; Miss
        mov byte [ai_view + eax], MISS
        mov eax, SYS_PRINT
        mov ebx, msg_miss
        int 0x80
        ret
.phit:
        mov byte [ai_grid + eax], HIT
        mov byte [ai_view + eax], HIT
        mov eax, SYS_PRINT
        mov ebx, msg_hit
        int 0x80
        ret

; ---------- AI shot (sequential scan) ----------
ai_shot:
        mov eax, [ai_shot_pos]
.ai_find:
        cmp eax, 100
        jge .ai_skip
        movzx ebx, byte [player_grid + eax]
        cmp ebx, HIT
        je .ai_next
        cmp ebx, MISS
        je .ai_next
        ; Fire here
        mov [ai_shot_pos], eax
        inc dword [ai_shot_pos]
        cmp ebx, SHIP
        je .ai_hit
        mov byte [player_grid + eax], MISS
        mov eax, SYS_PRINT
        mov ebx, msg_ai_miss
        int 0x80
        ret
.ai_hit:
        mov byte [player_grid + eax], HIT
        mov eax, SYS_PRINT
        mov ebx, msg_ai_hit
        int 0x80
        ret
.ai_next:
        inc eax
        jmp .ai_find
.ai_skip:
        ret

; ---------- Check win ----------
; Returns EAX=0 none, 1=player wins, 2=AI wins
check_win:
        ; Count remaining SHIP cells in ai_grid
        xor ecx, ecx
        xor ebp, ebp
.cw_ai:
        cmp ebp, 100
        jge .cw_ai_done
        cmp byte [ai_grid + ebp], SHIP
        jne .cw_ai_next
        inc ecx
.cw_ai_next:
        inc ebp
        jmp .cw_ai
.cw_ai_done:
        test ecx, ecx
        jz .player_w

        ; Count remaining SHIP cells in player_grid
        xor ecx, ecx
        xor ebp, ebp
.cw_pl:
        cmp ebp, 100
        jge .cw_pl_done
        cmp byte [player_grid + ebp], SHIP
        jne .cw_pl_next
        inc ecx
.cw_pl_next:
        inc ebp
        jmp .cw_pl
.cw_pl_done:
        test ecx, ecx
        jz .ai_w

        xor eax, eax
        ret
.player_w:
        mov eax, 1
        ret
.ai_w:
        mov eax, 2
        ret

; Messages
msg_place_hdr:  db "Placing ships automatically...", 10, 0
msg_ships_placed: db "Ships placed. Let's play!", 10, 10, 0
msg_header:     db "  YOUR FLEET          ENEMY WATERS", 10
                db "  1 2 3 4 5 6 7 8 9 0   1 2 3 4 5 6 7 8 9 0", 10, 0
msg_col_hdr:    db 0
msg_sep:        db "  |  ", 0
msg_your_turn:  db "Your turn.", 10, 0
msg_ask_shot:   db "Enter shot (e.g. A5): ", 0
msg_hit:        db "HIT!", 10, 0
msg_miss:       db "Miss.", 10, 0
msg_ai_hit:     db "AI hit your ship!", 10, 0
msg_ai_miss:    db "AI missed.", 10, 0
msg_p_win:      db "=== YOU WIN! All enemy ships sunk! ===", 10, 0
msg_a_win:      db "=== GAME OVER. AI sank all your ships. ===", 10, 0

player_grid:    times 100 db 0
ai_grid:        times 100 db 0
ai_view:        times 100 db 0
shot_row:       dd 0
shot_col:       dd 0
ai_shot_pos:    dd 0
input_buf:      times 16 db 0
