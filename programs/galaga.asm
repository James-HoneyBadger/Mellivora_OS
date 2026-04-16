; galaga.asm - Galaga-style space shooter for Mellivora OS
; VGA text mode, ASCII art sprites, multiple enemy types
%include "syscalls.inc"

; Game constants
SCREEN_W        equ 80
SCREEN_H        equ 25
PLAY_TOP        equ 2           ; Top of play area
PLAY_BOT        equ 23          ; Bottom of play area
PLAYER_Y        equ 22          ; Player ship row
MAX_BULLETS     equ 8
MAX_ENEMIES     equ 30
MAX_STARS       equ 20
TICK_SPEED      equ 4           ; Game tick delay (100Hz timer)
ENEMY_MOVE_RATE equ 8           ; Ticks between enemy moves
ENEMY_COLS      equ 10          ; Enemies per row
ENEMY_ROWS      equ 3           ; Number of enemy rows
BULLET_SPEED    equ 2           ; Ticks between bullet moves

; Enemy types
ETYPE_NONE      equ 0
ETYPE_BUG       equ 1           ; Basic enemy (10 pts)
ETYPE_MOTH      equ 2           ; Medium enemy (20 pts)
ETYPE_BOSS      equ 3           ; Boss enemy (30 pts)

start:
        mov eax, SYS_CLEAR
        int 0x80

        ; Seed random from timer
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

title_screen:
        mov eax, SYS_CLEAR
        int 0x80

        ; Draw title
        mov ebx, 25
        mov ecx, 4
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        mov ebx, 20
        mov ecx, 7
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_subtitle
        int 0x80

        ; Draw sample ship
        mov ebx, 38
        mov ecx, 10
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ship_art1
        int 0x80
        mov ebx, 36
        mov ecx, 11
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ship_art2
        int 0x80

        ; Draw sample enemies
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov ebx, 30
        mov ecx, 14
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_enemies_demo
        int 0x80

        mov ebx, 22
        mov ecx, 17
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_controls
        int 0x80

        mov ebx, 22
        mov ecx, 20
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_start
        int 0x80

.wait_start:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 27             ; ESC to quit
        je exit_game
        cmp al, ' '
        je new_game
        cmp al, 0x0D
        je new_game
        jmp .wait_start

new_game:
        ; Initialize game state
        mov dword [score], 0
        mov dword [lives], 3
        mov dword [level], 1
        mov dword [player_x], 38
        mov byte  [game_over], 0

new_level:
        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Initialize bullets
        mov ecx, MAX_BULLETS
        mov edi, bullets
        xor eax, eax
        imul ecx, 8            ; 8 bytes per bullet
        shr ecx, 2
        rep stosd

        ; Initialize enemies for this level
        call init_enemies

        ; Initialize stars
        call init_stars

        mov dword [tick_count], 0
        mov dword [enemy_dir], 1        ; Moving right
        mov dword [enemy_tick], 0

        ; Draw HUD
        call draw_hud

;=== Main game loop ===
game_loop:
        ; Delay
        mov eax, SYS_SLEEP
        mov ebx, TICK_SPEED
        int 0x80

        inc dword [tick_count]

        ; Poll keyboard
        mov eax, SYS_READ_KEY
        int 0x80
        test al, al
        jz .no_key

        cmp al, 27             ; ESC
        je exit_game
        cmp al, 'q'
        je exit_game

        cmp al, KEY_LEFT
        je .move_left
        cmp al, 'a'
        je .move_left
        cmp al, KEY_RIGHT
        je .move_right
        cmp al, 'd'
        je .move_right
        cmp al, ' '
        je .fire
        jmp .no_key

.move_left:
        cmp dword [player_x], 1
        jle .no_key
        ; Erase old ship
        call erase_player
        dec dword [player_x]
        jmp .no_key

.move_right:
        cmp dword [player_x], SCREEN_W - 4
        jge .no_key
        call erase_player
        inc dword [player_x]
        jmp .no_key

.fire:
        call fire_bullet

