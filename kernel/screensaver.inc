;=======================================================================
; screensaver.inc - Desktop Screensavers for Burrows
;
; Provides animated screensavers that activate after idle timeout.
; Available modes:
;   0 = Starfield (3D parallax stars)
;   1 = Matrix rain (green cascading characters)
;   2 = Pipes (colored pipes growing in random directions)
;   3 = Bouncing logo (Mellivora text bouncing around)
;   4 = Plasma (color-cycling plasma effect)
;
; Integration:
;   Call scr_idle_tick each frame from gui_desktop_loop.
;   It tracks idle time and activates the screensaver automatically.
;   Any keyboard/mouse input dismisses the screensaver.
;=======================================================================

SCR_IDLE_TIMEOUT equ 30000      ; ~5 minutes at 100 Hz PIT (30000 ticks)
SCR_NUM_STARS    equ 64         ; Number of stars in starfield
SCR_MATRIX_COLS  equ 80         ; Matrix rain columns (640/8)
SCR_MATRIX_ROWS  equ 30         ; Matrix rain visible rows (480/16)
SCR_NUM_PIPES    equ 6          ; Number of concurrent pipes

;---------------------------------------
; scr_idle_tick - Called each desktop frame
; Tracks idle time, activates screensaver
; when timeout reached.
;---------------------------------------
scr_idle_tick:
        pushad

        ; Check if any input happened this frame
        movzx eax, byte [mouse_btn_latch]
        test eax, eax
        jnz .scr_reset_idle

        ; Check mouse movement
        mov eax, [mouse_x]
        cmp eax, [.scr_last_mx]
        jne .scr_reset_idle
        mov eax, [mouse_y]
        cmp eax, [.scr_last_my]
        jne .scr_reset_idle

        ; Check keyboard (peek without consuming)
        mov eax, [kb_read_idx]
        cmp eax, [kb_write_idx]
        jne .scr_reset_idle

        ; No input — increment idle counter
        inc dword [scr_idle_count]
        mov eax, [scr_idle_count]
        cmp eax, SCR_IDLE_TIMEOUT
        jb .scr_idle_done

        ; Timeout reached — activate screensaver
        call scr_run
        mov dword [scr_idle_count], 0

        jmp .scr_idle_done

.scr_reset_idle:
        mov dword [scr_idle_count], 0
        mov eax, [mouse_x]
        mov [.scr_last_mx], eax
        mov eax, [mouse_y]
        mov [.scr_last_my], eax

.scr_idle_done:
        popad
        ret

.scr_last_mx:  dd 0
.scr_last_my:  dd 0

;---------------------------------------
; scr_run - Run the active screensaver
; Loops until any key/mouse press.
;---------------------------------------
scr_run:
        pushad

        ; Initialize based on current mode
        movzx eax, byte [scr_mode]
        cmp eax, 1
        je .scr_init_matrix
        cmp eax, 2
        je .scr_init_pipes
        cmp eax, 3
        je .scr_init_bounce
        cmp eax, 4
        je .scr_init_plasma
        ; Default: starfield
        call scr_starfield_init
        jmp .scr_loop
.scr_init_matrix:
        call scr_matrix_init
        jmp .scr_loop
.scr_init_pipes:
        call scr_pipes_init
        jmp .scr_loop
.scr_init_bounce:
        call scr_bounce_init
        jmp .scr_loop
.scr_init_plasma:
        call scr_plasma_init

.scr_loop:
        ; Clear backbuffer to black (Pipes skips clear to accumulate)
        movzx eax, byte [scr_mode]
        cmp eax, 2
        je .scr_no_clear        ; Pipes accumulates draws
        push eax
        mov edi, GUI_BACKBUF
        mov ecx, (GUI_SCREEN_W * GUI_SCREEN_H)
        xor eax, eax
        rep stosd
        pop eax
.scr_no_clear:

        ; Draw active screensaver frame
        cmp eax, 1
        je .scr_draw_matrix
        cmp eax, 2
        je .scr_draw_pipes
        cmp eax, 3
        je .scr_draw_bounce
        cmp eax, 4
        je .scr_draw_plasma
        call scr_starfield_frame
        jmp .scr_flip
