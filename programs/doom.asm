; ==========================================================================
; doom - Text-mode raycaster FPS for Mellivora OS
; A first-person dungeon explorer rendered in 80x25 text mode.
; Controls: W/S = forward/back, A/D = strafe, Q/E = turn, X = quit
; ==========================================================================

%include "syscalls.inc"

SCREEN_W    equ 80
SCREEN_H    equ 25
MAP_W       equ 16
MAP_H       equ 16
FOV         equ 64              ; Field of view (out of 256 angles)
MAX_DEPTH   equ 160             ; Max ray steps (16.0 in 8.4 fixed)
STEP_SIZE   equ 1               ; Ray step in 8.4 (0.0625 units)
MOVE_SPEED  equ 6               ; Movement speed (8.4 fixed = 0.375)
TURN_SPEED  equ 8               ; Turn speed (out of 256)

; 8.4 fixed-point: 1.0 = 16

start:
        ; Initialize player position (center of map)
        mov word [player_x], 2 * 16 + 8    ; 2.5 in 8.4
        mov word [player_y], 2 * 16 + 8    ; 2.5 in 8.4
        mov byte [player_a], 0              ; Facing east

.main_loop:
        ; Handle input (non-blocking)
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .no_input

        cmp al, 'x'
        je .quit
        cmp al, 'X'
        je .quit
        cmp al, 27             ; ESC
        je .quit

        cmp al, 'w'
        je .move_forward
        cmp al, 'W'
        je .move_forward
        cmp al, 's'
        je .move_back
        cmp al, 'S'
        je .move_back
        cmp al, 'a'
        je .strafe_left
        cmp al, 'A'
        je .strafe_left
        cmp al, 'd'
        je .strafe_right
        cmp al, 'D'
        je .strafe_right
        cmp al, 'q'
        je .turn_left
        cmp al, 'Q'
        je .turn_left
        cmp al, 'e'
        je .turn_right
        cmp al, 'E'
        je .turn_right
        jmp .no_input

.move_forward:
        movzx eax, byte [player_a]
        movsx ebx, word [cos_table + eax * 2]
        imul ebx, MOVE_SPEED
        sar ebx, 4
        movsx ecx, word [player_x]
        add ecx, ebx
        movsx ebx, word [sin_table + eax * 2]
        imul ebx, MOVE_SPEED
        sar ebx, 4
        movsx edx, word [player_y]
        add edx, ebx
        call try_move
        jmp .no_input

.move_back:
        movzx eax, byte [player_a]
        movsx ebx, word [cos_table + eax * 2]
        imul ebx, MOVE_SPEED
        sar ebx, 4
        movsx ecx, word [player_x]
        sub ecx, ebx
        movsx ebx, word [sin_table + eax * 2]
        imul ebx, MOVE_SPEED
        sar ebx, 4
        movsx edx, word [player_y]
        sub edx, ebx
        call try_move
        jmp .no_input

.strafe_left:
        movzx eax, byte [player_a]
        sub al, 64              ; -90 degrees
        movsx ebx, word [cos_table + eax * 2]
        imul ebx, MOVE_SPEED
        sar ebx, 4
        movsx ecx, word [player_x]
        add ecx, ebx
        movsx ebx, word [sin_table + eax * 2]
        imul ebx, MOVE_SPEED
        sar ebx, 4
        movsx edx, word [player_y]
        add edx, ebx
        call try_move
        jmp .no_input

.strafe_right:
        movzx eax, byte [player_a]
        add al, 64              ; +90 degrees
        movsx ebx, word [cos_table + eax * 2]
        imul ebx, MOVE_SPEED
        sar ebx, 4
        movsx ecx, word [player_x]
        add ecx, ebx
        movsx ebx, word [sin_table + eax * 2]
        imul ebx, MOVE_SPEED
        sar ebx, 4
        movsx edx, word [player_y]
        add edx, ebx
        call try_move
        jmp .no_input

.turn_left:
        sub byte [player_a], TURN_SPEED
        jmp .no_input

.turn_right:
        add byte [player_a], TURN_SPEED

