; breakout.asm - Breakout / Arkanoid clone for Mellivora OS
; Runs in a Burrows GUI window. Paddle+ball physics, colored bricks,
; multiple levels, score tracking.

%include "syscalls.inc"
%include "lib/gui.inc"

; Window
WIN_W           equ 300
WIN_H           equ 280

; Play area
PLAY_X          equ 0
PLAY_Y          equ 30
PLAY_W          equ 300
PLAY_H          equ 250

; Paddle
PAD_W           equ 50
PAD_H           equ 8
PAD_Y           equ (PLAY_Y + PLAY_H - PAD_H - 4)
PAD_SPEED       equ 6

; Ball
BALL_SIZE       equ 6
BALL_INIT_DX    equ 2
BALL_INIT_DY    equ -3

; Bricks
BRICK_ROWS      equ 6
BRICK_COLS      equ 10
BRICK_W         equ 28
BRICK_H         equ 10
BRICK_GAP       equ 2
BRICK_X_OFS     equ 3
BRICK_Y_OFS     equ (PLAY_Y + 10)
MAX_BRICKS      equ (BRICK_ROWS * BRICK_COLS)

; Game states
STATE_MENU      equ 0
STATE_PLAY      equ 1
STATE_PAUSED    equ 2
STATE_GAMEOVER  equ 3
STATE_WIN       equ 4

; Colors
COL_BG          equ 0x00000000
COL_PADDLE      equ 0x00CCCCCC
COL_BALL        equ 0x00FFFFFF
COL_HUD_BG     equ 0x00222222
COL_HUD_TEXT    equ 0x0000FF00
COL_GAME_OVER   equ 0x00FF4444
COL_WIN         equ 0x0000FF00

start:
        ; Create window
        mov eax, 100
        mov ebx, 50
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        call game_init

.main_loop:
        call gui_compose

        ; Handle events
        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        jne .no_key

        ; Key handling
        cmp dword [game_state], STATE_PLAY
        jne .menu_key

        ; In-game keys
        cmp bl, KEY_LEFT
        je .move_left
        cmp bl, 'a'
        je .move_left
        cmp bl, 'A'
        je .move_left
        cmp bl, KEY_RIGHT
        je .move_right
        cmp bl, 'd'
        je .move_right
        cmp bl, 'D'
        je .move_right
        cmp bl, 'p'
        je .pause_game
        cmp bl, 'P'
        je .pause_game
        cmp bl, 27             ; ESC
        je .close
        jmp .no_key

.menu_key:
        cmp dword [game_state], STATE_PAUSED
        jne .check_restart
        cmp bl, 'p'
        je .unpause
        cmp bl, 'P'
        je .unpause
        cmp bl, 27
        je .close
        jmp .no_key
.check_restart:
        ; Game over or win — press space to restart
        cmp bl, ' '
        je .restart
        cmp bl, 27
        je .close
        jmp .no_key

.move_left:
        mov eax, [pad_x]
        sub eax, PAD_SPEED
        cmp eax, PLAY_X
        jge .ml_ok
        mov eax, PLAY_X
.ml_ok:
        mov [pad_x], eax
        jmp .no_key

.move_right:
        mov eax, [pad_x]
        add eax, PAD_SPEED
        mov ecx, PLAY_X + PLAY_W - PAD_W
        cmp eax, ecx
        jle .mr_ok
        mov eax, ecx
.mr_ok:
        mov [pad_x], eax
        jmp .no_key

.pause_game:
        mov dword [game_state], STATE_PAUSED
        jmp .no_key

.unpause:
        mov dword [game_state], STATE_PLAY
        jmp .no_key

.restart:
        call game_init
        jmp .no_key

.no_key:
        ; Update game logic
        cmp dword [game_state], STATE_PLAY
        jne .skip_update
        call game_update
.skip_update:

        ; Render
        call game_render
        call gui_flip
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; GAME INIT
;=======================================================================

game_init:
        pushad
        ; Reset state
        mov dword [game_state], STATE_PLAY
        mov dword [score], 0
        mov dword [lives], 3
        mov dword [level], 1
        mov dword [bricks_left], MAX_BRICKS

        ; Paddle center
        mov dword [pad_x], (PLAY_X + PLAY_W/2 - PAD_W/2)

        ; Ball on paddle
        call reset_ball

        ; Init bricks
        call init_bricks

        popad
        ret

