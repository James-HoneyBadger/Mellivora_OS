; galaga.asm - Galaga-style space shooter — VBE pixel graphics
%include "syscalls.inc"

; ─── Screen / layout ───────────────────────────────────────────────
SCREEN_W        equ 640
SCREEN_H        equ 480
CHR_W           equ 8
CHR_H           equ 16
HUD_H           equ CHR_H

; ─── Game constants (char-grid coords, same as original) ──────────
PLAY_TOP        equ 2
PLAY_BOT        equ 28
PLAYER_Y        equ 27
MAX_BULLETS     equ 8
MAX_ENEMIES     equ 30
MAX_STARS       equ 40
TICK_SPEED      equ 4
ENEMY_MOVE_RATE equ 8
ENEMY_COLS      equ 10
ENEMY_ROWS      equ 3

; ─── Enemy types ────────────────────────────────────────────────────
ETYPE_NONE      equ 0
ETYPE_BUG       equ 1
ETYPE_MOTH      equ 2
ETYPE_BOSS      equ 3

; ─── Sprite pixel sizes ─────────────────────────────────────────────
PLR_W           equ 24
PLR_H           equ 16
ENE_W           equ 20
ENE_H           equ 12
BLT_W           equ 3
BLT_H           equ 12
STAR_SZ         equ 2

; ─── Colors ─────────────────────────────────────────────────────────
C_BG            equ 0x000000
C_HUD           equ 0x000F3B
C_HUD_TXT       equ 0xFFFFFF
C_STAR          equ 0x444466
C_PLR_BODY      equ 0x44AAFF
C_PLR_ENG       equ 0xFFDD44
C_BULLET        equ 0xFFFF44
C_BUG           equ 0x22FF44
C_MOTH          equ 0xFF44CC
C_BOSS          equ 0xFF4422
C_GOBOX         equ 0xAA0000
C_GO_TXT        equ 0xFFFFFF

; ─── Key codes ──────────────────────────────────────────────────────
KEY_LEFT        equ 0x82
KEY_RIGHT       equ 0x83

start:
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

        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

title_screen:
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCREEN_W
        mov esi, SCREEN_H
        xor edi, edi
        call fb_fill_rect

        mov ebx, 220
        mov ecx, 60
        mov esi, msg_title
        mov edi, 0xFFDD00
        call fb_draw_text

        mov ebx, 170
        mov ecx, 90
        mov esi, msg_subtitle
        mov edi, 0x44FF44
        call fb_draw_text

        mov ebx, 200
        mov ecx, 140
        mov esi, msg_enemies_demo
        mov edi, 0xFF4444
        call fb_draw_text

        mov ebx, 160
        mov ecx, 200
        mov esi, msg_controls
        mov edi, 0xAAAAAA
        call fb_draw_text

        mov ebx, 196
        mov ecx, 250
        mov esi, msg_start
        mov edi, 0x44CCFF
        call fb_draw_text

.wait_start:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 27
        je exit_game
        cmp al, ' '
        je new_game
        cmp al, 0x0D
        je new_game
        jmp .wait_start

new_game:
        mov dword [score], 0
        mov dword [lives], 3
        mov dword [level], 1
        mov dword [player_x], 38
        mov byte  [game_over], 0

new_level:
        mov ecx, MAX_BULLETS * 2
        mov edi, bullets
        xor eax, eax
        rep stosd

        call init_enemies
        call init_stars

        mov dword [tick_count], 0
        mov dword [enemy_dir], 1
        mov dword [enemy_tick], 0

game_loop:
        mov eax, SYS_SLEEP
        mov ebx, TICK_SPEED
        int 0x80

        inc dword [tick_count]

        mov eax, SYS_READ_KEY
        int 0x80
        test al, al
        jz .no_key

        cmp al, 27
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
        dec dword [player_x]
        jmp .no_key
.move_right:
        mov eax, SCREEN_W / CHR_W - 4
        cmp [player_x], eax
        jge .no_key
        inc dword [player_x]
        jmp .no_key
.fire:
        call fire_bullet

.no_key:
        call update_bullets
        call update_enemies
        call check_collisions

        call clear_frame
        call draw_stars
        call draw_enemies
        call draw_bullets
        call draw_player
        call draw_hud

        cmp byte [game_over], 0
        jne game_over_screen

        call count_enemies
        cmp eax, 0
        jne game_loop

        inc dword [level]
        mov eax, SYS_SLEEP
        mov ebx, 100
        int 0x80
        jmp new_level