.no_input:
        ; Render frame
        call render_frame

        ; Display
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, screen_buf
        int 0x80

        ; Frame delay
        mov eax, SYS_SLEEP
        mov ebx, 5              ; ~50ms
        int 0x80
        jmp .main_loop

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ---- try_move: ECX=new_x, EDX=new_y (8.4 fixed) ----
; Check collision before moving
try_move:
        ; Convert to map coords
        push rcx
        push rdx
        mov eax, ecx
        sar eax, 4              ; map_x
        cmp eax, 0
        jl .tm_blocked
        cmp eax, MAP_W
        jge .tm_blocked
        mov ebx, edx
        sar ebx, 4              ; map_y
        cmp ebx, 0
        jl .tm_blocked
        cmp ebx, MAP_H
        jge .tm_blocked

        ; Check map cell
        imul ebx, MAP_W
        add ebx, eax
        cmp byte [game_map + ebx], 0
        jne .tm_blocked

        pop rdx
        pop rcx
        mov [player_x], cx
        mov [player_y], dx
        ret
.tm_blocked:
        pop rdx
        pop rcx
        ret

; ---- render_frame ----
render_frame:
        PUSHALL
        mov edi, screen_buf

        ; For each column 0..79, cast a ray
        xor ebp, ebp           ; column counter

.rf_column:
        ; Calculate ray angle: player_a - FOV/2 + col * FOV / SCREEN_W
        movzx eax, byte [player_a]
        sub eax, FOV / 2
        ; Add col * FOV / 80
        mov ecx, ebp
        imul ecx, FOV
        push rdx
        xor edx, edx
        push rax
        mov eax, ecx
        mov ecx, SCREEN_W
        xor edx, edx
        div ecx                 ; EAX = col * FOV / 80
        mov ecx, eax
        pop rax
        pop rdx
        add eax, ecx
        and eax, 0xFF           ; Wrap to 0-255

        ; Cast ray from player position at this angle
        ; Step along ray in small increments
        movsx ecx, word [player_x]    ; ray_x (8.4)
        movsx edx, word [player_y]    ; ray_y (8.4)

        ; Get ray direction from lookup tables
        movsx esi, word [cos_table + eax * 2]  ; dx per step (8.4)
        push rax
        movsx eax, word [sin_table + eax * 2]  ; dy per step (8.4)
        mov [ray_dy], eax
        pop rax
        mov [ray_dx], esi

        ; Step along ray
        xor ebx, ebx           ; step counter = distance
.rf_step:
        cmp ebx, MAX_DEPTH
        jge .rf_max_dist

        ; Advance ray position
        add ecx, esi                   ; ray_x += dx
        mov eax, [ray_dy]
        add edx, eax                   ; ray_y += dy

        ; Check map at ray position
        mov eax, ecx
        sar eax, 4              ; map_x
        cmp eax, 0
        jl .rf_max_dist
        cmp eax, MAP_W
        jge .rf_max_dist
        push rbx
        mov ebx, edx
        sar ebx, 4              ; map_y
        cmp ebx, 0
        jl .rf_max_dist_pop
        cmp ebx, MAP_H
        jge .rf_max_dist_pop

        imul ebx, MAP_W
        add ebx, eax
        movzx eax, byte [game_map + ebx]
        pop rbx
        test eax, eax
        jnz .rf_hit_wall

        inc ebx
        jmp .rf_step

.rf_max_dist_pop:
        pop rbx
.rf_max_dist:
        ; No wall hit — distance = max
        mov ebx, MAX_DEPTH

.rf_hit_wall:
        ; EBX = distance in steps, EAX = wall type (if hit)
        ; Calculate wall height: height = K / distance
        ; K = SCREEN_H * 8 (tuning constant)
        mov [col_wall_type], al
        push rdx

        mov eax, SCREEN_H * 12
        cmp ebx, 0
        je .rf_full_height
        xor edx, edx
        div ebx
        jmp .rf_got_height
.rf_full_height:
        mov eax, SCREEN_H
.rf_got_height:
        cmp eax, SCREEN_H
        jle .rf_height_ok
        mov eax, SCREEN_H
.rf_height_ok:
        mov [col_height], eax
        pop rdx

        ; Calculate ceiling and floor rows
        mov ecx, SCREEN_H
        sub ecx, eax
        shr ecx, 1              ; ceiling rows
        mov [col_ceil], ecx
        mov eax, ecx
        add eax, [col_height]
        mov [col_floor], eax

        ; Choose wall character based on distance
        mov al, 0xDB            ; Full block (close)
        cmp ebx, 20
        jl .rf_shade_done
        mov al, 0xB2            ; Dark shade
        cmp ebx, 50
        jl .rf_shade_done
        mov al, 0xB1            ; Medium shade
        cmp ebx, 90
        jl .rf_shade_done
        mov al, 0xB0            ; Light shade
        cmp ebx, 130
        jl .rf_shade_done
        mov al, '.'             ; Very far