reset_ball:
        pushad
        mov eax, [pad_x]
        add eax, PAD_W / 2 - BALL_SIZE / 2
        mov [ball_x], eax
        mov dword [ball_y], PAD_Y - BALL_SIZE - 1
        mov dword [ball_dx], BALL_INIT_DX
        mov dword [ball_dy], BALL_INIT_DY
        popad
        ret

init_bricks:
        pushad
        mov dword [bricks_left], MAX_BRICKS
        mov edi, bricks
        xor ecx, ecx           ; Row
.ib_row:
        cmp ecx, BRICK_ROWS
        jge .ib_done
        xor edx, edx           ; Col
.ib_col:
        cmp edx, BRICK_COLS
        jge .ib_next_row
        mov byte [edi], 1      ; Brick alive
        inc edi
        inc edx
        jmp .ib_col
.ib_next_row:
        inc ecx
        jmp .ib_row
.ib_done:
        popad
        ret

;=======================================================================
; GAME UPDATE
;=======================================================================

game_update:
        pushad

        ; Move ball
        mov eax, [ball_dx]
        add [ball_x], eax
        mov eax, [ball_dy]
        add [ball_y], eax

        ; Wall collisions
        ; Left wall
        cmp dword [ball_x], PLAY_X
        jge .no_left_wall
        mov dword [ball_x], PLAY_X
        neg dword [ball_dx]
.no_left_wall:
        ; Right wall
        mov eax, [ball_x]
        add eax, BALL_SIZE
        cmp eax, PLAY_X + PLAY_W
        jle .no_right_wall
        mov eax, PLAY_X + PLAY_W - BALL_SIZE
        mov [ball_x], eax
        neg dword [ball_dx]
.no_right_wall:
        ; Ceiling
        cmp dword [ball_y], PLAY_Y
        jge .no_ceiling
        mov dword [ball_y], PLAY_Y
        neg dword [ball_dy]
.no_ceiling:

        ; Bottom — lose life
        mov eax, [ball_y]
        cmp eax, PLAY_Y + PLAY_H
        jl .no_bottom
        dec dword [lives]
        cmp dword [lives], 0
        jle .game_over
        call reset_ball
        jmp .gu_done
.game_over:
        mov dword [game_state], STATE_GAMEOVER
        jmp .gu_done
.no_bottom:

        ; Paddle collision
        ; Ball bottom edge touching paddle top
        mov eax, [ball_y]
        add eax, BALL_SIZE
        cmp eax, PAD_Y
        jl .no_pad
        cmp eax, PAD_Y + PAD_H
        jg .no_pad
        ; Check x overlap
        mov eax, [ball_x]
        add eax, BALL_SIZE
        cmp eax, [pad_x]
        jl .no_pad
        mov eax, [ball_x]
        mov ecx, [pad_x]
        add ecx, PAD_W
        cmp eax, ecx
        jg .no_pad
        ; Bounce
        neg dword [ball_dy]
        mov dword [ball_y], PAD_Y - BALL_SIZE - 1

        ; Adjust dx based on where ball hits paddle
        ; If left third: dx = -3, middle: keep, right third: dx = 3
        mov eax, [ball_x]
        add eax, BALL_SIZE / 2
        sub eax, [pad_x]        ; Offset from paddle left
        cmp eax, PAD_W / 3
        jl .pad_left
        cmp eax, (PAD_W * 2) / 3
        jl .pad_center
        ; Right third
        mov dword [ball_dx], 3
        jmp .no_pad
.pad_left:
        mov dword [ball_dx], -3
        jmp .no_pad
.pad_center:
        ; Keep current dx direction, set to 2
        cmp dword [ball_dx], 0
        jl .pad_neg
        mov dword [ball_dx], 2
        jmp .no_pad
.pad_neg:
        mov dword [ball_dx], -2
.no_pad:

        ; Brick collisions
        call check_brick_collision

        ; Check win
        cmp dword [bricks_left], 0
        jg .gu_done
        ; Advance level
        inc dword [level]
        cmp dword [level], 4
        jg .win_game
        call init_bricks
        call reset_ball
        jmp .gu_done
.win_game:
        mov dword [game_state], STATE_WIN
.gu_done:
        popad
        ret

;---------------------------------------
; check_brick_collision
;---------------------------------------
check_brick_collision:
        pushad
        ; Check each alive brick
        xor ecx, ecx           ; Row
        mov esi, bricks
