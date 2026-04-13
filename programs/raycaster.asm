; raycaster.asm - Wolfenstein 3D style raycaster for Mellivora OS
; Fixed-point 16.16 math, renders in a Burrows GUI window.
; WASD movement, arrow keys for turning.

%include "syscalls.inc"
%include "lib/gui.inc"

; Window / viewport
WIN_W           equ 320
WIN_H           equ 240
VIEW_W          equ 320
VIEW_H          equ 200
HUD_H           equ 40

; Fixed point (16.16)
FP_SHIFT        equ 16
FP_ONE          equ (1 << FP_SHIFT)    ; 65536
FP_HALF         equ (FP_ONE / 2)       ; 32768

; Map
MAP_W           equ 16
MAP_H           equ 16

; Player
MOVE_SPEED      equ 3277        ; ~0.05 in fixed-point
TURN_SPEED      equ 2048        ; ~0.03 rad per frame
FOV_HALF        equ 16384       ; ~0.25 (~30 deg half-FOV → ~60 deg total)

; Math constants
PI_FP           equ 205887      ; pi in 16.16
TWO_PI_FP       equ 411775      ; 2*pi in 16.16

; Max ray distance (to avoid infinite loops)
MAX_DIST        equ (FP_ONE * 16)

; Wall colors per map tile value
NUM_WALL_COLORS equ 5

start:
        ; Create window
        mov eax, 80
        mov ebx, 60
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        ; Initialize player position (center-ish of map, facing east)
        mov dword [player_x], (3 * FP_ONE + FP_HALF)  ; 3.5
        mov dword [player_y], (3 * FP_ONE + FP_HALF)  ; 3.5
        mov dword [player_angle], 0                     ; Facing east (0 rad)

.main_loop:
        call gui_compose
        call render_frame
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        je .on_key
        jmp .main_loop

.on_key:
        cmp bl, 27             ; ESC
        je .close
        cmp bl, 'w'
        je .move_forward
        cmp bl, 'W'
        je .move_forward
        cmp bl, 's'
        je .move_backward
        cmp bl, 'S'
        je .move_backward
        cmp bl, 'a'
        je .strafe_left
        cmp bl, 'A'
        je .strafe_left
        cmp bl, 'd'
        je .strafe_right
        cmp bl, 'D'
        je .strafe_right
        cmp bl, KEY_LEFT
        je .turn_left
        cmp bl, KEY_RIGHT
        je .turn_right
        jmp .main_loop

.move_forward:
        call fp_cos_angle
        imul eax, MOVE_SPEED
        sar eax, FP_SHIFT
        add [player_x], eax
        call fp_sin_angle
        imul eax, MOVE_SPEED
        sar eax, FP_SHIFT
        add [player_y], eax
        call check_wall_collision
        jmp .main_loop

.move_backward:
        call fp_cos_angle
        imul eax, MOVE_SPEED
        sar eax, FP_SHIFT
        sub [player_x], eax
        call fp_sin_angle
        imul eax, MOVE_SPEED
        sar eax, FP_SHIFT
        sub [player_y], eax
        call check_wall_collision
        jmp .main_loop

.strafe_left:
        call fp_sin_angle
        imul eax, MOVE_SPEED
        sar eax, FP_SHIFT
        sub [player_x], eax
        call fp_cos_angle
        imul eax, MOVE_SPEED
        sar eax, FP_SHIFT
        add [player_y], eax
        call check_wall_collision
        jmp .main_loop

.strafe_right:
        call fp_sin_angle
        imul eax, MOVE_SPEED
        sar eax, FP_SHIFT
        add [player_x], eax
        call fp_cos_angle
        imul eax, MOVE_SPEED
        sar eax, FP_SHIFT
        sub [player_y], eax
        call check_wall_collision
        jmp .main_loop

.turn_left:
        sub dword [player_angle], TURN_SPEED
        ; Wrap angle to [0, 2*pi)
        cmp dword [player_angle], 0
        jge .main_loop
        add dword [player_angle], TWO_PI_FP
        jmp .main_loop

.turn_right:
        add dword [player_angle], TURN_SPEED
        cmp dword [player_angle], TWO_PI_FP
        jl .main_loop
        sub dword [player_angle], TWO_PI_FP
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; WALL COLLISION CHECK
;=======================================================================

