; breakout.asm — Breakout/Arkanoid clone for Mellivora OS VBE
;
; Controls: LEFT / RIGHT arrow keys to move paddle
;           Q or any non-arrow key to quit
; Features: 5 rows × 10 columns of bricks, ball bounces, lives

%include "syscalls.inc"

SCR_W    equ 640
SCR_H    equ 480
PITCH    equ SCR_W * 4

; Paddle
PAD_W    equ 96
PAD_H    equ 10
PAD_Y    equ 448
PAD_SPD  equ 8

; Ball
BALL_W   equ 10
BALL_H   equ 10

; Brick layout
BRICK_ROWS   equ 5
BRICK_COLS   equ 10
BRICK_W      equ 56
BRICK_H      equ 18
BRICK_GAP_X  equ 4
BRICK_GAP_Y  equ 4
BRICK_START_X equ 22
BRICK_START_Y equ 60

; Lives
MAX_LIVES equ 3

start:
        ; VBE init
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .novbe

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb], eax

        ; Init game state
        call game_init

.mainloop:
        ; -- Read key (non-blocking) --
        mov eax, SYS_READ_KEY
        int 0x80
        mov [last_key], eax

        ; -- Move paddle --
        mov eax, [last_key]
        cmp eax, 0x82           ; left arrow
        je .move_left
        cmp eax, 0x83           ; right arrow
        je .move_right
        ; Check for quit
        test eax, eax
        jz .no_quit
        cmp eax, 0x71           ; 'q'
        je .quit
        cmp eax, 0x51           ; 'Q'
        je .quit
.no_quit:
        jmp .paddle_done
.move_left:
        mov eax, [pad_x]
        sub eax, PAD_SPD
        cmp eax, 0
        jge .set_pad_x
        xor eax, eax
        jmp .set_pad_x
.move_right:
        mov eax, [pad_x]
        add eax, PAD_SPD
        mov ebx, SCR_W - PAD_W
        cmp eax, ebx
        jle .set_pad_x
        mov eax, ebx
.set_pad_x:
        mov [pad_x], eax
.paddle_done:

        ; -- Move ball --
        call ball_move

        ; -- Check win/lose --
        cmp byte [game_over], 1
        je .show_over

        ; -- Draw frame --
        call draw_frame

        ; -- Blit --
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; -- Sleep ~16ms --
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80

        jmp .mainloop

.show_over:
        call draw_gameover
        ; Wait for key
.wait_key:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .wait_key
        jmp .quit

.quit:
        ; Restore text mode
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        ret

.novbe:
        ret

; ----------------------------------------------------------------
; game_init — reset all state
; ----------------------------------------------------------------
game_init:
        ; Paddle centre-ish
        mov dword [pad_x], (SCR_W - PAD_W) / 2

        ; Ball starts above paddle
        mov dword [ball_x], SCR_W / 2 - BALL_W / 2
        mov dword [ball_y], PAD_Y - BALL_H - 2
        mov dword [ball_vx], 3
        mov dword [ball_vy], -4

        mov dword [score], 0
        mov dword [lives], MAX_LIVES
        mov byte [game_over], 0

        ; Init bricks: all alive
        mov edi, bricks
        mov ecx, BRICK_ROWS * BRICK_COLS
        mov al, 1
        rep stosb
        ret

; ----------------------------------------------------------------
; ball_move — update ball position and handle collisions
; ----------------------------------------------------------------
ball_move:
        ; Move
        mov eax, [ball_x]
        add eax, [ball_vx]
        mov [ball_x], eax

        mov eax, [ball_y]
        add eax, [ball_vy]
        mov [ball_y], eax

        ; Wall bounce X
        mov eax, [ball_x]
        cmp eax, 0
        jge .no_left_wall
        neg eax
        mov [ball_x], eax
        neg dword [ball_vx]
        jmp .wall_x_done
.no_left_wall:
        add eax, BALL_W
        cmp eax, SCR_W
        jle .wall_x_done
        ; Hit right wall
        mov eax, SCR_W - BALL_W
        mov [ball_x], eax
        neg dword [ball_vx]