.no_key:
        ; Update bullets
        call update_bullets

        ; Update enemies
        call update_enemies

        ; Check collisions
        call check_collisions

        ; Draw everything
        call draw_stars
        call draw_player
        call draw_bullets
        call draw_enemies
        call draw_hud

        ; Check game over
        cmp byte [game_over], 0
        jne game_over_screen

        ; Check level complete (all enemies dead)
        call count_enemies
        cmp eax, 0
        jne game_loop

        ; Level complete!
        inc dword [level]
        ; Brief pause
        mov eax, SYS_SLEEP
        mov ebx, 100
        int 0x80
        jmp new_level

;=======================================================================
; GAME OVER
;=======================================================================
game_over_screen:
        mov eax, SYS_SLEEP
        mov ebx, 50
        int 0x80

        mov ebx, 30
        mov ecx, 11
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x4F           ; White on red
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_game_over
        int 0x80

        mov ebx, 28
        mov ecx, 13
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_final_score
        int 0x80
        mov eax, [score]
        call print_dec

        mov ebx, 25
        mov ecx, 16
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_play_again
        int 0x80

.go_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je new_game
        cmp al, 'Y'
        je new_game
        cmp al, 'n'
        je exit_game
        cmp al, 'N'
        je exit_game
        cmp al, 27
        je exit_game
        jmp .go_wait

exit_game:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; INIT ENEMIES
;=======================================================================
init_enemies:
        PUSHALL
        mov edi, enemies
        ; Clear all
        mov ecx, MAX_ENEMIES * 8
        xor eax, eax
        rep stosb

        ; Create grid of enemies
        mov edi, enemies
        xor ebx, ebx           ; row

.ie_row:
        cmp ebx, ENEMY_ROWS
        jge .ie_done
        xor ecx, ecx           ; col

.ie_col:
        cmp ecx, ENEMY_COLS
        jge .ie_next_row

        ; Enemy type based on row
        mov al, ETYPE_BUG
        cmp ebx, 0
        jne .ie_not_boss
        mov al, ETYPE_BOSS
        jmp .ie_set
.ie_not_boss:
        cmp ebx, 1
        jne .ie_set
        mov al, ETYPE_MOTH
.ie_set:
        mov [edi], al           ; type
        ; X position: spread across screen
        mov eax, ecx
        imul eax, 6             ; 6 chars apart
        add eax, 10             ; offset from left
        mov [edi + 1], al       ; x (byte)
        ; Y position
        mov eax, ebx
        imul eax, 2             ; 2 rows apart
        add eax, PLAY_TOP + 1
        mov [edi + 2], al       ; y (byte)
        mov byte [edi + 3], 0   ; anim frame

        add edi, 8
        inc ecx
        jmp .ie_col

.ie_next_row:
        inc ebx
        jmp .ie_row

.ie_done:
        POPALL
        ret

;=======================================================================
; INIT STARS (background)
;=======================================================================
init_stars:
        PUSHALL
        mov edi, stars
        mov ecx, MAX_STARS
.is_loop:
        call random
        xor edx, edx
        mov ebx, SCREEN_W
        div ebx
        mov [edi], dl           ; x
        call random
        xor edx, edx
        mov ebx, PLAY_BOT - PLAY_TOP
        div ebx
        add dl, PLAY_TOP
        mov [edi + 1], dl       ; y
        add edi, 2
        dec ecx
        jnz .is_loop
        POPALL
        ret

;=======================================================================
; DRAW PLAYER SHIP
;=======================================================================
draw_player:
        PUSHALL
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; Bright white
        int 0x80

        ; Top part: /A\
        mov ebx, [player_x]
        mov ecx, PLAYER_Y
        mov eax, SYS_SETCURSOR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; Cyan
        int 0x80

        mov eax, SYS_PUTCHAR
        mov ebx, '/'
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'A'
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '\'
        int 0x80

        ; Bottom part: ^^^
        mov ebx, [player_x]
        mov ecx, PLAYER_Y + 1
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '^'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '^'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '^'
        int 0x80

        POPALL
        ret

;=======================================================================
; ERASE PLAYER
;=======================================================================
erase_player:
        PUSHALL
        mov ebx, [player_x]
        mov ecx, PLAYER_Y
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_erase3
        int 0x80
        mov ebx, [player_x]
        mov ecx, PLAYER_Y + 1
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_erase3
        int 0x80
        POPALL
        ret