check_wall_collision:
        pushad
        ; Check if player is inside a wall, push them back
        mov eax, [player_x]
        sar eax, FP_SHIFT
        mov ebx, [player_y]
        sar ebx, FP_SHIFT
        ; Bounds check
        cmp eax, 0
        jl .cwc_reset
        cmp eax, MAP_W
        jge .cwc_reset
        cmp ebx, 0
        jl .cwc_reset
        cmp ebx, MAP_H
        jge .cwc_reset
        ; Check map cell
        imul ebx, MAP_W
        add ebx, eax
        movzx eax, byte [level_map + ebx]
        cmp eax, 0
        je .cwc_ok
.cwc_reset:
        ; Push player back to previous safe position
        mov dword [player_x], (3 * FP_ONE + FP_HALF)
        mov dword [player_y], (3 * FP_ONE + FP_HALF)
.cwc_ok:
        popad
        ret

;=======================================================================
; RENDERING
;=======================================================================

render_frame:
        pushad

        ; Draw ceiling (dark gray)
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, VIEW_W
        mov esi, VIEW_H / 2
        mov edi, 0x00333344    ; Dark blue-gray ceiling
        call gui_fill_rect

        ; Draw floor (dark brown)
        mov eax, [win_id]
        xor ebx, ebx
        mov ecx, VIEW_H / 2
        mov edx, VIEW_W
        mov esi, VIEW_H / 2
        mov edi, 0x00443322    ; Dark brown floor
        call gui_fill_rect

        ; Cast rays for each screen column
        xor edi, edi           ; Column index
.cast_loop:
        cmp edi, VIEW_W
        jge .draw_hud

        ; Calculate ray angle for this column
        ; ray_angle = player_angle + FOV_HALF - (column * FOV / VIEW_W)
        mov eax, edi
        shl eax, 1             ; column * 2 * FOV_HALF / VIEW_W
        imul eax, FOV_HALF
        xor edx, edx
        mov ebx, VIEW_W / 2
        cmp ebx, 0
        je .cast_next
        cdq
        idiv ebx
        mov ecx, [player_angle]
        add ecx, FOV_HALF
        sub ecx, eax           ; ray_angle

        ; Normalize angle to [0, 2*pi)
        cmp ecx, 0
        jge .angle_pos
        add ecx, TWO_PI_FP
.angle_pos:
        cmp ecx, TWO_PI_FP
        jl .angle_ok
        sub ecx, TWO_PI_FP
.angle_ok:
        ; ECX = ray angle (fixed-point)
        mov [.ray_angle], ecx

        ; Cast ray using DDA
        call cast_ray
        ; EAX = distance (fixed-point), EBX = wall type

        ; Correct for fisheye: dist *= cos(ray_angle - player_angle)
        push ebx
        mov ecx, [.ray_angle]
        sub ecx, [player_angle]
        ; Approximate cos for small angles: 1 - x^2/2
        ; For simplicity, use lookup cos
        push eax
        mov eax, ecx
        call fp_cos
        mov ecx, eax           ; cos value
        pop eax
        imul eax, ecx
        sar eax, FP_SHIFT
        pop ebx

        ; Calculate wall height
        ; wall_height = VIEW_H * FP_ONE / distance
        cmp eax, 1
        jl .wall_max
        push ebx
        push edx
        mov ecx, eax           ; Distance
        mov eax, VIEW_H
        shl eax, FP_SHIFT
        cdq
        idiv ecx
        pop edx
        pop ebx
        ; EAX = wall height in pixels
        cmp eax, VIEW_H
        jle .wall_clip_ok
.wall_max:
        mov eax, VIEW_H
.wall_clip_ok:
        mov [.wall_h], eax

        ; Calculate wall top
        mov ecx, VIEW_H
        sub ecx, eax
        sar ecx, 1            ; (VIEW_H - wall_h) / 2
        mov [.wall_top], ecx

        ; Determine wall color based on type and distance
        movzx eax, bl          ; Wall type
        cmp eax, NUM_WALL_COLORS
        jb .color_ok
        xor eax, eax
