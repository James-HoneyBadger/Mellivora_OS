; bpaint.asm - Burrows Paint
; Freehand mouse drawing with color selection palette.

%include "syscalls.inc"
%include "lib/gui.inc"

WIN_W           equ 420
WIN_H           equ 320
PAL_Y           equ 284
PAL_H           equ 36
BRUSH_SIZE      equ 3
MAX_STROKES     equ 2048
STROKE_BYTES    equ 8           ; word x + word y + dword color
NUM_COLORS      equ 10
SWATCH_W        equ 30
SWATCH_GAP      equ 4
SWATCH_STEP     equ SWATCH_W + SWATCH_GAP

start:
        mov eax, 100
        mov ebx, 60
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        mov dword [cur_color], 0x00000000
        mov dword [stroke_count], 0
        mov byte [drawing], 0

.main_loop:
        call gui_compose
        call render_canvas
        call render_palette
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        je .on_key
        cmp eax, EVT_MOUSE_CLICK
        je .on_click
        cmp eax, EVT_MOUSE_MOVE
        je .on_move
        jmp .main_loop

; ---- Key handling ----
.on_key:
        cmp bl, 27
        je .close
        cmp bl, 'c'
        je .clear
        cmp bl, 'C'
        je .clear
        cmp bl, '1'
        jb .main_loop
        cmp bl, '9'
        ja .main_loop
        movzx eax, bl
        sub eax, '1'
        mov eax, [palette + eax * 4]
        mov [cur_color], eax
        jmp .main_loop

.clear:
        mov dword [stroke_count], 0
        jmp .main_loop

; ---- Mouse click handling ----
.on_click:
        ; EBX = rel_x, ECX = rel_y (window-relative)
        ; Check if left button is currently pressed via SYS_MOUSE
        push ebx
        push ecx
        mov eax, SYS_MOUSE
        int 0x80
        test ecx, 1
        pop ecx
        pop ebx
        jz .btn_release

        ; Button pressed
        cmp ecx, PAL_Y
        jge .palette_hit
        ; Canvas area: start drawing
        mov byte [drawing], 1
        call add_stroke
        jmp .main_loop

.btn_release:
        mov byte [drawing], 0
        jmp .main_loop

.palette_hit:
        ; Determine swatch index from x position
        sub ebx, 6
        cmp ebx, 0
        jl .main_loop
        xor edx, edx
        mov eax, ebx
        mov ecx, SWATCH_STEP
        div ecx
        cmp eax, NUM_COLORS
        jge .main_loop
        mov eax, [palette + eax * 4]
        mov [cur_color], eax
        jmp .main_loop

; ---- Mouse move handling ----
.on_move:
        cmp byte [drawing], 0
        je .main_loop
        ; Verify button still held
        push ebx
        push ecx
        mov eax, SYS_MOUSE
        int 0x80
        test ecx, 1
        pop ecx
        pop ebx
        jz .drag_end
        ; Bounds check: only draw in canvas area
        cmp ebx, 0
        jl .main_loop
        cmp ecx, 0
        jl .main_loop
        cmp ebx, WIN_W
        jge .main_loop
        cmp ecx, PAL_Y
        jge .main_loop
        call add_stroke
        jmp .main_loop
.drag_end:
        mov byte [drawing], 0
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================
; add_stroke - Record a brush stroke
; EBX = x, ECX = y (window-relative)
;=======================================
add_stroke:
        pushad
        mov eax, [stroke_count]
        cmp eax, MAX_STROKES
        jge .as_done
        shl eax, 3
        lea edi, [stroke_buf + eax]
        mov [edi], bx
        mov [edi + 2], cx
        mov edx, [cur_color]
        mov [edi + 4], edx
        inc dword [stroke_count]
.as_done:
        popad
        ret