.wall_x_done:

        ; Wall bounce top
        mov eax, [ball_y]
        cmp eax, 0
        jge .no_top_wall
        neg eax
        mov [ball_y], eax
        neg dword [ball_vy]
.no_top_wall:

        ; Paddle collision
        mov eax, [ball_y]
        add eax, BALL_H
        cmp eax, PAD_Y
        jl .no_paddle_check
        cmp eax, PAD_Y + PAD_H + 4
        jg .no_paddle_check
        mov ebx, [ball_x]
        mov ecx, [pad_x]
        cmp ebx, ecx
        jl .no_paddle_check
        add ecx, PAD_W
        cmp ebx, ecx
        jg .no_paddle_check
        ; Bounce off paddle: reflect vy, nudge up
        mov eax, [ball_y]
        mov ebx, PAD_Y - BALL_H - 1
        mov [ball_y], ebx
        neg dword [ball_vy]
        ; Vary angle based on paddle hit position
        mov eax, [ball_x]
        add eax, BALL_W / 2     ; ball centre x
        mov ebx, [pad_x]
        add ebx, PAD_W / 2      ; paddle centre x
        sub eax, ebx            ; offset from centre
        ; eax in range ~ -PAD_W/2 .. +PAD_W/2
        ; Scale to vx: vx = eax / 8 (rough)
        sar eax, 3
        ; clamp to [-6, 6]
        cmp eax, 6
        jle .vx_not_high
        mov eax, 6
.vx_not_high:
        cmp eax, -6
        jge .vx_not_low
        mov eax, -6
.vx_not_low:
        test eax, eax
        jnz .vx_ok
        ; avoid zero vx
        mov eax, 3
.vx_ok:
        mov [ball_vx], eax
        jmp .no_paddle_check
.no_paddle_check:

        ; Bottom — lose a life
        mov eax, [ball_y]
        cmp eax, SCR_H
        jl .no_bottom
        dec dword [lives]
        cmp dword [lives], 0
        jle .game_over_set
        ; Reset ball
        mov dword [ball_x], SCR_W / 2 - BALL_W / 2
        mov dword [ball_y], PAD_Y - BALL_H - 20
        mov dword [ball_vx], 3
        mov dword [ball_vy], -4
        jmp .bricks_done
.game_over_set:
        mov byte [game_over], 1
        ret
.no_bottom:

        ; Brick collision
        call check_brick_collision

.bricks_done:
        ; Check win: any brick alive?
        mov esi, bricks
        mov ecx, BRICK_ROWS * BRICK_COLS
.check_win_loop:
        mov al, [esi]
        test al, al
        jnz .no_win
        inc esi
        loop .check_win_loop
        ; All bricks gone — win
        mov byte [game_over], 2
.no_win:
        ret

; ----------------------------------------------------------------
; check_brick_collision — test ball against all bricks
; ----------------------------------------------------------------
check_brick_collision:
        xor ebx, ebx            ; row
.row_loop:
        cmp ebx, BRICK_ROWS
        jge .done
        xor ecx, ecx            ; col
.col_loop:
        cmp ecx, BRICK_COLS
        jge .next_row

        ; Brick index
        mov eax, ebx
        imul eax, BRICK_COLS
        add eax, ecx

        ; Check alive
        lea esi, [bricks + eax]
        cmp byte [esi], 0
        je .next_col

        ; Brick rect
        ; bx_l = BRICK_START_X + col*(BRICK_W+BRICK_GAP_X)
        mov edx, ecx
        imul edx, BRICK_W + BRICK_GAP_X
        add edx, BRICK_START_X  ; edx = brick left
        mov [tmp_bx], edx
        add edx, BRICK_W
        mov [tmp_bx2], edx      ; brick right

        mov edx, ebx
        imul edx, BRICK_H + BRICK_GAP_Y
        add edx, BRICK_START_Y  ; edx = brick top
        mov [tmp_by], edx
        add edx, BRICK_H
        mov [tmp_by2], edx      ; brick bottom

        ; Ball rect: [ball_x .. ball_x+BALL_W] x [ball_y .. ball_y+BALL_H]
        mov eax, [ball_x]       ; ball left
        mov edi, [ball_y]       ; ball top

        ; AABB overlap test
        ; no overlap if ball_right <= bx_l or ball_left >= bx_r
        ;             or ball_bottom <= by_t or ball_top >= by_b
        mov edx, eax
        add edx, BALL_W         ; ball right
        cmp edx, [tmp_bx]
        jle .next_col           ; ball_right <= brick_left
        cmp eax, [tmp_bx2]
        jge .next_col           ; ball_left >= brick_right
        mov edx, edi
        add edx, BALL_H         ; ball bottom
        cmp edx, [tmp_by]
        jle .next_col
        cmp edi, [tmp_by2]
        jge .next_col

        ; HIT! Destroy brick
        mov byte [esi], 0
        add dword [score], 10

        ; Determine bounce axis: which overlap is smaller?
        ; Horizontal overlap
        mov eax, [ball_x]
        add eax, BALL_W
        sub eax, [tmp_bx]       ; ball_right - brick_left (horiz overlap 1)
        cmp eax, 0
        jl .h_ov_done
        mov edi, [tmp_bx2]
        sub edi, [ball_x]       ; brick_right - ball_left (horiz overlap 2)
        cmp eax, edi
        jl .h_ov_set
        mov eax, edi
