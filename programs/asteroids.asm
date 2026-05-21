; asteroids.asm — Classic Asteroids for Mellivora OS v10
;
; Showcases:
;   SYS_DRAW_LINE  (95) — asteroid polygon outlines
;   SYS_DRAW_TRIANGLE (96) — vector ship hull
;   SYS_ALARM     (140) — FEVER mode countdown (clear fast for bonus)
;   lib/audio.inc       — SFX via PC speaker
;   lib/highscore.inc   — persistent score
;
; Controls:
;   LEFT / RIGHT  — rotate ship (8 directions)
;   UP            — thrust
;   SPACE         — fire
;   Q / ESC       — quit

%include "syscalls.inc"
%include "lib/audio.inc"
%include "lib/highscore.inc"

; ---- Screen ---------------------------------------------------------
SCR_W           equ 640
SCR_H           equ 480
PITCH           equ SCR_W * 4

; ---- Palette --------------------------------------------------------
C_BG            equ 0x000000
C_SHIP          equ 0x44FFAA
C_THRUST        equ 0xFF8800
C_BULLET        equ 0xFFFF44
C_AST           equ 0xAAAAAA
C_HUD           equ 0xFFFFFF
C_FEVER         equ 0xFF6600
C_INVULN        equ 0x224488
C_GAMEOVER      equ 0xFF2222

; ---- Game constants -------------------------------------------------
MAX_BULLETS     equ 6
MAX_ASTEROIDS   equ 20
BULLET_SPEED    equ 8
BULLET_LIFE     equ 55          ; ticks before bullet expires
SHIP_THRUST     equ 1
MAX_SPEED       equ 6
LIVES_START     equ 3
INVULN_TICKS    equ 90
FEVER_SECS      equ 12          ; FEVER alarm countdown
FEVER_BONUS     equ 1000        ; bonus points for clearing in fever

; ---- Data strides ---------------------------------------------------
; Asteroid: x(4) y(4) vx(4) vy(4) size(4) shape(4) = 24 bytes
AST_S           equ 24
; Bullet:   x(4) y(4) vx(4) vy(4) life(4) = 20 bytes
BLT_S           equ 20

; ====================================================================
; Ship vertex table — 8 angles × [nose_x, nose_y, lw_x, lw_y, rw_x, rw_y]
; Angle 0 = facing UP, increasing clockwise (45° per step).
; Computed with screen-space rotation (Y-axis down):
;   nose = 12 * (sin a, -cos a)
;   left  = -7*(sin a,-cos a) + -7*(cos a, sin a)
;   right = -7*(sin a,-cos a) +  7*(cos a, sin a)
; ====================================================================
ship_vtx:
        db   0,-12,  -7, 7,   7, 7   ; angle 0 (N)
        db   8, -8, -10, 0,   0,10   ; angle 1 (NE)
        db  12,  0,  -7,-7,  -7, 7   ; angle 2 (E)
        db   8,  8,   0,-10,-10, 0   ; angle 3 (SE)
        db   0, 12,   7,-7,  -7,-7   ; angle 4 (S)
        db  -8,  8,  10, 0,   0,-10  ; angle 5 (SW)
        db -12,  0,   7, 7,   7,-7   ; angle 6 (W)
        db  -8, -8,   0,10,  10, 0   ; angle 7 (NW)

; Thruster flame offset — tip opposite nose (engine exhaust)
; flame_base = -nose; flame_tip = flame_base + 6 in same direction
flame_base:
        db   0,12,  -8, 8,  -12,0,  -8,-8,  0,-12,  8,-8,  12,0,  8,8
flame_tip:
        db   0,20,  -14,14, -18,0, -14,-14,  0,-20, 14,-14, 18,0, 14,14

; Thrust direction: (dx, dy) per angle
thrust_dx:      db  0, 1, 1, 1, 0,-1,-1,-1
thrust_dy:      db -1,-1, 0, 1, 1, 1, 0,-1

; ---- Asteroid shape variants (6 verts, units = size*1 pixel) --------
; Stored as [dx, dy] pairs; scale by asteroid size before use.
ast_shapes:
        db  8, 0,  4, 7, -4, 7, -8, 0, -4,-7,  4,-7  ; variant 0 (round)
        db  7, 3,  2, 8, -6, 5, -7,-3, -3,-8,  5,-5  ; variant 1 (angular)
        db  9, 1,  3, 8, -5, 6, -9,-2, -4,-8,  4,-6  ; variant 2 (lumpy)

