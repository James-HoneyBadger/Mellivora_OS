; pong.asm - Classic Pong — VBE pixel graphics
; Single player vs CPU, first to 11 wins.
; Controls: W/S or Up/Down to move paddle.

%include "syscalls.inc"

SCREEN_W        equ 640
SCREEN_H        equ 480
HUD_H           equ 28          ; score bar height

; Game board in logical units (unchanged from original for physics)
BOARD_W         equ 60
BOARD_H         equ 22
PADDLE_H        equ 5
BALL_SPEED      equ 5
WIN_SCORE       equ 11

; Scale factors: logical → pixels
; Play area: 600×440 starting at (20, HUD_H+10)
AREA_X          equ 20
AREA_Y          equ HUD_H + 10
SCALE_X         equ 10          ; 60 * 10 = 600
SCALE_Y         equ 20          ; 22 * 20 = 440

; Derived pixel geometry
PADDLE_PX_W     equ 10
PADDLE_PX_H     equ PADDLE_H * SCALE_Y
BALL_PX         equ 12

; Colors
C_BG            equ 0x000000
C_HUD           equ 0x001A44
C_BORDER        equ 0x444444
C_P1_PADDLE     equ 0x00CCFF
C_P2_PADDLE     equ 0xFF4444
C_BALL          equ 0xFFFF00
C_TEXT          equ 0xFFFFFF
C_CENTER        equ 0x333333

; Key codes
KEY_UP          equ 0x80
KEY_DOWN        equ 0x81

start:
        ; Init VBE
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCREEN_W
        mov edx, SCREEN_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .exit_novbe

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr], eax
        mov dword [fb_pitch], SCREEN_W * 4

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

        ; Show winner text
        mov ebx, AREA_X + 200
        mov ecx, AREA_Y + 180
        cmp dword [p1_score], WIN_SCORE
        je .p1_wins
        mov esi, msg_cpu_wins
        jmp .show_msg
.p1_wins:
        mov esi, msg_you_win
.show_msg:
        mov edi, 0xFFFF44
        call fb_draw_text

        mov ebx, AREA_X + 160
        mov ecx, AREA_Y + 210
        mov esi, msg_restart
        mov edi, C_TEXT
        call fb_draw_text

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
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
.exit_novbe:
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
        neg dword [ball_dx]
        cmp dword [ball_dx], 0
        jne .rb_ok
        mov dword [ball_dx], 1
.rb_ok:
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
        mov eax, SYS_READ_KEY
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
        pushad
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
        popad
        ret

;---------------------------------------
move_ball:
        pushad
        mov eax, [ball_x]
        add eax, [ball_dx]
        mov [ball_x], eax

        mov ebx, [ball_y]
        add ebx, [ball_dy]
        mov [ball_y], ebx

        cmp ebx, 0
        jle .mb_bounce_v
        cmp ebx, BOARD_H - 1
        jge .mb_bounce_v
        jmp .mb_check_paddles

.mb_bounce_v:
        neg dword [ball_dy]
        cmp dword [ball_y], 0
        jge .mb_clamp_top_ok
        mov dword [ball_y], 1
.mb_clamp_top_ok:
        cmp dword [ball_y], BOARD_H - 1
        jle .mb_check_paddles
        mov eax, BOARD_H - 2
        mov [ball_y], eax

.mb_check_paddles:
        cmp dword [ball_x], 2
        jne .mb_check_right
        cmp dword [ball_dx], 0
        jg .mb_check_right
        mov eax, [ball_y]
        cmp eax, [p1_y]
        jl .mb_p2_scores
        mov ecx, [p1_y]
        add ecx, PADDLE_H
        cmp eax, ecx
        jge .mb_p2_scores
        neg dword [ball_dx]
        mov eax, [ball_y]
        sub eax, [p1_y]
        sub eax, PADDLE_H / 2
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
        mov eax, BOARD_W - 3
        cmp [ball_x], eax
        jne .mb_check_oob
        cmp dword [ball_dx], 0
        jl .mb_check_oob
        mov eax, [ball_y]
        cmp eax, [p2_y]
        jl .mb_p1_scores
        mov ecx, [p2_y]
        add ecx, PADDLE_H
        cmp eax, ecx
        jge .mb_p1_scores
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
        popad
        ret

;---------------------------------------
; draw_board — full VBE frame redraw
;---------------------------------------
draw_board:
        pushad

        ; Background
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCREEN_W
        mov esi, SCREEN_H
        mov edi, C_BG
        call fb_fill_rect

        ; HUD bar
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCREEN_W
        mov esi, HUD_H
        mov edi, C_HUD
        call fb_fill_rect

        ; HUD text: "YOU: X  vs  CPU: X"
        mov ebx, 20
        mov ecx, 6
        mov esi, str_you
        mov edi, C_TEXT
        call fb_draw_text

        mov eax, [p1_score]
        mov ebx, 20 + 5 * 8
        mov ecx, 6
        mov edi, C_TEXT
        call fb_draw_num

        mov ebx, 160
        mov ecx, 6
        mov esi, str_vs
        mov edi, C_TEXT
        call fb_draw_text

        mov eax, [p2_score]
        mov ebx, 200
        mov ecx, 6
        mov edi, C_TEXT
        call fb_draw_num

        mov ebx, 240
        mov ecx, 6
        mov esi, str_cpu
        mov edi, C_TEXT
        call fb_draw_text

        ; Controls hint
        mov ebx, 400
        mov ecx, 6
        mov esi, msg_controls
        mov edi, 0x777777
        call fb_draw_text

        ; Top / bottom play-area borders
        xor ebx, ebx
        mov ecx, AREA_Y - 2
        mov edx, SCREEN_W
        mov esi, 2
        mov edi, C_BORDER
        call fb_fill_rect

        xor ebx, ebx
        mov ecx, AREA_Y + BOARD_H * SCALE_Y
        mov edx, SCREEN_W
        mov esi, 2
        mov edi, C_BORDER
        call fb_fill_rect

        ; Centre dotted line (use EBP as row counter, saved/restored around call)
        xor ebp, ebp            ; row counter
