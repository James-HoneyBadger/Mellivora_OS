; bcalc.asm - Burrows Calculator
; Clickable GUI calculator with basic operations.

%include "syscalls.inc"
%include "lib/gui.inc"

BTN_W   equ 56
BTN_H   equ 36
BTN_PAD equ 4
COLS    equ 4
ROWS    equ 5

start:
        mov eax, 200
        mov ebx, 80
        mov ecx, 248
        mov edx, 300
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        ; Init state
        mov dword [accumulator], 0
        mov dword [current], 0
        mov byte [op], 0
        mov byte [fresh], 1
        mov dword [disp_len], 1
        mov byte [display], '0'
        mov byte [display+1], 0

.main_loop:
        call gui_compose
        call draw_calc
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        jne .check_mouse
        ; Keyboard input
        cmp bl, 27
        je .close
        cmp bl, '0'
        jl .check_op_key
        cmp bl, '9'
        jle .digit_key
.check_op_key:
        cmp bl, '+'
        je .op_add
        cmp bl, '-'
        je .op_sub
        cmp bl, '*'
        je .op_mul
        cmp bl, '/'
        je .op_div
        cmp bl, 13             ; Enter = equals
        je .op_eq
        cmp bl, '='
        je .op_eq
        cmp bl, 'c'
        je .op_clear
        cmp bl, 'C'
        je .op_clear
        jmp .main_loop

.check_mouse:
        cmp eax, EVT_MOUSE_CLICK
        jne .main_loop
        ; EBX = x, ECX = y relative to window
        ; Check which button was clicked
        sub ecx, 60            ; buttons start at y=60
        cmp ecx, 0
        jl .main_loop
        sub ebx, 8             ; left margin
        cmp ebx, 0
        jl .main_loop

        ; Calculate row/col
        push ecx
        push ebx
        xor edx, edx
        mov eax, ecx
        mov ecx, BTN_H + BTN_PAD
        div ecx
        mov [click_row], eax
        pop ebx
        xor edx, edx
        mov eax, ebx
        mov ecx, BTN_W + BTN_PAD
        div ecx
        mov [click_col], eax
        pop ecx

        ; Validate
        cmp dword [click_row], ROWS
        jge .main_loop
        cmp dword [click_col], COLS
        jge .main_loop

        ; Map button: row*4 + col -> button index
        mov eax, [click_row]
        shl eax, 2
        add eax, [click_col]
        ; Button layout:
        ; 0:7  1:8  2:9  3:/
        ; 4:4  5:5  6:6  7:*
        ; 8:1  9:2  10:3 11:-
        ; 12:0 13:C 14:= 15:+
        ; 16:. 17:( 18:) 19:CE (5th row unused, map to nop)
        cmp eax, 0
        je .d7
        cmp eax, 1
        je .d8
        cmp eax, 2
        je .d9
        cmp eax, 3
        je .op_div
        cmp eax, 4
        je .d4
        cmp eax, 5
        je .d5
        cmp eax, 6
        je .d6
        cmp eax, 7
        je .op_mul
        cmp eax, 8
        je .d1
        cmp eax, 9
        je .d2
        cmp eax, 10
        je .d3
        cmp eax, 11
        je .op_sub
        cmp eax, 12
        je .d0
        cmp eax, 13
        je .op_clear
        cmp eax, 14
        je .op_eq
        cmp eax, 15
        je .op_add
        jmp .main_loop

.d0:    mov bl, '0'
        jmp .digit_key
.d1:    mov bl, '1'
        jmp .digit_key
.d2:    mov bl, '2'
        jmp .digit_key
.d3:    mov bl, '3'
        jmp .digit_key
.d4:    mov bl, '4'
        jmp .digit_key
.d5:    mov bl, '5'
        jmp .digit_key
.d6:    mov bl, '6'
        jmp .digit_key
.d7:    mov bl, '7'
        jmp .digit_key
.d8:    mov bl, '8'
        jmp .digit_key
.d9:    mov bl, '9'
        jmp .digit_key

.digit_key:
        ; If fresh, reset current
        cmp byte [fresh], 1
        jne .dk_append
        mov dword [current], 0
        mov byte [fresh], 0
.dk_append:
        mov eax, [current]
        imul eax, 10
        movzx ebx, bl
        sub ebx, '0'
        add eax, ebx
        mov [current], eax
        call update_display
        jmp .main_loop

.op_add:
        call do_pending_op
        mov byte [op], '+'
        mov byte [fresh], 1
        jmp .main_loop
.op_sub:
        call do_pending_op
        mov byte [op], '-'
        mov byte [fresh], 1
        jmp .main_loop
.op_mul:
        call do_pending_op
        mov byte [op], '*'
        mov byte [fresh], 1
        jmp .main_loop
.op_div:
        call do_pending_op
        mov byte [op], '/'
        mov byte [fresh], 1
        jmp .main_loop

.op_eq:
        call do_pending_op
        mov byte [op], 0
        mov byte [fresh], 1
        jmp .main_loop

.op_clear:
        mov dword [accumulator], 0
        mov dword [current], 0
        mov byte [op], 0
        mov byte [fresh], 1
        call update_display
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; do_pending_op
;---------------------------------------
do_pending_op:
        pushad
        cmp byte [op], 0
        je .dpo_first
        cmp byte [op], '+'
        je .dpo_add
        cmp byte [op], '-'
        je .dpo_sub
        cmp byte [op], '*'
        je .dpo_mul
        cmp byte [op], '/'
        je .dpo_div
        jmp .dpo_done
