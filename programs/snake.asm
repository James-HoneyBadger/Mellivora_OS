; snake.asm - Snake game for Mellivora OS — VBE pixel graphics
%include "syscalls.inc"

SCREEN_W        equ 640
SCREEN_H        equ 480
; Board dimensions in cells (border cells included in pixel layout)
BOARD_W         equ 78          ; inner play columns
BOARD_H         equ 54          ; inner play rows (expanded to fill screen)
MAX_SNAKE       equ 500
TICK_DELAY      equ 8

; Each cell = 8×8 pixels. Total with borders = (78+2)×8=640, (54+2)×8=448+32=480
CELL            equ 8
; Pixel offset of inner board (cell 1,1 maps to pixel BOFF_X, BOFF_Y+HUD_H)
HUD_H           equ 24          ; score bar height in pixels
BOFF_X          equ 0           ; border col 0 starts at x=0
BOFF_Y          equ HUD_H       ; border row 0 starts at y=HUD_H

; Colors
C_BG            equ 0x000000
C_BORDER        equ 0x00CCCC
C_HEAD          equ 0x00FF44
C_BODY          equ 0x007700
C_FOOD          equ 0xFF2200
C_SCORE         equ 0xFFFF00
C_GAMEOVER_BG   equ 0xCC0000
C_GAMEOVER_TXT  equ 0xFFFFFF

; Arrow key codes
KEY_UP          equ 0x80
KEY_DOWN        equ 0x81
KEY_LEFT        equ 0x82
KEY_RIGHT       equ 0x83

; Direction
DIR_UP          equ 0
DIR_DOWN        equ 1
DIR_LEFT        equ 2
DIR_RIGHT       equ 3

start:
        ; Init VBE 640x480x32
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCREEN_W
        mov edx, SCREEN_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je exit_game

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr], eax
        mov dword [fb_pitch], SCREEN_W * 4

        ; Seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

new_game:
        mov dword [direction], DIR_RIGHT
        mov dword [snake_len], 3
        mov dword [score], 0
        mov byte  [game_over], 0

        ; Place snake near centre
        mov eax, BOARD_W / 2 + 2
        mov [snake_x], eax
        dec eax
        mov [snake_x + 4], eax
        dec eax
        mov [snake_x + 8], eax
        mov eax, BOARD_H / 2
        mov [snake_y], eax
        mov [snake_y + 4], eax
        mov [snake_y + 8], eax

        ; Clear screen and draw board
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCREEN_W
        mov esi, SCREEN_H
        mov edi, C_BG
        call fb_fill_rect

        call draw_border
        call place_food
        call draw_score

;=== Main game loop ===
game_loop:
        mov eax, SYS_SLEEP
        mov ebx, TICK_DELAY
        int 0x80

        mov eax, SYS_READ_KEY
        int 0x80
        test al, al
        jz .no_key

        cmp al, 27
        je exit_game
        cmp al, 'q'
        je exit_game
        cmp al, KEY_UP
        je .go_up
        cmp al, 'w'
        je .go_up
        cmp al, KEY_DOWN
        je .go_down
        cmp al, 's'
        je .go_down
        cmp al, KEY_LEFT
        je .go_left
        cmp al, 'a'
        je .go_left
        cmp al, KEY_RIGHT
        je .go_right
        cmp al, 'd'
        je .go_right
        jmp .no_key

.go_up:
        cmp dword [direction], DIR_DOWN
        je .no_key
        mov dword [direction], DIR_UP
        jmp .no_key
.go_down:
        cmp dword [direction], DIR_UP
        je .no_key
        mov dword [direction], DIR_DOWN
        jmp .no_key
.go_left:
        cmp dword [direction], DIR_RIGHT
        je .no_key
        mov dword [direction], DIR_LEFT
        jmp .no_key
.go_right:
        cmp dword [direction], DIR_LEFT
        je .no_key
        mov dword [direction], DIR_RIGHT
        jmp .no_key

.no_key:
        ; Save old tail
        mov ecx, [snake_len]
        dec ecx
        mov eax, [snake_x + ecx*4]
        mov [old_tail_x], eax
        mov eax, [snake_y + ecx*4]
        mov [old_tail_y], eax

        ; Shift body
        mov ecx, [snake_len]
        dec ecx