; ====================================================================
start:
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

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr], eax
        mov dword [fb_pitch], PITCH

        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

        push esi
        mov esi, hs_name
        call hs_load
        mov [hi_score], eax
        pop esi

new_game:
        mov dword [ship_x],      SCR_W / 2
        mov dword [ship_y],      SCR_H / 2
        mov dword [ship_vx],     0
        mov dword [ship_vy],     0
        mov dword [ship_angle],  0
        mov dword [ship_invuln], INVULN_TICKS
        mov dword [lives],       LIVES_START
        mov dword [score],       0
        mov dword [wave],        1
        mov byte  [game_over],   0
        mov byte  [thrusting],   0
        call clear_bullets
        call clear_asteroids
        call spawn_wave

        mov eax, SYS_ALARM
        mov ebx, FEVER_SECS
        int 0x80

; ====================================================================
game_loop:
        mov eax, SYS_READ_KEY
        int 0x80

        cmp eax, 'q'
        je .quit
        cmp eax, 'Q'
        je .quit
        cmp eax, 27
        je .quit

        ; Rotate left
        cmp eax, 0x82
        jne .no_rl
        dec dword [ship_angle]
        and dword [ship_angle], 7
.no_rl:
        cmp eax, 0x83
        jne .no_rr
        inc dword [ship_angle]
        and dword [ship_angle], 7
.no_rr:

        mov byte [thrusting], 0
        cmp eax, 0x80           ; up arrow = thrust
        jne .no_thrust
        mov byte [thrusting], 1
        call do_thrust
.no_thrust:

        cmp eax, ' '
        jne .no_fire
        call do_fire
.no_fire:

        ; --- Update ---
        call update_ship
        call update_bullets
        call update_asteroids

        cmp byte [game_over], 1
        jne .no_go
        call show_gameover
        call wait_gameover_key
        jmp new_game
.no_go:

        ; Check all asteroids dead → next wave
        call count_active_asts
        test eax, eax
        jnz .wave_ongoing

        ; FEVER bonus if alarm still active
        mov eax, SYS_ALARM
        xor ebx, ebx
        int 0x80
        test eax, eax
        jz .no_fever_bonus
        add dword [score], FEVER_BONUS
.no_fever_bonus:
        inc dword [wave]
        call spawn_wave
        mov eax, SYS_ALARM
        mov ebx, FEVER_SECS
        int 0x80
.wave_ongoing:

        ; --- Render ---
        call render_frame

        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        jmp game_loop

.quit:
        mov eax, SYS_ALARM
        xor ebx, ebx
        int 0x80
        push esi
        mov esi, hs_name
        mov ebx, [score]
        call hs_update
        pop esi
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        xor ecx, ecx
        xor edx, edx
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ====================================================================
; do_thrust — apply velocity increment in current angle direction
; ====================================================================
do_thrust:
        pushad
        mov ecx, [ship_angle]
        movsx eax, byte [thrust_dx + ecx]
        movsx ebx, byte [thrust_dy + ecx]
        add [ship_vx], eax
        add [ship_vy], ebx
        ; Clamp velocity
        mov eax, [ship_vx]
        cmp eax, MAX_SPEED
        jle .vx_hi_ok
        mov dword [ship_vx], MAX_SPEED
        jmp .vx_done
.vx_hi_ok:
        cmp eax, -MAX_SPEED
        jge .vx_done
        mov dword [ship_vx], -MAX_SPEED
.vx_done:
        mov eax, [ship_vy]
        cmp eax, MAX_SPEED
        jle .vy_hi_ok
        mov dword [ship_vy], MAX_SPEED
        jmp .vy_done
.vy_hi_ok:
        cmp eax, -MAX_SPEED
        jge .vy_done
        mov dword [ship_vy], -MAX_SPEED
.vy_done:
        ; SFX: short high-pitched blip
        mov eax, SYS_BEEP
        mov ebx, 300
        mov ecx, 1
        int 0x80
        popad
        ret

; ====================================================================
; do_fire — spawn bullet at ship nose in current angle direction
; ====================================================================
do_fire:
        pushad
        ; Find free bullet slot
        mov edi, bullets
        xor ecx, ecx
.find_slot:
        cmp ecx, MAX_BULLETS
        jge .no_slot
        cmp dword [edi + 16], 0  ; life == 0?
        je .got_slot
        add edi, BLT_S
        inc ecx
        jmp .find_slot