.color_ok:
        mov eax, [wall_colors + eax*4]

        ; Shade by distance (darken distant walls)
        ; Simple: use wall_h as brightness proxy
        mov ecx, [.wall_h]
        cmp ecx, VIEW_H
        jge .no_shade
        ; Scale each channel
        push eax
        ; Extract R, G, B and scale by wall_h / VIEW_H
        mov edx, eax
        and edx, 0xFF         ; Blue
        imul edx, ecx
        xor ebx, ebx
        mov ebx, VIEW_H
        push eax
        mov eax, edx
        xor edx, edx
        div ebx
        mov esi, eax            ; shaded B
        pop eax

        mov edx, eax
        shr edx, 8
        and edx, 0xFF         ; Green
        imul edx, ecx
        push eax
        mov eax, edx
        xor edx, edx
        div ebx
        shl eax, 8
        or esi, eax             ; shaded G
        pop eax

        mov edx, eax
        shr edx, 16
        and edx, 0xFF         ; Red
        imul edx, ecx
        mov eax, edx
        xor edx, edx
        div ebx
        shl eax, 16
        or esi, eax             ; shaded R

        pop eax                 ; discard original color
        mov eax, esi
        jmp .draw_wall
.no_shade:
        ; No shading needed

.draw_wall:
        ; Draw wall column: x=edi, y=wall_top, w=1, h=wall_h
        push edi
        mov ecx, [.wall_top]
        mov edx, 1
        mov esi, [.wall_h]
        push eax               ; Save color
        mov eax, [win_id]
        mov ebx, edi
        pop edi                 ; Color
        call gui_fill_rect
        pop edi

.cast_next:
        inc edi
        jmp .cast_loop

.draw_hud:
        ; Draw HUD bar at bottom
        mov eax, [win_id]
        xor ebx, ebx
        mov ecx, VIEW_H
        mov edx, WIN_W
        mov esi, HUD_H
        mov edi, 0x00222222    ; Dark HUD background
        call gui_fill_rect

        ; Draw HUD text
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, VIEW_H + 8
        mov esi, str_hud
        mov edi, 0x0000FF00    ; Green text
        call gui_draw_text

        ; Draw minimap
        call draw_minimap

        popad
        ret

.ray_angle: dd 0
.wall_h:    dd 0
.wall_top:  dd 0

;=======================================================================
; RAY CASTING (DDA Algorithm)
;=======================================================================

; cast_ray - Cast a single ray from player position
; ECX = ray angle (16.16 fixed-point)
; Returns: EAX = distance, BL = wall type
cast_ray:
        pushad

        ; Get ray direction components
        mov eax, ecx
        push ecx
        call fp_cos
        mov [.ray_dx], eax     ; cos(angle)
        pop ecx
        mov eax, ecx
        call fp_sin
        mov [.ray_dy], eax     ; sin(angle)

        ; Start at player position
        mov eax, [player_x]
        mov [.ray_x], eax
        mov ebx, [player_y]
        mov [.ray_y], ebx

        ; Step ray forward in small increments
        ; Use 1/64 of a tile as step size for reasonable precision
        mov dword [.step_count], 0
        mov dword [.hit_type], 0

.ray_step:
        inc dword [.step_count]
        cmp dword [.step_count], 512    ; Max steps
        jge .ray_miss

        ; Advance ray: x += dx * step, y += dy * step
        ; Step size = FP_ONE / 32 = 2048
        mov eax, [.ray_dx]
        sar eax, 5             ; dx / 32
        add [.ray_x], eax
        mov eax, [.ray_dy]
        sar eax, 5             ; dy / 32
        add [.ray_y], eax

        ; Check map cell at ray position
        mov eax, [.ray_x]
        sar eax, FP_SHIFT      ; Integer X
        cmp eax, 0
        jl .ray_miss
        cmp eax, MAP_W
        jge .ray_miss
        mov ebx, [.ray_y]
        sar ebx, FP_SHIFT      ; Integer Y
        cmp ebx, 0
        jl .ray_miss
        cmp ebx, MAP_H
        jge .ray_miss

        imul ebx, MAP_W
        add ebx, eax
        movzx ecx, byte [level_map + ebx]
        cmp ecx, 0
        je .ray_step           ; Empty cell, continue

        ; Hit a wall!
        mov [.hit_type], ecx

        ; Calculate distance from player to hit point
        mov eax, [.ray_x]
        sub eax, [player_x]
        mov [.dx], eax
        mov eax, [.ray_y]
        sub eax, [player_y]
        mov [.dy], eax

        ; Distance = sqrt(dx^2 + dy^2) approximation
        ; Use |dx| + |dy| * 0.4 (fast approximation)
        ; Or better: max(|dx|,|dy|) + min(|dx|,|dy|) * 3/8
        mov eax, [.dx]
        cdq
        xor eax, edx
        sub eax, edx           ; |dx|
        mov ebx, [.dy]
        mov edx, ebx
        sar edx, 31
        xor ebx, edx
        sub ebx, edx           ; |dy|

        cmp eax, ebx
        jge .dist_calc
        xchg eax, ebx         ; EAX = max, EBX = min
