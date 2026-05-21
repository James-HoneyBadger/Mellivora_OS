; reaction.asm — Reaction Speed Test for Mellivora OS v10
;
; Showcases:
;   SYS_MOUSE           (36)  — click coloured diamond target
;   SYS_ALARM           (140) — per-round countdown (SIGALRM as backup)
;   SYS_GETTIME         (15)  — measure exact reaction ticks
;   SYS_DRAW_TRIANGLE   (96)  — draw diamond targets (2 triangles)
;   SYS_BEEP            (24)  — hit / miss / countdown audio
;   lib/highscore.inc         — persistent best score
;
; Rules:
;   10 rounds.  A coloured diamond appears at a random position.
;   Click it before the countdown expires.
;   Score per hit = (remaining_ticks) / 8.  Faster = more points.
;   Timeout starts at 3 s, decreases by 0.3 s per round (floor 1 s).
;
; Controls: left-click to hit target  |  Q / ESC to quit

%include "syscalls.inc"
%include "lib/highscore.inc"

; ---- Screen --------------------------------------------------------
SCR_W           equ 640
SCR_H           equ 480

; ---- Game constants ------------------------------------------------
NUM_ROUNDS      equ 10
TICK_RATE       equ 100         ; 100 Hz PIT
INIT_TIMEOUT    equ 3           ; seconds at round 1
MIN_TIMEOUT     equ 1           ; minimum timeout (seconds)
TIMEOUT_STEP    equ 30          ; decrease by 30 ticks (0.3 s) per round
TARGET_R        equ 36          ; diamond half-width/height in pixels
MIN_TX          equ 80          ; keep target away from edges
MAX_TX          equ SCR_W - 80
MIN_TY          equ 80
MAX_TY          equ SCR_H - 80

; ---- Palette -------------------------------------------------------
C_BG            equ 0x0A0A18
C_HUD           equ 0xFFFFFF
C_HIT           equ 0x00FF66
C_MISS          equ 0xFF2222
C_TIMER_OK      equ 0x44AAFF
C_TIMER_WARN    equ 0xFF6600
C_SCORE_CLR     equ 0xFFFF44
C_BEST_CLR      equ 0x00CC88

; Target colours (5 choices)
TGT_COLOR0      equ 0xFF3333   ; red
TGT_COLOR1      equ 0xFF9900   ; orange
TGT_COLOR2      equ 0xFFFF00   ; yellow
TGT_COLOR3      equ 0x00FF55   ; green
TGT_COLOR4      equ 0x44AAFF   ; blue

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
        ; Get framebuffer address (shadow buf) — store in [fb_addr]
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr], eax
        mov dword [fb_pitch], SCR_W * 4

        ; Seed RNG with current time
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

        ; Load high score
        push esi
        mov esi, hs_name
        call hs_load
        mov [hi_score], eax
        pop esi

        ; Show title screen; wait for click/key
        call draw_title
.title_loop:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .start_game
        mov eax, SYS_MOUSE
        int 0x80
        test ecx, 1
        jz .title_loop
        ; Click detected
.start_game:
        cmp eax, 'q'
        je quit_early
        cmp eax, 27
        je quit_early

; ====================================================================
new_game:
        mov dword [score], 0
        mov dword [round_num], 0
        ; Timeout starts at INIT_TIMEOUT*TICK_RATE ticks
        mov dword [round_ticks], INIT_TIMEOUT * TICK_RATE

; ====================================================================
round_loop:
        mov eax, [round_num]
        cmp eax, NUM_ROUNDS
        jge game_over

        ; Pick random target position
        call rand_target_pos

        ; Pick random target colour (index 0..4)
        call rand
        xor edx, edx
        mov ebx, 5
        div ebx
        mov [tgt_color_idx], edx

        ; Draw initial round state
        call draw_round_screen

        ; Record start time
        mov eax, SYS_GETTIME
        int 0x80
        mov [round_start], eax

        ; Set alarm as external signal (demonstration)
        mov eax, [round_ticks]
        xor edx, edx
        mov ebx, TICK_RATE
        div ebx                 ; EAX = whole seconds
        test eax, eax
        jz .al_skip
        mov ebx, eax
        mov eax, SYS_ALARM
        int 0x80
.al_skip:
        mov byte [prev_btn2], 0

; ---- Poll loop -----------------------------------------------------
.poll:
        ; Mouse state
        mov eax, SYS_MOUSE
        int 0x80
        mov [mX], eax
        mov [mY], ebx

        ; Rising-edge left-click detection
        test ecx, 1
        jz .no_click
        cmp byte [prev_btn2], 0
        jne .no_click
        ; Click happened — did it land on the target?
        call check_hit
        test eax, eax
        jnz .round_hit
        ; Missed (clicked elsewhere) — no penalty, keep going
