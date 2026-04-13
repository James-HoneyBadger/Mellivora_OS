; bsettings.asm - Burrows Desktop Settings
; GUI applet for customizing desktop theme colors.

%include "syscalls.inc"
%include "lib/gui.inc"

NUM_COLORS      equ 6

start:
        ; Create window
        mov eax, 100
        mov ebx, 80
        mov ecx, 340
        mov edx, 280
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        ; Load current theme
        mov eax, [win_id]
        mov ebx, GUI_GET_THEME
        mov ecx, theme_buf
        mov eax, SYS_GUI
        int 0x80

        xor eax, eax
        mov [sel_color], eax
        mov [modified], al

.main_loop:
        call draw_settings

        mov eax, [win_id]
        call gui_compose

        mov eax, [win_id]
        call gui_flip

        mov eax, SYS_SLEEP
        mov ebx, 3
        int 0x80

        ; Poll events
        mov eax, [win_id]
        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        je .handle_key
        cmp eax, EVT_MOUSE_CLICK
        je .handle_click
        jmp .main_loop

.handle_key:
        cmp ebx, 27             ; ESC
        je .close
        cmp ebx, KEY_UP
        je .key_up
        cmp ebx, KEY_DOWN
        je .key_down
        cmp ebx, KEY_LEFT
        je .dec_red
        cmp ebx, KEY_RIGHT
        je .inc_red
        cmp ebx, 'r'
        je .inc_red
        cmp ebx, 'g'
        je .inc_green
        cmp ebx, 'b'
        je .inc_blue
        cmp ebx, 'R'
        je .dec_red
        cmp ebx, 'G'
        je .dec_green
        cmp ebx, 'B'
        je .dec_blue
        cmp ebx, 13             ; Enter = apply
        je .apply_theme
        jmp .main_loop

.key_up:
        cmp dword [sel_color], 0
        je .main_loop
        dec dword [sel_color]
        jmp .main_loop
.key_down:
        mov eax, [sel_color]
        inc eax
        cmp eax, NUM_COLORS
        jge .main_loop
        mov [sel_color], eax
        jmp .main_loop

.inc_red:
        call get_sel_ptr
        mov eax, [edi]
        add eax, 0x00040000     ; +4 to red channel
        and eax, 0x00FFFFFF
        mov [edi], eax
        mov byte [modified], 1
        jmp .main_loop
.dec_red:
        call get_sel_ptr
        mov eax, [edi]
        sub eax, 0x00040000
        and eax, 0x00FFFFFF
        mov [edi], eax
        mov byte [modified], 1
        jmp .main_loop
.inc_green:
        call get_sel_ptr
        mov eax, [edi]
        add eax, 0x00000400     ; +4 to green
        and eax, 0x00FFFFFF
        mov [edi], eax
        mov byte [modified], 1
        jmp .main_loop
.dec_green:
        call get_sel_ptr
        mov eax, [edi]
        sub eax, 0x00000400
        and eax, 0x00FFFFFF
        mov [edi], eax
        mov byte [modified], 1
        jmp .main_loop
.inc_blue:
        call get_sel_ptr
        mov eax, [edi]
        add eax, 0x00000004     ; +4 to blue
        and eax, 0x00FFFFFF
        mov [edi], eax
        mov byte [modified], 1
        jmp .main_loop
.dec_blue:
        call get_sel_ptr
        mov eax, [edi]
        sub eax, 0x00000004
        and eax, 0x00FFFFFF
        mov [edi], eax
        mov byte [modified], 1
        jmp .main_loop

.handle_click:
        ; EBX = rel x, ECX = rel y
        ; Check color list clicks (y: 40..40+6*30)
        cmp ecx, 40
        jl .check_buttons
        mov eax, ecx
        sub eax, 40
        xor edx, edx
        push ebx
        mov ebx, 30
        div ebx
        pop ebx
        cmp eax, NUM_COLORS
        jge .check_buttons
        mov [sel_color], eax
        jmp .main_loop

.check_buttons:
        ; Apply button: x=10..90, y=240..260
        cmp ecx, 240
        jl .main_loop
        cmp ecx, 260
        jg .main_loop
        cmp ebx, 10
        jl .check_reset
        cmp ebx, 90
        jg .check_reset
        jmp .apply_theme
.check_reset:
        ; Reset button: x=100..180
        cmp ebx, 100
        jl .main_loop
        cmp ebx, 180
        jg .main_loop
        jmp .reset_theme

.apply_theme:
        mov eax, SYS_GUI
        mov ebx, GUI_SET_THEME
        mov ecx, theme_buf
        int 0x80
        mov byte [modified], 0
        jmp .main_loop

.reset_theme:
        ; Load default colors
        mov esi, default_theme
        mov edi, theme_buf
        mov ecx, 48 / 4
        rep movsd
        mov byte [modified], 1
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; get_sel_ptr - Get pointer to selected color in theme_buf
; Returns: EDI = pointer
;---------------------------------------
get_sel_ptr:
        mov eax, [sel_color]
        shl eax, 2              ; *4 (dword per color)
        lea edi, [theme_buf + eax]
        ret