.shift_body:
        cmp ecx, 0
        jle .shift_done
        mov eax, ecx
        dec eax
        mov edx, [snake_x + eax*4]
        mov [snake_x + ecx*4], edx
        mov edx, [snake_y + eax*4]
        mov [snake_y + ecx*4], edx
        dec ecx
        jmp .shift_body
.shift_done:

        ; Move head
        mov eax, [direction]
        cmp eax, DIR_UP
        je .move_up
        cmp eax, DIR_DOWN
        je .move_down
        cmp eax, DIR_LEFT
        je .move_left
        inc dword [snake_x]
        jmp .moved
.move_up:
        dec dword [snake_y]
        jmp .moved
.move_down:
        inc dword [snake_y]
        jmp .moved
.move_left:
        dec dword [snake_x]
.moved:

        ; Wall collision
        mov eax, [snake_x]
        cmp eax, 1
        jl .die
        cmp eax, BOARD_W
        jg .die
        mov eax, [snake_y]
        cmp eax, 1
        jl .die
        cmp eax, BOARD_H
        jg .die

        ; Self collision
        mov ecx, 1
.self_check:
        cmp ecx, [snake_len]
        jge .no_collision
        mov eax, [snake_x]
        cmp eax, [snake_x + ecx*4]
        jne .self_next
        mov eax, [snake_y]
        cmp eax, [snake_y + ecx*4]
        je .die
.self_next:
        inc ecx
        jmp .self_check
.no_collision:

        ; Food collision
        mov eax, [snake_x]
        cmp eax, [food_x]
        jne .no_food
        mov eax, [snake_y]
        cmp eax, [food_y]
        jne .no_food
        inc dword [score]
        mov eax, [snake_len]
        cmp eax, MAX_SNAKE - 1
        jge .no_grow
        inc dword [snake_len]
.no_grow:
        call place_food
        call draw_score

.no_food:
        ; Erase old tail cell
        mov eax, [old_tail_x]
        mov ebx, [old_tail_y]
        call erase_cell

        ; Redraw snake + food
        call draw_snake
        call draw_food_cell

        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        jmp game_loop

.die:
        call show_game_over
        jmp new_game

;=== draw_border ===
draw_border:
        pushad
        ; Top border (row 0)
        xor ebx, ebx
        mov ecx, BOFF_Y
        mov edx, SCREEN_W
        mov esi, CELL
        mov edi, C_BORDER
        call fb_fill_rect

        ; Bottom border (row BOARD_H+1)
        xor ebx, ebx
        mov ecx, BOFF_Y + (BOARD_H + 1) * CELL
        mov edx, SCREEN_W
        mov esi, CELL
        mov edi, C_BORDER
        call fb_fill_rect

        ; Left border (col 0)
        xor ebx, ebx
        mov ecx, BOFF_Y
        mov edx, CELL
        mov esi, (BOARD_H + 2) * CELL
        mov edi, C_BORDER
        call fb_fill_rect

        ; Right border (col BOARD_W+1)
        mov ebx, (BOARD_W + 1) * CELL
        mov ecx, BOFF_Y
        mov edx, CELL
        mov esi, (BOARD_H + 2) * CELL
        mov edi, C_BORDER
        call fb_fill_rect

        popad
        ret

;=== draw_snake ===
draw_snake:
        pushad
        ; Head
        mov eax, [snake_x]
        mov ebx, [snake_y]
        mov edi, C_HEAD
        call draw_cell

        ; Body
        mov edx, 1
.body_loop:
        cmp edx, [snake_len]
        jge .body_done
        mov eax, [snake_x + edx*4]
        mov ebx, [snake_y + edx*4]
        mov edi, C_BODY
        call draw_cell
        inc edx
        jmp .body_loop
.body_done:
        popad
        ret