.got_slot:
        ; Nose offset from ship_vtx
        mov ecx, [ship_angle]
        imul ecx, 6
        movsx eax, byte [ship_vtx + ecx]      ; nose_x offset
        movsx ebx, byte [ship_vtx + ecx + 1]  ; nose_y offset
        add eax, [ship_x]
        add ebx, [ship_y]
        mov [edi + 0], eax      ; bullet_x
        mov [edi + 4], ebx      ; bullet_y
        ; Velocity: thrust direction × BULLET_SPEED
        mov ecx, [ship_angle]
        movsx eax, byte [thrust_dx + ecx]
        imul eax, BULLET_SPEED
        add eax, [ship_vx]      ; inherit ship velocity
        mov [edi + 8], eax
        movsx eax, byte [thrust_dy + ecx]
        imul eax, BULLET_SPEED
        add eax, [ship_vy]
        mov [edi + 12], eax
        mov dword [edi + 16], BULLET_LIFE
        ; Fire SFX
        mov eax, SYS_BEEP
        mov ebx, 1200
        mov ecx, 2
        int 0x80
.no_slot:
        popad
        ret

; ====================================================================
; update_ship — move, wrap, collide, decrement invuln
; ====================================================================
update_ship:
        pushad
        ; Move
        mov eax, [ship_x]
        add eax, [ship_vx]
        ; Wrap X
        cmp eax, 0
        jge .sx_ok_h
        add eax, SCR_W
        jmp .sx_done
.sx_ok_h:
        cmp eax, SCR_W
        jl .sx_done
        sub eax, SCR_W
.sx_done:
        mov [ship_x], eax

        mov eax, [ship_y]
        add eax, [ship_vy]
        cmp eax, 0
        jge .sy_ok_h
        add eax, SCR_H
        jmp .sy_done
.sy_ok_h:
        cmp eax, SCR_H
        jl .sy_done
        sub eax, SCR_H
.sy_done:
        mov [ship_y], eax

        ; Decrement invulnerability
        cmp dword [ship_invuln], 0
        je .no_invuln_dec
        dec dword [ship_invuln]
.no_invuln_dec:

        ; Check ship-asteroid collision (if not invulnerable)
        cmp dword [ship_invuln], 0
        jne .skip_collide
        call check_ship_hit
.skip_collide:
        popad
        ret

; ====================================================================
; check_ship_hit — kill ship if overlapping an asteroid
; ====================================================================
check_ship_hit:
        pushad
        mov esi, asteroids
        xor ecx, ecx
.csh_loop:
        cmp ecx, MAX_ASTEROIDS
        jge .csh_done
        cmp dword [esi + 16], 0   ; size == 0 → dead
        je .csh_next
        ; dist² = (ship_x - ast_x)² + (ship_y - ast_y)²
        mov eax, [ship_x]
        sub eax, [esi + 0]
        imul eax, eax
        mov ebx, [ship_y]
        sub ebx, [esi + 4]
        imul ebx, ebx
        add eax, ebx
        ; radius_sum = 8 + size*8 = (size+1)*8; threshold = ((size+1)*8)²
        mov ebx, [esi + 16]
        inc ebx
        imul ebx, 8
        imul ebx, ebx
        cmp eax, ebx
        jg .csh_next
        ; HIT: lose life
        dec dword [lives]
        ; Death SFX
        mov eax, SYS_BEEP
        mov ebx, 150
        mov ecx, 30
        int 0x80
        cmp dword [lives], 0
        jg .csh_respawn
        mov byte [game_over], 1
        jmp .csh_done
.csh_respawn:
        ; Respawn at centre with invulnerability
        mov dword [ship_x],      SCR_W / 2
        mov dword [ship_y],      SCR_H / 2
        mov dword [ship_vx],     0
        mov dword [ship_vy],     0
        mov dword [ship_invuln], INVULN_TICKS
        jmp .csh_done
.csh_next:
        add esi, AST_S
        inc ecx
        jmp .csh_loop
.csh_done:
        popad
        ret

; ====================================================================
; update_bullets — advance, expire, and check vs asteroids
; ====================================================================
update_bullets:
        pushad
        mov esi, bullets
        xor ecx, ecx
.ubl_loop:
        cmp ecx, MAX_BULLETS
        jge .ubl_done
        cmp dword [esi + 16], 0
        je .ubl_next
        ; Advance
        mov eax, [esi + 8]
        add [esi + 0], eax
        mov eax, [esi + 12]
        add [esi + 4], eax
        ; Wrap X
        mov eax, [esi + 0]
        cmp eax, 0
        jge .bx_ok_h
        add eax, SCR_W
        mov [esi + 0], eax
        jmp .bx_done