.no_click:
        ; Update prev button
        mov eax, SYS_MOUSE
        int 0x80
        test ecx, 1
        jz .btn_clear
        mov byte [prev_btn2], 1
        jmp .btn_done
.btn_clear:
        mov byte [prev_btn2], 0
.btn_done:

        ; Check timeout via elapsed ticks
        mov eax, SYS_GETTIME
        int 0x80
        sub eax, [round_start]  ; elapsed ticks
        cmp eax, [round_ticks]
        jl .no_timeout
        jmp .round_miss
.no_timeout:

        ; Update countdown display (no-side-effect: compute from gettime)
        call draw_countdown

        ; Key check (quit)
        mov eax, SYS_READ_KEY
        int 0x80
        cmp eax, 'q'
        je .round_quit
        cmp eax, 27
        je .round_quit

        mov eax, SYS_YIELD
        int 0x80
        jmp .poll

.round_quit:
        ; Cancel alarm before quitting
        mov eax, SYS_ALARM
        xor ebx, ebx
        int 0x80
        jmp quit_early

; ---- HIT -----------------------------------------------------------
.round_hit:
        ; Cancel alarm
        mov eax, SYS_ALARM
        xor ebx, ebx
        int 0x80
        ; Compute reaction time
        mov eax, SYS_GETTIME
        int 0x80
        sub eax, [round_start]      ; elapsed ticks
        mov ecx, [round_ticks]
        sub ecx, eax                ; remaining ticks
        jl .no_pts
        sar ecx, 3                  ; divide by 8
        add [score], ecx
.no_pts:
        ; Hit SFX
        mov eax, SYS_BEEP
        mov ebx, 880
        mov ecx, 6
        int 0x80
        ; Feedback screen
        call draw_hit_msg
        mov eax, SYS_SLEEP
        mov ebx, 40
        int 0x80
        jmp .advance_round

; ---- MISS (timeout) ------------------------------------------------
.round_miss:
        ; Cancel alarm
        mov eax, SYS_ALARM
        xor ebx, ebx
        int 0x80
        ; Miss SFX (low buzz)
        mov eax, SYS_BEEP
        mov ebx, 200
        mov ecx, 20
        int 0x80
        call draw_miss_msg
        mov eax, SYS_SLEEP
        mov ebx, 50
        int 0x80

.advance_round:
        inc dword [round_num]
        ; Decrease timeout (floor at MIN_TIMEOUT * TICK_RATE)
        mov eax, [round_ticks]
        cmp eax, MIN_TIMEOUT * TICK_RATE + TIMEOUT_STEP
        jle .no_timeout_dec
        sub eax, TIMEOUT_STEP
        mov [round_ticks], eax
.no_timeout_dec:
        jmp round_loop

; ====================================================================
game_over:
        ; Update high score
        push esi
        mov esi, hs_name
        mov ebx, [score]
        call hs_update
        mov [hi_score], eax
        pop esi
        call draw_game_over

.go_wait:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .go_wait
        cmp eax, 'q'
        je quit_early
        cmp eax, 27
        je quit_early
        ; Any other key → replay
        jmp new_game

quit_early:
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        xor ecx, ecx
        xor edx, edx
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ====================================================================
; rand_target_pos — randomise [tgt_x] and [tgt_y]
; ====================================================================
rand_target_pos:
        pushad
        call rand
        xor edx, edx
        mov ebx, MAX_TX - MIN_TX
        div ebx
        add edx, MIN_TX
        mov [tgt_x], edx

        call rand
        xor edx, edx
        mov ebx, MAX_TY - MIN_TY
        div ebx
        add edx, MIN_TY
        mov [tgt_y], edx
        popad
        ret

; ====================================================================
; check_hit — is the current mouse position inside the diamond?
; Returns EAX = 1 if hit, 0 otherwise.
; Diamond hit test: |mX-cx| + |mY-cy| <= TARGET_R  (Manhattan distance)
; ====================================================================
check_hit:
        mov eax, [mX]
        sub eax, [tgt_x]
        ; abs(eax)
        test eax, eax
        jns .ax_pos
        neg eax
.ax_pos:
        mov ecx, eax        ; |dx|
        mov eax, [mY]
        sub eax, [tgt_y]
        test eax, eax
        jns .ay_pos
        neg eax
.ay_pos:
        add ecx, eax        ; |dx| + |dy|
        cmp ecx, TARGET_R
        jle .is_hit
        xor eax, eax
        ret
.is_hit:
        mov eax, 1
        ret

