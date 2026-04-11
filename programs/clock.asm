; clock.asm - Analog ASCII Clock for Mellivora OS  
; Beautiful ticking clock rendered in text mode with second/minute/hour hands.
; Press any key to exit.
%include "syscalls.inc"

SCREEN_W        equ 80
SCREEN_H        equ 25
CENTER_X        equ 40
CENTER_Y        equ 12
RADIUS          equ 10
TICK_DELAY      equ 50          ; 0.5s refresh

start:
;=== Main loop ===
.main_loop:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        call draw_clock

        mov eax, SYS_SLEEP
        mov ebx, TICK_DELAY
        int 0x80
        jmp .main_loop

.exit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================
; draw_clock
;=======================================
draw_clock:
        pushad

        ; Clear screen (black)
        mov edi, VGA_BASE
        mov eax, 0x00200020
        mov ecx, SCREEN_W * SCREEN_H / 2
        rep stosd

        ; Get current time in ticks, derive H:M:S
        mov eax, SYS_GETTIME
        int 0x80
        xor edx, edx
        mov ecx, 100
        div ecx                 ; EAX = total seconds
        mov [total_secs], eax

        ; Seconds
        xor edx, edx
        mov ecx, 60
        div ecx
        mov [cur_sec], edx

        ; Minutes
        xor edx, edx
        div ecx
        mov [cur_min], edx

        ; Hours (mod 12)
        xor edx, edx
        mov ecx, 12
        div ecx
        mov [cur_hour], edx

        ; Draw clock face circle using character art
        ; Use 12-point markers
        mov ecx, 0              ; angle index (0-59 for 60 positions)