;=== draw_cell: EAX=board_x, EBX=board_y, EDI=color ===
; board_x in 1..BOARD_W, board_y in 1..BOARD_H
draw_cell:
        push ebx
        push ecx
        push edx
        push esi
        ; pixel_x = col * CELL, pixel_y = BOFF_Y + row * CELL
        mov ecx, eax
        imul ecx, CELL
        mov edx, ebx
        imul edx, CELL
        add edx, BOFF_Y
        mov ebx, ecx            ; ebx = pixel_x
        mov ecx, edx            ; ecx = pixel_y
        mov edx, CELL
        mov esi, CELL
        call fb_fill_rect
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;=== erase_cell: EAX=board_x, EBX=board_y ===
erase_cell:
        push edi
        mov edi, C_BG
        call draw_cell
        pop edi
        ret

;=== draw_food_cell ===
draw_food_cell:
        pushad
        mov eax, [food_x]
        mov ebx, [food_y]
        mov edi, C_FOOD
        call draw_cell
        popad
        ret

;=== place_food ===
place_food:
        pushad
.retry:
        call rand
        xor edx, edx
        mov ebx, BOARD_W
        div ebx
        inc edx
        mov [food_x], edx

        call rand
        xor edx, edx
        mov ebx, BOARD_H
        div ebx
        inc edx
        mov [food_y], edx

        xor ecx, ecx
.check_snake:
        cmp ecx, [snake_len]
        jge .food_ok
        mov eax, [food_x]
        cmp eax, [snake_x + ecx*4]
        jne .next_seg
        mov eax, [food_y]
        cmp eax, [snake_y + ecx*4]
        je .retry
.next_seg:
        inc ecx
        jmp .check_snake
.food_ok:
        ; Draw the food
        mov eax, [food_x]
        mov ebx, [food_y]
        mov edi, C_FOOD
        call draw_cell
        popad
        ret

;=== draw_score ===
draw_score:
        pushad
        ; Clear HUD bar
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCREEN_W
        mov esi, HUD_H
        xor edi, edi
        call fb_fill_rect

        mov ebx, 8
        mov ecx, 4
        mov esi, str_score
        mov edi, C_SCORE
        call fb_draw_text

        mov eax, [score]
        mov ebx, 8 + 8 * 7
        mov ecx, 4
        mov edi, C_SCORE
        call fb_draw_num

        ; Controls hint
        mov ebx, 300
        mov ecx, 4
        mov esi, str_controls
        mov edi, 0x888888
        call fb_draw_text

        popad
        ret

;=== show_game_over ===
show_game_over:
        pushad
        ; Darken center
        mov ebx, 160
        mov ecx, 200
        mov edx, 320
        mov esi, 80
        mov edi, C_GAMEOVER_BG
        call fb_fill_rect

        mov ebx, 180
        mov ecx, 208
        mov esi, str_gameover
        mov edi, C_GAMEOVER_TXT
        call fb_draw_text

        mov ebx, 170
        mov ecx, 232
        mov esi, str_restart
        mov edi, C_GAMEOVER_TXT
        call fb_draw_text

.wait_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 27
        je exit_game
        cmp al, 'q'
        je exit_game
        cmp al, 'r'
        jne .wait_key

        ; Redraw border area before restart
        xor ebx, ebx
        mov ecx, HUD_H
        mov edx, SCREEN_W
        mov esi, SCREEN_H - HUD_H
        xor edi, edi
        call fb_fill_rect
        call draw_border

        popad
        ret

exit_game:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=== rand ===
rand:
        push ebx
        push ecx
        mov eax, [rand_seed]
        mov ecx, 1103515245
        imul eax, ecx
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        pop ecx
        pop ebx
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
str_score:      db "Score: ", 0
str_controls:   db "WASD/Arrows=Move  Q=Quit", 0
str_gameover:   db "  GAME OVER  ", 0
str_restart:    db "R=Restart  Q/ESC=Quit", 0

direction:      dd 0
snake_len:      dd 0
score:          dd 0
food_x:         dd 0
food_y:         dd 0
rand_seed:      dd 0
game_over:      db 0
old_tail_x:     dd 0
old_tail_y:     dd 0

snake_x:        times MAX_SNAKE dd 0
snake_y:        times MAX_SNAKE dd 0

section .bss
fb_addr:        resd 1
fb_pitch:       resd 1
num_buf:        resb 12