.cb_row:
        cmp ecx, BRICK_ROWS
        jge .cb_done
        xor edx, edx           ; Col
.cb_col:
        cmp edx, BRICK_COLS
        jge .cb_next_row

        ; Is brick alive?
        cmp byte [esi], 0
        je .cb_next

        ; Calculate brick position
        mov eax, edx
        imul eax, (BRICK_W + BRICK_GAP)
        add eax, BRICK_X_OFS   ; Brick left x
        mov [.cb_bx], eax

        mov eax, ecx
        imul eax, (BRICK_H + BRICK_GAP)
        add eax, BRICK_Y_OFS   ; Brick top y
        mov [.cb_by], eax

        ; AABB collision: ball vs brick
        ; Ball right < brick left? → no collision
        mov eax, [ball_x]
        add eax, BALL_SIZE
        cmp eax, [.cb_bx]
        jle .cb_next
        ; Ball left > brick right?
        mov eax, [ball_x]
        mov ebx, [.cb_bx]
        add ebx, BRICK_W
        cmp eax, ebx
        jge .cb_next
        ; Ball bottom < brick top?
        mov eax, [ball_y]
        add eax, BALL_SIZE
        cmp eax, [.cb_by]
        jle .cb_next
        ; Ball top > brick bottom?
        mov eax, [ball_y]
        mov ebx, [.cb_by]
        add ebx, BRICK_H
        cmp eax, ebx
        jge .cb_next

        ; Collision! Kill brick
        mov byte [esi], 0
        dec dword [bricks_left]
        add dword [score], 10 ; 10 points per brick
        neg dword [ball_dy]     ; Bounce vertically
        jmp .cb_done            ; Only one brick per frame

.cb_next:
        inc esi
        inc edx
        jmp .cb_col
.cb_next_row:
        inc ecx
        jmp .cb_row
.cb_done:
        popad
        ret

.cb_bx: dd 0
.cb_by: dd 0

;=======================================================================
; RENDERING
;=======================================================================

game_render:
        pushad

        ; Clear play area
        mov eax, [win_id]
        mov ebx, PLAY_X
        mov ecx, PLAY_Y
        mov edx, PLAY_W
        mov esi, PLAY_H
        mov edi, COL_BG
        call gui_fill_rect

        ; Draw HUD background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, 28
        mov edi, COL_HUD_BG
        call gui_fill_rect

        ; Draw score text
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 4
        mov esi, str_score
        mov edi, COL_HUD_TEXT
        call gui_draw_text

        ; Draw score number
        mov eax, [score]
        call itoa
        mov eax, [win_id]
        mov ebx, 56
        mov ecx, 4
        mov esi, num_buf
        mov edi, COL_HUD_TEXT
        call gui_draw_text

        ; Draw lives
        mov eax, [win_id]
        mov ebx, 120
        mov ecx, 4
        mov esi, str_lives
        mov edi, COL_HUD_TEXT
        call gui_draw_text

        mov eax, [lives]
        call itoa
        mov eax, [win_id]
        mov ebx, 168
        mov ecx, 4
        mov esi, num_buf
        mov edi, COL_HUD_TEXT
        call gui_draw_text

        ; Draw level
        mov eax, [win_id]
        mov ebx, 210
        mov ecx, 4
        mov esi, str_level
        mov edi, COL_HUD_TEXT
        call gui_draw_text

        mov eax, [level]
        call itoa
        mov eax, [win_id]
        mov ebx, 260
        mov ecx, 4
        mov esi, num_buf
        mov edi, COL_HUD_TEXT
        call gui_draw_text

        ; Draw bricks
        call draw_bricks

        ; Draw paddle
        mov eax, [win_id]
        mov ebx, [pad_x]
        mov ecx, PAD_Y
        mov edx, PAD_W
        mov esi, PAD_H
        mov edi, COL_PADDLE
        call gui_fill_rect

        ; Draw ball
        mov eax, [win_id]
        mov ebx, [ball_x]
        mov ecx, [ball_y]
        mov edx, BALL_SIZE
        mov esi, BALL_SIZE
        mov edi, COL_BALL
        call gui_fill_rect

        ; Draw state overlays
        cmp dword [game_state], STATE_PAUSED
        je .draw_paused
        cmp dword [game_state], STATE_GAMEOVER
        je .draw_gameover
        cmp dword [game_state], STATE_WIN
        je .draw_win
        jmp .render_done