; ====================================================================
; draw_target — draw diamond at [tgt_x], [tgt_y] using 2 triangles
; Triangle packing: EBX=x0|(y0<<16) etc (16-bit coords)
; SYS_DRAW_TRIANGLE: EBX=v0 ECX=v1 EDX=v2 ESI=color
; ====================================================================
draw_target:
        pushad
        ; Choose colour from table
        mov eax, [tgt_color_idx]
        imul eax, 4
        mov edi, [tgt_colors + eax]

        mov ecx, [tgt_x]
        mov edx, [tgt_y]
        ; Diamond vertices:
        ;   top    = (cx, cy - R)
        ;   left   = (cx - R, cy)
        ;   right  = (cx + R, cy)
        ;   bottom = (cx, cy + R)
        ; Upper triangle: top, left, right
        ; Lower triangle: bottom, left, right

        ; Pack top: cx | ((cy - R) << 16)
        mov eax, ecx
        and eax, 0xFFFF
        mov ebx, edx
        sub ebx, TARGET_R
        and ebx, 0xFFFF
        shl ebx, 16
        or  eax, ebx
        mov [.v_top], eax

        ; Pack left: (cx-R) | (cy << 16)
        mov eax, ecx
        sub eax, TARGET_R
        and eax, 0xFFFF
        mov ebx, edx
        and ebx, 0xFFFF
        shl ebx, 16
        or  eax, ebx
        mov [.v_left], eax

        ; Pack right: (cx+R) | (cy << 16)
        mov eax, ecx
        add eax, TARGET_R
        and eax, 0xFFFF
        mov ebx, edx
        and ebx, 0xFFFF
        shl ebx, 16
        or  eax, ebx
        mov [.v_right], eax

        ; Pack bottom: cx | ((cy+R) << 16)
        mov eax, ecx
        and eax, 0xFFFF
        mov ebx, edx
        add ebx, TARGET_R
        and ebx, 0xFFFF
        shl ebx, 16
        or  eax, ebx
        mov [.v_bot], eax

        ; Upper triangle
        mov eax, SYS_DRAW_TRIANGLE
        mov ebx, [.v_top]
        mov ecx, [.v_left]
        mov edx, [.v_right]
        mov esi, edi
        int 0x80

        ; Lower triangle
        mov eax, SYS_DRAW_TRIANGLE
        mov ebx, [.v_bot]
        mov ecx, [.v_left]
        mov edx, [.v_right]
        mov esi, edi
        int 0x80
        popad
        ret
.v_top:  dd 0
.v_left: dd 0
.v_right: dd 0
.v_bot:  dd 0

; ====================================================================
; draw_round_screen — clear + HUD + target
; ====================================================================
draw_round_screen:
        pushad
        call clear_screen
        call draw_hud
        call draw_target
        call present_frame
        popad
        ret

; ====================================================================
; draw_countdown — update the timer bar in the top area
; ====================================================================
draw_countdown:
        pushad
        ; Get elapsed ticks
        mov eax, SYS_GETTIME
        int 0x80
        sub eax, [round_start]
        ; remaining = round_ticks - elapsed
        mov ecx, [round_ticks]
        sub ecx, eax
        jl .dc_zero
        jmp .dc_ok
.dc_zero:
        xor ecx, ecx
.dc_ok:
        ; Convert to seconds for display
        mov eax, ecx
        xor edx, edx
        mov ebx, TICK_RATE
        div ebx             ; EAX = whole seconds remaining
        call itoa
        ; Pick timer colour (warm when <= 1 s)
        mov edi, C_TIMER_OK
        cmp eax, 1
        jg .dc_col_ok
        mov edi, C_TIMER_WARN
.dc_col_ok:
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 300
        mov edx, 440
        mov esi, num_buf
        int 0x80
        call present_frame
        popad
        ret

; ====================================================================
; draw_hud — round number and score
; ====================================================================
draw_hud:
        pushad
        ; "Round N / 10"
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 8
        mov edx, 8
        mov esi, str_round
        mov edi, C_HUD
        int 0x80
        mov eax, [round_num]
        inc eax
        call itoa
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 80
        mov edx, 8
        mov esi, num_buf
        mov edi, C_HUD
        int 0x80
        ; "Score: N"
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 200
        mov edx, 8
        mov esi, str_score
        mov edi, C_SCORE_CLR
        int 0x80
        mov eax, [score]
        call itoa
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 280
        mov edx, 8
        mov esi, num_buf
        mov edi, C_SCORE_CLR
        int 0x80
        ; "Best: N"
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 440
        mov edx, 8
        mov esi, str_best
        mov edi, C_BEST_CLR
        int 0x80
        mov eax, [hi_score]
        call itoa
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 510
        mov edx, 8
        mov esi, num_buf
        mov edi, C_BEST_CLR
        int 0x80
        popad
        ret