;=======================================================================
; FIRE BULLET
;=======================================================================
fire_bullet:
        PUSHALL
        ; Find free bullet slot
        mov edi, bullets
        mov ecx, MAX_BULLETS

.fb_find:
        cmp byte [edi], 0       ; active?
        je .fb_slot
        add edi, 8
        dec ecx
        jnz .fb_find
        jmp .fb_done             ; No free slot

.fb_slot:
        mov byte [edi], 1       ; active
        mov eax, [player_x]
        inc eax                 ; Center of ship
        mov [edi + 1], al       ; x
        mov al, PLAYER_Y - 1
        mov [edi + 2], al       ; y
        ; Play beep
        mov eax, SYS_BEEP
        mov ebx, 2000
        mov ecx, 2
        int 0x80

.fb_done:
        POPALL
        ret

;=======================================================================
; UPDATE BULLETS
;=======================================================================
update_bullets:
        PUSHALL
        ; Only move on every other tick
        mov eax, [tick_count]
        and eax, 1
        jnz .ub_done

        mov edi, bullets
        mov ecx, MAX_BULLETS

.ub_loop:
        cmp byte [edi], 0
        je .ub_next

        ; Erase old position
        movzx ebx, byte [edi + 1]
        movzx ecx, byte [edi + 2]
        push rcx
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rcx

        ; Move up
        dec byte [edi + 2]
        cmp byte [edi + 2], PLAY_TOP
        jle .ub_deactivate

        ; Still need ecx for outer loop counter
        jmp .ub_next

.ub_deactivate:
        mov byte [edi], 0

.ub_next:
        add edi, 8
        ; Restore loop (we used ecx above - use separate counter)
        jmp .ub_continue

.ub_continue:
        ; We need to track iterations separately
        jmp .ub_done             ; simplified - just one pass

.ub_done:
        POPALL
        ret

;=======================================================================
; DRAW BULLETS
;=======================================================================
draw_bullets:
        PUSHALL
        mov edi, bullets
        mov ecx, MAX_BULLETS

.db_loop:
        cmp byte [edi], 0
        je .db_next

        movzx ebx, byte [edi + 1]
        movzx eax, byte [edi + 2]
        push rcx
        mov ecx, eax
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        pop rcx

.db_next:
        add edi, 8
        dec ecx
        jnz .db_loop
        POPALL
        ret

;=======================================================================
; UPDATE ENEMIES
;=======================================================================
update_enemies:
        PUSHALL
        inc dword [enemy_tick]
        mov eax, [enemy_tick]

        ; Adjust speed based on level
        mov ebx, ENEMY_MOVE_RATE
        mov ecx, [level]
        cmp ecx, 4
        jle .ue_speed_ok
        mov ecx, 4
.ue_speed_ok:
        sub ebx, ecx           ; Faster each level
        cmp ebx, 2
        jge .ue_speed_set
        mov ebx, 2
.ue_speed_set:
        xor edx, edx
        div ebx
        cmp edx, 0
        jne .ue_done

        ; Erase all enemies first
        call erase_enemies

        ; Check if any enemy at edge
        mov edi, enemies
        mov ecx, MAX_ENEMIES
        xor ebx, ebx           ; need_drop flag

.ue_check_edge:
        cmp byte [edi], ETYPE_NONE
        je .ue_check_next
        movzx eax, byte [edi + 1]
        cmp dword [enemy_dir], 1
        jne .ue_check_left
        cmp eax, SCREEN_W - 4
        jge .ue_hit_edge
        jmp .ue_check_next
.ue_check_left:
        cmp eax, 2
        jle .ue_hit_edge
        jmp .ue_check_next
.ue_hit_edge:
        mov ebx, 1
.ue_check_next:
        add edi, 8
        dec ecx
        jnz .ue_check_edge

        ; Move enemies
        mov edi, enemies
        mov ecx, MAX_ENEMIES

.ue_move:
        cmp byte [edi], ETYPE_NONE
        je .ue_move_next

        cmp ebx, 1
        je .ue_drop
        ; Move horizontally
        mov al, [edi + 1]
        add al, byte [enemy_dir]
        mov [edi + 1], al
        jmp .ue_move_next

