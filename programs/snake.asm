; snake.asm - Snake game for Mellivora OS
; Converted from asm_snake (MIT License) by pmikkelsen
; 32-bit protected mode, uses INT 0x80 syscalls + direct VGA
%include "syscalls.inc"

; Game constants
BOARD_W         equ 78          ; playfield width (inside border)
BOARD_H         equ 23          ; playfield height (inside border)
MAX_SNAKE       equ 500         ; max snake segments
TICK_DELAY      equ 8           ; game speed (ticks between moves, 100Hz timer)

; Direction constants
DIR_UP          equ 0
DIR_DOWN        equ 1
DIR_LEFT        equ 2
DIR_RIGHT       equ 3

start:
        mov eax, SYS_CLEAR
        int 0x80

        ; Seed random from timer
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

new_game:
        ; Initialize game state
        mov dword [direction], DIR_RIGHT
        mov dword [snake_len], 3
        mov dword [score], 0
        mov byte  [game_over], 0

        ; Place snake in center
        mov eax, 40             ; x
        mov [snake_x], eax
        mov [snake_x + 4], eax
        mov [snake_x + 8], eax
        mov eax, 12             ; y
        mov [snake_y], eax
        mov [snake_y + 4], eax
        mov [snake_y + 8], eax
        ; Initial body offsets
        mov dword [snake_x + 4], 39     ; one behind head
        mov dword [snake_x + 8], 38     ; two behind head

        ; Clear screen and draw border
        mov eax, SYS_CLEAR
        int 0x80
        call draw_border

        ; Place first food
        call place_food

        ; Draw initial score
        call draw_score

;=== Main game loop ===
game_loop:
        ; Delay
        mov eax, SYS_SLEEP
        mov ebx, TICK_DELAY
        int 0x80

        ; Poll keyboard (non-blocking)
        mov eax, SYS_READ_KEY
        int 0x80
        test al, al
        jz .no_key

        ; Process key
        cmp al, 27              ; ESC
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
        je .no_key              ; can't reverse
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
        ; Save old tail position before shifting
        mov ecx, [snake_len]
        dec ecx
        mov eax, [snake_x + ecx*4]
        mov [old_tail_x], eax
        mov eax, [snake_y + ecx*4]
        mov [old_tail_y], eax

        ; Move snake: shift body
        mov ecx, [snake_len]
        dec ecx                 ; start from tail
.shift_body:
        cmp ecx, 0
        jle .shift_done
        ; snake_x[ecx] = snake_x[ecx-1]
        mov eax, ecx
        dec eax
        mov edx, [snake_x + eax*4]
        mov [snake_x + ecx*4], edx
        mov edx, [snake_y + eax*4]
        mov [snake_y + ecx*4], edx
        dec ecx
        jmp .shift_body
.shift_done:

        ; Move head based on direction
        mov eax, [direction]
        cmp eax, DIR_UP
        je .move_up
        cmp eax, DIR_DOWN
        je .move_down
        cmp eax, DIR_LEFT
        je .move_left
        ; DIR_RIGHT
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

        ; Check wall collision
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

        ; Check self collision
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

        ; Check food collision
        mov eax, [snake_x]
        cmp eax, [food_x]
        jne .no_food
        mov eax, [snake_y]
        cmp eax, [food_y]
        jne .no_food

        ; Eat food!
        inc dword [score]
        mov eax, [snake_len]
        cmp eax, MAX_SNAKE - 1
        jge .no_grow
        inc dword [snake_len]
.no_grow:
        call place_food
        call draw_score

.no_food:
        ; Erase old tail (using saved position from before shift)
        mov eax, [old_tail_x]
        mov ebx, [old_tail_y]
        call vga_put_space

        ; Draw snake
        call draw_snake

        ; Draw food
        mov eax, [food_x]
        mov ebx, [food_y]
        mov cl, '*'
        mov ch, 0x0C            ; bright red
        call vga_putchar_at

        jmp game_loop

.die:
        ; Game over
        call show_game_over
        jmp new_game

;=== Draw border ===
draw_border:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; cyan
        int 0x80

        ; Top border
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        mov ecx, VGA_WIDTH
.top:
        mov eax, SYS_PUTCHAR
        mov ebx, 0xC4           ; horizontal line char ─
        int 0x80
        dec ecx
        jnz .top

        ; Bottom border
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, VGA_HEIGHT - 1
        int 0x80
        mov ecx, VGA_WIDTH
.bottom:
        mov eax, SYS_PUTCHAR
        mov ebx, 0xC4
        int 0x80
        dec ecx
        jnz .bottom

        ; Left/right borders
        mov edx, 1
.sides:
        cmp edx, VGA_HEIGHT - 1
        jge .sides_done
        ; Left
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, edx
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        ; Right
        mov eax, SYS_SETCURSOR
        mov ebx, VGA_WIDTH - 1
        mov ecx, edx
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        inc edx
        jmp .sides