.scr_draw_matrix:
        call scr_matrix_frame
        jmp .scr_flip
.scr_draw_pipes:
        call scr_pipes_frame
        jmp .scr_flip
.scr_draw_bounce:
        call scr_bounce_frame
        jmp .scr_flip
.scr_draw_plasma:
        call scr_plasma_frame

.scr_flip:
        call gui_flip

        ; Check for dismiss (any key or mouse click)
        call kb_pollchar
        test al, al
        jnz .scr_dismiss

        movzx eax, byte [mouse_btn_latch]
        mov byte [mouse_btn_latch], 0
        test eax, eax
        jnz .scr_dismiss

        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .scr_dismiss

        ; Small delay (~16ms = 60fps-ish)
        push dword [tick_count]
.scr_wait_tick:
        mov eax, [tick_count]
        cmp eax, [esp]
        je .scr_wait_tick
        pop eax

        jmp .scr_loop

.scr_dismiss:
        ; Drain any remaining keypress
        call kb_pollchar

        popad
        ret

;=======================================================================
; SCREENSAVER 0: STARFIELD (3D Parallax)
;=======================================================================

scr_starfield_init:
        pushad
        ; Initialize star positions with pseudo-random values
        mov edi, scr_star_x
        mov ecx, SCR_NUM_STARS
        mov eax, 12345          ; seed
.sf_init_loop:
        ; X: random in range -320..+319
        imul eax, 1103515245
        add eax, 12345
        mov edx, eax
        sar edx, 16
        and edx, 0x3FF         ; 0..1023
        sub edx, 512
        mov [edi], dx           ; X
        add edi, 2

        ; Y: random
        imul eax, 1103515245
        add eax, 12345
        mov edx, eax
        sar edx, 16
        and edx, 0x1FF         ; 0..511
        sub edx, 256
        mov [edi], dx           ; Y (in star_y)

        ; Z: random depth 1..256
        imul eax, 1103515245
        add eax, 12345
        mov edx, eax
        shr edx, 16
        and edx, 0xFF
        inc edx
        mov [edi + SCR_NUM_STARS*2 - SCR_NUM_STARS*2], dx

        add edi, 2
        dec ecx
        jnz .sf_init_loop

        ; Store Z values separately
        mov edi, scr_star_z
        mov ecx, SCR_NUM_STARS
        mov eax, 54321
.sf_init_z:
        imul eax, 1103515245
        add eax, 12345
        mov edx, eax
        shr edx, 16
        and edx, 0xFF
        inc edx
        mov [edi], dx
        add edi, 2
        dec ecx
        jnz .sf_init_z

        mov [scr_rng_state], eax
        popad
        ret

scr_starfield_frame:
        pushad
        mov ecx, SCR_NUM_STARS
        xor esi, esi            ; Index

.sf_frame_loop:
        ; Decrease Z (star moves closer)
        movsx eax, word [scr_star_z + esi*2]
        sub eax, 2
        cmp eax, 1
        jg .sf_z_ok
        ; Reset star to far distance
        mov eax, 255
        ; Randomize X, Y
        mov edx, [scr_rng_state]
        imul edx, 1103515245
        add edx, 12345
        mov [scr_rng_state], edx
        sar edx, 16
        and edx, 0x3FF
        sub edx, 512
        mov [scr_star_x + esi*2], dx
        mov edx, [scr_rng_state]
        imul edx, 1103515245
        add edx, 12345
        mov [scr_rng_state], edx
        sar edx, 16
        and edx, 0x1FF
        sub edx, 256
        mov [scr_star_y + esi*2], dx