.db_center:
        cmp ebp, BOARD_H
        jge .db_paddles

        ; Draw dash only on even rows
        test ebp, 1
        jnz .db_center_next

        ; Pixel y for this dash
        mov eax, ebp
        imul eax, SCALE_Y
        add eax, AREA_Y + 2     ; small inset

        push ebp                ; save row counter across call
        mov ebx, BOARD_W / 2 * SCALE_X + AREA_X
        mov ecx, eax
        mov edx, 2
        mov esi, SCALE_Y - 4
        mov edi, C_CENTER
        call fb_fill_rect
        pop ebp                 ; restore row counter

.db_center_next:
        inc ebp
        jmp .db_center

.db_paddles:
        ; Player 1 paddle (left side, col 1)
        mov eax, [p1_y]
        imul eax, SCALE_Y
        add eax, AREA_Y
        mov ebx, AREA_X + SCALE_X      ; col 1 * SCALE_X
        mov ecx, eax
        mov edx, PADDLE_PX_W
        mov esi, PADDLE_PX_H
        mov edi, C_P1_PADDLE
        call fb_fill_rect

        ; Player 2 paddle (right side, col BOARD_W-2)
        mov eax, [p2_y]
        imul eax, SCALE_Y
        add eax, AREA_Y
        mov ebx, AREA_X + (BOARD_W - 2) * SCALE_X
        mov ecx, eax
        mov edx, PADDLE_PX_W
        mov esi, PADDLE_PX_H
        mov edi, C_P2_PADDLE
        call fb_fill_rect

        ; Ball
        mov eax, [ball_x]
        imul eax, SCALE_X
        add eax, AREA_X
        mov ebx, eax
        mov eax, [ball_y]
        imul eax, SCALE_Y
        add eax, AREA_Y
        mov ecx, eax
        mov edx, BALL_PX
        mov esi, BALL_PX
        mov edi, C_BALL
        call fb_fill_rect

        popad
        ret

;=======================================================================
; VBE HELPERS
;=======================================================================

; fb_fill_rect: EBX=x, ECX=y, EDX=w, ESI=h, EDI=color
fb_fill_rect:
        pushad
        test edx, edx
        jz .ffr_done
        test esi, esi
        jz .ffr_done
        mov eax, ecx
        imul eax, [fb_pitch]
        add eax, [fb_addr]
        lea eax, [eax + ebx*4]
.ffr_row:
        push eax
        push edx
        mov ecx, edx
.ffr_col:
        mov [eax], edi
        add eax, 4
        dec ecx
        jnz .ffr_col
        pop edx
        pop eax
        add eax, [fb_pitch]
        dec esi
        jnz .ffr_row
.ffr_done:
        popad
        ret

; fb_draw_text: EBX=x, ECX=y, ESI=str_ptr, EDI=color
fb_draw_text:
        pushad
        mov edx, ecx
        mov ecx, ebx
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        int 0x80
        popad
        ret

; itoa: EAX=number → decimal string in num_buf
itoa:
        pushad
        mov edi, num_buf + 11
        mov byte [edi], 0
        dec edi
        test eax, eax
        jnz .itoa_d
        mov byte [edi], '0'
        dec edi
        jmp .itoa_cp
.itoa_d:
        mov ecx, 10
.itoa_lp:
        test eax, eax
        jz .itoa_cp
        xor edx, edx
        div ecx
        add dl, '0'
        mov [edi], dl
        dec edi
        jmp .itoa_lp
.itoa_cp:
        inc edi
        mov esi, edi
        mov edi, num_buf
.itoa_mv:
        mov al, [esi]
        mov [edi], al
        inc esi
        inc edi
        test al, al
        jnz .itoa_mv
        popad
        ret

; fb_draw_num: EAX=number, EBX=x, ECX=y, EDI=color
fb_draw_num:
        push esi
        push ebx
        push ecx
        push edi
        call itoa
        pop edi
        pop ecx
        pop ebx
        mov esi, num_buf
        call fb_draw_text
        pop esi
        ret

;=======================================================================
; DATA
;=======================================================================
str_you:        db "YOU: ", 0
str_vs:         db " vs ", 0
str_cpu:        db " CPU", 0
msg_you_win:    db "  YOU WIN!", 0
msg_cpu_wins:   db "  CPU WINS!", 0
msg_restart:    db "  R=Restart  Q=Quit", 0
msg_controls:   db "W/S=Move  Q=Quit", 0

p1_y:           dd 0
p2_y:           dd 0
p1_score:       dd 0
p2_score:       dd 0
ball_x:         dd BOARD_W / 2
ball_y:         dd BOARD_H / 2
ball_dx:        dd 1
ball_dy:        dd 1
game_over:      db 0

section .bss
fb_addr:        resd 1
fb_pitch:       resd 1
num_buf:        resb 12