.sides_done:

        ; Corners
        ; Top-left
        mov cl, '+'
        mov ch, 0x0B
        xor eax, eax
        xor ebx, ebx
        call vga_putchar_at
        ; Top-right
        mov eax, VGA_WIDTH - 1
        xor ebx, ebx
        call vga_putchar_at
        ; Bottom-left
        xor eax, eax
        mov ebx, VGA_HEIGHT - 1
        call vga_putchar_at
        ; Bottom-right
        mov eax, VGA_WIDTH - 1
        mov ebx, VGA_HEIGHT - 1
        call vga_putchar_at

        popad
        ret

;=== Draw snake ===
draw_snake:
        pushad
        ; Draw head
        mov eax, [snake_x]
        mov ebx, [snake_y]
        mov cl, '@'
        mov ch, 0x0A            ; bright green
        call vga_putchar_at

        ; Draw body
        mov edx, 1
.body_loop:
        cmp edx, [snake_len]
        jge .body_done
        mov eax, [snake_x + edx*4]
        mov ebx, [snake_y + edx*4]
        mov cl, 'o'
        mov ch, 0x02            ; dark green
        call vga_putchar_at
        inc edx
        jmp .body_loop
.body_done:
        popad
        ret

;=== Place food at random location ===
place_food:
        pushad
.retry:
        ; Random X: 1 to BOARD_W
        call rand
        xor edx, edx
        mov ebx, BOARD_W
        div ebx
        inc edx
        mov [food_x], edx

        ; Random Y: 1 to BOARD_H
        call rand
        xor edx, edx
        mov ebx, BOARD_H
        div ebx
        inc edx
        mov [food_y], edx

        ; Make sure food isn't on snake
        xor ecx, ecx
.check_snake:
        cmp ecx, [snake_len]
        jge .food_ok
        mov eax, [food_x]
        cmp eax, [snake_x + ecx*4]
        jne .next_seg
        mov eax, [food_y]
        cmp eax, [snake_y + ecx*4]
        je .retry               ; on snake, try again
.next_seg:
        inc ecx
        jmp .check_snake
.food_ok:
        popad
        ret

;=== Draw score ===
draw_score:
        pushad
        ; Put score in top-right area of border
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        xor ecx, ecx
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; yellow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_score
        int 0x80
        mov eax, [score]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_space
        int 0x80
        popad
        ret

;=== Show game over ===
show_game_over:
        pushad
        ; Draw "GAME OVER" centered
        mov eax, SYS_SETCURSOR
        mov ebx, 30
        mov ecx, 11
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x4F           ; white on red
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_gameover
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 25
        mov ecx, 13
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; bright white
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_final_score
        int 0x80
        mov eax, [score]
        call print_dec

        mov eax, SYS_SETCURSOR
        mov ebx, 22
        mov ecx, 15
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_restart
        int 0x80

        ; Wait for keypress
.wait_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 27              ; ESC
        je exit_game
        cmp al, 'q'
        je exit_game
        cmp al, 'r'
        jne .wait_key

        ; Clear and restart
        mov eax, SYS_CLEAR
        int 0x80
        popad
        ret

exit_game:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;=== Put char at screen position (direct VGA) ===
; EAX = x, EBX = y, CL = char, CH = color attribute
vga_putchar_at:
        pushad
        imul ebx, VGA_WIDTH * 2
        lea edi, [VGA_BASE + ebx + eax*2]
        mov [edi], cl
        mov [edi+1], ch
        popad
        ret

;=== Put space at screen position ===
; EAX = x, EBX = y
vga_put_space:
        pushad
        imul ebx, VGA_WIDTH * 2
        lea edi, [VGA_BASE + ebx + eax*2]
        mov byte [edi], ' '
        mov byte [edi+1], 0x00 ; black on black
        popad
        ret

;=== Simple PRNG (LCG) ===
rand:
        push ebx
        push ecx
        mov eax, [rand_seed]
        mov ecx, 1103515245
        imul eax, ecx
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16             ; use upper bits (better distribution)
        and eax, 0x7FFF
        pop ecx
        pop ebx
        ret

;=== Data ===
msg_score:      db " Score: ", 0
msg_space:      db "  ", 0
msg_gameover:   db "  GAME OVER  ", 0
msg_final_score: db "Final Score: ", 0
msg_restart:    db "Press R to restart, ESC to quit", 0

;=== BSS (uninitialized data at end) ===
direction:      dd 0
snake_len:      dd 0
score:          dd 0
food_x:         dd 0
food_y:         dd 0
rand_seed:      dd 0
game_over:      db 0
snake_x:        times MAX_SNAKE dd 0
snake_y:        times MAX_SNAKE dd 0
old_tail_x:     dd 0
old_tail_y:     dd 0