.draw_paused:
        mov eax, [win_id]
        mov ebx, 100
        mov ecx, 140
        mov esi, str_paused
        mov edi, COL_HUD_TEXT
        call gui_draw_text
        jmp .render_done

.draw_gameover:
        mov eax, [win_id]
        mov ebx, 90
        mov ecx, 130
        mov esi, str_gameover
        mov edi, COL_GAME_OVER
        call gui_draw_text
        mov eax, [win_id]
        mov ebx, 60
        mov ecx, 150
        mov esi, str_restart
        mov edi, COL_HUD_TEXT
        call gui_draw_text
        jmp .render_done

.draw_win:
        mov eax, [win_id]
        mov ebx, 80
        mov ecx, 130
        mov esi, str_you_win
        mov edi, COL_WIN
        call gui_draw_text
        mov eax, [win_id]
        mov ebx, 60
        mov ecx, 150
        mov esi, str_restart
        mov edi, COL_HUD_TEXT
        call gui_draw_text

.render_done:
        popad
        ret

;---------------------------------------
; draw_bricks
;---------------------------------------
draw_bricks:
        pushad
        xor ecx, ecx           ; Row
        mov esi, bricks
.db_row:
        cmp ecx, BRICK_ROWS
        jge .db_done
        xor edx, edx           ; Col
.db_col:
        cmp edx, BRICK_COLS
        jge .db_next_row
        cmp byte [esi], 0
        je .db_next             ; Dead brick

        ; Calculate position
        push ecx
        push edx

        ; Brick x
        mov eax, edx
        imul eax, (BRICK_W + BRICK_GAP)
        add eax, BRICK_X_OFS
        mov [.db_bx], eax

        ; Brick y
        mov eax, ecx
        imul eax, (BRICK_H + BRICK_GAP)
        add eax, BRICK_Y_OFS
        mov [.db_by], eax

        ; Color based on row
        mov eax, ecx
        cmp eax, 6
        jl .db_color_ok
        mov eax, 5
.db_color_ok:
        mov eax, [brick_colors + eax*4]

        ; Draw it
        push eax               ; Color
        mov eax, [win_id]
        mov ebx, [.db_bx]
        mov ecx, [.db_by]
        mov edx, BRICK_W
        mov esi, BRICK_H
        pop edi                 ; Color
        call gui_fill_rect

        pop edx
        pop ecx

.db_next:
        inc esi
        inc edx
        jmp .db_col
.db_next_row:
        inc ecx
        jmp .db_row
.db_done:
        popad
        ret

.db_bx: dd 0
.db_by: dd 0

;=======================================================================
; UTILITY
;=======================================================================

; itoa - Convert unsigned integer to decimal string
; EAX = number → num_buf filled
itoa:
        pushad
        mov edi, num_buf
        mov ecx, 0              ; Digit count
        mov ebx, 10
.itoa_div:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        cmp eax, 0
        jne .itoa_div
.itoa_write:
        pop edx
        add dl, '0'
        mov [edi], dl
        inc edi
        dec ecx
        jnz .itoa_write
        mov byte [edi], 0       ; Null terminate
        popad
        ret

;=======================================================================
; DATA
;=======================================================================

title_str:   db "Breakout", 0
str_score:   db "Score:", 0
str_lives:   db "Lives:", 0
str_level:   db "Level:", 0
str_paused:  db "PAUSED (P)", 0
str_gameover: db "GAME OVER", 0
str_you_win: db "YOU WIN!", 0
str_restart: db "Press SPACE to restart", 0

; Brick colors per row (6 rows)
brick_colors:
        dd 0x00FF2222          ; Red
        dd 0x00FF8822          ; Orange
        dd 0x00FFFF22          ; Yellow
        dd 0x0022FF22          ; Green
        dd 0x002288FF          ; Blue
        dd 0x00AA22FF          ; Purple

;=======================================================================
; BSS
;=======================================================================
align 4
win_id:         dd 0
game_state:     dd 0
score:          dd 0
lives:          dd 0
level:          dd 0
pad_x:          dd 0
ball_x:         dd 0
ball_y:         dd 0
ball_dx:        dd 0
ball_dy:        dd 0
bricks_left:    dd 0
num_buf:        times 12 db 0
bricks:         times MAX_BRICKS db 0
