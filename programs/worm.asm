; worm.asm - Classic Nibbles/Snake-style worm game for Mellivora OS
; Grow the worm by eating food. Don't hit walls or yourself!
;
; Controls: WASD or arrow keys. Q to quit.

%include "syscalls.inc"

BOARD_W         equ 40
BOARD_H         equ 20
MAX_LEN         equ 200
TICK_SPEED      equ 8           ; ticks per frame (lower=faster)
WALL_CHAR       equ '#'
FOOD_CHAR       equ '*'
BODY_CHAR       equ 'o'
HEAD_CHAR       equ '@'

; Direction
DIR_UP          equ 0
DIR_DOWN        equ 1
DIR_LEFT        equ 2
DIR_RIGHT       equ 3

start:
        call init_game

.game_loop:
        call draw_board
        call check_input
        call move_worm
        cmp byte [game_over], 1
        je .dead

        mov eax, SYS_SLEEP
        mov ebx, TICK_SPEED
        int 0x80
        jmp .game_loop

.dead:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_dead
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_score
        int 0x80
        mov eax, [score]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_restart
        int 0x80
.dead_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'r'
        je start
        cmp al, 'R'
        je start
        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        jmp .dead_key

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
init_game:
        PUSHALL
        mov byte [game_over], 0
        mov dword [score], 0
        mov dword [direction], DIR_RIGHT
        mov dword [worm_len], 4

        ; Clear board
        mov edi, board
        mov ecx, BOARD_W * BOARD_H
        xor eax, eax
        rep stosb

        ; Place worm in center
        ; Body stored as (x,y) pairs: worm_x[0..len-1], worm_y[0..len-1]
        ; Index 0 = head
        mov dword [worm_x], BOARD_W / 2
        mov dword [worm_y], BOARD_H / 2
        mov eax, BOARD_W / 2
        dec eax
        mov dword [worm_x + 4], eax
        dec eax
        mov dword [worm_x + 8], eax
        dec eax
        mov dword [worm_x + 12], eax

        mov eax, BOARD_H / 2
        mov dword [worm_y + 4], eax
        mov dword [worm_y + 8], eax
        mov dword [worm_y + 12], eax

        call place_food
        POPALL
        ret

;---------------------------------------
place_food:
        PUSHALL
.pf_try:
        mov eax, SYS_GETTIME
        int 0x80
        imul eax, eax, 1103515245
        add eax, 12345
        mov [rng], eax

        ; X in 1..BOARD_W-2
        xor edx, edx
        mov ecx, BOARD_W - 2
        div ecx
        inc edx
        mov [food_x], edx

        mov eax, [rng]
        shr eax, 16
        xor edx, edx
        mov ecx, BOARD_H - 2
        div ecx
        inc edx
        mov [food_y], edx

        ; Make sure food isn't on worm
        xor ecx, ecx
.pf_check:
        cmp ecx, [worm_len]
        jge .pf_ok
        mov eax, [worm_x + ecx * 4]
        cmp eax, [food_x]
        jne .pf_next
        mov eax, [worm_y + ecx * 4]
        cmp eax, [food_y]
        je .pf_try              ; collision, retry
.pf_next:
        inc ecx
        jmp .pf_check
.pf_ok:
        POPALL
        ret

;---------------------------------------
check_input:
        PUSHALL
.ci_drain:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp eax, 0
        je .ci_done
        cmp al, 'w'
        je .ci_up
        cmp al, 'W'
        je .ci_up
        cmp eax, KEY_UP
        je .ci_up
        cmp al, 's'
        je .ci_down
        cmp al, 'S'
        je .ci_down
        cmp eax, KEY_DOWN
        je .ci_down
        cmp al, 'a'
        je .ci_left
        cmp al, 'A'
        je .ci_left
        cmp eax, KEY_LEFT
        je .ci_left
        cmp al, 'd'
        je .ci_right
        cmp al, 'D'
        je .ci_right
        cmp eax, KEY_RIGHT
        je .ci_right
        cmp al, 'q'
        je .ci_quit
        cmp al, 'Q'
        je .ci_quit
        jmp .ci_drain

.ci_up:
        cmp dword [direction], DIR_DOWN
        je .ci_drain            ; prevent 180° turn
        mov dword [direction], DIR_UP
        jmp .ci_drain
.ci_down:
        cmp dword [direction], DIR_UP
        je .ci_drain
        mov dword [direction], DIR_DOWN
        jmp .ci_drain
.ci_left:
        cmp dword [direction], DIR_RIGHT
        je .ci_drain
        mov dword [direction], DIR_LEFT
        jmp .ci_drain
.ci_right:
        cmp dword [direction], DIR_LEFT
        je .ci_drain
        mov dword [direction], DIR_RIGHT
        jmp .ci_drain

.ci_quit:
        mov byte [game_over], 1
.ci_done:
        POPALL
        ret

;---------------------------------------
move_worm:
        PUSHALL
        ; Calculate new head position
        mov eax, [worm_x]      ; current head x
        mov ebx, [worm_y]      ; current head y

        cmp dword [direction], DIR_UP
        je .mw_up
        cmp dword [direction], DIR_DOWN
        je .mw_down
        cmp dword [direction], DIR_LEFT
        je .mw_left
        ; DIR_RIGHT
        inc eax
        jmp .mw_check
.mw_up:
        dec ebx
        jmp .mw_check
.mw_down:
        inc ebx
        jmp .mw_check
.mw_left:
        dec eax