.rf_shade_done:
        mov [col_shade], al

        ; Write column into screen buffer at correct positions
        ; Screen buffer layout: 25 rows x 80 cols + newlines
        xor ecx, ecx           ; row counter

.rf_draw_row:
        cmp ecx, SCREEN_H
        jge .rf_col_done

        ; Calculate buffer position: row * 81 + col
        ; (81 = 80 chars + 1 newline per row)
        mov eax, ecx
        imul eax, SCREEN_W + 1
        add eax, ebp
        
        cmp ecx, [col_ceil]
        jl .rf_draw_ceil
        cmp ecx, [col_floor]
        jge .rf_draw_floor

        ; Wall
        mov bl, [col_shade]
        mov [screen_buf + eax], bl
        jmp .rf_draw_next

.rf_draw_ceil:
        mov byte [screen_buf + eax], ' '
        jmp .rf_draw_next

.rf_draw_floor:
        ; Floor shading by distance from center
        mov bl, '.'
        mov edx, ecx
        sub edx, SCREEN_H / 2
        cmp edx, 10
        jl .rf_floor_dot
        mov bl, ':'
.rf_floor_dot:
        mov [screen_buf + eax], bl

.rf_draw_next:
        inc ecx
        jmp .rf_draw_row

.rf_col_done:
        inc ebp
        cmp ebp, SCREEN_W
        jl .rf_column

        ; Add newlines at end of each row
        xor ecx, ecx
.rf_newlines:
        mov eax, ecx
        imul eax, SCREEN_W + 1
        add eax, SCREEN_W
        mov byte [screen_buf + eax], 10
        inc ecx
        cmp ecx, SCREEN_H
        jl .rf_newlines

        ; Null-terminate
        mov eax, SCREEN_H
        imul eax, SCREEN_W + 1
        mov byte [screen_buf + eax], 0

        ; Draw mini-map overlay (top-right corner, 5x5 around player)
        movsx eax, word [player_x]
        sar eax, 4
        mov [mm_px], eax
        movsx eax, word [player_y]
        sar eax, 4
        mov [mm_py], eax

        xor ecx, ecx           ; mini-map row
.rf_mm_row:
        cmp ecx, 5
        jge .rf_mm_done
        xor edx, edx           ; mini-map col
.rf_mm_col:
        cmp edx, 5
        jge .rf_mm_row_next

        ; Map coords
        mov eax, [mm_px]
        add eax, edx
        sub eax, 2
        mov ebx, [mm_py]
        add ebx, ecx
        sub ebx, 2

        mov esi, '.'            ; default = empty
        cmp eax, 0
        jl .rf_mm_oob
        cmp eax, MAP_W
        jge .rf_mm_oob
        cmp ebx, 0
        jl .rf_mm_oob
        cmp ebx, MAP_H
        jge .rf_mm_oob

        ; Check if this is player position
        cmp eax, [mm_px]
        jne .rf_mm_not_player
        cmp ebx, [mm_py]
        jne .rf_mm_not_player
        mov esi, '@'
        jmp .rf_mm_write

.rf_mm_not_player:
        push rax
        imul ebx, MAP_W
        add ebx, eax
        cmp byte [game_map + ebx], 0
        pop rax
        je .rf_mm_empty
        mov esi, '#'
        jmp .rf_mm_write
.rf_mm_empty:
        mov esi, ' '
        jmp .rf_mm_write
.rf_mm_oob:
        mov esi, '~'

.rf_mm_write:
        ; Position in screen buffer: row ecx, col (SCREEN_W - 7 + edx)
        push rax
        mov eax, ecx
        imul eax, SCREEN_W + 1
        add eax, SCREEN_W - 7
        add eax, edx
        mov [screen_buf + eax], esi
        pop rax

        inc edx
        jmp .rf_mm_col

.rf_mm_row_next:
        inc ecx
        jmp .rf_mm_row

.rf_mm_done:
        POPALL
        ret

; ---- Data ----

; 16x16 map (0=empty, 1-3=wall types)
game_map:
        db 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
        db 1,0,0,1,1,0,0,0,0,0,1,1,0,0,0,1
        db 1,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
        db 1,0,0,0,0,0,2,2,2,0,0,0,0,0,0,1
        db 1,0,0,0,0,0,2,0,2,0,0,0,0,0,0,1
        db 1,0,0,0,0,0,2,0,2,0,0,0,0,0,0,1
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
        db 1,0,0,3,0,0,0,0,0,0,0,3,0,0,0,1
        db 1,0,0,3,3,0,0,0,0,0,3,3,0,0,0,1
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
        db 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
        db 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1