.bx_ok_h:
        cmp eax, SCR_W
        jl .bx_done
        sub eax, SCR_W
        mov [esi + 0], eax
.bx_done:
        ; Wrap Y
        mov eax, [esi + 4]
        cmp eax, 0
        jge .by_ok_h
        add eax, SCR_H
        mov [esi + 4], eax
        jmp .by_done
.by_ok_h:
        cmp eax, SCR_H
        jl .by_done
        sub eax, SCR_H
        mov [esi + 4], eax
.by_done:
        ; Decrement life
        dec dword [esi + 16]
        ; Check vs asteroids
        push ecx
        push esi
        call check_bullet_vs_asts
        pop esi
        pop ecx
.ubl_next:
        add esi, BLT_S
        inc ecx
        jmp .ubl_loop
.ubl_done:
        popad
        ret

; ====================================================================
; check_bullet_vs_asts — ESI = bullet slot; kills bullet + asteroid on hit
; ====================================================================
check_bullet_vs_asts:
        pushad
        mov edi, asteroids
        xor ecx, ecx
.cba_loop:
        cmp ecx, MAX_ASTEROIDS
        jge .cba_done
        cmp dword [edi + 16], 0
        je .cba_next
        mov eax, [esi + 0]
        sub eax, [edi + 0]
        imul eax, eax
        mov ebx, [esi + 4]
        sub ebx, [edi + 4]
        imul ebx, ebx
        add eax, ebx
        ; threshold = (size*8)²
        mov ebx, [edi + 16]
        imul ebx, 8
        imul ebx, ebx
        cmp eax, ebx
        jg .cba_next
        ; HIT asteroid
        mov dword [esi + 16], 0  ; kill bullet
        ; score: large(3)=25 med(2)=50 small(1)=100  =>  (4-size)*25
        mov eax, 4
        sub eax, [edi + 16]
        imul eax, 25
        add [score], eax
        ; Split or destroy
        mov eax, [edi + 16]
        cmp eax, 1
        je .destroy
        ; Split: spawn 2 smaller
        push edi
        call split_asteroid
        pop edi
.destroy:
        mov dword [edi + 16], 0  ; kill asteroid
        ; Explosion SFX: pitch inversely proportional to size
        mov eax, SYS_BEEP
        mov ebx, 800
        mov ecx, 8
        int 0x80
        jmp .cba_done
.cba_next:
        add edi, AST_S
        inc ecx
        jmp .cba_loop
.cba_done:
        popad
        ret

; ====================================================================
; split_asteroid — EDI = asteroid slot; spawn 2 children of size-1
; ====================================================================
split_asteroid:
        pushad
        mov eax, [edi + 16]
        dec eax                  ; child_size
        mov [.child_size], eax
        mov eax, [edi + 0]
        mov [.parent_x], eax
        mov eax, [edi + 4]
        mov [.parent_y], eax
        mov eax, [edi + 8]
        mov [.parent_vx], eax
        mov eax, [edi + 12]
        mov [.parent_vy], eax

        ; Spawn 2 children at parent position with divergent velocities
        xor ecx, ecx             ; 0 = first child, 1 = second
.split_loop:
        cmp ecx, 2
        jge .split_done
        ; Find free slot
        mov esi, asteroids
        xor ebx, ebx
.find_free:
        cmp ebx, MAX_ASTEROIDS
        jge .skip_child
        cmp dword [esi + 16], 0
        je .found_free
        add esi, AST_S
        inc ebx
        jmp .find_free
.found_free:
        mov eax, [.parent_x]
        mov [esi + 0], eax
        mov eax, [.parent_y]
        mov [esi + 4], eax
        ; Diverge velocity: first child goes +perp, second goes -perp
        mov eax, [.parent_vy]    ; perpendicular to parent velocity
        neg eax
        test ecx, ecx
        jnz .neg_perp
        add eax, [.parent_vx]
        mov [esi + 8], eax
        mov eax, [.parent_vx]
        add eax, [.parent_vy]
        mov [esi + 12], eax
        jmp .set_size
.neg_perp:
        sub eax, [.parent_vx]
        mov [esi + 8], eax
        mov eax, [.parent_vx]
        sub eax, [.parent_vy]
        mov [esi + 12], eax