.dist_calc:
        ; dist ≈ max + min * 3/8
        mov ecx, ebx
        shr ecx, 1             ; min/2
        mov edx, ebx
        shr edx, 3             ; min/8
        add ecx, edx           ; min * 5/8... let's just use simpler
        ; dist = max + min/2 (rough but fast)
        shr ebx, 1
        add eax, ebx

        ; Return via pushad frame
        mov [esp + 28], eax    ; EAX = distance
        movzx eax, byte [.hit_type]
        mov [esp + 16], eax    ; EBX = wall type (low byte)
        popad
        ret

.ray_miss:
        mov dword [esp + 28], MAX_DIST
        mov dword [esp + 16], 0
        popad
        ret

.ray_dx:     dd 0
.ray_dy:     dd 0
.ray_x:      dd 0
.ray_y:      dd 0
.dx:         dd 0
.dy:         dd 0
.step_count: dd 0
.hit_type:   dd 0

;=======================================================================
; MINIMAP
;=======================================================================

draw_minimap:
        pushad
        ; Draw in bottom-right corner of HUD
        ; Each map cell = 2x2 pixels
        MINI_X equ (WIN_W - MAP_W * 2 - 8)
        MINI_Y equ (VIEW_H + 4)

        mov dword [.mm_row_v], 0
.mm_row_loop:
        mov esi, [.mm_row_v]
        cmp esi, MAP_H
        jge .mm_player
        mov dword [.mm_col_v], 0
.mm_col_loop:
        mov edi, [.mm_col_v]
        cmp edi, MAP_W
        jge .mm_next_row

        ; Get cell value
        mov eax, esi
        imul eax, MAP_W
        add eax, edi
        movzx eax, byte [level_map + eax]

        ; Determine color
        cmp eax, 0
        je .mm_empty
        mov dword [.mm_color], 0x00AA6666  ; Wall color
        jmp .mm_draw
.mm_empty:
        mov dword [.mm_color], 0x00444444  ; Floor

.mm_draw:
        ; Draw 2x2 pixel for this cell
        mov eax, [win_id]
        mov ebx, [.mm_col_v]
        shl ebx, 1
        add ebx, MINI_X
        mov ecx, [.mm_row_v]
        shl ecx, 1
        add ecx, MINI_Y
        mov edx, 2
        mov esi, 2
        mov edi, [.mm_color]
        call gui_fill_rect

        inc dword [.mm_col_v]
        mov esi, [.mm_row_v]   ; Restore row for inner loop
        jmp .mm_col_loop

.mm_next_row:
        inc dword [.mm_row_v]
        jmp .mm_row_loop

.mm_player:
        ; Draw player position (yellow dot)
        mov eax, [player_x]
        sar eax, FP_SHIFT
        shl eax, 1
        add eax, MINI_X
        mov [.mm_px], eax

        mov eax, [win_id]
        mov ebx, [.mm_px]
        mov ecx, [player_y]
        sar ecx, FP_SHIFT
        shl ecx, 1
        add ecx, MINI_Y
        mov edx, 2
        mov esi, 2
        mov edi, 0x00FFFF00   ; Yellow
        call gui_fill_rect

        popad
        ret

.mm_color: dd 0
.mm_row_v: dd 0
.mm_col_v: dd 0
.mm_px:    dd 0

;=======================================================================
; FIXED-POINT TRIG (Lookup tables)
;=======================================================================

; sin/cos using a 64-entry lookup table for [0, 2*pi)
; Table stores sin values in 16.16 fixed-point
; cos(x) = sin(x + pi/2)

TRIG_TABLE_SIZE equ 64

; fp_sin - Fixed-point sine
; EAX = angle in 16.16 fixed-point radians
; Returns: EAX = sin(angle) in 16.16
fp_sin:
        push ebx
        push ecx
        push edx

        ; Normalize to [0, TWO_PI_FP)
.sin_norm:
        cmp eax, 0
        jge .sin_pos
        add eax, TWO_PI_FP
        jmp .sin_norm
.sin_pos:
        cmp eax, TWO_PI_FP
        jl .sin_ok
        sub eax, TWO_PI_FP
        jmp .sin_pos