;=======================================================================
game_over_screen:
        mov eax, SYS_SLEEP
        mov ebx, 50
        int 0x80

        mov ebx, 160
        mov ecx, 200
        mov edx, 320
        mov esi, 80
        mov edi, C_GOBOX
        call fb_fill_rect

        mov ebx, 208
        mov ecx, 214
        mov esi, msg_game_over
        mov edi, C_GO_TXT
        call fb_draw_text

        mov ebx, 196
        mov ecx, 238
        mov esi, msg_final_score
        mov edi, 0xFFDD44
        call fb_draw_text

        mov eax, [score]
        mov ebx, 340
        mov ecx, 238
        mov edi, 0xFFDD44
        call fb_draw_num

        mov ebx, 180
        mov ecx, 262
        mov esi, msg_play_again
        mov edi, C_GO_TXT
        call fb_draw_text

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
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; clear_frame
;=======================================================================
clear_frame:
        xor ebx, ebx
        mov ecx, HUD_H
        mov edx, SCREEN_W
        mov esi, SCREEN_H - HUD_H
        xor edi, edi
        call fb_fill_rect
        ret

;=======================================================================
; init_enemies
;=======================================================================
init_enemies:
        pushad
        mov edi, enemies
        mov ecx, MAX_ENEMIES * 8
        xor eax, eax
        rep stosb

        mov edi, enemies
        xor ebx, ebx

.ie_row:
        cmp ebx, ENEMY_ROWS
        jge .ie_done
        xor ecx, ecx

.ie_col:
        cmp ecx, ENEMY_COLS
        jge .ie_next_row

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
        mov [edi], al
        mov eax, ecx
        imul eax, 6
        add eax, 10
        mov [edi + 1], al
        mov eax, ebx
        imul eax, 2
        add eax, PLAY_TOP + 1
        mov [edi + 2], al
        mov byte [edi + 3], 0

        add edi, 8
        inc ecx
        jmp .ie_col

.ie_next_row:
        inc ebx
        jmp .ie_row

.ie_done:
        popad
        ret

;=======================================================================
; init_stars
;=======================================================================
init_stars:
        pushad
        mov edi, stars
        mov ecx, MAX_STARS
.is_loop:
        call random
        xor edx, edx
        mov ebx, SCREEN_W / CHR_W
        div ebx
        mov [edi], dl
        call random
        xor edx, edx
        mov ebx, PLAY_BOT - PLAY_TOP
        div ebx
        add dl, PLAY_TOP
        mov [edi + 1], dl
        add edi, 2
        dec ecx
        jnz .is_loop
        popad
        ret

;=======================================================================
; draw_player
;=======================================================================
draw_player:
        pushad
        mov eax, [player_x]
        imul eax, CHR_W

        mov ebx, eax
        mov ecx, PLAYER_Y * CHR_H
        mov edx, PLR_W
        mov esi, PLR_H / 2
        mov edi, C_PLR_BODY
        call fb_fill_rect

        mov eax, [player_x]
        imul eax, CHR_W
        add eax, 4
        mov ebx, eax
        mov ecx, PLAYER_Y * CHR_H + PLR_H / 2
        mov edx, PLR_W - 8
        mov esi, PLR_H / 2
        mov edi, C_PLR_ENG
        call fb_fill_rect
        popad
        ret

;=======================================================================
; fire_bullet
;=======================================================================
fire_bullet:
        pushad
        mov edi, bullets
        mov ecx, MAX_BULLETS

.fb_find:
        cmp byte [edi], 0
        je .fb_slot
        add edi, 8
        dec ecx
        jnz .fb_find
        jmp .fb_done

.fb_slot:
        mov byte [edi], 1
        mov eax, [player_x]
        inc eax
        mov [edi + 1], al
        mov al, PLAYER_Y - 1
        mov [edi + 2], al
        mov eax, SYS_BEEP
        mov ebx, 2000
        mov ecx, 2
        int 0x80

.fb_done:
        popad
        ret

;=======================================================================
; update_bullets
;=======================================================================
update_bullets:
        pushad
        mov eax, [tick_count]
        and eax, 1
        jnz .ub_done

        mov edi, bullets
        mov ecx, MAX_BULLETS

.ub_loop:
        cmp byte [edi], 0
        je .ub_next

        dec byte [edi + 2]
        cmp byte [edi + 2], PLAY_TOP
        jle .ub_deact
        jmp .ub_next
.ub_deact:
        mov byte [edi], 0

.ub_next:
        add edi, 8
        dec ecx
        jnz .ub_loop