.mw_check:
        ; Wall collision
        cmp eax, 0
        jle .mw_die
        cmp eax, BOARD_W - 1
        jge .mw_die
        cmp ebx, 0
        jle .mw_die
        cmp ebx, BOARD_H - 1
        jge .mw_die

        ; Self collision
        mov [new_hx], eax
        mov [new_hy], ebx
        xor ecx, ecx
.mw_self:
        cmp ecx, [worm_len]
        jge .mw_no_self
        cmp eax, [worm_x + ecx * 4]
        jne .mw_self_next
        cmp ebx, [worm_y + ecx * 4]
        je .mw_die
.mw_self_next:
        inc ecx
        jmp .mw_self

.mw_no_self:
        ; Check food
        mov eax, [new_hx]
        cmp eax, [food_x]
        jne .mw_no_food
        mov eax, [new_hy]
        cmp eax, [food_y]
        jne .mw_no_food
        ; Eat food! Grow worm (don't remove tail)
        inc dword [score]
        cmp dword [worm_len], MAX_LEN
        jge .mw_shift
        inc dword [worm_len]
        jmp .mw_shift_grow

.mw_no_food:
        ; Normal move: shift body, no growth
.mw_shift:
        ; Shift all segments backward (tail gets removed)
        mov ecx, [worm_len]
        dec ecx
.mw_shloop:
        cmp ecx, 0
        jle .mw_set_head
        mov eax, [worm_x + ecx * 4 - 4]
        mov [worm_x + ecx * 4], eax
        mov eax, [worm_y + ecx * 4 - 4]
        mov [worm_y + ecx * 4], eax
        dec ecx
        jmp .mw_shloop

.mw_set_head:
        mov eax, [new_hx]
        mov [worm_x], eax
        mov eax, [new_hy]
        mov [worm_y], eax

        ; If food was eaten, place new food
        mov eax, [new_hx]
        cmp eax, [food_x]
        jne .mw_done
        mov eax, [new_hy]
        cmp eax, [food_y]
        jne .mw_done
        call place_food
        jmp .mw_done

.mw_shift_grow:
        ; Shift all body segments backward, then set new head
        ; worm_len already incremented, shift from len-2 down to 0
        mov ecx, [worm_len]
        dec ecx                 ; ecx = last index
.mw_sgloop:
        cmp ecx, 1
        jl .mw_sg_head
        mov eax, [worm_x + ecx * 4 - 4]
        mov [worm_x + ecx * 4], eax
        mov eax, [worm_y + ecx * 4 - 4]
        mov [worm_y + ecx * 4], eax
        dec ecx
        jmp .mw_sgloop
.mw_sg_head:
        mov eax, [new_hx]
        mov [worm_x], eax
        mov eax, [new_hy]
        mov [worm_y], eax
        call place_food
        jmp .mw_done

.mw_die:
        mov byte [game_over], 1
.mw_done:
        POPALL
        ret

;---------------------------------------
draw_board:
        PUSHALL
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80

        ; Header
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_header
        int 0x80
        mov eax, [score]
        call print_dec
        ; Pad the line
        mov ecx, 20
.db_pad:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec ecx
        jnz .db_pad
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Draw rows
        xor ebp, ebp           ; row
.db_row:
        cmp ebp, BOARD_H
        jge .db_done

        xor edi, edi           ; col
.db_col:
        cmp edi, BOARD_W
        jge .db_eol

        ; Wall?
        cmp ebp, 0
        je .db_wall
        mov eax, BOARD_H - 1
        cmp ebp, eax
        je .db_wall
        cmp edi, 0
        je .db_wall
        mov eax, BOARD_W - 1
        cmp edi, eax
        je .db_wall

        ; Food?
        cmp edi, [food_x]
        jne .db_not_food
        cmp ebp, [food_y]
        jne .db_not_food
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; red
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, FOOD_CHAR
        int 0x80
        jmp .db_next

.db_not_food:
        ; Worm?
        xor ecx, ecx
.db_wcheck:
        cmp ecx, [worm_len]
        jge .db_empty
        cmp edi, [worm_x + ecx * 4]
        jne .db_wnext
        cmp ebp, [worm_y + ecx * 4]
        jne .db_wnext
        ; It's the worm!
        cmp ecx, 0
        jne .db_body
        ; Head
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; green
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, HEAD_CHAR
        int 0x80
        jmp .db_next
.db_body:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x02           ; dark green
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, BODY_CHAR
        int 0x80
        jmp .db_next
.db_wnext:
        inc ecx
        jmp .db_wcheck

.db_empty:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        jmp .db_next

.db_wall:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; dark gray
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, WALL_CHAR
        int 0x80

.db_next:
        inc edi
        jmp .db_col

.db_eol:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc ebp
        jmp .db_row

.db_done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_controls
        int 0x80
        POPALL
        ret

;=======================================
; Data
;=======================================
msg_header:     db " WORM - Score: ", 0
msg_dead:       db 10, " GAME OVER! You crashed!", 10, 0
msg_score:      db " Final score: ", 0
msg_restart:    db " R=Restart  Q=Quit", 10, 0
msg_controls:   db " WASD=Move  Q=Quit", 10, 0

; Game state
direction:      dd DIR_RIGHT
worm_len:       dd 4
score:          dd 0
game_over:      db 0
food_x:         dd 0
food_y:         dd 0
new_hx:         dd 0
new_hy:         dd 0
rng:            dd 0

; max 200 segments, stored as separate x and y arrays
worm_x:         times MAX_LEN dd 0
worm_y:         times MAX_LEN dd 0

; Board scratch (unused but reserved)
board:          times BOARD_W * BOARD_H db 0