;=======================================
; render_canvas - Draw white bg + replay strokes
;=======================================
render_canvas:
        pushad
        ; White canvas background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, PAL_Y
        mov edi, 0x00FFFFFF
        call gui_fill_rect

        ; Replay each stroke as a BRUSH_SIZE x BRUSH_SIZE filled rect
        xor ebp, ebp
.rc_loop:
        cmp ebp, [stroke_count]
        jge .rc_done

        lea esi, [stroke_buf + ebp * 8]
        movzx ebx, word [esi]
        movzx ecx, word [esi + 2]
        mov edi, [esi + 4]

        ; Center the brush
        sub ebx, BRUSH_SIZE / 2
        sub ecx, BRUSH_SIZE / 2
        cmp ebx, 0
        jge .rc_xok
        xor ebx, ebx
.rc_xok:
        cmp ecx, 0
        jge .rc_yok
        xor ecx, ecx
.rc_yok:
        mov eax, [win_id]
        mov edx, BRUSH_SIZE
        mov esi, BRUSH_SIZE
        call gui_fill_rect

        inc ebp
        jmp .rc_loop
.rc_done:
        ; Show "FULL" if buffer is maxed
        mov eax, [stroke_count]
        cmp eax, MAX_STROKES
        jl .rc_end
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, 4
        mov esi, full_str
        mov edi, 0x00FF0000
        call gui_draw_text
.rc_end:
        popad
        ret

;=======================================
; render_palette - Draw color swatches
;=======================================
render_palette:
        pushad
        ; Palette background
        mov eax, [win_id]
        xor ebx, ebx
        mov ecx, PAL_Y
        mov edx, WIN_W
        mov esi, PAL_H
        mov edi, 0x00D8D8D8
        call gui_fill_rect

        ; Draw each swatch
        xor ebp, ebp
.rp_loop:
        cmp ebp, NUM_COLORS
        jge .rp_hint

        ; Check if this is the current color (draw highlight)
        mov eax, [palette + ebp * 4]
        cmp eax, [cur_color]
        jne .rp_no_hl
        mov eax, [win_id]
        imul ebx, ebp, SWATCH_STEP
        add ebx, 4
        mov ecx, PAL_Y + 2
        mov edx, SWATCH_W + 4
        mov esi, SWATCH_W + 2
        mov edi, 0x00FF4400
        call gui_fill_rect
.rp_no_hl:
        ; Swatch rectangle
        mov eax, [win_id]
        imul ebx, ebp, SWATCH_STEP
        add ebx, 6
        mov ecx, PAL_Y + 4
        mov edx, SWATCH_W
        mov esi, SWATCH_W - 4
        mov edi, [palette + ebp * 4]
        call gui_fill_rect

        inc ebp
        jmp .rp_loop

.rp_hint:
        ; Key hints
        mov eax, [win_id]
        mov ebx, 348
        mov ecx, PAL_Y + 4
        mov esi, hint1_str
        mov edi, 0x00505050
        call gui_draw_text
        mov eax, [win_id]
        mov ebx, 348
        mov ecx, PAL_Y + 20
        mov esi, hint2_str
        mov edi, 0x00505050
        call gui_draw_text

        popad
        ret

; ---- Data ----
title_str:      db "Paint", 0
full_str:       db "FULL!", 0
hint1_str:      db "1-9:Col", 0
hint2_str:      db "C:Clr", 0

palette:
        dd 0x00000000           ; 1: black
        dd 0x00FF0000           ; 2: red
        dd 0x0000CC00           ; 3: green
        dd 0x000000FF           ; 4: blue
        dd 0x00FFFF00           ; 5: yellow
        dd 0x00FF8800           ; 6: orange
        dd 0x00CC00CC           ; 7: magenta
        dd 0x0000CCCC           ; 8: cyan
        dd 0x00888888           ; 9: gray
        dd 0x00FFFFFF           ; (white, click only)

win_id:         dd 0
cur_color:      dd 0
stroke_count:   dd 0
drawing:        db 0

stroke_buf:     times MAX_STROKES * STROKE_BYTES db 0