.sf_z_ok:
        mov [scr_star_z + esi*2], ax

        ; Project: screen_x = 320 + (star_x * 256) / Z
        movsx eax, word [scr_star_x + esi*2]
        shl eax, 8
        movsx ebx, word [scr_star_z + esi*2]
        test ebx, ebx
        jz .sf_next
        cdq
        idiv ebx
        add eax, 320
        cmp eax, 0
        jl .sf_next
        cmp eax, 639
        jg .sf_next
        mov edi, eax            ; screen X

        ; screen_y = 240 + (star_y * 256) / Z
        movsx eax, word [scr_star_y + esi*2]
        shl eax, 8
        cdq
        idiv ebx
        add eax, 240
        cmp eax, 0
        jl .sf_next
        cmp eax, 479
        jg .sf_next

        ; Calculate pixel offset: y * 640 + x
        imul eax, GUI_SCREEN_W
        add eax, edi

        ; Brightness based on distance: closer = brighter
        movsx ebx, word [scr_star_z + esi*2]
        mov edx, 255
        sub edx, ebx
        cmp edx, 0
        jl .sf_next
        ; Make RGB from brightness
        mov ebx, edx
        shl ebx, 16            ; R
        or ebx, edx
        shl edx, 8
        or ebx, edx            ; G (white star)

        ; Draw 1-pixel star
        mov [GUI_BACKBUF + eax*4], ebx

.sf_next:
        inc esi
        cmp esi, SCR_NUM_STARS
        jb .sf_frame_loop

        popad
        ret

;=======================================================================
; SCREENSAVER 1: MATRIX RAIN
;=======================================================================

scr_matrix_init:
        pushad
        ; Initialize column drop positions to random values
        mov edi, scr_matrix_pos
        mov ecx, SCR_MATRIX_COLS
        mov eax, [tick_count]
.mi_loop:
        imul eax, 1103515245
        add eax, 12345
        mov edx, eax
        shr edx, 16
        and edx, 0x1F          ; 0..31
        mov [edi], dl
        inc edi
        dec ecx
        jnz .mi_loop

        ; Initialize column speeds
        mov edi, scr_matrix_spd
        mov ecx, SCR_MATRIX_COLS
.mi_spd:
        imul eax, 1103515245
        add eax, 12345
        mov edx, eax
        shr edx, 16
        and edx, 0x03          ; Speed 0..3
        inc edx                ; 1..4
        mov [edi], dl
        inc edi
        dec ecx
        jnz .mi_spd

        ; Initialize character values
        mov edi, scr_matrix_chr
        mov ecx, SCR_MATRIX_COLS * SCR_MATRIX_ROWS
.mi_chr:
        imul eax, 1103515245
        add eax, 12345
        mov edx, eax
        shr edx, 16
        and edx, 0x5E          ; Range for printable chars
        add edx, 0x21          ; '!' to '~'
        mov [edi], dl
        inc edi
        dec ecx
        jnz .mi_chr

        mov [scr_rng_state], eax
        mov byte [scr_matrix_tick], 0
        popad
        ret

scr_matrix_frame:
        pushad

        ; Advance tick counter
        inc byte [scr_matrix_tick]

        ; Draw all columns
        xor esi, esi            ; Column index
