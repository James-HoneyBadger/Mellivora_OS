; pong2p.asm — Two-Player Pong for Mellivora OS v10
;
; Showcases:
;   SYS_MOUSE             (36)  — P2 paddle follows mouse Y
;   SYS_SURFACE_CREATE   (135) — compositor score panels
;   SYS_SURFACE_COMMIT   (136) — update panels each frame
;   SYS_SURFACE_DESTROY  (137) — cleanup on exit
;   lib/vbe.inc                — fill_rect / clear / present helpers
;   SYS_BEEP              (24)  — hit / wall / score audio
;
; Controls:
;   P1 (left)   W / S    — paddle up / down
;   P2 (right)  Mouse Y  — paddle follows cursor
;   Left-click           — serve ball after each point
;   Q / ESC              — quit

%include "syscalls.inc"
%include "lib/vbe.inc"

; ---- Screen -------------------------------------------------------
SCR_W           equ 640
SCR_H           equ 480

; ---- Paddle -------------------------------------------------------
PAD_W           equ 12
PAD_H           equ 80
PAD_SPEED       equ 5
PAD1_X          equ 18
PAD2_X          equ SCR_W - 18 - PAD_W   ; = 610

; ---- Ball ---------------------------------------------------------
BALL_W          equ 10
BALL_H          equ 10
MAX_BALL_DX     equ 6

; ---- Net ----------------------------------------------------------
NET_X           equ SCR_W / 2 - 1        ; = 319

; ---- Compositor panels (score indicators) -------------------------
PANEL_W         equ 120
PANEL_H         equ 40
PANEL1_X        equ 0
PANEL1_Y        equ 440
PANEL2_X        equ SCR_W - PANEL_W      ; = 520
PANEL2_Y        equ 440

; ---- Game rules ---------------------------------------------------
WIN_SCORE       equ 7
RALLY_SPD_INTV  equ 2   ; increase speed every N rallies

; ---- Palette ------------------------------------------------------
C_BG            equ 0x001122
C_PAD1          equ 0x44AAFF
C_PAD2          equ 0xFF6644
C_BALL          equ 0xFFFFFF
C_NET           equ 0x334455
C_HUD           equ 0xFFFFFF
C_WIN_PANEL     equ 0x00AA44
C_LOSE_PANEL    equ 0xAA2200
C_TIED_PANEL    equ 0x334466
C_WINNER        equ 0xFFFF44

; ====================================================================
start:
        ; Init VBE 640×480×32
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        jne .vbe_ok
        mov eax, SYS_PRINT
        mov ebx, msg_novbe
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
.vbe_ok:
        ; Query framebuffer info
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr],   eax
        mov [fb_width],  ebx
        mov [fb_height], ecx
        shl ebx, 2
        mov [fb_pitch],  ebx

        ; Seed RNG
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

        ; Create compositor surfaces (-1 = not available, game still works)
        mov dword [surf1_id], -1
        mov dword [surf2_id], -1

        mov eax, SYS_SURFACE_CREATE
        mov ebx, PANEL_W
        mov ecx, PANEL_H
        mov edx, PANEL1_X
        mov esi, PANEL1_Y
        int 0x80
        mov [surf1_id], eax

        mov eax, SYS_SURFACE_CREATE
        mov ebx, PANEL_W
        mov ecx, PANEL_H
        mov edx, PANEL2_X
        mov esi, PANEL2_Y
        int 0x80
        mov [surf2_id], eax

; ====================================================================
new_game:
        mov dword [score1], 0
        mov dword [score2], 0
        mov dword [pad1_y], SCR_H / 2 - PAD_H / 2
        mov dword [pad2_y], SCR_H / 2 - PAD_H / 2
        call reset_ball

; ====================================================================
game_loop:
        ; Winner check
        mov eax, [score1]
        cmp eax, WIN_SCORE
        jne .no_p1_win
        call show_winner
        call wait_key_or_quit
        jmp new_game
.no_p1_win:
        mov eax, [score2]
        cmp eax, WIN_SCORE
        jne .no_p2_win
        call show_winner
        call wait_key_or_quit
        jmp new_game