.h_ov_set:
        mov [tmp_hov], eax
        jmp .h_ov_end
.h_ov_done:
        mov dword [tmp_hov], 0x7fffffff
.h_ov_end:

        ; Vertical overlap
        mov eax, [ball_y]
        add eax, BALL_H
        sub eax, [tmp_by]
        cmp eax, 0
        jl .v_ov_done
        mov edi, [tmp_by2]
        sub edi, [ball_y]
        cmp eax, edi
        jl .v_ov_set
        mov eax, edi
.v_ov_set:
        mov [tmp_vov], eax
        jmp .v_ov_end
.v_ov_done:
        mov dword [tmp_vov], 0x7fffffff
.v_ov_end:

        mov eax, [tmp_hov]
        mov edx, [tmp_vov]
        cmp eax, edx
        jl .bounce_x
        neg dword [ball_vy]
        jmp .next_col
.bounce_x:
        neg dword [ball_vx]

.next_col:
        inc ecx
        jmp .col_loop
.next_row:
        inc ebx
        jmp .row_loop
.done:
        ret

; ----------------------------------------------------------------
; draw_frame — clear and render everything to shadow buffer
; ----------------------------------------------------------------
draw_frame:
        ; Clear black
        mov edi, [fb]
        mov ecx, SCR_W * SCR_H
        xor eax, eax
        rep stosd

        ; Draw bricks
        xor ebx, ebx
.dr_row:
        cmp ebx, BRICK_ROWS
        jge .bricks_done_d
        xor ecx, ecx
.dr_col:
        cmp ecx, BRICK_COLS
        jge .next_row_d
        ; Index
        mov eax, ebx
        imul eax, BRICK_COLS
        add eax, ecx
        cmp byte [bricks + eax], 0
        je .skip_brick

        ; Brick color based on row
        movzx eax, bl
        mov edi, [brick_colors + eax * 4]

        ; Brick top-left
        mov eax, ecx
        imul eax, BRICK_W + BRICK_GAP_X
        add eax, BRICK_START_X  ; x
        mov esi, ebx
        imul esi, BRICK_H + BRICK_GAP_Y
        add esi, BRICK_START_Y  ; y

        ; Draw filled brick rect
        push ebx
        push ecx
        mov [drect_x], eax
        mov [drect_y], esi
        mov [drect_w], dword BRICK_W
        mov [drect_h], dword BRICK_H
        call fill_rect
        pop ecx
        pop ebx

.skip_brick:
        inc ecx
        jmp .dr_col
.next_row_d:
        inc ebx
        jmp .dr_row
.bricks_done_d:

        ; Draw paddle (white-ish)
        mov eax, [pad_x]
        mov [drect_x], eax
        mov [drect_y], dword PAD_Y
        mov [drect_w], dword PAD_W
        mov [drect_h], dword PAD_H
        mov [drect_col], dword 0xCCCCFF
        call fill_rect

        ; Draw ball (bright white)
        mov eax, [ball_x]
        mov [drect_x], eax
        mov eax, [ball_y]
        mov [drect_y], eax
        mov [drect_w], dword BALL_W
        mov [drect_h], dword BALL_H
        mov [drect_col], dword 0xFFFFFF
        call fill_rect

        ; Draw score text (top-left via SYS_FRAMEBUF sub=3)
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 2              ; col
        mov edx, 0              ; row
        mov esi, score_str
        int 0x80

        ; Draw lives
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 70             ; col (near right edge)
        mov edx, 0
        mov esi, lives_str
        int 0x80

        ; Update score/lives text
        call update_hud
        ret