.set_size:
        mov eax, [.child_size]
        mov [esi + 16], eax
        ; Random shape
        call rand
        xor edx, edx
        mov ebx, 3
        div ebx
        mov [esi + 20], edx
.skip_child:
        inc ecx
        jmp .split_loop
.split_done:
        popad
        ret
.child_size:    dd 0
.parent_x:      dd 0
.parent_y:      dd 0
.parent_vx:     dd 0
.parent_vy:     dd 0

; ====================================================================
; update_asteroids — advance all active asteroids, wrap edges
; ====================================================================
update_asteroids:
        pushad
        mov esi, asteroids
        xor ecx, ecx
.ua_loop:
        cmp ecx, MAX_ASTEROIDS
        jge .ua_done
        cmp dword [esi + 16], 0
        je .ua_next
        mov eax, [esi + 8]
        add [esi + 0], eax
        mov eax, [esi + 12]
        add [esi + 4], eax
        ; Wrap X
        mov eax, [esi + 0]
        cmp eax, -40
        jg .ax_ok_l
        add eax, SCR_W + 80
        mov [esi + 0], eax
        jmp .ax_done
.ax_ok_l:
        cmp eax, SCR_W + 40
        jl .ax_done
        sub eax, SCR_W + 80
        mov [esi + 0], eax
.ax_done:
        ; Wrap Y
        mov eax, [esi + 4]
        cmp eax, -40
        jg .ay_ok_l
        add eax, SCR_H + 80
        mov [esi + 4], eax
        jmp .ay_done
.ay_ok_l:
        cmp eax, SCR_H + 40
        jl .ay_done
        sub eax, SCR_H + 80
        mov [esi + 4], eax
.ay_done:
.ua_next:
        add esi, AST_S
        inc ecx
        jmp .ua_loop
.ua_done:
        popad
        ret

; ====================================================================
; spawn_wave — spawn (wave + 3) large asteroids far from ship
; ====================================================================
spawn_wave:
        pushad
        ; Count to spawn: 4 + wave (capped at 8)
        mov ecx, [wave]
        add ecx, 3
        cmp ecx, 8
        jle .sw_cnt_ok
        mov ecx, 8
.sw_cnt_ok:
.sw_loop:
        cmp ecx, 0
        jle .sw_done
        call spawn_one_large
        dec ecx
        jmp .sw_loop
.sw_done:
        popad
        ret

; ====================================================================
; spawn_one_large — place a large asteroid at a random screen edge
; ====================================================================
spawn_one_large:
        pushad
        ; Find free slot
        mov edi, asteroids
        xor ecx, ecx
.sol_find:
        cmp ecx, MAX_ASTEROIDS
        jge .sol_no_slot
        cmp dword [edi + 16], 0
        je .sol_found
        add edi, AST_S
        inc ecx
        jmp .sol_find
.sol_found:
        ; Random edge (0=top, 1=right, 2=bottom, 3=left)
        call rand
        and eax, 3
        cmp eax, 0
        je .edge_top
        cmp eax, 1
        je .edge_right
        cmp eax, 2
        je .edge_bottom
        ; edge_left
        call rand
        xor edx, edx
        mov ebx, SCR_H
        div ebx
        mov dword [edi + 0], 0
        mov [edi + 4], edx
        jmp .set_vel
.edge_top:
        call rand
        xor edx, edx
        mov ebx, SCR_W
        div ebx
        mov [edi + 0], edx
        mov dword [edi + 4], 0
        jmp .set_vel
.edge_right:
        call rand
        xor edx, edx
        mov ebx, SCR_H
        div ebx
        mov dword [edi + 0], SCR_W - 1
        mov [edi + 4], edx
        jmp .set_vel
.edge_bottom:
        call rand
        xor edx, edx
        mov ebx, SCR_W
        div ebx
        mov [edi + 0], edx
        mov dword [edi + 4], SCR_H - 1
.set_vel:
        ; Random velocity -2..2 (non-zero)
        call rand
        xor edx, edx
        mov ebx, 5
        div ebx
        sub edx, 2
        test edx, edx
        jnz .vx_nonzero
        inc edx
.vx_nonzero:
        mov [edi + 8], edx
        call rand
        xor edx, edx
        mov ebx, 5
        div ebx
        sub edx, 2
        test edx, edx
        jnz .vy_nonzero
        dec edx
