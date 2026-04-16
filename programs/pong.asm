; pong.asm - Classic Pong game for Mellivora OS
; Single player vs CPU. First to 11 wins.
;
; Controls: W/S or Up/Down to move paddle. Q to quit.

%include "syscalls.inc"

BOARD_W         equ 60
BOARD_H         equ 22
PADDLE_H        equ 5
BALL_SPEED      equ 5           ; ticks per frame
WIN_SCORE       equ 11

start:
        mov eax, SYS_CLEAR
        int 0x80

        call init_game

.game_loop:
        cmp byte [game_over], 1
        je .show_winner

        call draw_board
        call check_input_pong
        call move_ball
        call move_cpu

        mov eax, SYS_SLEEP
        mov ebx, BALL_SPEED
        int 0x80
        jmp .game_loop

.show_winner:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        cmp dword [p1_score], WIN_SCORE
        je .p1_wins
        mov eax, SYS_PRINT
        mov ebx, msg_cpu_wins
        int 0x80
        jmp .wait_end
.p1_wins:
        mov eax, SYS_PRINT
        mov ebx, msg_you_win
        int 0x80

.wait_end:
        mov eax, SYS_PRINT
        mov ebx, msg_restart
        int 0x80
.we_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'q'
        je .exit
        cmp al, 'Q'
        je .exit
        cmp al, 'r'
        je start
        cmp al, 'R'
        je start
        jmp .we_key

.exit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
init_game:
        mov dword [p1_y], BOARD_H / 2 - PADDLE_H / 2
        mov dword [p2_y], BOARD_H / 2 - PADDLE_H / 2
        mov dword [p1_score], 0
        mov dword [p2_score], 0
        mov byte [game_over], 0
        call reset_ball
        ret

reset_ball:
        mov dword [ball_x], BOARD_W / 2
        mov dword [ball_y], BOARD_H / 2
        ; Alternate serve direction
        neg dword [ball_dx]
        cmp dword [ball_dx], 0
        jne .rb_ok
        mov dword [ball_dx], 1
.rb_ok:
        ; Random vertical direction
        mov eax, SYS_GETTIME
        int 0x80
        and eax, 1
        jz .rb_down
        mov dword [ball_dy], -1
        ret
.rb_down:
        mov dword [ball_dy], 1
        ret

;---------------------------------------
check_input_pong:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp eax, 0
        je .cip_done
        cmp al, 'w'
        je .cip_up
        cmp al, 'W'
        je .cip_up
        cmp al, 's'
        je .cip_down
        cmp al, 'S'
        je .cip_down
        cmp eax, KEY_UP
        je .cip_up
        cmp eax, KEY_DOWN
        je .cip_down
        cmp al, 'q'
        je .cip_quit
        cmp al, 'Q'
        je .cip_quit
        jmp .cip_done
.cip_up:
        cmp dword [p1_y], 0
        jle .cip_done
        dec dword [p1_y]
        jmp .cip_done
.cip_down:
        mov eax, [p1_y]
        add eax, PADDLE_H
        cmp eax, BOARD_H
        jge .cip_done
        inc dword [p1_y]
        jmp .cip_done
.cip_quit:
        mov byte [game_over], 1
.cip_done:
        ret

;---------------------------------------
move_cpu:
        PUSHALL
        ; Simple AI: track ball y
        mov eax, [ball_y]
        mov ebx, [p2_y]
        add ebx, PADDLE_H / 2

        cmp eax, ebx
        jl .mc_up
        cmp eax, ebx
        jg .mc_down
        jmp .mc_done
.mc_up:
        cmp dword [p2_y], 0
        jle .mc_done
        dec dword [p2_y]
        jmp .mc_done
.mc_down:
        mov ecx, [p2_y]
        add ecx, PADDLE_H
        cmp ecx, BOARD_H
        jge .mc_done
        inc dword [p2_y]
.mc_done:
        POPALL
        ret

;---------------------------------------
move_ball:
        PUSHALL
        ; Move ball
        mov eax, [ball_x]
        add eax, [ball_dx]
        mov [ball_x], eax

        mov ebx, [ball_y]
        add ebx, [ball_dy]
        mov [ball_y], ebx

        ; Top/bottom wall bounce
        cmp ebx, 0
        jle .mb_bounce_v
        cmp ebx, BOARD_H - 1
        jge .mb_bounce_v
        jmp .mb_check_paddles

.mb_bounce_v:
        neg dword [ball_dy]
        ; Clamp
        cmp dword [ball_y], 0
        jge .mb_clamp_top_ok
        mov dword [ball_y], 1
.mb_clamp_top_ok:
        cmp dword [ball_y], BOARD_H - 1
        jle .mb_check_paddles
        mov eax, BOARD_H - 2
        mov [ball_y], eax

.mb_check_paddles:
        ; Left paddle (player 1 at x=1)
        cmp dword [ball_x], 2
        jne .mb_check_right
        cmp dword [ball_dx], 0
        jg .mb_check_right      ; moving right, ignore
        mov eax, [ball_y]
        cmp eax, [p1_y]
        jl .mb_p2_scores
        mov ecx, [p1_y]
        add ecx, PADDLE_H
        cmp eax, ecx
        jge .mb_p2_scores
        ; Hit paddle!
        neg dword [ball_dx]
        ; Add spin based on hit position
        mov eax, [ball_y]
        sub eax, [p1_y]
        sub eax, PADDLE_H / 2
        ; If hit top half, ball goes up; bottom half, down
        cmp eax, 0
        jl .mb_spin_up1
        mov dword [ball_dy], 1
        jmp .mb_done