.no_p2_win:

        ; Read keys
        mov eax, SYS_READ_KEY
        int 0x80
        cmp eax, 'q'
        je .quit
        cmp eax, 'Q'
        je .quit
        cmp eax, 27
        je .quit
        ; P1 movement
        cmp eax, 'w'
        jne .no_p1_up
        sub dword [pad1_y], PAD_SPEED
        cmp dword [pad1_y], 0
        jge .no_p1_up
        mov dword [pad1_y], 0
.no_p1_up:
        cmp eax, 's'
        jne .no_p1_dn
        add dword [pad1_y], PAD_SPEED
        mov ecx, SCR_H - PAD_H
        cmp dword [pad1_y], ecx
        jle .no_p1_dn
        mov [pad1_y], ecx
.no_p1_dn:

        ; P2: mouse Y
        mov eax, SYS_MOUSE
        int 0x80
        ; EAX=mouse_x EBX=mouse_y ECX=buttons
        mov [mouse_btn_cur], ecx

        ; Clamp P2 paddle to mouse y (centered)
        sub ebx, PAD_H / 2
        cmp ebx, 0
        jge .p2_hi
        xor ebx, ebx
.p2_hi:
        mov ecx, SCR_H - PAD_H
        cmp ebx, ecx
        jle .p2_set
        mov ebx, ecx
.p2_set:
        mov [pad2_y], ebx

        ; Serve input: rising-edge left click
        cmp byte [waiting_serve], 0
        je .skip_serve
        mov eax, [mouse_btn_cur]
        and eax, 1              ; left button
        test eax, eax
        jz .no_click
        cmp byte [prev_btn], 0
        jne .no_click
        ; Launch!
        mov byte [waiting_serve], 0
        call launch_ball
.no_click:
        ; Update prev_btn
        mov eax, [mouse_btn_cur]
        and eax, 1
        mov [prev_btn], al
.skip_serve:

        ; Update ball physics
        cmp byte [waiting_serve], 0
        je .do_update
        jmp .skip_update
.do_update:
        call update_ball
.skip_update:

        ; Render
        call render_frame

        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        jmp game_loop

.quit:
        ; Destroy compositor surfaces
        cmp dword [surf1_id], -1
        je .no_ds1
        mov eax, SYS_SURFACE_DESTROY
        mov ebx, [surf1_id]
        int 0x80
.no_ds1:
        cmp dword [surf2_id], -1
        je .no_ds2
        mov eax, SYS_SURFACE_DESTROY
        mov ebx, [surf2_id]
        int 0x80
.no_ds2:
        ; Restore text mode
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        xor ecx, ecx
        xor edx, edx
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ====================================================================
; reset_ball — centre ball, enter serve-wait state
; ====================================================================
reset_ball:
        mov dword [ball_x],      SCR_W / 2 - BALL_W / 2
        mov dword [ball_y],      SCR_H / 2 - BALL_H / 2
        mov dword [ball_dx],     0
        mov dword [ball_dy],     0
        mov byte  [waiting_serve], 1
        mov dword [rally_count], 0
        mov byte  [prev_btn],    0
        ret

; ====================================================================
; launch_ball — random initial velocity
; ====================================================================
launch_ball:
        pushad
        call rand
        test eax, 1
        jnz .go_right
        mov dword [ball_dx], -3
        jmp .set_dy
.go_right:
        mov dword [ball_dx], 3
.set_dy:
        call rand
        xor edx, edx
        mov ebx, 5
        div ebx             ; edx = 0..4
        sub edx, 2          ; edx = -2..2
        test edx, edx
        jnz .dy_ok
        inc edx
.dy_ok:
        mov [ball_dy], edx
        popad
        ret

; ====================================================================
; update_ball — physics tick
; ====================================================================
update_ball:
        pushad

        ; Move
        mov eax, [ball_x]
        add eax, [ball_dx]
        mov [ball_x], eax
        mov eax, [ball_y]
        add eax, [ball_dy]
        mov [ball_y], eax

        ; Top wall bounce
        mov eax, [ball_y]
        cmp eax, 0
        jge .no_top
        neg dword [ball_dy]
        mov dword [ball_y], 0
        mov eax, SYS_BEEP
        mov ebx, 660
        mov ecx, 3
        int 0x80
        jmp .check_exit