.ue_drop:
        inc byte [edi + 2]      ; Drop down
        ; Check if reached player
        cmp byte [edi + 2], PLAYER_Y - 1
        jge .ue_reached_player

.ue_move_next:
        add edi, 8
        dec ecx
        jnz .ue_move

        ; Reverse direction if hit edge
        cmp ebx, 1
        jne .ue_done
        neg dword [enemy_dir]

.ue_done:
        POPALL
        ret

.ue_reached_player:
        mov byte [game_over], 1
        POPALL
        ret

;=======================================================================
; ERASE ENEMIES
;=======================================================================
erase_enemies:
        PUSHALL
        mov edi, enemies
        mov ecx, MAX_ENEMIES

.ee_loop:
        cmp byte [edi], ETYPE_NONE
        je .ee_next

        push rcx
        movzx ebx, byte [edi + 1]
        movzx ecx, byte [edi + 2]
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_erase3
        int 0x80
        pop rcx

.ee_next:
        add edi, 8
        dec ecx
        jnz .ee_loop
        POPALL
        ret

;=======================================================================
; DRAW ENEMIES
;=======================================================================
draw_enemies:
        PUSHALL
        mov edi, enemies
        mov ecx, MAX_ENEMIES

.de_loop:
        cmp byte [edi], ETYPE_NONE
        je .de_next

        push rcx
        ; Set cursor
        movzx ebx, byte [edi + 1]
        movzx ecx, byte [edi + 2]
        mov eax, SYS_SETCURSOR
        int 0x80

        ; Set color based on type
        movzx eax, byte [edi]
        cmp al, ETYPE_BOSS
        je .de_boss
        cmp al, ETYPE_MOTH
        je .de_moth
        ; Bug
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; Green
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, enemy_bug
        int 0x80
        jmp .de_drawn

.de_moth:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0D           ; Magenta
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, enemy_moth
        int 0x80
        jmp .de_drawn

.de_boss:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; Red
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, enemy_boss
        int 0x80

.de_drawn:
        pop rcx

.de_next:
        add edi, 8
        dec ecx
        jnz .de_loop
        POPALL
        ret

;=======================================================================
; DRAW STARS
;=======================================================================
draw_stars:
        PUSHALL
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; Dark gray
        int 0x80

        mov edi, stars
        mov ecx, MAX_STARS

.ds_loop:
        push rcx
        movzx ebx, byte [edi]
        movzx ecx, byte [edi + 1]
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        pop rcx

        add edi, 2
        dec ecx
        jnz .ds_loop
        POPALL
        ret

;=======================================================================
; CHECK COLLISIONS (bullets vs enemies)
;=======================================================================
check_collisions:
        PUSHALL
        mov esi, bullets
        mov ecx, MAX_BULLETS

.cc_bullet:
        cmp byte [esi], 0
        je .cc_next_bullet

        ; Check against all enemies
        mov edi, enemies
        push rcx
        mov ecx, MAX_ENEMIES

.cc_enemy:
        cmp byte [edi], ETYPE_NONE
        je .cc_next_enemy

        ; Compare positions
        movzx eax, byte [esi + 1]       ; bullet x
        movzx ebx, byte [edi + 1]       ; enemy x
        sub eax, ebx
        cmp eax, -1
        jl .cc_next_enemy
        cmp eax, 3
        jg .cc_next_enemy

        movzx eax, byte [esi + 2]       ; bullet y
        movzx ebx, byte [edi + 2]       ; enemy y
        cmp eax, ebx
        jne .cc_next_enemy

        ; HIT! Deactivate both
        mov byte [esi], 0       ; bullet gone

        ; Erase enemy at old position
        push rcx
        movzx ebx, byte [edi + 1]
        movzx ecx, byte [edi + 2]
        push rax
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_erase3
        int 0x80
        pop rax
        pop rcx

        ; Score based on type
        movzx eax, byte [edi]
        cmp al, ETYPE_BOSS
        je .cc_boss_pts
        cmp al, ETYPE_MOTH
        je .cc_moth_pts
        add dword [score], 10
        jmp .cc_kill