; fill_rect — draw solid rectangle
; inputs: [drect_x], [drect_y], [drect_w], [drect_h], [drect_col]
fill_rect:
        mov edx, [drect_y]      ; current y
        mov eax, [drect_y]
        add eax, [drect_h]
        mov [drect_y2], eax     ; y end
.fr_row:
        cmp edx, [drect_y2]
        jge .fr_done
        ; Compute row base: fb + y*PITCH + x*4
        mov eax, edx
        imul eax, PITCH
        add eax, [fb]
        mov esi, [drect_x]
        lea eax, [eax + esi * 4]
        ; Fill row pixels
        mov ecx, [drect_w]
        mov edi, [drect_col]
.fr_pix:
        mov [eax], edi
        add eax, 4
        loop .fr_pix
        inc edx
        jmp .fr_row
.fr_done:
        ret

; draw_gameover — show game over or win message
draw_gameover:
        ; Final blit first
        call draw_frame
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; Show text in the middle
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 30             ; col
        mov edx, 12             ; row (middle-ish)
        cmp byte [game_over], 2
        je .show_win
        mov esi, gameover_str
        jmp .show_msg
.show_win:
        mov esi, win_str
.show_msg:
        int 0x80

        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80
        ret

; update_hud — format score/lives into strings
update_hud:
        ; Format score: "SCORE: NNNNN"
        mov esi, score_str + 7  ; after "SCORE: "
        mov eax, [score]
        call fmt_dec5
        ; Format lives: "LIVES: N"
        mov esi, lives_str + 7
        mov eax, [lives]
        call fmt_dec1
        ret

; fmt_dec5 — write 5-digit decimal of EAX to [ESI], no null
fmt_dec5:
        push eax
        push ebx
        push ecx
        push edx
        mov ecx, 5
        add esi, 4              ; point to last digit
.fd5:
        xor edx, edx
        mov ebx, 10
        div ebx
        add dl, '0'
        mov [esi], dl
        dec esi
        loop .fd5
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; fmt_dec1 — write 1-digit decimal of EAX to [ESI]
fmt_dec1:
        and eax, 0xFF
        add al, '0'
        mov [esi], al
        ret

; ----------------------------------------------------------------
; Data
; ----------------------------------------------------------------
section .data

fb:         dd 0
last_key:   dd 0
pad_x:      dd (SCR_W - PAD_W) / 2
ball_x:     dd SCR_W / 2 - BALL_W / 2
ball_y:     dd PAD_Y - BALL_H - 2
ball_vx:    dd 3
ball_vy:    dd -4
score:      dd 0
lives:      dd MAX_LIVES
game_over:  db 0

; Brick alive flags (5 rows × 10 cols)
bricks:     times BRICK_ROWS * BRICK_COLS db 1

; Brick row colors (one per row)
brick_colors:
        dd 0xFF4040         ; row 0: red
        dd 0xFF8C00         ; row 1: orange
        dd 0xFFFF00         ; row 2: yellow
        dd 0x40FF40         ; row 3: green
        dd 0x4080FF         ; row 4: blue

; HUD strings (padded to fixed width)
score_str:  db "SCORE: 00000", 0
lives_str:  db "LIVES: 3", 0
gameover_str: db "GAME OVER! Press any key", 0
win_str:    db "YOU WIN!  Press any key ", 0

; Scratch
drect_x:    dd 0
drect_y:    dd 0
drect_w:    dd 0
drect_h:    dd 0
drect_col:  dd 0xFFFFFF
drect_y2:   dd 0
tmp_bx:     dd 0
tmp_bx2:    dd 0
tmp_by:     dd 0
tmp_by2:    dd 0
tmp_hov:    dd 0
tmp_vov:    dd 0