.no_top:
        ; Bottom wall bounce
        cmp eax, SCR_H - BALL_H
        jle .check_p1_hit
        neg dword [ball_dy]
        mov dword [ball_y], SCR_H - BALL_H
        mov eax, SYS_BEEP
        mov ebx, 660
        mov ecx, 3
        int 0x80

.check_p1_hit:
        ; P1 paddle: only check when ball moving left (dx < 0)
        mov eax, [ball_dx]
        cmp eax, 0
        jge .check_p2_hit
        ; Ball x overlaps P1 paddle?
        mov eax, [ball_x]
        cmp eax, PAD1_X + PAD_W
        jg .check_p2_hit
        cmp eax, PAD1_X
        jl .check_p2_hit
        ; Ball y overlaps P1 paddle?
        mov eax, [ball_y]
        cmp eax, [pad1_y]
        jl .check_p2_hit
        mov ecx, [pad1_y]
        add ecx, PAD_H
        cmp eax, ecx
        jg .check_p2_hit
        call paddle_hit_p1
        jmp .check_exit

.check_p2_hit:
        ; P2 paddle: only check when ball moving right (dx > 0)
        mov eax, [ball_dx]
        cmp eax, 0
        jle .check_exit
        ; Ball right-edge overlaps P2 paddle?
        mov eax, [ball_x]
        add eax, BALL_W
        cmp eax, PAD2_X
        jl .check_exit
        cmp eax, PAD2_X + PAD_W
        jg .check_exit
        ; Ball y overlaps P2 paddle?
        mov eax, [ball_y]
        cmp eax, [pad2_y]
        jl .check_exit
        mov ecx, [pad2_y]
        add ecx, PAD_H
        cmp eax, ecx
        jg .check_exit
        call paddle_hit_p2

.check_exit:
        ; Ball exits left → P2 scores
        mov eax, [ball_x]
        cmp eax, -BALL_W
        jg .no_p2_score
        inc dword [score2]
        mov eax, SYS_BEEP
        mov ebx, 330
        mov ecx, 20
        int 0x80
        call reset_ball
        jmp .ub_done
.no_p2_score:
        ; Ball exits right → P1 scores
        mov eax, [ball_x]
        cmp eax, SCR_W
        jl .ub_done
        inc dword [score1]
        mov eax, SYS_BEEP
        mov ebx, 330
        mov ecx, 20
        int 0x80
        call reset_ball

.ub_done:
        popad
        ret

; ====================================================================
; paddle_hit_p1 — reverse dx, compute dy, speed-up
; ====================================================================
paddle_hit_p1:
        pushad
        ; Reverse dx (now goes right)
        neg dword [ball_dx]
        ; Push ball out of paddle
        mov dword [ball_x], PAD1_X + PAD_W + 1
        ; dy based on hit position within paddle
        mov eax, [ball_y]
        sub eax, [pad1_y]   ; 0..PAD_H-1
        imul eax, 6
        xor edx, edx
        mov ebx, PAD_H
        idiv ebx            ; eax = 0..5
        sub eax, 3          ; eax = -3..2
        mov [ball_dy], eax
        cmp dword [ball_dy], 0
        jne .p1h_dy_ok
        mov dword [ball_dy], 1
.p1h_dy_ok:
        ; Maybe increase speed
        inc dword [rally_count]
        mov eax, [rally_count]
        xor edx, edx
        mov ebx, RALLY_SPD_INTV
        div ebx
        test edx, edx
        jnz .p1h_spd_done
        ; Increase speed: ball_dx is positive here
        mov eax, [ball_dx]
        cmp eax, MAX_BALL_DX
        jge .p1h_spd_done
        inc dword [ball_dx]
.p1h_spd_done:
        mov eax, SYS_BEEP
        mov ebx, 880
        mov ecx, 4
        int 0x80
        popad
        ret

; ====================================================================
; paddle_hit_p2 — reverse dx, compute dy, speed-up
; ====================================================================
paddle_hit_p2:
        pushad
        neg dword [ball_dx]
        mov dword [ball_x], PAD2_X - BALL_W - 1
        ; dy from hit position
        mov eax, [ball_y]
        sub eax, [pad2_y]
        imul eax, 6
        xor edx, edx
        mov ebx, PAD_H
        idiv ebx
        sub eax, 3
        mov [ball_dy], eax
        cmp dword [ball_dy], 0
        jne .p2h_dy_ok
        mov dword [ball_dy], -1