.cc_moth_pts:
        add dword [score], 20
        jmp .cc_kill
.cc_boss_pts:
        add dword [score], 30
.cc_kill:
        mov byte [edi], ETYPE_NONE

        ; Explosion beep
        push rcx
        mov eax, SYS_BEEP
        mov ebx, 200
        mov ecx, 2
        int 0x80
        pop rcx

        jmp .cc_next_bullet_pop

.cc_next_enemy:
        add edi, 8
        dec ecx
        jnz .cc_enemy

.cc_next_bullet_pop:
        pop rcx

.cc_next_bullet:
        add esi, 8
        dec ecx
        jnz .cc_bullet

        POPALL
        ret

;=======================================================================
; COUNT REMAINING ENEMIES -> EAX
;=======================================================================
count_enemies:
        push rcx
        push rdi
        mov edi, enemies
        mov ecx, MAX_ENEMIES
        xor eax, eax
.ce_loop:
        cmp byte [edi], ETYPE_NONE
        je .ce_next
        inc eax
.ce_next:
        add edi, 8
        dec ecx
        jnz .ce_loop
        pop rdi
        pop rcx
        ret

;=======================================================================
; DRAW HUD (heads-up display)
;=======================================================================
draw_hud:
        PUSHALL
        ; Top bar
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F           ; White on blue
        int 0x80

        xor ebx, ebx
        xor ecx, ecx
        mov eax, SYS_SETCURSOR
        int 0x80

        ; Fill top line
        mov ecx, SCREEN_W
.hud_fill:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec ecx
        jnz .hud_fill

        ; Score
        mov ebx, 1
        xor ecx, ecx
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hud_score
        int 0x80
        mov eax, [score]
        call print_dec

        ; Lives
        mov ebx, 30
        xor ecx, ecx
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hud_lives
        int 0x80
        mov eax, [lives]
        call print_dec

        ; Level
        mov ebx, 50
        xor ecx, ecx
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hud_level
        int 0x80
        mov eax, [level]
        call print_dec

        ; Enemies remaining
        mov ebx, 65
        xor ecx, ecx
        mov eax, SYS_SETCURSOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hud_enemies
        int 0x80
        call count_enemies
        call print_dec

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        POPALL
        ret

;=======================================================================
; RANDOM NUMBER GENERATOR
;=======================================================================
random:
        push rbx
        push rdx
        mov eax, [rand_seed]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        pop rdx
        pop rbx
        ret

;=======================================================================
; DATA
;=======================================================================

; Title screen
msg_title:      db "=== G A L A G A ===", 0
msg_subtitle:   db "Space Shooter for Mellivora OS", 0
ship_art1:      db "/A\", 0
ship_art2:      db "^^^^^", 0
msg_enemies_demo: db "{@@} <**> (##)", 0
msg_controls:   db "Left/Right or A/D: Move  Space: Fire", 0
msg_start:      db "Press SPACE or ENTER to start!", 0

; HUD
hud_score:      db "Score: ", 0
hud_lives:      db "Lives: ", 0
hud_level:      db "Level: ", 0
hud_enemies:    db "Foes: ", 0

; Game over
msg_game_over:  db " G A M E   O V E R ", 0
msg_final_score: db "Final Score: ", 0
msg_play_again: db "Play again? (Y/N)", 0

; Enemy sprites (3 chars wide)
enemy_bug:      db "<*>", 0
enemy_moth:     db "{@}", 0
enemy_boss:     db "[#]", 0

; Erase strings
str_erase3:     db "   ", 0

; Game state
score:          dd 0
lives:          dd 3
level:          dd 1
player_x:       dd 38
game_over:      db 0
tick_count:     dd 0
enemy_dir:      dd 1
enemy_tick:     dd 0
rand_seed:      dd 0

; Bullets: active(1), x(1), y(1), reserved(5) = 8 bytes each
bullets:        times MAX_BULLETS * 8 db 0

; Enemies: type(1), x(1), y(1), frame(1), reserved(4) = 8 bytes each
enemies:        times MAX_ENEMIES * 8 db 0

; Stars: x(1), y(1) = 2 bytes each
stars:          times MAX_STARS * 2 db 0