.mb_spin_up1:
        mov dword [ball_dy], -1
        jmp .mb_done

.mb_p2_scores:
        cmp dword [ball_x], 0
        jg .mb_check_right
        inc dword [p2_score]
        cmp dword [p2_score], WIN_SCORE
        jge .mb_gameover
        call reset_ball
        jmp .mb_done

.mb_check_right:
        ; Right paddle (CPU at x=BOARD_W-2)
        mov eax, BOARD_W - 3
        cmp [ball_x], eax
        jne .mb_check_oob
        cmp dword [ball_dx], 0
        jl .mb_check_oob       ; moving left, ignore
        mov eax, [ball_y]
        cmp eax, [p2_y]
        jl .mb_p1_scores
        mov ecx, [p2_y]
        add ecx, PADDLE_H
        cmp eax, ecx
        jge .mb_p1_scores
        ; Hit paddle
        neg dword [ball_dx]
        mov eax, [ball_y]
        sub eax, [p2_y]
        sub eax, PADDLE_H / 2
        cmp eax, 0
        jl .mb_spin_up2
        mov dword [ball_dy], 1
        jmp .mb_done
.mb_spin_up2:
        mov dword [ball_dy], -1
        jmp .mb_done

.mb_p1_scores:
        mov eax, BOARD_W - 1
        cmp [ball_x], eax
        jl .mb_check_oob
        inc dword [p1_score]
        cmp dword [p1_score], WIN_SCORE
        jge .mb_gameover
        call reset_ball
        jmp .mb_done

.mb_check_oob:
        ; Out of bounds left
        cmp dword [ball_x], 0
        jg .mb_check_oob_r
        inc dword [p2_score]
        cmp dword [p2_score], WIN_SCORE
        jge .mb_gameover
        call reset_ball
        jmp .mb_done
.mb_check_oob_r:
        mov eax, BOARD_W - 1
        cmp [ball_x], eax
        jl .mb_done
        inc dword [p1_score]
        cmp dword [p1_score], WIN_SCORE
        jge .mb_gameover
        call reset_ball
        jmp .mb_done

.mb_gameover:
        mov byte [game_over], 1
.mb_done:
        POPALL
        ret

;---------------------------------------
draw_board:
        PUSHALL

        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80

        ; Score header
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; white
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_player
        int 0x80
        mov eax, [p1_score]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_vs
        int 0x80
        mov eax, [p2_score]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_cpu
        int 0x80

        ; Pad line
        mov ecx, 30
.db_hpad:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec ecx
        jnz .db_hpad
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Top border
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov ecx, BOARD_W + 2
.db_top:
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        dec ecx
        jnz .db_top
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Board rows
        xor ebp, ebp            ; row 0..BOARD_H-1
.db_row:
        cmp ebp, BOARD_H
        jge .db_bottom

        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80

        ; Columns
        xor edi, edi
.db_col:
        cmp edi, BOARD_W
        jge .db_rend

        ; Center line
        mov eax, BOARD_W / 2
        cmp edi, eax
        jne .db_not_center
        ; Draw dotted center line
        mov eax, ebp
        and eax, 1
        jnz .db_not_center
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
        jmp .db_next

.db_not_center:
        ; Check player paddle (x=1)
        cmp edi, 1
        jne .db_not_p1
        cmp ebp, [p1_y]
        jl .db_not_p1
        mov eax, [p1_y]
        add eax, PADDLE_H
        cmp ebp, eax
        jge .db_not_p1
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; cyan
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '#'
        int 0x80
        jmp .db_next

.db_not_p1:
        ; Check CPU paddle (x=BOARD_W-2)
        mov eax, BOARD_W - 2
        cmp edi, eax
        jne .db_not_p2
        cmp ebp, [p2_y]
        jl .db_not_p2
        mov eax, [p2_y]
        add eax, PADDLE_H
        cmp ebp, eax
        jge .db_not_p2
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; red
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '#'
        int 0x80
        jmp .db_next

.db_not_p2:
        ; Check ball
        cmp edi, [ball_x]
        jne .db_empty
        cmp ebp, [ball_y]
        jne .db_empty
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; yellow
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'O'
        int 0x80
        jmp .db_next

.db_empty:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

.db_next:
        inc edi
        jmp .db_col

.db_rend:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        inc ebp
        jmp .db_row

.db_bottom:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov ecx, BOARD_W + 2
.db_bot:
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        dec ecx
        jnz .db_bot

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
msg_player:     db " YOU: ", 0
msg_vs:         db "  vs  ", 0
msg_cpu:        db " CPU", 0
msg_you_win:    db "  YOU WIN!", 10, 0
msg_cpu_wins:   db "  CPU WINS!", 10, 0
msg_restart:    db "  R=Restart  Q=Quit", 10, 0
msg_controls:   db 10, " W/S=Move  Q=Quit  First to 11 wins", 10, 0

; Game state
p1_y:           dd 0
p2_y:           dd 0
p1_score:       dd 0
p2_score:       dd 0
ball_x:         dd BOARD_W / 2
ball_y:         dd BOARD_H / 2
ball_dx:        dd 1
ball_dy:        dd 1
game_over:      db 0