; ====================================================================
; draw_title — splash screen
; ====================================================================
draw_title:
        pushad
        call clear_screen
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 190
        mov edx, 180
        mov esi, str_title
        mov edi, 0xFFFF44
        int 0x80
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 120
        mov edx, 230
        mov esi, str_instr
        mov edi, C_HUD
        int 0x80
        call present_frame
        popad
        ret

; ====================================================================
; draw_hit_msg — flash HIT message
; ====================================================================
draw_hit_msg:
        pushad
        call clear_screen
        call draw_hud
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 290
        mov edx, 220
        mov esi, str_hit
        mov edi, C_HIT
        int 0x80
        call present_frame
        popad
        ret

; ====================================================================
; draw_miss_msg — flash MISS message
; ====================================================================
draw_miss_msg:
        pushad
        call clear_screen
        call draw_hud
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 280
        mov edx, 220
        mov esi, str_miss
        mov edi, C_MISS
        int 0x80
        call present_frame
        popad
        ret

; ====================================================================
; draw_game_over — end-of-game summary
; ====================================================================
draw_game_over:
        pushad
        call clear_screen
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 240
        mov edx, 160
        mov esi, str_gameover
        mov edi, 0xFF4444
        int 0x80
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 200
        mov edx, 210
        mov esi, str_final
        mov edi, C_HUD
        int 0x80
        mov eax, [score]
        call itoa
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 380
        mov edx, 210
        mov esi, num_buf
        mov edi, C_SCORE_CLR
        int 0x80
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 200
        mov edx, 250
        mov esi, str_best_sc
        mov edi, C_HUD
        int 0x80
        mov eax, [hi_score]
        call itoa
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 380
        mov edx, 250
        mov esi, num_buf
        mov edi, C_BEST_CLR
        int 0x80
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 130
        mov edx, 310
        mov esi, str_replay
        mov edi, 0xAAAAAA
        int 0x80
        call present_frame
        popad
        ret

; ====================================================================
; Utilities
; ====================================================================
clear_screen:
        pushad
        mov edi, [fb_addr]
        mov eax, C_BG
        mov ecx, SCR_W * SCR_H
        rep stosd
        popad
        ret

present_frame:
        push eax
        push ebx
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80
        pop ebx
        pop eax
        ret

rand:
        imul eax, [rand_seed], 1664525
        add  eax, 1013904223
        mov  [rand_seed], eax
        ret

; itoa — EAX → num_buf (null-terminated decimal)
itoa:
        pushad
        mov edi, num_buf + 11
        mov byte [edi], 0
        dec edi
        test eax, eax
        jnz .ita_d
        mov byte [edi], '0'
        dec edi
        jmp .ita_c
.ita_d:
        mov ecx, 10
.ita_l:
        test eax, eax
        jz .ita_c
        xor edx, edx
        div ecx
        add dl, '0'
        mov [edi], dl
        dec edi
        jmp .ita_l
.ita_c:
        inc edi
        mov esi, edi
        mov edi, num_buf
.ita_m:
        mov al, [esi]
        mov [edi], al
        inc esi
        inc edi
        test al, al
        jnz .ita_m
        popad
        ret

; ---- Data ----------------------------------------------------------
hs_name:        db "reaction", 0
msg_novbe:      db "reaction: VBE not available", 10, 0
str_title:      db "REACTION TEST", 0
str_instr:      db "Click the diamond before it disappears!", 0
str_round:      db "Round:", 0
str_score:      db "Score:", 0
str_best:       db "Best:", 0
str_hit:        db "HIT!", 0
str_miss:       db "MISS!", 0
str_gameover:   db "GAME OVER", 0
str_final:      db "Final Score:", 0
str_best_sc:    db "Best Score:", 0
str_replay:     db "Any key to replay  |  Q / ESC to quit", 0

tgt_colors:     dd TGT_COLOR0, TGT_COLOR1, TGT_COLOR2, TGT_COLOR3, TGT_COLOR4

score:          dd 0
round_num:      dd 0
round_start:    dd 0
round_ticks:    dd INIT_TIMEOUT * TICK_RATE
tgt_x:          dd 0
tgt_y:          dd 0
tgt_color_idx:  dd 0
mX:             dd 0
mY:             dd 0
prev_btn2:      db 0
hi_score:       dd 0
rand_seed:      dd 99999
fb_addr:        dd 0
fb_pitch:       dd 0

section .bss
num_buf:        resb 16