; Sin/Cos lookup table: 256 entries, 8.4 fixed-point
; sin(angle) where angle = index * 2*PI / 256
; Values range from -16 to +16 (representing -1.0 to +1.0 in 8.4)
; cos(a) = sin(a + 64)
sin_table:
    ; 0-15: sin(0) to sin(~33.75 deg)
    dw 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7
    ; 16-31
    dw 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11
    ; 32-47
    dw 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13
    ; 48-63
    dw 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14
    ; 64-79: sin(90) = 16
    dw 16, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 13, 13, 13, 13
    ; 80-95
    dw 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 12, 11
    ; 96-111
    dw 11, 11, 11, 11, 11, 10, 10, 10, 10, 9, 9, 9, 8, 8, 8, 7
    ; 112-127
    dw 7, 7, 6, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0
    ; 128-143: sin(180) = 0, going negative
    dw 0, 0, -1, -1, -2, -2, -3, -3, -4, -4, -5, -5, -6, -6, -6, -7
    ; 144-159
    dw -7, -7, -8, -8, -8, -9, -9, -9, -10, -10, -10, -10, -11, -11, -11, -11
    ; 160-175
    dw -11, -11, -12, -12, -12, -12, -12, -12, -12, -12, -13, -13, -13, -13, -13, -13
    ; 176-191
    dw -13, -13, -13, -13, -13, -14, -14, -14, -14, -14, -14, -14, -14, -14, -14, -14
    ; 192-207: sin(270) = -16
    dw -16, -14, -14, -14, -14, -14, -14, -14, -14, -14, -14, -14, -13, -13, -13, -13
    ; 208-223
    dw -13, -13, -13, -13, -13, -13, -13, -12, -12, -12, -12, -12, -12, -12, -12, -11
    ; 224-239
    dw -11, -11, -11, -11, -11, -10, -10, -10, -10, -9, -9, -9, -8, -8, -8, -7
    ; 240-255
    dw -7, -7, -6, -6, -6, -5, -5, -4, -4, -3, -3, -2, -2, -1, -1, 0

; cos_table = sin_table shifted by 64 entries (90 degrees)
cos_table:
    ; cos(a) = sin(a + 64)
    ; 0-15: cos(0)=16 to cos(~33.75)
    dw 16, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 13, 13, 13, 13
    ; 16-31
    dw 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 12, 11
    ; 32-47
    dw 11, 11, 11, 11, 11, 10, 10, 10, 10, 9, 9, 9, 8, 8, 8, 7
    ; 48-63
    dw 7, 7, 6, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0
    ; 64-79: cos(90)=0
    dw 0, 0, -1, -1, -2, -2, -3, -3, -4, -4, -5, -5, -6, -6, -6, -7
    ; 80-95
    dw -7, -7, -8, -8, -8, -9, -9, -9, -10, -10, -10, -10, -11, -11, -11, -11
    ; 96-111
    dw -11, -11, -12, -12, -12, -12, -12, -12, -12, -12, -13, -13, -13, -13, -13, -13
    ; 112-127
    dw -13, -13, -13, -13, -13, -14, -14, -14, -14, -14, -14, -14, -14, -14, -14, -14
    ; 128-143: cos(180)=-16
    dw -16, -14, -14, -14, -14, -14, -14, -14, -14, -14, -14, -14, -13, -13, -13, -13
    ; 144-159
    dw -13, -13, -13, -13, -13, -13, -13, -12, -12, -12, -12, -12, -12, -12, -12, -11
    ; 160-175
    dw -11, -11, -11, -11, -11, -10, -10, -10, -10, -9, -9, -9, -8, -8, -8, -7
    ; 176-191
    dw -7, -7, -6, -6, -6, -5, -5, -4, -4, -3, -3, -2, -2, -1, -1, 0
    ; 192-207: cos(270)=0
    dw 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7
    ; 208-223
    dw 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11
    ; 224-239
    dw 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13
    ; 240-255
    dw 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14

; ---- BSS ----
player_x:       dw 0
player_y:       dw 0
player_a:       db 0
ray_dx:         dd 0
ray_dy:         dd 0
col_height:     dd 0
col_ceil:       dd 0
col_floor:      dd 0
col_shade:      db 0
col_wall_type:  db 0
mm_px:          dd 0
mm_py:          dd 0
screen_buf:     times (SCREEN_W + 1) * SCREEN_H + 1 db 0