.p2h_dy_ok:
        ; ball_dx is negative here; clamp leftward speed
        inc dword [rally_count]
        mov eax, [rally_count]
        xor edx, edx
        mov ebx, RALLY_SPD_INTV
        div ebx
        test edx, edx
        jnz .p2h_spd_done
        mov eax, [ball_dx]
        mov ebx, -MAX_BALL_DX
        cmp eax, ebx
        jle .p2h_spd_done
        dec dword [ball_dx]
.p2h_spd_done:
        mov eax, SYS_BEEP
        mov ebx, 880
        mov ecx, 4
        int 0x80
        popad
        ret

; ====================================================================
; render_frame — draw scene, present, update compositor surfaces
; ====================================================================
render_frame:
        pushad

        ; Clear shadow buffer
        mov edx, C_BG
        call vbe_clear_screen

        ; Draw dashed net
        xor ecx, ecx
.net_loop:
        cmp ecx, SCR_H
        jge .net_done
        push ecx
        mov ebx, NET_X
        ; ecx already set
        mov edx, 2
        mov esi, 4
        mov edi, C_NET
        call vbe_fill_rect
        pop ecx
        add ecx, 8
        jmp .net_loop
.net_done:

        ; Draw P1 paddle
        mov ebx, PAD1_X
        mov ecx, [pad1_y]
        mov edx, PAD_W
        mov esi, PAD_H
        mov edi, C_PAD1
        call vbe_fill_rect

        ; Draw P2 paddle
        mov ebx, PAD2_X
        mov ecx, [pad2_y]
        mov edx, PAD_W
        mov esi, PAD_H
        mov edi, C_PAD2
        call vbe_fill_rect

        ; Draw ball (only if in play)
        cmp byte [waiting_serve], 1
        je .skip_ball
        mov ebx, [ball_x]
        mov ecx, [ball_y]
        mov edx, BALL_W
        mov esi, BALL_H
        mov edi, C_BALL
        call vbe_fill_rect
.skip_ball:

        ; Score P1
        mov eax, [score1]
        call itoa
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 260
        mov edx, 4
        mov esi, num_buf
        mov edi, C_PAD1
        int 0x80

        ; Score P2
        mov eax, [score2]
        call itoa
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 370
        mov edx, 4
        mov esi, num_buf
        mov edi, C_PAD2
        int 0x80

        ; "CLICK TO SERVE" hint
        cmp byte [waiting_serve], 1
        jne .no_serve_hint
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 185
        mov edx, 230
        mov esi, str_serve
        mov edi, 0xAAAAAA
        int 0x80
.no_serve_hint:

        ; Present shadow → LFB
        call vbe_present

        ; Update compositor surface panels
        call update_panels

        popad
        ret

; ====================================================================
; update_panels — fill each surface with a status colour and commit
; ====================================================================
update_panels:
        pushad

        ; P1 panel colour: winning=green, tied=blue, losing=red
        mov eax, [score1]
        mov ebx, [score2]
        mov edi, C_TIED_PANEL
        cmp eax, ebx
        je .p1_col_done
        jg .p1_winning
        mov edi, C_LOSE_PANEL
        jmp .p1_col_done
.p1_winning:
        mov edi, C_WIN_PANEL
.p1_col_done:
        cmp dword [surf1_id], -1
        je .skip_p1
        mov [panel_color], edi
        ; Get pixel buffer
        mov eax, SYS_FRAMEBUF
        mov ebx, 5
        mov ecx, [surf1_id]
        int 0x80
        cmp eax, -1
        je .skip_p1
        ; Fill PANEL_W×PANEL_H pixels with colour
        mov edi, eax
        mov eax, [panel_color]
        mov ecx, PANEL_W * PANEL_H
        rep stosd
        ; Commit dirty region = whole panel
        mov eax, SYS_SURFACE_COMMIT
        mov ebx, [surf1_id]
        xor ecx, ecx
        xor edx, edx
        mov esi, PANEL_W
        mov edi, PANEL_H
        int 0x80