.dpo_first:
        mov eax, [current]
        mov [accumulator], eax
        jmp .dpo_done
.dpo_add:
        mov eax, [accumulator]
        add eax, [current]
        mov [accumulator], eax
        mov [current], eax
        jmp .dpo_done
.dpo_sub:
        mov eax, [accumulator]
        sub eax, [current]
        mov [accumulator], eax
        mov [current], eax
        jmp .dpo_done
.dpo_mul:
        mov eax, [accumulator]
        imul eax, [current]
        mov [accumulator], eax
        mov [current], eax
        jmp .dpo_done
.dpo_div:
        cmp dword [current], 0
        je .dpo_done
        mov eax, [accumulator]
        cdq
        idiv dword [current]
        mov [accumulator], eax
        mov [current], eax
.dpo_done:
        call update_display
        popad
        ret

;---------------------------------------
; update_display - Convert current to string
;---------------------------------------
update_display:
        pushad
        mov eax, [current]
        mov edi, display
        ; Handle negative
        test eax, eax
        jns .ud_pos
        neg eax
        mov byte [edi], '-'
        inc edi
.ud_pos:
        ; Convert to decimal
        xor ecx, ecx
        mov ebx, 10
.ud_push:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        test eax, eax
        jnz .ud_push
.ud_pop:
        pop edx
        add dl, '0'
        mov [edi], dl
        inc edi
        dec ecx
        jnz .ud_pop
        mov byte [edi], 0
        ; Calculate display length
        mov eax, edi
        sub eax, display
        mov [disp_len], eax
        popad
        ret

;---------------------------------------
; draw_calc
;---------------------------------------
draw_calc:
        pushad
        ; Background
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 0
        mov edx, 248
        mov esi, 300
        mov edi, 0x00F0F0F0
        call gui_fill_rect

        ; Display area
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 8
        mov edx, 232
        mov esi, 40
        mov edi, 0x00FFFFFF
        call gui_fill_rect

        ; Display border
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 8
        mov edx, 232
        mov esi, 1
        mov edi, 0x00A0A0A0
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 47
        mov edx, 232
        mov esi, 1
        mov edi, 0x00A0A0A0
        call gui_fill_rect

        ; Display text (right-aligned)
        mov eax, [disp_len]
        shl eax, 3             ; * 8
        mov ebx, 232
        sub ebx, eax
        mov eax, [win_id]
        mov ecx, 20
        mov esi, display
        mov edi, 0x00000000
        call gui_draw_text

        ; Draw buttons
        xor ecx, ecx           ; button index
.draw_btn:
        cmp ecx, 16
        jge .draw_end

        ; Calculate position
        push ecx
        mov eax, ecx
        shr eax, 2             ; row
        and ecx, 3             ; col

        push eax
        ; X = 8 + col * (BTN_W + BTN_PAD)
        imul ecx, BTN_W + BTN_PAD
        add ecx, 8
        ; Y = 60 + row * (BTN_H + BTN_PAD)
        imul eax, BTN_H + BTN_PAD
        add eax, 60

        ; Draw button bg
        push ecx
        push eax
        mov eax, [win_id]
        mov ebx, ecx
        pop ecx
        push ecx
        mov edx, BTN_W
        mov esi, BTN_H
        ; Color depends on button type
        pop ecx
        pop ebx
        push ebx
        push ecx
        mov eax, [esp + 12]    ; button index
        cmp eax, 3
        je .btn_op_color
        cmp eax, 7
        je .btn_op_color
        cmp eax, 11
        je .btn_op_color
        cmp eax, 15
        je .btn_op_color
        cmp eax, 14
        je .btn_eq_color
        mov edi, 0x00FFFFFF    ; number button
        jmp .btn_draw
.btn_op_color:
        mov edi, 0x00FFB020    ; orange operator
        jmp .btn_draw
.btn_eq_color:
        mov edi, 0x003060A0    ; blue equals
.btn_draw:
        pop ecx
        pop ebx
        push ebx
        push ecx
        mov eax, [win_id]
        mov edx, BTN_W
        mov esi, BTN_H
        call gui_fill_rect

        ; Draw button label
        pop ecx
        pop eax
        push eax
        push ecx
        add ecx, 4             ; adjust for text offset
        mov eax, [win_id]
        add ebx, 20
        ; Get label
        mov edx, [esp + 12]    ; button index
        movzx edx, byte [btn_labels + edx]
        mov [tmp_char], dl
        mov byte [tmp_char+1], 0
        mov esi, tmp_char
        mov edi, 0x00000000
        cmp edx, '='
        jne .btn_txt
        mov edi, 0x00FFFFFF
.btn_txt:
        call gui_draw_text

        pop ecx
        pop eax
        pop eax              ; discard saved row
        pop ecx              ; restore button index

        inc ecx
        jmp .draw_btn

.draw_end:
        popad
        ret

click_row: dd 0
click_col: dd 0
tmp_char:  db 0, 0

; Button labels (row-major: 7 8 9 / 4 5 6 * 1 2 3 - 0 C = +)
btn_labels: db "789/456*123-0C=+"

; Data
title_str:   db "Calculator", 0
display:     times 16 db 0
disp_len:    dd 1
win_id:      dd 0
accumulator: dd 0
current:     dd 0
op:          db 0
fresh:       db 1