.ub_done:
        popad
        ret

;=======================================================================
; draw_bullets  (use EBP as loop index to keep struct ptr stable)
;=======================================================================
draw_bullets:
        pushad
        xor ebp, ebp

.db_loop:
        cmp ebp, MAX_BULLETS
        jge .db_done

        mov eax, ebp
        imul eax, 8
        add eax, bullets
        mov edi, eax

        cmp byte [edi], 0
        je .db_next

        movzx eax, byte [edi + 1]
        imul eax, CHR_W
        add eax, (CHR_W - BLT_W) / 2
        mov ebx, eax

        movzx eax, byte [edi + 2]
        imul eax, CHR_H
        mov ecx, eax

        mov edx, BLT_W
        mov esi, BLT_H
        mov edi, C_BULLET
        call fb_fill_rect

.db_next:
        inc ebp
        jmp .db_loop
.db_done:
        popad
        ret

;=======================================================================
; update_enemies
;=======================================================================
update_enemies:
        pushad
        inc dword [enemy_tick]
        mov eax, [enemy_tick]

        mov ebx, ENEMY_MOVE_RATE
        mov ecx, [level]
        cmp ecx, 4
        jle .ue_speed_ok
        mov ecx, 4
.ue_speed_ok:
        sub ebx, ecx
        cmp ebx, 2
        jge .ue_speed_set
        mov ebx, 2
.ue_speed_set:
        xor edx, edx
        div ebx
        cmp edx, 0
        jne .ue_done

        mov edi, enemies
        mov ecx, MAX_ENEMIES
        xor ebx, ebx

.ue_check_edge:
        cmp byte [edi], ETYPE_NONE
        je .ue_ce_next
        movzx eax, byte [edi + 1]
        cmp dword [enemy_dir], 1
        jne .ue_check_left
        cmp eax, SCREEN_W / CHR_W - 4
        jge .ue_hit_edge
        jmp .ue_ce_next
.ue_check_left:
        cmp eax, 2
        jle .ue_hit_edge
        jmp .ue_ce_next
.ue_hit_edge:
        mov ebx, 1
.ue_ce_next:
        add edi, 8
        dec ecx
        jnz .ue_check_edge

        mov edi, enemies
        mov ecx, MAX_ENEMIES
.ue_move:
        cmp byte [edi], ETYPE_NONE
        je .ue_move_next
        cmp ebx, 1
        je .ue_drop
        mov al, [edi + 1]
        add al, byte [enemy_dir]
        mov [edi + 1], al
        jmp .ue_move_next
.ue_drop:
        inc byte [edi + 2]
        cmp byte [edi + 2], PLAYER_Y - 1
        jge .ue_reached_player
.ue_move_next:
        add edi, 8
        dec ecx
        jnz .ue_move

        cmp ebx, 1
        jne .ue_done
        neg dword [enemy_dir]

.ue_done:
        popad
        ret

.ue_reached_player:
        mov byte [game_over], 1
        popad
        ret

;=======================================================================
; draw_enemies  (use EBP as loop index)
;=======================================================================
draw_enemies:
        pushad
        xor ebp, ebp

.de_loop:
        cmp ebp, MAX_ENEMIES
        jge .de_done

        mov eax, ebp
        imul eax, 8
        add eax, enemies
        mov edi, eax

        cmp byte [edi], ETYPE_NONE
        je .de_next

        movzx eax, byte [edi + 1]
        imul eax, CHR_W
        mov ebx, eax

        movzx eax, byte [edi + 2]
        imul eax, CHR_H
        mov ecx, eax

        movzx eax, byte [edi]
        cmp al, ETYPE_BOSS
        je .de_boss
        cmp al, ETYPE_MOTH
        je .de_moth
        mov edi, C_BUG
        jmp .de_draw
.de_moth:
        mov edi, C_MOTH
        jmp .de_draw
.de_boss:
        mov edi, C_BOSS
.de_draw:
        mov edx, ENE_W
        mov esi, ENE_H
        call fb_fill_rect

.de_next:
        inc ebp
        jmp .de_loop
.de_done:
        popad
        ret

;=======================================================================
; draw_stars  (use EBP as loop index)
;=======================================================================
draw_stars:
        pushad
        xor ebp, ebp

.ds_loop:
        cmp ebp, MAX_STARS
        jge .ds_done

        mov eax, ebp
        imul eax, 2
        add eax, stars
        mov edi, eax

        movzx eax, byte [edi]
        imul eax, CHR_W
        mov ebx, eax

        movzx eax, byte [edi + 1]
        imul eax, CHR_H
        mov ecx, eax

        mov edx, STAR_SZ
        mov esi, STAR_SZ
        mov edi, C_STAR
        call fb_fill_rect