.skip_p1:

        ; P2 panel colour
        mov eax, [score2]
        mov ebx, [score1]
        mov edi, C_TIED_PANEL
        cmp eax, ebx
        je .p2_col_done
        jg .p2_winning
        mov edi, C_LOSE_PANEL
        jmp .p2_col_done
.p2_winning:
        mov edi, C_WIN_PANEL
.p2_col_done:
        cmp dword [surf2_id], -1
        je .skip_p2
        mov [panel_color], edi
        ; Get pixel buffer
        mov eax, SYS_FRAMEBUF
        mov ebx, 5
        mov ecx, [surf2_id]
        int 0x80
        cmp eax, -1
        je .skip_p2
        ; Fill
        mov edi, eax
        mov eax, [panel_color]
        mov ecx, PANEL_W * PANEL_H
        rep stosd
        ; Commit
        mov eax, SYS_SURFACE_COMMIT
        mov ebx, [surf2_id]
        xor ecx, ecx
        xor edx, edx
        mov esi, PANEL_W
        mov edi, PANEL_H
        int 0x80
.skip_p2:
        popad
        ret

; ====================================================================
; show_winner — overlay winner message
; ====================================================================
show_winner:
        pushad
        ; Dim background — draw semi-transparent overlay (just dark rect over centre)
        mov ebx, 160
        mov ecx, 180
        mov edx, 320
        mov esi, 100
        mov edi, 0x000066
        call vbe_fill_rect

        ; Who won?
        mov eax, [score1]
        cmp eax, WIN_SCORE
        jne .p2_wins
        mov esi, str_p1wins
        jmp .show_msg
.p2_wins:
        mov esi, str_p2wins
.show_msg:
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 185
        mov edx, 200
        mov edi, C_WINNER
        int 0x80
        ; "Any key for new game"
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 170
        mov edx, 230
        mov esi, str_newgame
        mov edi, 0xAAAAAA
        int 0x80
        call vbe_present
        call update_panels
        popad
        ret

; ====================================================================
; wait_key_or_quit — wait for a keypress; quit on Q/ESC
; ====================================================================
wait_key_or_quit:
.wkq:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .wkq
        cmp eax, 'q'
        je .wkq_quit
        cmp eax, 27
        jne .wkq_ret
.wkq_quit:
        ; Jump into the quit handler in game_loop
        jmp game_loop.quit
.wkq_ret:
        ret

; ====================================================================
; rand — LCG, returns EAX = pseudo-random
; ====================================================================
rand:
        imul eax, [rand_seed], 1664525
        add  eax, 1013904223
        mov  [rand_seed], eax
        ret

; ====================================================================
; itoa — EAX → num_buf (null-terminated decimal)
; ====================================================================
itoa:
        pushad
        mov edi, num_buf + 11
        mov byte [edi], 0
        dec edi
        test eax, eax
        jnz .ita_digits
        mov byte [edi], '0'
        dec edi
        jmp .ita_copy
.ita_digits:
        mov ecx, 10
.ita_loop:
        test eax, eax
        jz .ita_copy
        xor edx, edx
        div ecx
        add dl, '0'
        mov [edi], dl
        dec edi
        jmp .ita_loop
.ita_copy:
        inc edi
        mov esi, edi
        mov edi, num_buf
.ita_mv:
        mov al, [esi]
        mov [edi], al
        inc esi
        inc edi
        test al, al
        jnz .ita_mv
        popad
        ret

; ---- Data ----------------------------------------------------------
msg_novbe:      db "pong2p: VBE not available", 10, 0
str_serve:      db "Left-click to serve", 0
str_p1wins:     db "Player 1 Wins!", 0
str_p2wins:     db "Player 2 Wins!", 0
str_newgame:    db "Any key for new game  Q/ESC to quit", 0

surf1_id:       dd -1
surf2_id:       dd -1
score1:         dd 0
score2:         dd 0
pad1_y:         dd 0
pad2_y:         dd 0
ball_x:         dd 0
ball_y:         dd 0
ball_dx:        dd 0
ball_dy:        dd 0
rally_count:    dd 0
waiting_serve:  db 1
prev_btn:       db 0
mouse_btn_cur:  dd 0
rand_seed:      dd 54321
panel_color:    dd 0

section .bss
num_buf:        resb 16