.vy_nonzero:
        mov [edi + 12], edx
        mov dword [edi + 16], 3  ; large size
        call rand
        xor edx, edx
        mov ebx, 3
        div ebx
        mov [edi + 20], edx      ; shape
.sol_no_slot:
        popad
        ret

; ====================================================================
; count_active_asts — EAX = number of live asteroids
; ====================================================================
count_active_asts:
        push ecx
        push esi
        mov esi, asteroids
        xor eax, eax
        xor ecx, ecx
.caa_loop:
        cmp ecx, MAX_ASTEROIDS
        jge .caa_done
        cmp dword [esi + 16], 0
        je .caa_next
        inc eax
.caa_next:
        add esi, AST_S
        inc ecx
        jmp .caa_loop
.caa_done:
        pop esi
        pop ecx
        ret

; ====================================================================
; clear_bullets — zero all bullet slots
; ====================================================================
clear_bullets:
        pushad
        mov edi, bullets
        mov ecx, MAX_BULLETS * BLT_S / 4
        xor eax, eax
        rep stosd
        popad
        ret

; ====================================================================
; clear_asteroids — zero all asteroid slots
; ====================================================================
clear_asteroids:
        pushad
        mov edi, asteroids
        mov ecx, MAX_ASTEROIDS * AST_S / 4
        xor eax, eax
        rep stosd
        popad
        ret

; ====================================================================
; render_frame — clear + draw everything + present
; ====================================================================
render_frame:
        pushad
        ; Clear
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCR_W
        mov esi, SCR_H
        mov edi, C_BG
        call fb_fill_rect

        ; Draw asteroids
        mov esi, asteroids
        xor ecx, ecx
.rf_ast:
        cmp ecx, MAX_ASTEROIDS
        jge .rf_ast_done
        cmp dword [esi + 16], 0
        je .rf_ast_next
        push ecx
        push esi
        call draw_asteroid
        pop esi
        pop ecx
.rf_ast_next:
        add esi, AST_S
        inc ecx
        jmp .rf_ast
.rf_ast_done:

        ; Draw bullets
        mov esi, bullets
        xor ecx, ecx
.rf_blt:
        cmp ecx, MAX_BULLETS
        jge .rf_blt_done
        cmp dword [esi + 16], 0
        je .rf_blt_next
        push ecx
        push esi
        mov ebx, [esi + 0]
        mov ecx, [esi + 4]
        mov edx, 3
        mov esi, 3
        mov edi, C_BULLET
        call fb_fill_rect
        pop esi
        pop ecx
.rf_blt_next:
        add esi, BLT_S
        inc ecx
        jmp .rf_blt
.rf_blt_done:

        ; Draw ship (if not invulnerable, or blink on invuln)
        cmp dword [ship_invuln], 0
        je .draw_ship_now
        mov eax, [ship_invuln]
        and eax, 4              ; blink every 4 ticks
        jnz .skip_ship
.draw_ship_now:
        call draw_ship
.skip_ship:

        ; HUD
        call draw_hud

        ; Present
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80
        popad
        ret

; ====================================================================
; draw_asteroid — ESI = asteroid slot
; ====================================================================
draw_asteroid:
        pushad
        mov eax, [esi + 0]       ; ax = ast_x
        mov ebx, [esi + 4]       ; bx = ast_y
        mov ecx, [esi + 16]      ; size
        mov edx, [esi + 20]      ; shape variant
        ; shape_base = ast_shapes + variant * 12 (6 verts × 2 bytes)
        imul edx, 12
        add edx, ast_shapes      ; edx = shape base ptr
        mov [.ax], eax
        mov [.ay], ebx
        mov [.sz], ecx
        mov [.sp], edx

        xor ecx, ecx             ; edge index 0..5
.da_edge:
        cmp ecx, 6
        jge .da_done
        mov edx, [.sp]
        ; Vertex 0 of this edge
        movsx eax, byte [edx + ecx*2]
        imul eax, [.sz]
        add eax, [.ax]
        mov [.x0], eax
        movsx eax, byte [edx + ecx*2 + 1]
        imul eax, [.sz]
        add eax, [.ay]
        mov [.y0], eax
        ; Vertex 1 (next, wrapping)
        mov eax, ecx
        inc eax
        cmp eax, 6
        jne .da_no_wrap
        xor eax, eax