.ds_next:
        inc ebp
        jmp .ds_loop
.ds_done:
        popad
        ret

;=======================================================================
; check_collisions
;=======================================================================
check_collisions:
        pushad
        mov esi, bullets
        mov ecx, MAX_BULLETS

.cc_bullet:
        cmp byte [esi], 0
        je .cc_next_bullet

        mov edi, enemies
        push ecx
        mov ecx, MAX_ENEMIES

.cc_enemy:
        cmp byte [edi], ETYPE_NONE
        je .cc_next_enemy

        movzx eax, byte [esi + 1]
        movzx ebx, byte [edi + 1]
        sub eax, ebx
        cmp eax, -1
        jl .cc_next_enemy
        cmp eax, 3
        jg .cc_next_enemy

        movzx eax, byte [esi + 2]
        movzx ebx, byte [edi + 2]
        cmp eax, ebx
        jne .cc_next_enemy

        mov byte [esi], 0

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

        push ecx
        mov eax, SYS_BEEP
        mov ebx, 200
        mov ecx, 2
        int 0x80
        pop ecx

        jmp .cc_next_bullet_pop

.cc_next_enemy:
        add edi, 8
        dec ecx
        jnz .cc_enemy

.cc_next_bullet_pop:
        pop ecx

.cc_next_bullet:
        add esi, 8
        dec ecx
        jnz .cc_bullet

        popad
        ret

;=======================================================================
; count_enemies -> EAX
;=======================================================================
count_enemies:
        push ecx
        push edi
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
        pop edi
        pop ecx
        ret

;=======================================================================
; draw_hud
;=======================================================================
draw_hud:
        pushad

        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCREEN_W
        mov esi, HUD_H
        mov edi, C_HUD
        call fb_fill_rect

        mov ebx, 4
        xor ecx, ecx
        mov esi, hud_score
        mov edi, C_HUD_TXT
        call fb_draw_text

        mov eax, [score]
        mov ebx, 60
        xor ecx, ecx
        mov edi, 0xFFFF44
        call fb_draw_num

        mov ebx, 160
        xor ecx, ecx
        mov esi, hud_lives
        mov edi, C_HUD_TXT
        call fb_draw_text

        mov eax, [lives]
        mov ebx, 216
        xor ecx, ecx
        mov edi, 0x44FF44
        call fb_draw_num

        mov ebx, 290
        xor ecx, ecx
        mov esi, hud_level
        mov edi, C_HUD_TXT
        call fb_draw_text

        mov eax, [level]
        mov ebx, 346
        xor ecx, ecx
        mov edi, 0x44CCFF
        call fb_draw_num

        mov ebx, 430
        xor ecx, ecx
        mov esi, hud_enemies
        mov edi, C_HUD_TXT
        call fb_draw_text

        call count_enemies
        mov ebx, 500
        xor ecx, ecx
        mov edi, 0xFF8844
        call fb_draw_num

        popad
        ret

;=======================================================================
; random -> EAX
;=======================================================================
random:
        push ebx
        push edx
        mov eax, [rand_seed]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        pop edx
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

; itoa: EAX=number -> num_buf
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
msg_title:       db "=== G A L A G A ===", 0
msg_subtitle:    db "Space Shooter for Mellivora OS", 0
msg_enemies_demo: db "{Boss}  {Moth}  <Bug>", 0
msg_controls:    db "Left/Right or A/D: Move   Space: Fire", 0
msg_start:       db "Press SPACE or ENTER to start!", 0
hud_score:       db "Score: ", 0
hud_lives:       db "Lives: ", 0
hud_level:       db "Level: ", 0
hud_enemies:     db "Foes: ", 0
msg_game_over:   db " G A M E   O V E R ", 0
msg_final_score: db "Final Score: ", 0
msg_play_again:  db "Play again? (Y/N)", 0

score:           dd 0
lives:           dd 3
level:           dd 1
player_x:        dd 38
game_over:       db 0
tick_count:      dd 0
enemy_dir:       dd 1
enemy_tick:      dd 0
rand_seed:       dd 0

bullets:         times MAX_BULLETS * 8 db 0
enemies:         times MAX_ENEMIES * 8 db 0
stars:           times MAX_STARS * 2   db 0

section .bss
fb_addr:         resd 1
fb_pitch:        resd 1
num_buf:         resb 12