;---------------------------------------
; draw_settings - Render the settings window
;---------------------------------------
draw_settings:
        pushad
        mov eax, [win_id]

        ; Background
        mov ebx, 0
        mov ecx, 0
        mov edx, 340
        mov esi, 280
        mov edi, 0x00E8E8E8
        call gui_fill_rect

        ; Title label
        mov ebx, 10
        mov ecx, 8
        mov esi, hdr_str
        mov edi, 0x00000000
        call gui_draw_text

        ; Draw color entries
        xor ebp, ebp
.ds_entry:
        cmp ebp, NUM_COLORS
        jge .ds_buttons

        ; Highlight selected
        cmp ebp, [sel_color]
        jne .ds_no_hl
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, ebp
        imul ecx, 30
        add ecx, 38
        mov edx, 320
        mov esi, 28
        mov edi, 0x00C0D8FF
        call gui_fill_rect
.ds_no_hl:
        ; Color name
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, ebp
        imul ecx, 30
        add ecx, 44
        mov esi, ebp
        shl esi, 4              ; *16
        add esi, color_names
        mov edi, 0x00000000
        call gui_draw_text

        ; Color swatch (32x20)
        mov eax, [win_id]
        mov ebx, 160
        mov ecx, ebp
        imul ecx, 30
        add ecx, 40
        mov edx, 32
        mov esi, 20
        push ebp
        mov edi, ebp
        shl edi, 2
        mov edi, [theme_buf + edi]
        call gui_fill_rect
        pop ebp

        ; RGB value text
        push ebp
        mov eax, ebp
        shl eax, 2
        mov eax, [theme_buf + eax]
        call format_rgb
        mov eax, [win_id]
        mov ebx, 200
        mov ecx, ebp
        imul ecx, 30
        add ecx, 44
        mov esi, rgb_str_buf
        mov edi, 0x00404040
        call gui_draw_text
        pop ebp

        inc ebp
        jmp .ds_entry

.ds_buttons:
        ; Apply button
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, 240
        mov edx, 80
        mov esi, 20
        mov edi, 0x00C0C0C0
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 20
        mov ecx, 242
        mov esi, btn_apply
        mov edi, 0x00000000
        call gui_draw_text

        ; Reset button
        mov eax, [win_id]
        mov ebx, 100
        mov ecx, 240
        mov edx, 80
        mov esi, 20
        mov edi, 0x00C0C0C0
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 110
        mov ecx, 242
        mov esi, btn_reset
        mov edi, 0x00000000
        call gui_draw_text

        ; Instructions
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, 264
        mov esi, instr_str
        mov edi, 0x00808080
        call gui_draw_text

        popad
        ret

;---------------------------------------
; format_rgb - Format EAX as "RR GG BB" into rgb_str_buf
;---------------------------------------
format_rgb:
        push ebx
        push ecx
        push edx
        mov edx, eax
        ; Red
        mov eax, edx
        shr eax, 16
        and eax, 0xFF
        call hex_byte
        mov [rgb_str_buf], ah
        mov [rgb_str_buf + 1], al
        mov byte [rgb_str_buf + 2], ' '
        ; Green
        mov eax, edx
        shr eax, 8
        and eax, 0xFF
        call hex_byte
        mov [rgb_str_buf + 3], ah
        mov [rgb_str_buf + 4], al
        mov byte [rgb_str_buf + 5], ' '
        ; Blue
        mov eax, edx
        and eax, 0xFF
        call hex_byte
        mov [rgb_str_buf + 6], ah
        mov [rgb_str_buf + 7], al
        mov byte [rgb_str_buf + 8], 0
        pop edx
        pop ecx
        pop ebx
        ret

; hex_byte - Convert AL to 2 hex chars in AH:AL
hex_byte:
        push ecx
        mov cl, al
        shr al, 4
        call hex_nibble
        mov ah, al
        mov al, cl
        and al, 0x0F
        call hex_nibble
        pop ecx
        ret

hex_nibble:
        cmp al, 10
        jl .hn_digit
        add al, 'A' - 10
        ret
.hn_digit:
        add al, '0'
        ret

;=======================================
; Data
;=======================================

title_str:      db "Settings", 0
hdr_str:        db "Desktop Theme Colors", 0
btn_apply:      db " Apply", 0
btn_reset:      db " Reset", 0
instr_str:      db "r/g/b=inc R/G/B=dec Enter=apply", 0

color_names:
        db "Desktop BG", 0, 0, 0, 0, 0, 0   ; 16 bytes each
        db "Taskbar", 0, 0, 0, 0, 0, 0, 0, 0, 0
        db "Title Active", 0, 0, 0, 0
        db "Title Inactive", 0, 0
        db "Window BG", 0, 0, 0, 0, 0, 0, 0
        db "Text Color", 0, 0, 0, 0, 0, 0

; Default theme
default_theme:
        dd 0x003A6EA5           ; Desktop BG
        dd 0x00C0C0C0           ; Taskbar
        dd 0x000A246A           ; Title Active
        dd 0x00808080           ; Title Inactive
        dd 0x00FFFFFF           ; Window BG
        dd 0x00000000           ; Text Color

; BSS
win_id:         dd 0
sel_color:      dd 0
modified:       db 0
theme_buf:      times 48 db 0
rgb_str_buf:    times 12 db 0