.da_no_wrap:
        movsx ebx, byte [edx + eax*2]
        imul ebx, [.sz]
        add ebx, [.ax]
        mov [.x1], ebx
        movsx ebx, byte [edx + eax*2 + 1]
        imul ebx, [.sz]
        add ebx, [.ay]
        mov [.y1], ebx
        ; SYS_DRAW_LINE: EBX=x0 ECX=y0 EDX=x1 ESI=y1 EDI=color
        push ecx
        mov eax, SYS_DRAW_LINE
        mov ebx, [.x0]
        mov ecx, [.y0]
        mov edx, [.x1]
        mov esi, [.y1]
        mov edi, C_AST
        int 0x80
        pop ecx
        inc ecx
        jmp .da_edge
.da_done:
        popad
        ret
.ax: dd 0
.ay: dd 0
.sz: dd 0
.sp: dd 0
.x0: dd 0
.y0: dd 0
.x1: dd 0
.y1: dd 0

; ====================================================================
; draw_ship — pack vertices and call SYS_DRAW_TRIANGLE
; ====================================================================
draw_ship:
        pushad
        mov ecx, [ship_angle]
        imul ecx, 6              ; 6 bytes per angle entry

        ; Vertex 0 = nose
        movsx eax, byte [ship_vtx + ecx]
        movsx ebx, byte [ship_vtx + ecx + 1]
        add eax, [ship_x]
        add ebx, [ship_y]
        ; Pack v0: EBX_packed = x0 | (y0 << 16)
        and eax, 0xFFFF
        and ebx, 0xFFFF
        shl ebx, 16
        or  eax, ebx
        mov [.v0p], eax

        ; Vertex 1 = left wing
        movsx eax, byte [ship_vtx + ecx + 2]
        movsx ebx, byte [ship_vtx + ecx + 3]
        add eax, [ship_x]
        add ebx, [ship_y]
        and eax, 0xFFFF
        and ebx, 0xFFFF
        shl ebx, 16
        or  eax, ebx
        mov [.v1p], eax

        ; Vertex 2 = right wing
        movsx eax, byte [ship_vtx + ecx + 4]
        movsx ebx, byte [ship_vtx + ecx + 5]
        add eax, [ship_x]
        add ebx, [ship_y]
        and eax, 0xFFFF
        and ebx, 0xFFFF
        shl ebx, 16
        or  eax, ebx
        mov [.v2p], eax

        ; Draw hull triangle
        mov eax, SYS_DRAW_TRIANGLE
        mov ebx, [.v0p]
        mov ecx, [.v1p]
        mov edx, [.v2p]
        mov esi, C_SHIP
        int 0x80

        ; If thrusting, draw flame using a second line (exhaust)
        cmp byte [thrusting], 0
        je .no_flame
        mov ecx, [ship_angle]
        movsx ebx, byte [flame_base + ecx*2]
        movsx eax, byte [flame_base + ecx*2 + 1]
        add ebx, [ship_x]
        add eax, [ship_y]
        mov [.fx0], ebx
        mov [.fy0], eax
        movsx ebx, byte [flame_tip + ecx*2]
        movsx eax, byte [flame_tip + ecx*2 + 1]
        add ebx, [ship_x]
        add eax, [ship_y]
        mov [.fx1], ebx
        mov [.fy1], eax
        mov eax, SYS_DRAW_LINE
        mov ebx, [.fx0]
        mov ecx, [.fy0]
        mov edx, [.fx1]
        mov esi, [.fy1]
        mov edi, C_THRUST
        int 0x80
.no_flame:
        popad
        ret
.v0p: dd 0
.v1p: dd 0
.v2p: dd 0
.fx0: dd 0
.fy0: dd 0
.fx1: dd 0
.fy1: dd 0