.mf_col_loop:
        cmp esi, SCR_MATRIX_COLS
        jge .mf_done

        ; Get this column's drop position
        movzx ebp, byte [scr_matrix_pos + esi]

        ; Advance drop position based on speed
        movzx eax, byte [scr_matrix_tick]
        movzx ebx, byte [scr_matrix_spd + esi]
        xor edx, edx
        div ebx
        test edx, edx
        jnz .mf_draw_col

        ; Time to advance this column
        inc byte [scr_matrix_pos + esi]
        cmp byte [scr_matrix_pos + esi], SCR_MATRIX_ROWS + 15
        jb .mf_draw_col
        ; Reset column
        mov byte [scr_matrix_pos + esi], 0
        ; Randomize speed
        mov eax, [scr_rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [scr_rng_state], eax
        mov edx, eax
        shr edx, 16
        and edx, 0x03
        inc edx
        mov [scr_matrix_spd + esi], dl

.mf_draw_col:
        ; Draw characters in this column
        movzx ebp, byte [scr_matrix_pos + esi]
        xor edi, edi            ; Row index
.mf_row_loop:
        cmp edi, SCR_MATRIX_ROWS
        jge .mf_next_col

        ; Calculate distance from head
        mov eax, ebp
        sub eax, edi
        cmp eax, 0
        jl .mf_next_row         ; Above head = dark
        cmp eax, 15
        jg .mf_next_row         ; Too far behind = dark

        ; Color: head is white, trail fades from bright to dark green
        cmp eax, 0
        jne .mf_trail
        mov edx, 0x00FFFFFF     ; White head
        jmp .mf_draw_char
.mf_trail:
        ; Green intensity: 255 - (distance * 16)
        shl eax, 4
        mov edx, 255
        sub edx, eax
        cmp edx, 30
        jl .mf_next_row
        shl edx, 8             ; Green channel
        ; Add slight blue tint
        mov eax, edx
        shr eax, 3
        or edx, eax

.mf_draw_char:
        ; Get character for this cell
        mov eax, edi
        imul eax, SCR_MATRIX_COLS
        add eax, esi
        movzx ecx, byte [scr_matrix_chr + eax]

        ; gui_bb_draw_char: EAX=x, EBX=y, CL=char, EDX=color
        ; X = col * 8, Y = row * 16
        push esi
        push edi
        mov eax, esi
        shl eax, 3             ; X = col * 8
        mov ebx, edi
        shl ebx, 4             ; Y = row * 16
        ; CL already has char, EDX already has color
        call gui_bb_draw_char
        pop edi
        pop esi

.mf_next_row:
        inc edi
        jmp .mf_row_loop

.mf_next_col:
        inc esi
        jmp .mf_col_loop

.mf_done:
        ; Randomly mutate some characters
        mov ecx, 20
.mf_mutate:
        mov eax, [scr_rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [scr_rng_state], eax
        mov edx, eax
        shr edx, 8
        and edx, 0x7FF         ; 0..2047
        cmp edx, SCR_MATRIX_COLS * SCR_MATRIX_ROWS
        jge .mf_mut_skip
        mov ebx, eax
        shr ebx, 20
        and ebx, 0x5E
        add ebx, 0x21
        mov [scr_matrix_chr + edx], bl
.mf_mut_skip:
        dec ecx
        jnz .mf_mutate

        popad
        ret

;=======================================================================
; SCREENSAVER 2: PIPES (Growing colored pipes)
;=======================================================================

scr_pipes_init:
        pushad
        ; Clear backbuffer to black for pipe accumulation
        mov edi, GUI_BACKBUF
        mov ecx, GUI_SCREEN_W * GUI_SCREEN_H
        xor eax, eax
        rep stosd

        mov eax, [tick_count]
        mov [scr_rng_state], eax

        ; Initialize pipes
        mov ecx, SCR_NUM_PIPES
        xor esi, esi
.pi_loop:
        ; Random start X
        mov eax, [scr_rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [scr_rng_state], eax
        mov edx, eax
        shr edx, 16
        and edx, 0x1FF         ; 0..511
        add edx, 64
        mov [scr_pipe_x + esi*4], edx

        ; Random start Y
        mov eax, [scr_rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [scr_rng_state], eax
        mov edx, eax
        shr edx, 16
        and edx, 0x1FF
        sub edx, 16
        add edx, 120
        mov [scr_pipe_y + esi*4], edx

        ; Random direction: 0=right, 1=down, 2=left, 3=up
        mov eax, [scr_rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [scr_rng_state], eax
        mov edx, eax
        shr edx, 16
        and edx, 3
        mov [scr_pipe_dir + esi], dl

        ; Random color
        mov eax, [scr_rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [scr_rng_state], eax
        mov edx, eax
        and edx, 0x00FFFFFF
        or edx, 0x00404040     ; Ensure minimum brightness
        mov [scr_pipe_color + esi*4], edx

        ; Segment counter
        mov byte [scr_pipe_seg + esi], 0

        inc esi
        dec ecx
        jnz .pi_loop

        mov dword [scr_pipe_frame], 0
        popad
        ret

scr_pipes_frame:
        pushad

        inc dword [scr_pipe_frame]

        ; Only advance every 2 frames
        test dword [scr_pipe_frame], 1
        jnz .pf_done

        mov ecx, SCR_NUM_PIPES
        xor esi, esi
.pf_pipe_loop:
        ; Get current position
        mov eax, [scr_pipe_x + esi*4]
        mov ebx, [scr_pipe_y + esi*4]

        ; Draw a 4x4 filled square at current position
        push esi
        push ecx
        mov ecx, 4             ; Width
        mov edx, 4             ; Height
        push dword [scr_pipe_color + esi*4]
        pop esi                 ; Color
        call gui_bb_fill_rect
        pop ecx
        pop esi

        ; Advance pipe position based on direction
        movzx edx, byte [scr_pipe_dir + esi]
        cmp edx, 0
        je .pf_right
        cmp edx, 1
        je .pf_down
        cmp edx, 2
        je .pf_left
        ; Up
        sub dword [scr_pipe_y + esi*4], 4
        jmp .pf_moved
.pf_right:
        add dword [scr_pipe_x + esi*4], 4
        jmp .pf_moved
.pf_down:
        add dword [scr_pipe_y + esi*4], 4
        jmp .pf_moved
.pf_left:
        sub dword [scr_pipe_x + esi*4], 4

.pf_moved:
        ; Increment segment count
        inc byte [scr_pipe_seg + esi]

        ; Random direction change every 8-20 segments
        movzx eax, byte [scr_pipe_seg + esi]
        cmp eax, 8
        jb .pf_no_turn

        ; Random chance to turn
        mov eax, [scr_rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [scr_rng_state], eax
        test eax, 0x07          ; ~12.5% chance per frame after 8 segments
        jnz .pf_no_turn

        ; Turn: pick new direction (perpendicular preferred)
        mov edx, eax
        shr edx, 8
        and edx, 3
        mov [scr_pipe_dir + esi], dl
        mov byte [scr_pipe_seg + esi], 0

.pf_no_turn:
        ; Bounds check: reset pipe if out of screen
        mov eax, [scr_pipe_x + esi*4]
        cmp eax, 0
        jl .pf_reset
        cmp eax, GUI_SCREEN_W - 4
        jg .pf_reset
        mov eax, [scr_pipe_y + esi*4]
        cmp eax, 0
        jl .pf_reset
        cmp eax, GUI_SCREEN_H - 4
        jg .pf_reset
        jmp .pf_next

.pf_reset:
        ; Respawn pipe at random position with new color
        mov eax, [scr_rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [scr_rng_state], eax
        mov edx, eax
        shr edx, 16
        and edx, 0x1FF
        add edx, 64
        mov [scr_pipe_x + esi*4], edx
        mov eax, [scr_rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [scr_rng_state], eax
        mov edx, eax
        shr edx, 16
        and edx, 0x1FF
        sub edx, 16
        add edx, 120
        mov [scr_pipe_y + esi*4], edx
        mov edx, eax
        and edx, 0x00FFFFFF
        or edx, 0x00404040
        mov [scr_pipe_color + esi*4], edx
        mov byte [scr_pipe_seg + esi], 0
        mov edx, eax
        shr edx, 4
        and edx, 3
        mov [scr_pipe_dir + esi], dl

.pf_next:
        inc esi
        dec ecx
        jnz .pf_pipe_loop

.pf_done:
        popad
        ret

;=======================================================================
; SCREENSAVER 3: BOUNCING LOGO
;=======================================================================

scr_bounce_init:
        pushad
        mov dword [scr_bounce_x], 200
        mov dword [scr_bounce_y], 180
        mov dword [scr_bounce_dx], 2
        mov dword [scr_bounce_dy], 1
        popad
        ret

scr_bounce_frame:
        pushad

        ; Move logo
        mov eax, [scr_bounce_x]
        add eax, [scr_bounce_dx]
        mov [scr_bounce_x], eax
        mov ebx, [scr_bounce_y]
        add ebx, [scr_bounce_dy]
        mov [scr_bounce_y], ebx

        ; Bounce X
        cmp eax, 0
        jle .bb_flip_dx
        cmp eax, 640 - 160     ; Logo is ~160px wide (20 chars * 8)
        jge .bb_flip_dx
        jmp .bb_check_y
.bb_flip_dx:
        neg dword [scr_bounce_dx]
        mov eax, [scr_bounce_x]
        add eax, [scr_bounce_dx]
        mov [scr_bounce_x], eax

.bb_check_y:
        cmp ebx, 0
        jle .bb_flip_dy
        cmp ebx, 480 - 48      ; Logo is ~48px tall (3 lines * 16)
        jge .bb_flip_dy
        jmp .bb_draw
.bb_flip_dy:
        neg dword [scr_bounce_dy]
        mov ebx, [scr_bounce_y]
        add ebx, [scr_bounce_dy]
        mov [scr_bounce_y], ebx

.bb_draw:
        ; Draw "MELLIVORA OS" text
        mov eax, [scr_bounce_x]
        mov ebx, [scr_bounce_y]
        mov esi, scr_bounce_str1
        mov edx, 0x00FF8800    ; Orange
        call gui_bb_draw_string

        mov eax, [scr_bounce_x]
        mov ebx, [scr_bounce_y]
        add ebx, 18
        mov esi, scr_bounce_str2
        mov edx, 0x00FFCC00    ; Gold
        call gui_bb_draw_string

        mov eax, [scr_bounce_x]
        mov ebx, [scr_bounce_y]
        add ebx, 36
        mov esi, scr_bounce_str3
        mov edx, 0x00666666    ; Gray
        call gui_bb_draw_string

        popad
        ret

;---------------------------------------
; Screensaver strings
;---------------------------------------
scr_bounce_str1: db "  MELLIVORA OS  ", 0
scr_bounce_str2: db " Honey Badger Edition ", 0
scr_bounce_str3: db "  v2.2.0  ", 0

;=======================================================================
; SCREENSAVER 4: PLASMA (Color-cycling plasma effect)
;=======================================================================
; Uses a simple integer approximation of sin-based plasma.
; Renders at reduced resolution (160x120) and scales 4x to 640x480.

SCR_PLASMA_W    equ 160
SCR_PLASMA_H    equ 120

scr_plasma_init:
        pushad
        mov dword [scr_plasma_time], 0

        ; Build approximate sine table (256 entries, values 0-255)
        ; Uses a simple quadratic approximation
        mov edi, scr_sine_tbl
        xor ecx, ecx           ; i = 0
.sine_loop:
        ; Approximate: sin(i * 2pi/256) mapped to 0..255
        ; Split into quadrants
        mov eax, ecx
        and eax, 0x3F          ; position within quadrant (0-63)
        test ecx, 0x40         ; 2nd/4th quadrant?
        jz .sine_rising
        mov ebx, 63
        sub ebx, eax
        mov eax, ebx
.sine_rising:
        ; eax = 0..63, map to 0..127 with smooth curve
        ; Use (x * (128 - x)) >> 5 for a smooth-ish curve
        mov ebx, 64
        sub ebx, eax           ; 64 - x
        imul eax, ebx          ; x * (64 - x), max=1024
        shr eax, 3             ; max ~128
        cmp eax, 128
        jbe .sine_cap
        mov eax, 128
.sine_cap:
        ; Map to 0..255 based on half of cycle
        test ecx, 0x80
        jz .sine_pos
        ; Negative half: 128 - val
        mov ebx, 128
        sub ebx, eax
        mov eax, ebx
        jmp .sine_store
.sine_pos:
        add eax, 128           ; Positive half: 128 + val
        cmp eax, 255
        jbe .sine_store
        mov eax, 255
.sine_store:
        mov [edi], al
        inc edi
        inc ecx
        cmp ecx, 256
        jb .sine_loop

        popad
        ret

;---------------------------------------
; scr_plasma_frame - Render one plasma frame
;---------------------------------------
scr_plasma_frame:
        pushad

        inc dword [scr_plasma_time]
        mov ebp, [scr_plasma_time]

        ; For each pixel at reduced resolution, compute color
        mov edi, GUI_BACKBUF
        xor ebx, ebx           ; y = 0

.pf_row:
        xor ecx, ecx           ; x = 0

.pf_col:
        ; Compute plasma value from multiple sine terms
        ; val = sin[x+t] + sin[y+t*2] + sin[(x+y)/2+t] + sin[sqrt_approx(x,y)+t*3]

        ; Term 1: sin[(x*3 + time) & 0xFF]
        mov eax, ecx
        lea eax, [eax + eax*2]  ; x*3
        add eax, ebp
        and eax, 0xFF
        movzx esi, byte [scr_sine_tbl + eax]

        ; Term 2: sin[(y*3 + time*2) & 0xFF]
        mov eax, ebx
        lea eax, [eax + eax*2]  ; y*3
        lea edx, [ebp + ebp]    ; time*2
        add eax, edx
        and eax, 0xFF
        movzx eax, byte [scr_sine_tbl + eax]
        add esi, eax

        ; Term 3: sin[((x+y)*2 + time) & 0xFF]
        mov eax, ecx
        add eax, ebx
        shl eax, 1
        add eax, ebp
        and eax, 0xFF
        movzx eax, byte [scr_sine_tbl + eax]
        add esi, eax

        ; Term 4: sin[(x*2 - y + time*3) & 0xFF]
        mov eax, ecx
        shl eax, 1
        sub eax, ebx
        lea edx, [ebp + ebp*2]  ; time*3
        add eax, edx
        and eax, 0xFF
        movzx eax, byte [scr_sine_tbl + eax]
        add esi, eax

        ; esi = sum of 4 terms (0..1020), divide by 4 -> 0..255
        shr esi, 2

        ; Map value to RGB color using smooth gradient
        ; R = sin[val], G = sin[val + 85], B = sin[val + 170]
        movzx eax, byte [scr_sine_tbl + esi]
        shr eax, 1              ; R (0-127 range, softer)
        mov edx, eax
        shl edx, 16             ; R in position

        lea eax, [esi + 85]
        and eax, 0xFF
        movzx eax, byte [scr_sine_tbl + eax]
        shr eax, 1
        shl eax, 8              ; G in position
        or edx, eax

        lea eax, [esi + 170]
        and eax, 0xFF
        movzx eax, byte [scr_sine_tbl + eax]
        shr eax, 1              ; B
        or edx, eax

        ; Write 4x4 block (scale 160x120 -> 640x480)
        push ebx
        push ecx
        push edi

        ; Calculate top-left framebuffer offset: (y*4)*640 + (x*4)
        mov eax, ebx
        shl eax, 2              ; y*4
        imul eax, GUI_SCREEN_W  ; * 640
        mov edi, ecx
        shl edi, 2              ; x*4
        add edi, eax
        shl edi, 2              ; * 4 bytes per pixel
        add edi, GUI_BACKBUF

        ; Write 4 rows of 4 pixels each
        mov eax, edx
        %assign _prow 0
        %rep 4
            mov [edi + _prow * GUI_SCREEN_W * 4], eax
            mov [edi + _prow * GUI_SCREEN_W * 4 + 4], eax
            mov [edi + _prow * GUI_SCREEN_W * 4 + 8], eax
            mov [edi + _prow * GUI_SCREEN_W * 4 + 12], eax
            %assign _prow _prow + 1
        %endrep

        pop edi
        pop ecx
        pop ebx

        inc ecx
        cmp ecx, SCR_PLASMA_W
        jb .pf_col

        inc ebx
        cmp ebx, SCR_PLASMA_H
        jb .pf_row

        popad
        ret

;---------------------------------------
; Plasma data
;---------------------------------------
scr_plasma_time: dd 0
scr_sine_tbl:    times 256 db 0