.sin_ok:
        ; Map angle to table index: index = angle * TABLE_SIZE / TWO_PI
        imul eax, TRIG_TABLE_SIZE
        xor edx, edx
        mov ebx, TWO_PI_FP
        div ebx
        and eax, (TRIG_TABLE_SIZE - 1)
        mov eax, [sin_table + eax*4]

        pop edx
        pop ecx
        pop ebx
        ret

; fp_cos - Fixed-point cosine
; EAX = angle in 16.16 fixed-point radians
; Returns: EAX = cos(angle) in 16.16
fp_cos:
        push ebx
        push ecx
        push edx

        ; cos(x) = sin(x + pi/2)
        add eax, (TWO_PI_FP / 4)       ; + pi/2
.cos_norm:
        cmp eax, TWO_PI_FP
        jl .cos_ok
        sub eax, TWO_PI_FP
        jmp .cos_norm
.cos_ok:
        cmp eax, 0
        jge .cos_pos
        add eax, TWO_PI_FP
        jmp .cos_ok
.cos_pos:
        imul eax, TRIG_TABLE_SIZE
        xor edx, edx
        mov ebx, TWO_PI_FP
        div ebx
        and eax, (TRIG_TABLE_SIZE - 1)
        mov eax, [sin_table + eax*4]

        pop edx
        pop ecx
        pop ebx
        ret

; fp_cos_angle / fp_sin_angle - convenience: use player_angle
fp_cos_angle:
        mov eax, [player_angle]
        call fp_cos
        ret

fp_sin_angle:
        mov eax, [player_angle]
        call fp_sin
        ret

;=======================================================================
; DATA
;=======================================================================

title_str: db "Raycaster 3D", 0
str_hud:   db "WASD=Move  Arrows=Turn  ESC=Quit", 0

; Wall colors (indexed by map tile value 1-4)
wall_colors:
        dd 0x00888888          ; Gray (default/unused for 0)
        dd 0x00AA2222          ; Red brick
        dd 0x006666AA          ; Blue stone
        dd 0x0022AA22          ; Green moss
        dd 0x00AA8822          ; Brown wood

; Sin lookup table: 64 entries for [0, 2*pi)
; Values are sin(i * 2*pi / 64) * 65536 (16.16 fixed-point)
sin_table:
        dd      0,   6424,  12785,  19024,  25080,  30893,  36410,  41576  ; 0-7
        dd  46341,  50660,  54491,  57798,  60547,  62714,  64277,  65220  ; 8-15
        dd  65536,  65220,  64277,  62714,  60547,  57798,  54491,  50660  ; 16-23
        dd  46341,  41576,  36410,  30893,  25080,  19024,  12785,   6424  ; 24-31
        dd      0,  -6424, -12785, -19024, -25080, -30893, -36410, -41576  ; 32-39
        dd -46341, -50660, -54491, -57798, -60547, -62714, -64277, -65220  ; 40-47
        dd -65536, -65220, -64277, -62714, -60547, -57798, -54491, -50660  ; 48-55
        dd -46341, -41576, -36410, -30893, -25080, -19024, -12785,  -6424  ; 56-63

; Level map (16x16, 0=empty, 1-4=wall types)
level_map:
        db 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1  ; Row 0
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1  ; Row 1
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1  ; Row 2
        db 1,0,0,0,2,2,0,0,0,3,3,0,0,0,0,1  ; Row 3
        db 1,0,0,0,2,0,0,0,0,0,3,0,0,0,0,1  ; Row 4
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1  ; Row 5
        db 1,0,0,0,0,0,4,4,4,0,0,0,0,0,0,1  ; Row 6
        db 1,0,0,0,0,0,4,0,4,0,0,0,0,0,0,1  ; Row 7
        db 1,0,0,0,0,0,4,0,4,0,0,0,2,0,0,1  ; Row 8
        db 1,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1  ; Row 9
        db 1,0,0,3,0,0,0,0,0,0,0,0,0,0,0,1  ; Row 10
        db 1,0,0,3,0,0,0,0,0,4,0,0,0,0,0,1  ; Row 11
        db 1,0,0,3,3,0,0,0,0,4,4,4,0,0,0,1  ; Row 12
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1  ; Row 13
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1  ; Row 14
        db 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1  ; Row 15

;=======================================================================
; BSS
;=======================================================================
align 4
win_id:        dd 0
player_x:      dd 0
player_y:      dd 0
player_angle:  dd 0