.face_loop:
        cmp ecx, 60
        jge .face_done

        ; Is this a major tick (every 5)?
        push ecx
        mov eax, ecx
        xor edx, edx
        push ecx
        mov ecx, 5
        div ecx
        pop ecx
        test edx, edx
        pop ecx
        jnz .face_minor

        ; Major tick (hour markers 12,1,2,...,11)
        push ecx
        call get_circle_pos     ; ECX=angle(0-59), RADIUS -> (EAX=x, EDX=y)
        cmp eax, 0
        jl .face_skip_draw
        cmp eax, SCREEN_W
        jge .face_skip_draw
        cmp edx, 0
        jl .face_skip_draw
        cmp edx, SCREEN_H
        jge .face_skip_draw

        ; Determine hour number
        pop ecx
        push ecx
        mov eax, ecx
        xor edx, edx
        push ecx
        mov ecx, 5
        div ecx
        pop ecx
        ; EAX = 0-11 (0=12 o'clock)
        test eax, eax
        jnz .not_twelve
        mov eax, 12
.not_twelve:
        push eax               ; hour number

        ; Recalculate position
        call get_circle_pos
        mov ebx, eax            ; x
        mov ecx, edx            ; y

        pop eax                 ; hour number
        ; Write hour digit(s)
        cmp eax, 10
        jl .single_digit
        ; Two digits: write "1" then digit
        push eax
        mov al, '1'
        mov ah, 0x0E            ; Yellow
        push ecx
        imul ecx, SCREEN_W
        add ecx, ebx
        dec ecx                 ; one left
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop ecx
        pop eax
        sub al, 10
        add al, '0'
        mov ah, 0x0E
        push ecx
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop ecx
        jmp .face_next

.single_digit:
        add al, '0'
        mov ah, 0x0E
        push ecx
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop ecx
        jmp .face_next

.face_minor:
        ; Minor tick marker
        push ecx
        call get_circle_pos
        cmp eax, 0
        jl .face_skip_draw
        cmp eax, SCREEN_W
        jge .face_skip_draw
        cmp edx, 0
        jl .face_skip_draw
        cmp edx, SCREEN_H
        jge .face_skip_draw
        mov ebx, eax
        mov ecx, edx
        mov al, 0xFA            ; middle dot
        mov ah, 0x08            ; dark gray
        push ecx
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop ecx
        pop ecx
        jmp .face_next

.face_skip_draw:
        pop ecx
.face_next:
        inc ecx
        jmp .face_loop
.face_done:

        ; Draw center dot
        mov al, 'o'
        mov ah, 0x0F
        mov ecx, CENTER_Y * SCREEN_W + CENTER_X
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax

        ; Draw hour hand (short, thick)
        mov eax, [cur_hour]
        imul eax, 5             ; convert to 60-pos
        ; Add minute offset
        mov ecx, [cur_min]
        xor edx, edx
        push eax
        mov eax, ecx
        mov ecx, 12
        xor edx, edx
        div ecx
        mov ecx, eax
        pop eax
        add eax, ecx           ; hour position in 60-scale
        mov ecx, eax
        mov eax, 5              ; hand length
        mov dl, 0xDB            ; full block
        mov dh, 0x0C            ; light red
        call draw_hand

        ; Draw minute hand (medium)
        mov ecx, [cur_min]
        mov eax, 8
        mov dl, 0xDB
        mov dh, 0x0B            ; light cyan
        call draw_hand

        ; Draw second hand (long, thin)
        mov ecx, [cur_sec]
        mov eax, RADIUS - 1
        mov dl, 0xB3            ; thin vertical line
        mov dh, 0x0A            ; light green
        call draw_hand

        ; Display digital time below
        mov eax, SYS_SETCURSOR
        mov ebx, 33
        mov ecx, 24
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80

        ; Hours (12-hr format)
        mov eax, [cur_hour]
        test eax, eax
        jnz .hour_ok
        mov eax, 12
.hour_ok:
        call print_2digit
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
        mov eax, [cur_min]
        call print_2digit
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
        mov eax, [cur_sec]
        call print_2digit

        ; Title at top
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 0
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, title_str
        int 0x80

        popad
        ret

;---------------------------------------
; draw_hand: ECX=angle(0-59), EAX=length, DL=char, DH=color
; Draws from center along direction
;---------------------------------------
draw_hand:
        pushad
        mov [hand_len], eax
        mov [hand_char], dl
        mov [hand_color], dh
        mov [hand_angle], ecx

        mov esi, 1              ; distance from center
.dh_loop:
        cmp esi, [hand_len]
        jg .dh_done

        ; Get position at this distance
        push ecx
        mov ecx, [hand_angle]
        call get_hand_pos       ; ECX=angle, ESI=distance -> (EAX=x, EDX=y)
        pop ecx

        ; Bounds check
        cmp eax, 0
        jl .dh_next
        cmp eax, SCREEN_W
        jge .dh_next
        cmp edx, 0
        jl .dh_next
        cmp edx, SCREEN_H
        jge .dh_next

        ; Write to VGA
        push ecx
        mov ecx, edx
        imul ecx, SCREEN_W
        add ecx, eax
        shl ecx, 1
        add ecx, VGA_BASE
        mov al, [hand_char]
        mov ah, [hand_color]
        mov [ecx], ax
        pop ecx

.dh_next:
        inc esi
        jmp .dh_loop
.dh_done:
        popad
        ret

;---------------------------------------
; get_circle_pos: ECX=angle(0-59) -> EAX=x, EDX=y (at RADIUS from center)
;---------------------------------------
get_circle_pos:
        push ebx
        push ecx

        ; Use lookup table for sin/cos approximation
        ; angle 0 = 12 o'clock (top), goes clockwise
        ; x = center_x + sin(angle) * radius * 2 (double for aspect ratio)
        ; y = center_y - cos(angle) * radius

        ; Get sin/cos from table (scaled by 100)
        cmp ecx, 60
        jl .gcp_ok
        sub ecx, 60
.gcp_ok:
        movsx eax, word [sin_table + ecx*2]   ; sin * 100
        imul eax, RADIUS
        imul eax, 2            ; aspect ratio correction
        cdq
        mov ebx, 100
        idiv ebx
        add eax, CENTER_X
        push eax

        movsx eax, word [cos_table + ecx*2]
        imul eax, RADIUS
        cdq
        mov ebx, 100
        idiv ebx
        mov edx, CENTER_Y
        sub edx, eax

        pop eax
        pop ecx
        pop ebx
        ret

;---------------------------------------
; get_hand_pos: ECX=angle(0-59), ESI=distance -> (EAX=x, EDX=y)
;---------------------------------------
get_hand_pos:
        push ebx
        push ecx

        cmp ecx, 60
        jl .ghp_ok
        sub ecx, 60
.ghp_ok:
        movsx eax, word [sin_table + ecx*2]
        imul eax, esi
        imul eax, 2
        cdq
        mov ebx, 100
        idiv ebx
        add eax, CENTER_X
        push eax

        movsx eax, word [cos_table + ecx*2]
        imul eax, esi
        cdq
        mov ebx, 100
        idiv ebx
        mov edx, CENTER_Y
        sub edx, eax

        pop eax
        pop ecx
        pop ebx
        ret

;---------------------------------------
; print_2digit: print EAX as 2-digit number
;---------------------------------------
print_2digit:
        pushad
        xor edx, edx
        mov ecx, 10
        div ecx
        ; EAX=tens, EDX=ones
        add eax, '0'
        push edx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop edx
        add edx, '0'
        mov ebx, edx
        mov eax, SYS_PUTCHAR
        int 0x80
        popad
        ret

; === Data ===
title_str:      db "Mellivora OS - Analog Clock", 0

; Sin table for 60 positions (0=12 o'clock going clockwise), values *100
; sin(angle) where angle = position * 6 degrees
sin_table:
        dw   0,  10,  21,  31,  41,  50,  59,  67,  74,  81  ; 0-9
        dw  87,  91,  95,  98,  99, 100,  99,  98,  95,  91  ; 10-19
        dw  87,  81,  74,  67,  59,  50,  41,  31,  21,  10  ; 20-29
        dw   0, -10, -21, -31, -41, -50, -59, -67, -74, -81  ; 30-39
        dw -87, -91, -95, -98, -99,-100, -99, -98, -95, -91  ; 40-49
        dw -87, -81, -74, -67, -59, -50, -41, -31, -21, -10  ; 50-59

; Cos table (cos = sin shifted by 15 positions)
cos_table:
        dw 100,  99,  98,  95,  91,  87,  81,  74,  67,  59  ; 0-9
        dw  50,  41,  31,  21,  10,   0, -10, -21, -31, -41  ; 10-19
        dw -50, -59, -67, -74, -81, -87, -91, -95, -98, -99  ; 20-29
        dw-100, -99, -98, -95, -91, -87, -81, -74, -67, -59  ; 30-39
        dw -50, -41, -31, -21, -10,   0,  10,  21,  31,  41  ; 40-49
        dw  50,  59,  67,  74,  81,  87,  91,  95,  98,  99  ; 50-59

; BSS
total_secs:     dd 0
cur_hour:       dd 0
cur_min:        dd 0
cur_sec:        dd 0
hand_len:       dd 0
hand_angle:     dd 0
hand_char:      db 0
hand_color:     db 0