; ====================================================================
; draw_hud
; ====================================================================
draw_hud:
        pushad
        ; Score label
        mov ebx, 8
        mov ecx, 8
        mov esi, str_score
        mov edi, C_HUD
        call fb_draw_text
        mov eax, [score]
        call itoa
        mov ebx, 70
        mov ecx, 8
        mov esi, num_buf
        mov edi, C_HUD
        call fb_draw_text
        ; Lives
        mov ebx, 200
        mov ecx, 8
        mov esi, str_lives
        mov edi, C_HUD
        call fb_draw_text
        mov eax, [lives]
        call itoa
        mov ebx, 250
        mov ecx, 8
        mov esi, num_buf
        mov edi, C_HUD
        call fb_draw_text
        ; Wave
        mov ebx, 300
        mov ecx, 8
        mov esi, str_wave
        mov edi, C_HUD
        call fb_draw_text
        mov eax, [wave]
        call itoa
        mov ebx, 350
        mov ecx, 8
        mov esi, num_buf
        mov edi, C_HUD
        call fb_draw_text
        ; Hi-score
        mov ebx, 460
        mov ecx, 8
        mov esi, str_hi
        mov edi, C_HUD
        call fb_draw_text
        mov eax, [hi_score]
        call itoa
        mov ebx, 510
        mov ecx, 8
        mov esi, num_buf
        mov edi, 0xFFCC00
        call fb_draw_text
        ; FEVER status: check alarm remaining
        mov eax, SYS_ALARM
        xor ebx, ebx
        int 0x80
        test eax, eax
        jz .no_fever_hud
        push eax
        ; Restore alarm
        mov ebx, eax
        mov eax, SYS_ALARM
        int 0x80
        pop eax
        ; Print "FEVER Ns"
        call itoa
        mov ebx, 260
        mov ecx, SCR_H - 24
        mov esi, str_fever
        mov edi, C_FEVER
        call fb_draw_text
        mov ebx, 350
        mov ecx, SCR_H - 24
        mov esi, num_buf
        mov edi, C_FEVER
        call fb_draw_text
.no_fever_hud:
        popad
        ret

; ====================================================================
; show_gameover + wait_gameover_key
; ====================================================================
show_gameover:
        pushad
        ; Update high score
        push esi
        mov esi, hs_name
        mov ebx, [score]
        call hs_update
        pop esi
        ; Dark overlay (just redraw clear)
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCR_W
        mov esi, SCR_H
        mov edi, 0x110000
        call fb_fill_rect
        mov ebx, 240
        mov ecx, 200
        mov esi, str_gameover
        mov edi, C_GAMEOVER
        call fb_draw_text
        mov ebx, 240
        mov ecx, 230
        mov esi, str_score
        mov edi, C_HUD
        call fb_draw_text
        mov eax, [score]
        call itoa
        mov ebx, 300
        mov ecx, 230
        mov esi, num_buf
        mov edi, 0xFFFF44
        call fb_draw_text
        mov ebx, 220
        mov ecx, 270
        mov esi, str_restart
        mov edi, 0xAAAAAA
        call fb_draw_text
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80
        popad
        ret

wait_gameover_key:
.wgk:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .wgk
        ret

; ====================================================================
; Utilities
; ====================================================================
rand:
        imul eax, [rand_seed], 1664525
        add  eax, 1013904223
        mov  [rand_seed], eax
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

; fb_fill_rect: EBX=x ECX=y EDX=w ESI=h EDI=color
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

; fb_draw_text: EBX=x ECX=y ESI=str EDI=color
fb_draw_text:
        pushad
        mov edx, ecx
        mov ecx, ebx
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        int 0x80
        popad
        ret

; itoa: EAX → num_buf
itoa:
        pushad
        mov edi, num_buf + 11
        mov byte [edi], 0
        dec edi
        test eax, eax
        jnz .id
        mov byte [edi], '0'
        dec edi
        jmp .ic
.id:
        mov ecx, 10
.il:
        test eax, eax
        jz .ic
        xor edx, edx
        div ecx
        add dl, '0'
        mov [edi], dl
        dec edi
        jmp .il
.ic:
        inc edi
        mov esi, edi
        mov edi, num_buf
.im:
        mov al, [esi]
        mov [edi], al
        inc esi
        inc edi
        test al, al
        jnz .im
        popad
        ret

; ---- Data -----------------------------------------------------------
hs_name:        db "asteroids", 0
msg_novbe:      db "asteroids: VBE not available", 10, 0
str_score:      db "Score:", 0
str_lives:      db "Lives:", 0
str_wave:       db "Wave:", 0
str_hi:         db "Hi:", 0
str_fever:      db "FEVER! Clear in ", 0
str_gameover:   db "GAME OVER", 0
str_restart:    db "Press any key to restart  Q/ESC to quit", 0

ship_x:         dd 0
ship_y:         dd 0
ship_vx:        dd 0
ship_vy:        dd 0
ship_angle:     dd 0
ship_invuln:    dd 0
lives:          dd 0
score:          dd 0
hi_score:       dd 0
wave:           dd 0
game_over:      db 0
thrusting:      db 0
rand_seed:      dd 42
fb_addr:        dd 0
fb_pitch:       dd 0

section .bss
bullets:        resb MAX_BULLETS * BLT_S
asteroids:      resb MAX_ASTEROIDS * AST_S
num_buf:        resb 16
