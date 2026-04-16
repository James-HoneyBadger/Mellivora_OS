; timewarp.asm - Time Warp for Mellivora
; TempleCode IDE: editor, interpreter (BASIC + PILOT + Logo), turtle graphics
; Based on Time Warp II by James-HoneyBadger
%include "syscalls.inc"
%include "lib/gui.inc"

;=======================================================================
; Constants
;=======================================================================
WIN_X           equ 20
WIN_Y           equ 5
WIN_W           equ 596
WIN_H           equ 420

; Editor panel (left side, top)
ED_X            equ 0
ED_Y            equ 24
ED_W            equ 300
ED_H            equ 240

; Canvas panel (right side, top) - turtle graphics
CV_X            equ 302
CV_Y            equ 24
CV_W            equ 294
CV_H            equ 240

; Output panel (bottom)
OUT_X           equ 0
OUT_Y           equ 266
OUT_W           equ 596
OUT_H           equ 134

; Toolbar
TB_Y            equ 0
TB_H            equ 24

; Status bar
SB_Y            equ 400
SB_H            equ 20

; Editor settings
MAX_LINES       equ 200
LINE_LEN        equ 64
VIS_LINES       equ 14          ; lines visible in editor
ED_COLS         equ 36          ; chars visible per line

; Output settings
OUT_LINES       equ 8           ; visible output lines
OUT_COLS        equ 72          ; chars per output line
OUT_LINE_LEN    equ 74

; Interpreter settings
VAR_COUNT       equ 26          ; A-Z
MAX_LABELS      equ 32
LABEL_NAME_LEN  equ 16
GOSUB_DEPTH     equ 16
FOR_DEPTH       equ 8
MAX_ITER        equ 50000

; Turtle settings
TURTLE_CX       equ 147         ; center X of canvas (CV_W/2)
TURTLE_CY       equ 120         ; center Y of canvas (CV_H/2)

; Colors
COL_BG          equ 0x002B2D30  ; editor dark bg
COL_CANVAS_BG   equ 0x00FFFFFF  ; canvas white bg
COL_OUTPUT_BG   equ 0x001E1E2E  ; output dark bg
COL_TOOLBAR_BG  equ 0x003C3F41  ; toolbar bg
COL_STATUS_BG   equ 0x003C3F41  ; status bar bg
COL_TEXT         equ 0x00D4D4D4  ; light gray text
COL_KEYWORD      equ 0x00569CD6  ; blue keywords
COL_CURSOR       equ 0x00FFFFFF  ; white cursor
COL_OUTPUT_TEXT  equ 0x0000CC88  ; green output
COL_BTN          equ 0x00505355  ; button bg
COL_BTN_RUN      equ 0x00388E3C  ; green run button
COL_BTN_STOP     equ 0x00C62828  ; red stop button
COL_LINE_NUM     equ 0x00858585  ; line number gray
COL_SEPARATOR    equ 0x00555555  ; separator line
COL_CANVAS_GRID  equ 0x00F0F0F0  ; canvas grid color
COL_TURTLE       equ 0x0000AA00  ; turtle green

; Sin/cos lookup table (scaled by 1024, every 1 degree, 0-90)
; We store 91 entries for quadrant 1, derive rest
TRIG_SCALE      equ 1024

start:
        ; Check for filename argument
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        mov [has_arg], al

        ; Create window
        mov eax, WIN_X
        mov ebx, WIN_Y
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        ; Initialize state
        call init_editor
        call init_interpreter

        ; Load file if argument given
        cmp byte [has_arg], 0
        je .main_loop
        call load_file

.main_loop:
        call gui_compose
        call draw_all
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        je .handle_key
        cmp eax, EVT_MOUSE_CLICK
        je .handle_click
        jmp .main_loop

.handle_key:
        mov eax, ebx
        call handle_keypress
        jmp .main_loop

.handle_click:
        ; EBX = x|y packed: x in high 16, y in low 16
        mov eax, ebx
        shr eax, 16                 ; x
        mov edx, ebx
        and edx, 0xFFFF             ; y
        call handle_mouse_click
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; INITIALIZATION
;=======================================================================
init_editor:
        ; Clear text buffer
        mov edi, text_buf
        mov ecx, MAX_LINES * LINE_LEN
        xor al, al
        rep stosb
        ; Set up initial state
        mov dword [cur_line], 0
        mov dword [cur_col], 0
        mov dword [scroll_y], 0
        mov dword [num_lines], 1
        ret

init_interpreter:
        ; Clear variables
        mov edi, variables
        mov ecx, VAR_COUNT
        xor eax, eax
        rep stosd
        ; Clear labels
        mov edi, labels
        mov ecx, MAX_LABELS * (LABEL_NAME_LEN + 4)
        xor al, al
        rep stosb
        mov dword [label_count], 0
        ; Clear stacks
        mov dword [gosub_sp], 0
        mov dword [for_sp], 0
        ; Clear turtle
        call turtle_reset
        ; Clear output
        call clear_output
        ; Reset state
        mov byte [running], 0
        mov byte [match_flag], 0
        mov dword [interp_line], 0
        ret

;=======================================================================
; DRAWING
;=======================================================================
draw_all:
        PUSHALL
        call draw_toolbar
        call draw_editor
        call draw_canvas
        call draw_output
        call draw_status
        POPALL
        ret

;---------------------------------------
; Draw toolbar
;---------------------------------------
draw_toolbar:
        PUSHALL
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, TB_H
        mov edi, COL_TOOLBAR_BG
        call gui_fill_rect

        ; Run button (F5)
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, 2
        mov edx, 40
        mov esi, 20
        mov edi, COL_BTN_RUN
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 4
        mov esi, str_run
        mov edi, 0x00FFFFFF
        call gui_draw_text

        ; Stop button
        mov eax, [win_id]
        mov ebx, 48
        mov ecx, 2
        mov edx, 40
        mov esi, 20
        mov edi, COL_BTN_STOP
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 52
        mov ecx, 4
        mov esi, str_stop
        mov edi, 0x00FFFFFF
        call gui_draw_text

        ; Clear button
        mov eax, [win_id]
        mov ebx, 92
        mov ecx, 2
        mov edx, 44
        mov esi, 20
        mov edi, COL_BTN
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 96
        mov ecx, 4
        mov esi, str_clear
        mov edi, 0x00FFFFFF
        call gui_draw_text

        ; New button
        mov eax, [win_id]
        mov ebx, 140
        mov ecx, 2
        mov edx, 36
        mov esi, 20
        mov edi, COL_BTN
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 144
        mov ecx, 4
        mov esi, str_new
        mov edi, 0x00FFFFFF
        call gui_draw_text

        ; Title
        mov eax, [win_id]
        mov ebx, 350
        mov ecx, 4
        mov esi, str_title_bar
        mov edi, 0x00AAAAAA
        call gui_draw_text

        POPALL
        ret

;---------------------------------------
; Draw editor panel
;---------------------------------------
draw_editor:
        PUSHALL
        ; Background
        mov eax, [win_id]
        mov ebx, ED_X
        mov ecx, ED_Y
        mov edx, ED_W
        mov esi, ED_H
        mov edi, COL_BG
        call gui_fill_rect

        ; Draw visible lines
        xor ecx, ecx           ; line counter
.draw_line_loop:
        cmp ecx, VIS_LINES
        jge .draw_cursor

        ; Calculate source line index
        mov eax, [scroll_y]
        add eax, ecx
        cmp eax, [num_lines]
        jge .draw_skip_line

        push rcx
        ; Get line pointer
        push rax
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        pop rax

        ; Draw line number (4 chars wide)
        push rcx
        push rax
        inc eax               ; 1-based
        mov edi, line_num_buf
        call int_to_str
        mov eax, [win_id]
        mov ebx, ED_X + 2
        pop rax                ; restore line index
        push rax
        ; Calculate Y position
        mov ecx, [rsp + 8]    ; vis line index
        imul ecx, 16
        add ecx, ED_Y + 2
        mov esi, line_num_buf
        mov edi, COL_LINE_NUM
        call gui_draw_text
        pop rax
        pop rcx
        pop rcx

        ; Draw line text (offset by 32 pixels for line numbers)
        push rcx
        mov eax, [scroll_y]
        add eax, ecx
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        cmp byte [esi], 0
        je .skip_text

        mov eax, [win_id]
        mov ebx, ED_X + 34
        imul ecx, 16
        add ecx, ED_Y + 2
        mov edi, COL_TEXT
        call gui_draw_text

.skip_text:
        pop rcx
        inc ecx
        jmp .draw_line_loop

.draw_skip_line:
        inc ecx
        jmp .draw_line_loop

.draw_cursor:
        ; Draw cursor block
        mov eax, [cur_line]
        sub eax, [scroll_y]
        cmp eax, 0
        jl .ed_done
        cmp eax, VIS_LINES
        jge .ed_done

        push rax
        mov ebx, [cur_col]
        imul ebx, 8
        add ebx, ED_X + 34
        pop rax
        imul eax, 16
        add eax, ED_Y + 2
        mov ecx, eax
        mov eax, [win_id]
        mov edx, 8
        mov esi, 16
        mov edi, COL_CURSOR
        call gui_fill_rect

        ; Redraw character under cursor in black
        mov eax, [cur_line]
        imul eax, LINE_LEN
        add eax, [cur_col]
        movzx edx, byte [text_buf + eax]
        cmp dl, 32
        jl .ed_done
        cmp dl, 126
        jg .ed_done

        ; Build single char string
        mov [char_tmp], dl
        mov byte [char_tmp + 1], 0

        mov ebx, [cur_col]
        imul ebx, 8
        add ebx, ED_X + 34
        mov ecx, [cur_line]
        sub ecx, [scroll_y]
        imul ecx, 16
        add ecx, ED_Y + 2
        mov eax, [win_id]
        mov esi, char_tmp
        mov edi, 0x00000000
        call gui_draw_text

.ed_done:
        ; Separator line between editor and canvas
        mov ecx, VIS_LINES + 1
        mov eax, [win_id]
        mov ebx, ED_W
        mov ecx, ED_Y
        mov edx, 2
        mov esi, ED_H
        mov edi, COL_SEPARATOR
        call gui_fill_rect

        POPALL
        ret

;---------------------------------------
; Draw turtle canvas
;---------------------------------------
draw_canvas:
        PUSHALL
        ; White background
        mov eax, [win_id]
        mov ebx, CV_X
        mov ecx, CV_Y
        mov edx, CV_W
        mov esi, CV_H
        mov edi, COL_CANVAS_BG
        call gui_fill_rect

        ; Draw crosshair at center
        mov eax, [win_id]
        mov ebx, CV_X + TURTLE_CX - 4
        mov ecx, CV_Y + TURTLE_CY
        mov edx, 9
        mov esi, 1
        mov edi, COL_CANVAS_GRID
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, CV_X + TURTLE_CX
        mov ecx, CV_Y + TURTLE_CY - 4
        mov edx, 1
        mov esi, 9
        mov edi, COL_CANVAS_GRID
        call gui_fill_rect

        ; Replay all turtle strokes
        mov ecx, [stroke_count]
        test ecx, ecx
        jz .draw_turtle_indicator

        mov esi, stroke_buf
.stroke_loop:
        push rcx
        ; Each stroke: word x, word y, dword color
        movzx ebx, word [esi]
        movzx edx, word [esi + 2]
        mov edi, [esi + 4]
        add ebx, CV_X
        add edx, CV_Y

        ; Draw 1-pixel dot
        mov eax, [win_id]
        mov ecx, edx
        push rsi
        mov esi, edi
        call gui_draw_pixel
        pop rsi

        pop rcx
        add esi, 8
        dec ecx
        jnz .stroke_loop

.draw_turtle_indicator:
        ; Draw turtle triangle if visible
        cmp byte [turtle_visible], 0
        je .cv_done

        ; Convert turtle position to canvas coords
        mov eax, [turtle_x]
        sar eax, 10             ; divide by TRIG_SCALE
        add eax, TURTLE_CX
        mov ebx, TURTLE_CY
        mov ecx, [turtle_y]
        sar ecx, 10
        sub ebx, ecx            ; Y inverted

        ; Draw small diamond at turtle position
        add eax, CV_X
        add ebx, CV_Y
        push rax
        push rbx
        mov eax, [win_id]
        mov ecx, ebx
        mov esi, COL_TURTLE
        call gui_draw_pixel
        ; +1,0
        pop rbx
        pop rax
        push rax
        push rbx
        inc eax
        mov ecx, ebx
        push rax
        mov eax, [win_id]
        pop rbx
        mov esi, COL_TURTLE
        call gui_draw_pixel
        ; -1,0
        pop rbx
        pop rax
        push rax
        push rbx
        dec eax
        mov ecx, ebx
        push rax
        mov eax, [win_id]
        pop rbx
        mov esi, COL_TURTLE
        call gui_draw_pixel
        ; 0,+1
        pop rbx
        pop rax
        push rax
        push rbx
        inc ebx
        mov ecx, ebx
        dec ebx
        push rax
        mov eax, [win_id]
        pop rbx
        dec ebx
        inc ecx
        mov esi, COL_TURTLE
        call gui_draw_pixel
        ; 0,-1
        pop rbx
        pop rax
        dec ebx
        mov ecx, ebx
        push rax
        mov eax, [win_id]
        pop rbx
        mov esi, COL_TURTLE
        call gui_draw_pixel

.cv_done:
        POPALL
        ret

;---------------------------------------
; Draw output panel
;---------------------------------------
draw_output:
        PUSHALL
        ; Background
        mov eax, [win_id]
        mov ebx, OUT_X
        mov ecx, OUT_Y
        mov edx, OUT_W
        mov esi, OUT_H
        mov edi, COL_OUTPUT_BG
        call gui_fill_rect

        ; Separator line
        mov eax, [win_id]
        mov ebx, OUT_X
        mov ecx, OUT_Y
        mov edx, OUT_W
        mov esi, 2
        mov edi, COL_SEPARATOR
        call gui_fill_rect

        ; Draw output lines
        xor ecx, ecx
.out_line_loop:
        cmp ecx, OUT_LINES
        jge .out_done

        push rcx
        ; Calculate source line
        mov eax, [out_scroll]
        add eax, ecx
        cmp eax, [out_count]
        jge .out_skip

        imul eax, OUT_LINE_LEN
        lea esi, [output_buf + eax]
        cmp byte [esi], 0
        je .out_skip

        mov eax, [win_id]
        mov ebx, OUT_X + 4
        pop rcx
        push rcx
        imul ecx, 16
        add ecx, OUT_Y + 4
        mov edi, COL_OUTPUT_TEXT
        call gui_draw_text

.out_skip:
        pop rcx
        inc ecx
        jmp .out_line_loop

.out_done:
        POPALL
        ret

;---------------------------------------
; Draw status bar
;---------------------------------------
draw_status:
        PUSHALL
        mov eax, [win_id]
        xor ebx, ebx
        mov ecx, SB_Y
        mov edx, WIN_W
        mov esi, SB_H
        mov edi, COL_STATUS_BG
        call gui_fill_rect

        ; Show line:col
        mov edi, status_buf
        mov eax, [cur_line]
        inc eax
        call int_to_str_at       ; writes to EDI, advances EDI
        mov byte [edi], ':'
        inc edi
        mov eax, [cur_col]
        inc eax
        call int_to_str_at
        mov byte [edi], ' '
        inc edi
        mov byte [edi], ' '
        inc edi

        ; Show mode
        cmp byte [running], 0
        je .status_idle
        mov esi, str_running
        jmp .status_copy
.status_idle:
        mov esi, str_ready
.status_copy:
        ; Copy mode string
.sc_loop:
        lodsb
        test al, al
        jz .sc_done
        mov [edi], al
        inc edi
        jmp .sc_loop
.sc_done:
        mov byte [edi], ' '
        inc edi
        mov byte [edi], ' '
        inc edi

        ; Show F5=Run hint
        mov esi, str_hint
.sc2:
        lodsb
        test al, al
        jz .sc2d
        mov [edi], al
        inc edi
        jmp .sc2
.sc2d:
        mov byte [edi], 0

        ; Draw status text
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, SB_Y + 2
        mov esi, status_buf
        mov edi, 0x00CCCCCC
        call gui_draw_text

        POPALL
        ret

;=======================================================================
; INPUT HANDLING
;=======================================================================
handle_keypress:
        ; EAX = key code
        PUSHALL

        ; F5 = Run
        cmp eax, KEY_F5
        je .do_run

        ; Escape = Stop / unfocus
        cmp eax, 27
        je .do_stop

        ; If interpreter is waiting for input, route to input handler
        cmp byte [input_waiting], 1
        je .input_mode

        ; Editor key handling
        cmp eax, KEY_UP
        je .key_up
        cmp eax, KEY_DOWN
        je .key_down
        cmp eax, KEY_LEFT
        je .key_left
        cmp eax, KEY_RIGHT
        je .key_right
        cmp eax, 13            ; Enter
        je .key_enter
        cmp eax, 8             ; Backspace
        je .key_bs
        cmp eax, 9             ; Tab -> 2 spaces
        je .key_tab

        ; Ctrl+S = Save (ASCII 19)
        cmp eax, 19
        je .do_save

        ; Printable characters
        cmp eax, 32
        jl .key_done
        cmp eax, 126
        jg .key_done

        ; Insert character
        call editor_insert_char
        jmp .key_done

.key_up:
        cmp dword [cur_line], 0
        je .key_done
        dec dword [cur_line]
        call adjust_scroll
        call clamp_col
        jmp .key_done

.key_down:
        mov eax, [cur_line]
        inc eax
        cmp eax, [num_lines]
        jge .key_done
        mov [cur_line], eax
        call adjust_scroll
        call clamp_col
        jmp .key_done

.key_left:
        cmp dword [cur_col], 0
        je .key_done
        dec dword [cur_col]
        jmp .key_done

.key_right:
        ; Don't go past end of line
        mov eax, [cur_line]
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        call strlen
        cmp [cur_col], eax
        jge .key_done
        inc dword [cur_col]
        jmp .key_done

.key_enter:
        call editor_newline
        jmp .key_done

.key_bs:
        call editor_backspace
        jmp .key_done

.key_tab:
        mov eax, ' '
        call editor_insert_char
        mov eax, ' '
        call editor_insert_char
        jmp .key_done

.do_run:
        call run_program
        jmp .key_done

.do_stop:
        mov byte [running], 0
        jmp .key_done

.do_save:
        call save_file
        jmp .key_done

.input_mode:
        ; Handle input for INPUT statement
        cmp eax, 13            ; Enter submits
        je .input_submit
        cmp eax, 8
        je .input_bs
        cmp eax, 32
        jl .key_done
        cmp eax, 126
        jg .key_done
        ; Add to input buffer
        mov ecx, [input_len]
        cmp ecx, 58
        jge .key_done
        mov [input_buf + ecx], al
        inc dword [input_len]
        mov byte [input_buf + ecx + 1], 0
        ; Echo in output
        mov [char_tmp], al
        mov byte [char_tmp + 1], 0
        ; Append to current output line
        jmp .key_done

.input_submit:
        mov byte [input_waiting], 0
        mov byte [input_done], 1
        jmp .key_done

.input_bs:
        cmp dword [input_len], 0
        je .key_done
        dec dword [input_len]
        mov ecx, [input_len]
        mov byte [input_buf + ecx], 0
        jmp .key_done

.key_done:
        POPALL
        ret

;=======================================================================
; EDITOR OPERATIONS
;=======================================================================
editor_insert_char:
        ; EAX = character to insert (in AL)
        PUSHALL
        mov edx, [cur_line]
        imul edx, LINE_LEN
        add edx, [cur_col]
        cmp dword [cur_col], LINE_LEN - 2
        jge .eic_done

        ; Shift rest of line right
        lea edi, [text_buf + edx]
        mov ecx, LINE_LEN - 2
        sub ecx, [cur_col]
        lea esi, [edi + ecx - 1]
        lea edi, [esi + 1]
        std
        rep movsb
        cld

        ; Insert character
        mov edx, [cur_line]
        imul edx, LINE_LEN
        add edx, [cur_col]
        mov [text_buf + edx], al
        inc dword [cur_col]
.eic_done:
        POPALL
        ret

editor_newline:
        PUSHALL
        ; Check line limit
        mov eax, [num_lines]
        cmp eax, MAX_LINES - 1
        jge .enl_done

        ; Shift all lines below down
        mov ecx, [num_lines]
        dec ecx
.enl_shift:
        cmp ecx, [cur_line]
        jle .enl_split
        ; Copy line ecx to ecx+1
        mov eax, ecx
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        lea edi, [esi + LINE_LEN]
        push rcx
        mov ecx, LINE_LEN
        rep movsb
        pop rcx
        dec ecx
        jmp .enl_shift

.enl_split:
        ; Split current line at cursor
        mov eax, [cur_line]
        imul eax, LINE_LEN
        add eax, [cur_col]
        lea esi, [text_buf + eax]

        ; Copy from cursor to next line
        mov eax, [cur_line]
        inc eax
        imul eax, LINE_LEN
        lea edi, [text_buf + eax]
        mov ecx, LINE_LEN
        sub ecx, [cur_col]
        push rsi
        rep movsb
        pop rsi

        ; Null-terminate current line at cursor
        mov ecx, LINE_LEN
        sub ecx, [cur_col]
        mov edi, esi
        xor al, al
        rep stosb

        inc dword [num_lines]
        inc dword [cur_line]
        mov dword [cur_col], 0
        call adjust_scroll

.enl_done:
        POPALL
        ret

editor_backspace:
        PUSHALL
        ; If at start of line, join with previous
        cmp dword [cur_col], 0
        jne .ebs_inline

        cmp dword [cur_line], 0
        je .ebs_done

        ; Find end of previous line
        mov eax, [cur_line]
        dec eax
        imul eax, LINE_LEN
        lea edi, [text_buf + eax]
        push rdi
        mov esi, edi
        call strlen
        mov [cur_col], eax      ; cursor goes to end of prev line
        pop rdi
        add edi, eax            ; append point

        ; Copy current line to end of previous
        mov eax, [cur_line]
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        mov ecx, LINE_LEN
        sub ecx, [cur_col]
        rep movsb

        ; Shift lines above current up
        mov ecx, [cur_line]
.ebs_shift:
        mov eax, ecx
        inc eax
        cmp eax, [num_lines]
        jge .ebs_shifted
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        lea edi, [esi - LINE_LEN]
        push rcx
        mov ecx, LINE_LEN
        rep movsb
        pop rcx
        inc ecx
        jmp .ebs_shift

.ebs_shifted:
        dec dword [num_lines]
        dec dword [cur_line]
        call adjust_scroll
        jmp .ebs_done

.ebs_inline:
        ; Delete char before cursor
        mov eax, [cur_line]
        imul eax, LINE_LEN
        add eax, [cur_col]
        dec eax
        lea edi, [text_buf + eax]
        lea esi, [edi + 1]
        mov ecx, LINE_LEN - 1
        sub ecx, [cur_col]
        rep movsb
        mov byte [edi], 0
        dec dword [cur_col]

.ebs_done:
        POPALL
        ret

adjust_scroll:
        mov eax, [cur_line]
        cmp eax, [scroll_y]
        jge .as_down
        mov [scroll_y], eax
        ret
.as_down:
        mov eax, [cur_line]
        sub eax, VIS_LINES
        inc eax
        cmp eax, [scroll_y]
        jle .as_ok
        mov [scroll_y], eax
.as_ok:
        ret

clamp_col:
        ; Clamp cur_col to line length
        mov eax, [cur_line]
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        call strlen
        cmp [cur_col], eax
        jle .cc_ok
        mov [cur_col], eax
.cc_ok:
        ret

;=======================================================================
; MOUSE CLICK HANDLING
;=======================================================================
handle_mouse_click:
        ; EAX = x, EDX = y
        PUSHALL

        ; Check toolbar buttons
        cmp edx, TB_H
        jg .check_editor

        ; Run button
        cmp eax, 4
        jl .mc_done
        cmp eax, 44
        jle .mc_run

        ; Stop button
        cmp eax, 48
        jl .mc_done
        cmp eax, 88
        jle .mc_stop

        ; Clear button
        cmp eax, 92
        jl .mc_done
        cmp eax, 136
        jle .mc_clear

        ; New button
        cmp eax, 140
        jl .mc_done
        cmp eax, 176
        jle .mc_new

        jmp .mc_done

.mc_run:
        call run_program
        jmp .mc_done
.mc_stop:
        mov byte [running], 0
        jmp .mc_done
.mc_clear:
        call clear_output
        call turtle_reset
        mov dword [stroke_count], 0
        jmp .mc_done
.mc_new:
        call init_editor
        call clear_output
        call turtle_reset
        mov dword [stroke_count], 0
        jmp .mc_done

.check_editor:
        ; Click in editor area?
        cmp eax, ED_X
        jl .mc_done
        cmp eax, ED_X + ED_W
        jg .mc_done
        cmp edx, ED_Y
        jl .mc_done
        cmp edx, ED_Y + ED_H
        jg .mc_done

        ; Calculate clicked line and column
        sub edx, ED_Y + 2
        shr edx, 4             ; /16 = vis line
        add edx, [scroll_y]
        cmp edx, [num_lines]
        jge .mc_done
        mov [cur_line], edx

        sub eax, ED_X + 34
        cmp eax, 0
        jl .mc_col_zero
        shr eax, 3             ; /8 = column
        mov [cur_col], eax
        call clamp_col
        jmp .mc_done
.mc_col_zero:
        mov dword [cur_col], 0

.mc_done:
        POPALL
        ret

;=======================================================================
; FILE I/O
;=======================================================================
load_file:
        PUSHALL
        mov eax, SYS_FREAD
        mov ebx, arg_buf
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .lf_fail
        mov [file_size], eax

        ; Parse file into editor lines
        call init_editor
        mov esi, file_buf
        xor ecx, ecx           ; current line
        xor edx, edx           ; current col

.lf_parse:
        cmp ecx, MAX_LINES
        jge .lf_done
        mov eax, [file_size]
        lea ebx, [file_buf]
        add ebx, eax
        cmp esi, ebx
        jge .lf_done

        movzx eax, byte [esi]
        cmp al, 0
        je .lf_done
        cmp al, 10             ; newline
        je .lf_newline
        cmp al, 13             ; CR
        je .lf_cr

        ; Store character
        cmp edx, LINE_LEN - 1
        jge .lf_skip_ch
        push rax
        mov eax, ecx
        imul eax, LINE_LEN
        add eax, edx
        pop rbx
        mov [text_buf + eax], bl
        inc edx

.lf_skip_ch:
        inc esi
        jmp .lf_parse

.lf_newline:
        inc ecx
        xor edx, edx
        inc esi
        jmp .lf_parse

.lf_cr:
        inc esi
        cmp byte [esi], 10     ; CR+LF
        jne .lf_parse
        jmp .lf_newline

.lf_done:
        inc ecx
        mov [num_lines], ecx
        POPALL
        ret

.lf_fail:
        ; Show error in output
        mov esi, str_load_err
        call output_add_line
        POPALL
        ret

save_file:
        PUSHALL
        cmp byte [has_arg], 0
        je .sf_no_name

        ; Flatten text_buf to file_buf with newlines
        mov edi, file_buf
        xor ecx, ecx           ; line counter
        xor edx, edx           ; total bytes

.sf_line:
        cmp ecx, [num_lines]
        jge .sf_write

        mov eax, ecx
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]

        ; Find line length
        push rcx
        push rdx
        call strlen
        mov ebx, eax           ; line length
        pop rdx
        pop rcx

        ; Copy line text
        push rcx
        mov ecx, ebx
        push rsi
        rep movsb
        pop rsi
        pop rcx

        ; Add newline
        mov byte [edi], 10
        inc edi
        add edx, ebx
        inc edx                ; for newline

        inc ecx
        jmp .sf_line

.sf_write:
        mov byte [edi], 0
        mov eax, SYS_FWRITE
        mov ebx, arg_buf
        mov ecx, file_buf
        ; edx already has size
        xor esi, esi
        int 0x80

        mov esi, str_saved
        call output_add_line
        POPALL
        ret

.sf_no_name:
        mov esi, str_no_name
        call output_add_line
        POPALL
        ret

;=======================================================================
; OUTPUT PANEL
;=======================================================================
clear_output:
        PUSHALL
        mov edi, output_buf
        mov ecx, OUT_LINES * OUT_LINE_LEN
        xor al, al
        rep stosb
        mov dword [out_count], 0
        mov dword [out_scroll], 0
        POPALL
        ret

; Add line to output panel (ESI = null-terminated string)
output_add_line:
        PUSHALL
        ; If buffer full, scroll up
        mov eax, [out_count]
        cmp eax, OUT_LINES
        jl .oal_add

        ; Shift lines up
        push rsi
        mov esi, output_buf + OUT_LINE_LEN
        mov edi, output_buf
        mov ecx, (OUT_LINES - 1) * OUT_LINE_LEN
        rep movsb
        pop rsi

        mov eax, OUT_LINES - 1
        mov [out_count], eax

.oal_add:
        ; Copy string to output line
        mov eax, [out_count]
        imul eax, OUT_LINE_LEN
        lea edi, [output_buf + eax]
        mov ecx, OUT_LINE_LEN - 1
.oal_copy:
        lodsb
        test al, al
        jz .oal_pad
        mov [edi], al
        inc edi
        dec ecx
        jnz .oal_copy
.oal_pad:
        ; Null terminate and pad
        xor al, al
        rep stosb

        inc dword [out_count]
        ; Auto-scroll to bottom
        mov eax, [out_count]
        sub eax, OUT_LINES
        cmp eax, 0
        jl .oal_no_scroll
        mov [out_scroll], eax
.oal_no_scroll:
        POPALL
        ret

; Output integer value (EAX = value)
output_int:
        PUSHALL
        mov edi, int_buf
        call int_to_str
        mov esi, int_buf
        call output_add_line
        POPALL
        ret

;=======================================================================
; TEMAPLECODE INTERPRETER
;=======================================================================
run_program:
        PUSHALL
        ; Reset interpreter state
        call init_interpreter

        ; Collect labels first
        call collect_labels

        mov byte [running], 1
        mov dword [interp_line], 0
        mov dword [iteration], 0

.run_loop:
        cmp byte [running], 0
        je .run_done

        ; Check iteration limit
        inc dword [iteration]
        mov eax, [iteration]
        cmp eax, MAX_ITER
        jge .run_overflow

        ; Check line bound
        mov eax, [interp_line]
        cmp eax, [num_lines]
        jge .run_done

        ; Get line text
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        cmp byte [esi], 0
        je .run_next

        ; Skip leading whitespace
        call skip_spaces

        ; Skip empty/comment lines
        cmp byte [esi], 0
        je .run_next
        cmp byte [esi], ';'
        je .run_next
        cmp byte [esi], '#'
        je .run_next

        ; Check for REM
        cmp byte [esi], 'R'
        jne .not_rem
        cmp byte [esi + 1], 'E'
        jne .not_rem
        cmp byte [esi + 2], 'M'
        jne .not_rem
        jmp .run_next
.not_rem:
        ; Check for comment (')
        cmp byte [esi], 0x27    ; single quote
        je .run_next

        ; Execute line
        call exec_line

        ; Check result
        cmp byte [jump_flag], 1
        je .run_jump
        cmp byte [running], 0
        je .run_done

.run_next:
        inc dword [interp_line]
        jmp .run_loop

.run_jump:
        mov byte [jump_flag], 0
        mov eax, [jump_target]
        mov [interp_line], eax
        jmp .run_loop

.run_overflow:
        mov esi, str_overflow
        call output_add_line
.run_done:
        mov byte [running], 0

        ; Render final state
        call gui_compose
        call draw_all
        call gui_flip

        POPALL
        ret

;---------------------------------------
; Collect labels from program (L:name, *name)
;---------------------------------------
collect_labels:
        PUSHALL
        mov dword [label_count], 0
        xor ecx, ecx           ; line index

.cl_loop:
        cmp ecx, [num_lines]
        jge .cl_done
        push rcx

        mov eax, ecx
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        call skip_spaces

        ; Check for L: prefix
        cmp byte [esi], 'L'
        jne .cl_check_star
        cmp byte [esi + 1], ':'
        jne .cl_check_star
        add esi, 2
        call skip_spaces
        pop rcx
        push rcx
        call add_label          ; ESI=name, ECX=line number
        jmp .cl_next

.cl_check_star:
        ; Check for *label
        cmp byte [esi], '*'
        jne .cl_next
        inc esi
        pop rcx
        push rcx
        call add_label

.cl_next:
        pop rcx
        inc ecx
        jmp .cl_loop

.cl_done:
        POPALL
        ret

; Add label: ESI=name start, ECX=line number
add_label:
        PUSHALL
        mov eax, [label_count]
        cmp eax, MAX_LABELS
        jge .al_done

        ; Calculate label entry address
        imul eax, LABEL_NAME_LEN + 4
        lea edi, [labels + eax]

        ; Copy name (up to LABEL_NAME_LEN-1 chars)
        mov ecx, LABEL_NAME_LEN - 1
.al_copy:
        lodsb
        cmp al, ' '
        je .al_name_done
        cmp al, 0
        je .al_name_done
        cmp al, 0x0A
        je .al_name_done
        mov [edi], al
        inc edi
        dec ecx
        jnz .al_copy
.al_name_done:
        mov byte [edi], 0

        ; Store line number after name
        mov eax, [label_count]
        imul eax, LABEL_NAME_LEN + 4
        add eax, LABEL_NAME_LEN
        pop rcx                 ; restore from PUSHALL - line number is in original ecx
        push rcx
        ; Get original ECX from PUSHALL frame (at rsp+20)
        mov ecx, [rsp + 96]    ; ECX in PUSHALL frame
        mov [labels + eax], ecx

        inc dword [label_count]
.al_done:
        POPALL
        ret

; Find label: ESI=name -> EAX=line number or -1
find_label:
        PUSHALL
        xor ecx, ecx
.fl_loop:
        cmp ecx, [label_count]
        jge .fl_notfound

        mov eax, ecx
        imul eax, LABEL_NAME_LEN + 4
        lea edi, [labels + eax]

        ; Compare names
        push rsi
        push rcx
        call str_equal
        pop rcx
        pop rsi
        cmp eax, 1
        je .fl_found

        inc ecx
        jmp .fl_loop

.fl_found:
        mov eax, ecx
        imul eax, LABEL_NAME_LEN + 4
        add eax, LABEL_NAME_LEN
        mov eax, [labels + eax]
        mov [rsp + 112], eax     ; return via PUSHALL frame
        POPALL
        ret

.fl_notfound:
        mov dword [rsp + 112], -1
        POPALL
        ret

;=======================================================================
; EXECUTE A SINGLE LINE
;=======================================================================
exec_line:
        PUSHALL
        call skip_spaces

        ; Check for PILOT colon-commands (X:)
        cmp byte [esi + 1], ':'
        jne .not_pilot
        movzx eax, byte [esi]
        ; Uppercase it
        cmp al, 'a'
        jl .pilot_dispatch
        cmp al, 'z'
        jg .not_pilot
        sub al, 32
.pilot_dispatch:
        add esi, 2             ; skip X:
        call skip_spaces
        cmp al, 'T'
        je exec_pilot_type
        cmp al, 'A'
        je exec_pilot_accept
        cmp al, 'M'
        je exec_pilot_match
        cmp al, 'Y'
        je exec_pilot_yes
        cmp al, 'N'
        je exec_pilot_no
        cmp al, 'J'
        je exec_pilot_jump
        cmp al, 'U'
        je exec_pilot_use
        cmp al, 'E'
        je exec_pilot_end
        cmp al, 'C'
        je exec_pilot_compute
        cmp al, 'R'
        je .exec_done           ; R: = remark
        cmp al, 'L'
        je .exec_done           ; L: = label (handled at collect)
        jmp .exec_done

.not_pilot:
        ; Check for BASIC/Logo keywords
        call try_basic_logo

.exec_done:
        POPALL
        ret

;=======================================================================
; BASIC / LOGO COMMAND DISPATCHER
;=======================================================================
try_basic_logo:
        ; ESI points to start of command
        push rsi

        ; --- PRINT ---
        mov edi, kw_print
        call match_keyword
        jc near do_print

        ; --- LET ---
        pop rsi
        push rsi
        mov edi, kw_let
        call match_keyword
        jc near do_let

        ; --- IF ---
        pop rsi
        push rsi
        mov edi, kw_if
        call match_keyword
        jc near do_if

        ; --- FOR ---
        pop rsi
        push rsi
        mov edi, kw_for
        call match_keyword
        jc near do_for

        ; --- NEXT ---
        pop rsi
        push rsi
        mov edi, kw_next
        call match_keyword
        jc near do_next

        ; --- GOTO ---
        pop rsi
        push rsi
        mov edi, kw_goto
        call match_keyword
        jc near do_goto

        ; --- GOSUB ---
        pop rsi
        push rsi
        mov edi, kw_gosub
        call match_keyword
        jc near do_gosub

        ; --- RETURN ---
        pop rsi
        push rsi
        mov edi, kw_return
        call match_keyword
        jc near do_return

        ; --- INPUT ---
        pop rsi
        push rsi
        mov edi, kw_input
        call match_keyword
        jc near do_input

        ; --- DIM ---
        pop rsi
        push rsi
        mov edi, kw_dim
        call match_keyword
        jc near do_dim

        ; --- END ---
        pop rsi
        push rsi
        mov edi, kw_end
        call match_keyword
        jc near do_end

        ; --- Logo: FORWARD/FD ---
        pop rsi
        push rsi
        mov edi, kw_forward
        call match_keyword
        jc near do_forward
        pop rsi
        push rsi
        mov edi, kw_fd
        call match_keyword
        jc near do_forward

        ; --- BACK/BK ---
        pop rsi
        push rsi
        mov edi, kw_back
        call match_keyword
        jc near do_back
        pop rsi
        push rsi
        mov edi, kw_bk
        call match_keyword
        jc near do_back

        ; --- LEFT/LT ---
        pop rsi
        push rsi
        mov edi, kw_left
        call match_keyword
        jc near do_left
        pop rsi
        push rsi
        mov edi, kw_lt
        call match_keyword
        jc near do_left

        ; --- RIGHT/RT ---
        pop rsi
        push rsi
        mov edi, kw_right
        call match_keyword
        jc near do_right
        pop rsi
        push rsi
        mov edi, kw_rt
        call match_keyword
        jc near do_right

        ; --- PENUP/PU ---
        pop rsi
        push rsi
        mov edi, kw_penup
        call match_keyword
        jc near do_penup
        pop rsi
        push rsi
        mov edi, kw_pu
        call match_keyword
        jc near do_penup

        ; --- PENDOWN/PD ---
        pop rsi
        push rsi
        mov edi, kw_pendown
        call match_keyword
        jc near do_pendown
        pop rsi
        push rsi
        mov edi, kw_pd
        call match_keyword
        jc near do_pendown

        ; --- HOME ---
        pop rsi
        push rsi
        mov edi, kw_home
        call match_keyword
        jc near do_home

        ; --- CLEARSCREEN/CS ---
        pop rsi
        push rsi
        mov edi, kw_clearscreen
        call match_keyword
        jc near do_clearscreen
        pop rsi
        push rsi
        mov edi, kw_cs
        call match_keyword
        jc near do_clearscreen

        ; --- SETCOLOR ---
        pop rsi
        push rsi
        mov edi, kw_setcolor
        call match_keyword
        jc near do_setcolor

        ; --- CIRCLE ---
        pop rsi
        push rsi
        mov edi, kw_circle
        call match_keyword
        jc near do_circle

        ; --- REPEAT ---
        pop rsi
        push rsi
        mov edi, kw_repeat
        call match_keyword
        jc near do_repeat

        ; --- MAKE ---
        pop rsi
        push rsi
        mov edi, kw_make
        call match_keyword
        jc near do_make

        ; --- SETXY ---
        pop rsi
        push rsi
        mov edi, kw_setxy
        call match_keyword
        jc near do_setxy

        ; --- Try implicit LET (X = value) ---
        pop rsi
        push rsi
        movzx eax, byte [esi]
        cmp al, 'A'
        jl .tbl_unknown
        cmp al, 'z'
        jg .tbl_unknown
        ; Check for = sign after variable name
        push rsi
        inc esi
        call skip_spaces
        cmp byte [esi], '='
        pop rsi
        jne .tbl_unknown
        ; It's an implicit LET
        pop rsi
        push rsi
        jmp do_let

.tbl_unknown:
        pop rsi
        ret

;=======================================================================
; BASIC COMMANDS
;=======================================================================

;----------- PRINT -----------
do_print:
        pop rsi                 ; clean up push from try_basic_logo
        call skip_spaces

        ; Empty PRINT = blank line
        cmp byte [esi], 0
        je .dp_blank

        mov edi, print_buf
        mov dword [print_pos], 0

.dp_loop:
        call skip_spaces
        cmp byte [esi], 0
        je .dp_flush
        cmp byte [esi], ';'
        je .dp_semi

        ; String literal?
        cmp byte [esi], '"'
        je .dp_string

        ; Expression
        call eval_expr
        ; Convert result to string
        push rsi
        mov edi, int_buf
        call int_to_str
        ; Append to print_buf
        mov esi, int_buf
        call append_print_buf
        pop rsi
        jmp .dp_loop

.dp_string:
        inc esi                 ; skip opening "
.dp_str_ch:
        movzx eax, byte [esi]
        cmp al, '"'
        je .dp_str_end
        cmp al, 0
        je .dp_flush
        ; Check for *VAR* interpolation
        cmp al, '*'
        je .dp_interp
        mov ecx, [print_pos]
        cmp ecx, 70
        jge .dp_str_next
        mov [print_buf + ecx], al
        inc dword [print_pos]
.dp_str_next:
        inc esi
        jmp .dp_str_ch

.dp_interp:
        inc esi                 ; skip *
        ; Get variable name
        movzx eax, byte [esi]
        call to_upper_al
        cmp al, 'A'
        jl .dp_str_ch
        cmp al, 'Z'
        jg .dp_str_ch
        sub al, 'A'
        movzx ebx, al
        shl ebx, 2
        mov eax, [variables + ebx]
        inc esi
        ; Skip closing *
        cmp byte [esi], '*'
        jne .dp_no_close_star
        inc esi
.dp_no_close_star:
        ; Convert and append
        push rsi
        mov edi, int_buf
        call int_to_str
        mov esi, int_buf
        call append_print_buf
        pop rsi
        jmp .dp_str_ch

.dp_str_end:
        inc esi                 ; skip closing "
        jmp .dp_loop

.dp_semi:
        inc esi
        jmp .dp_loop

.dp_flush:
        mov ecx, [print_pos]
        mov byte [print_buf + ecx], 0
        mov esi, print_buf
        call output_add_line
        ret

.dp_blank:
        mov esi, str_empty
        call output_add_line
        pop rsi
        ret

;----------- LET -----------
do_let:
        pop rsi
        call skip_spaces

        ; Skip optional LET keyword
        cmp byte [esi], 'L'
        jne .dl_var
        cmp byte [esi+1], 'E'
        jne .dl_var
        cmp byte [esi+2], 'T'
        jne .dl_var
        add esi, 3
        call skip_spaces

.dl_var:
        ; Get variable name
        movzx eax, byte [esi]
        call to_upper_al
        cmp al, 'A'
        jl .dl_err
        cmp al, 'Z'
        jg .dl_err
        sub al, 'A'
        movzx ebx, al
        push rbx
        inc esi
        call skip_spaces

        ; Expect =
        cmp byte [esi], '='
        jne .dl_err2
        inc esi
        call skip_spaces

        ; Evaluate expression
        call eval_expr
        pop rbx
        shl ebx, 2
        mov [variables + ebx], eax
        ret

.dl_err2:
        pop rbx
.dl_err:
        ret

;----------- IF -----------
do_if:
        pop rsi
        call skip_spaces

        ; Evaluate left expression
        call eval_expr
        push rax

        call skip_spaces

        ; Get comparison operator
        xor edx, edx           ; operator type
        cmp byte [esi], '='
        je .if_eq
        cmp byte [esi], '<'
        je .if_lt_check
        cmp byte [esi], '>'
        je .if_gt_check
        pop rax
        ret

.if_eq:
        mov edx, 1             ; =
        inc esi
        jmp .if_rhs
.if_lt_check:
        inc esi
        cmp byte [esi], '='
        je .if_le
        cmp byte [esi], '>'
        je .if_ne
        mov edx, 2             ; <
        jmp .if_rhs
.if_le:
        mov edx, 4             ; <=
        inc esi
        jmp .if_rhs
.if_ne:
        mov edx, 5             ; <>
        inc esi
        jmp .if_rhs
.if_gt_check:
        inc esi
        cmp byte [esi], '='
        je .if_ge
        mov edx, 3             ; >
        jmp .if_rhs
.if_ge:
        mov edx, 6             ; >=
        inc esi

.if_rhs:
        call skip_spaces
        call eval_expr
        mov ecx, eax           ; right side
        pop rbx                ; left side

        ; Compare
        cmp edx, 1
        je .if_test_eq
        cmp edx, 2
        je .if_test_lt
        cmp edx, 3
        je .if_test_gt
        cmp edx, 4
        je .if_test_le
        cmp edx, 5
        je .if_test_ne
        cmp edx, 6
        je .if_test_ge
        ret

.if_test_eq:
        cmp ebx, ecx
        je .if_true
        ret
.if_test_lt:
        cmp ebx, ecx
        jl .if_true
        ret
.if_test_gt:
        cmp ebx, ecx
        jg .if_true
        ret
.if_test_le:
        cmp ebx, ecx
        jle .if_true
        ret
.if_test_ne:
        cmp ebx, ecx
        jne .if_true
        ret
.if_test_ge:
        cmp ebx, ecx
        jge .if_true
        ret

.if_true:
        ; Skip THEN keyword
        call skip_spaces
        mov edi, kw_then
        call match_keyword
        jnc .if_ret
        call skip_spaces

        ; Execute the THEN clause (rest of line)
        call exec_line_inner
.if_ret:
        ret

; Execute inner line (for IF...THEN <statement>)
exec_line_inner:
        PUSHALL
        call skip_spaces
        cmp byte [esi], 0
        je .eli_done

        ; Check for PILOT colon
        cmp byte [esi + 1], ':'
        jne .eli_basic
        movzx eax, byte [esi]
        cmp al, 'a'
        jl .eli_pilot
        cmp al, 'z'
        jg .eli_basic
        sub al, 32
.eli_pilot:
        add esi, 2
        call skip_spaces
        cmp al, 'T'
        je exec_pilot_type
        cmp al, 'J'
        je exec_pilot_jump
        jmp .eli_done

.eli_basic:
        call try_basic_logo
.eli_done:
        POPALL
        ret

;----------- FOR -----------
do_for:
        pop rsi
        call skip_spaces

        ; Get variable
        movzx eax, byte [esi]
        call to_upper_al
        sub al, 'A'
        movzx ebx, al          ; var index
        inc esi
        call skip_spaces

        ; Expect =
        cmp byte [esi], '='
        jne .for_err
        inc esi
        call skip_spaces

        ; Start value
        push rbx
        call eval_expr
        pop rbx
        shl ebx, 2
        mov [variables + ebx], eax
        shr ebx, 2             ; restore var index

        ; Expect TO
        call skip_spaces
        push rbx
        push rax
        mov edi, kw_to
        call match_keyword
        pop rax
        pop rbx
        jnc .for_err

        ; End value
        push rbx
        call skip_spaces
        call eval_expr
        mov ecx, eax           ; end value
        pop rbx

        ; Push FOR frame: var_index, end_value, return_line
        mov eax, [for_sp]
        cmp eax, FOR_DEPTH
        jge .for_err

        imul eax, 12
        mov [for_stack + eax], ebx
        mov [for_stack + eax + 4], ecx
        mov edx, [interp_line]
        mov [for_stack + eax + 8], edx
        inc dword [for_sp]
        ret

.for_err:
        ret

;----------- NEXT -----------
do_next:
        pop rsi
        cmp dword [for_sp], 0
        je .next_err

        ; Get top FOR frame
        mov eax, [for_sp]
        dec eax
        imul eax, 12

        ; Increment variable
        mov ebx, [for_stack + eax]
        shl ebx, 2
        inc dword [variables + ebx]

        ; Check end condition
        mov ecx, [variables + ebx]
        shr ebx, 2
        mov edx, [for_stack + eax + 4]     ; end value
        cmp ecx, edx
        jg .next_pop

        ; Loop back
        mov ecx, [for_stack + eax + 8]     ; return line
        inc ecx                             ; line after FOR
        mov [jump_target], ecx
        mov byte [jump_flag], 1
        ret

.next_pop:
        dec dword [for_sp]
.next_err:
        ret

;----------- GOTO -----------
do_goto:
        pop rsi
        call skip_spaces

        ; Check if target is a label or number
        movzx eax, byte [esi]
        cmp al, '0'
        jl .goto_label
        cmp al, '9'
        jg .goto_label

        ; Number - find line by scanning
        call parse_number
        ; Find line index with this number... we don't use line numbers
        ; Just use as absolute line index (0-based)
        dec eax                 ; convert 1-based to 0-based
        mov [jump_target], eax
        mov byte [jump_flag], 1
        ret

.goto_label:
        ; Label lookup
        call find_label
        cmp eax, -1
        je .goto_err
        mov [jump_target], eax
        mov byte [jump_flag], 1
        ret

.goto_err:
        mov esi, str_label_err
        call output_add_line
        ret

;----------- GOSUB -----------
do_gosub:
        pop rsi
        call skip_spaces

        ; Push return address
        mov eax, [gosub_sp]
        cmp eax, GOSUB_DEPTH
        jge .gosub_err

        mov ecx, [interp_line]
        mov [gosub_stack + eax * 4], ecx
        inc dword [gosub_sp]

        ; Jump to target (same as GOTO)
        movzx eax, byte [esi]
        cmp al, '0'
        jl .gosub_label
        cmp al, '9'
        jg .gosub_label
        call parse_number
        dec eax
        mov [jump_target], eax
        mov byte [jump_flag], 1
        ret

.gosub_label:
        call find_label
        cmp eax, -1
        je .gosub_err
        mov [jump_target], eax
        mov byte [jump_flag], 1
        ret

.gosub_err:
        mov esi, str_gosub_err
        call output_add_line
        ret

;----------- RETURN -----------
do_return:
        pop rsi
        cmp dword [gosub_sp], 0
        je .ret_err

        dec dword [gosub_sp]
        mov eax, [gosub_sp]
        mov ecx, [gosub_stack + eax * 4]
        ; Return to line after GOSUB
        mov [jump_target], ecx
        mov byte [jump_flag], 1
        ret

.ret_err:
        ret

;----------- INPUT -----------
do_input:
        pop rsi
        call skip_spaces

        ; Check for prompt string
        cmp byte [esi], '"'
        jne .di_var
        inc esi
        mov edi, print_buf
.di_prompt:
        movzx eax, byte [esi]
        cmp al, '"'
        je .di_prompt_end
        cmp al, 0
        je .di_prompt_end
        mov [edi], al
        inc edi
        inc esi
        jmp .di_prompt
.di_prompt_end:
        mov byte [edi], 0
        cmp byte [esi], '"'
        jne .di_show_prompt
        inc esi
        ; Skip separator (;,)
        cmp byte [esi], ';'
        je .di_skip_sep
        cmp byte [esi], ','
        je .di_skip_sep
        jmp .di_show_prompt
.di_skip_sep:
        inc esi
.di_show_prompt:
        push rsi
        mov esi, print_buf
        call output_add_line
        pop rsi
        call skip_spaces

.di_var:
        ; Get variable name
        movzx eax, byte [esi]
        call to_upper_al
        cmp al, 'A'
        jl .di_done
        cmp al, 'Z'
        jg .di_done
        sub al, 'A'
        movzx ebx, al
        mov [input_var], ebx

        ; Set up input wait
        mov byte [input_waiting], 1
        mov byte [input_done], 0
        mov dword [input_len], 0
        mov byte [input_buf], 0

        ; Show input prompt
        push rbx
        mov esi, str_input_prompt
        call output_add_line
        pop rbx

        ; Wait loop - render and poll until input is done
.di_wait:
        call gui_compose
        call draw_all

        ; Draw input text in output area
        cmp dword [input_len], 0
        je .di_no_text
        mov eax, [win_id]
        mov ebx, OUT_X + 20
        mov ecx, OUT_Y + OUT_H - 18
        mov esi, input_buf
        mov edi, 0x00FFFFFF
        call gui_draw_text
.di_no_text:
        ; Draw cursor
        mov eax, [win_id]
        mov ebx, [input_len]
        imul ebx, 8
        add ebx, OUT_X + 20
        mov ecx, OUT_Y + OUT_H - 18
        mov edx, 8
        mov esi, 2
        mov edi, 0x00FFFFFF
        call gui_fill_rect

        call gui_flip
        call gui_poll_event

        cmp eax, EVT_CLOSE
        je .di_abort
        cmp eax, EVT_KEY_PRESS
        jne .di_wait

        ; Handle key in input mode
        mov eax, ebx
        call handle_keypress

        cmp byte [input_done], 1
        jne .di_wait

        ; Parse input value
        mov esi, input_buf
        call parse_number
        mov ebx, [input_var]
        shl ebx, 2
        mov [variables + ebx], eax
        mov byte [input_waiting], 0
.di_done:
        ret

.di_abort:
        mov byte [input_waiting], 0
        mov byte [running], 0
        ret

;----------- DIM (stub) -----------
do_dim:
        pop rsi
        ret

;----------- END -----------
do_end:
        pop rsi
        mov byte [running], 0
        ret

;=======================================================================
; LOGO TURTLE COMMANDS
;=======================================================================
turtle_reset:
        PUSHALL
        mov dword [turtle_x], 0
        mov dword [turtle_y], 0
        mov dword [turtle_heading], 0       ; 0 = North (up)
        mov byte [turtle_pen], 1            ; pen down
        mov byte [turtle_visible], 1
        mov dword [turtle_color], 0x00000000 ; black
        mov dword [stroke_count], 0
        POPALL
        ret

;----------- FORWARD -----------
do_forward:
        pop rsi
        call skip_spaces
        call eval_expr          ; EAX = distance

        ; Move turtle forward by EAX units
        ; heading: 0=North, 90=East, 180=South, 270=West
        ; dx = distance * sin(heading)
        ; dy = distance * cos(heading)

        push rax                ; save distance
        mov ecx, [turtle_heading]

        ; Get sin and cos (scaled by TRIG_SCALE)
        push rcx
        call get_sin            ; EAX = sin(heading) * TRIG_SCALE
        mov ebx, eax            ; EBX = sin
        pop rcx
        push rbx
        call get_cos            ; EAX = cos(heading) * TRIG_SCALE
        mov ecx, eax            ; ECX = cos
        pop rbx                 ; EBX = sin

        pop rax                 ; distance

        ; dx = distance * sin / TRIG_SCALE (but keep scaled)
        push rcx
        imul ebx, eax           ; EBX = distance * sin (scaled by TRIG_SCALE)
        pop rcx
        imul ecx, eax           ; ECX = distance * cos (scaled by TRIG_SCALE)

        ; Old position (scaled)
        mov eax, [turtle_x]
        mov edx, [turtle_y]

        ; New position
        push rax
        push rdx
        add eax, ebx            ; new_x = old_x + dx (all scaled)
        add edx, ecx            ; new_y = old_y + dy

        ; If pen is down, draw line
        cmp byte [turtle_pen], 0
        je .fwd_no_draw

        ; Draw from old to new using Bresenham
        pop rcx                 ; old_y (scaled)
        pop rbx                 ; old_x (scaled)
        push rax
        push rdx
        ; Convert to canvas pixel coords
        sar ebx, 10             ; old_x / TRIG_SCALE
        add ebx, TURTLE_CX
        sar ecx, 10
        mov edi, TURTLE_CY
        sub edi, ecx            ; old_y canvas

        push rax
        sar eax, 10
        add eax, TURTLE_CX     ; new_x canvas
        pop rdx
        push rax
        mov eax, edx

        pop rdx                 ; new_x canvas in EDX
        push rdx

        ; Now draw from (EBX, EDI) to (new_x, new_y_canvas)
        pop rdx                 ; new_x canvas
        pop rax                 ; new_y scaled
        push rax
        sar eax, 10
        mov ecx, TURTLE_CY
        sub ecx, eax            ; new_y canvas

        ; EBX=x1, EDI=y1, EDX=x2, ECX=y2
        push rcx
        push rdx
        push rdi
        push rbx
        call draw_line_bresenham
        add rsp, 16

        pop rdx                 ; restore new_y
        pop rax                 ; restore new_x
        jmp .fwd_update

.fwd_no_draw:
        pop rdx                 ; discard old_y
        pop rbx                 ; discard old_x
                                ; eax=new_x, edx=new_y already set from above
                                ; Wait we need to recalculate:
        ; Actually eax and edx still have new_x and new_y from the adds above

.fwd_update:
        mov [turtle_x], eax
        mov [turtle_y], edx
        ret

;----------- BACK -----------
do_back:
        pop rsi
        call skip_spaces
        call eval_expr
        neg eax
        push rax
        push rsi
        ; Reuse forward logic
        pop rsi
        pop rax
        push rax
        push rsi

        ; Negate and call forward
        pop rsi
        pop rax
        ; We need to push back onto stack properly for forward
        ; Simpler: just negate heading, go forward, restore heading
        push qword [turtle_heading]
        mov ecx, [turtle_heading]
        add ecx, 180
        cmp ecx, 360
        jl .bk_ok
        sub ecx, 360
.bk_ok:
        mov [turtle_heading], ecx
        neg eax
        ; Now call forward logic inline
        push rax
        mov ecx, [turtle_heading]
        push rcx
        call get_sin
        mov ebx, eax
        pop rcx
        push rbx
        call get_cos
        mov ecx, eax
        pop rbx
        pop rax
        push rcx
        imul ebx, eax
        pop rcx
        imul ecx, eax
        mov eax, [turtle_x]
        mov edx, [turtle_y]
        add eax, ebx
        add edx, ecx
        mov [turtle_x], eax
        mov [turtle_y], edx
        ; Draw if pen down...  simplified: just record endpoint
        cmp byte [turtle_pen], 0
        je .bk_nodraw
        sar eax, 10
        add eax, TURTLE_CX
        push rax
        mov eax, edx
        sar eax, 10
        mov ecx, TURTLE_CY
        sub ecx, eax
        pop rax
        call record_stroke
.bk_nodraw:
        pop qword [turtle_heading]
        ret

;----------- LEFT -----------
do_left:
        pop rsi
        call skip_spaces
        call eval_expr
        mov ecx, [turtle_heading]
        sub ecx, eax
.left_norm:
        cmp ecx, 0
        jge .left_ok
        add ecx, 360
        jmp .left_norm
.left_ok:
        cmp ecx, 360
        jl .left_store
        sub ecx, 360
        jmp .left_ok
.left_store:
        mov [turtle_heading], ecx
        ret

;----------- RIGHT -----------
do_right:
        pop rsi
        call skip_spaces
        call eval_expr
        mov ecx, [turtle_heading]
        add ecx, eax
.right_norm:
        cmp ecx, 360
        jl .right_ok
        sub ecx, 360
        jmp .right_norm
.right_ok:
        cmp ecx, 0
        jge .right_store
        add ecx, 360
        jmp .right_ok
.right_store:
        mov [turtle_heading], ecx
        ret

;----------- PENUP -----------
do_penup:
        pop rsi
        mov byte [turtle_pen], 0
        ret

;----------- PENDOWN -----------
do_pendown:
        pop rsi
        mov byte [turtle_pen], 1
        ret

;----------- HOME -----------
do_home:
        pop rsi
        mov dword [turtle_x], 0
        mov dword [turtle_y], 0
        mov dword [turtle_heading], 0
        ret

;----------- CLEARSCREEN -----------
do_clearscreen:
        pop rsi
        call turtle_reset
        ret

;----------- SETCOLOR -----------
do_setcolor:
        pop rsi
        call skip_spaces
        ; Parse color name or number
        call eval_expr
        ; Map number to color
        cmp eax, 0
        je .sc_black
        cmp eax, 1
        je .sc_blue
        cmp eax, 2
        je .sc_green
        cmp eax, 3
        je .sc_cyan
        cmp eax, 4
        je .sc_red
        cmp eax, 5
        je .sc_magenta
        cmp eax, 6
        je .sc_yellow
        cmp eax, 7
        je .sc_white
        ; Default: use as raw color
        mov [turtle_color], eax
        ret
.sc_black:   mov dword [turtle_color], 0x00000000
        ret
.sc_blue:    mov dword [turtle_color], 0x000000FF
        ret
.sc_green:   mov dword [turtle_color], 0x0000CC00
        ret
.sc_cyan:    mov dword [turtle_color], 0x0000CCCC
        ret
.sc_red:     mov dword [turtle_color], 0x00FF0000
        ret
.sc_magenta: mov dword [turtle_color], 0x00CC00CC
        ret
.sc_yellow:  mov dword [turtle_color], 0x00FFFF00
        ret
.sc_white:   mov dword [turtle_color], 0x00FFFFFF
        ret

;----------- CIRCLE -----------
do_circle:
        pop rsi
        call skip_spaces
        call eval_expr          ; radius in EAX
        push rax

        ; Draw circle using midpoint algorithm
        ; Center = turtle position on canvas
        mov ebx, [turtle_x]
        sar ebx, 10
        add ebx, TURTLE_CX     ; cx
        mov ecx, [turtle_y]
        sar ecx, 10
        mov edx, TURTLE_CY
        sub edx, ecx            ; cy

        pop rax                 ; radius
        push rbx                ; save cx
        push rdx                ; save cy

        ; Midpoint circle: x=0, y=radius, d=1-radius
        xor esi, esi            ; x = 0
        mov edi, eax            ; y = radius
        mov ecx, 1
        sub ecx, eax            ; d = 1 - radius

.circle_loop:
        cmp esi, edi
        jg .circle_done

        ; Plot 8 octants
        push rcx
        mov eax, [rsp + 8]      ; cy
        mov ebx, [rsp + 16]     ; cx

        ; (cx+x, cy+y)
        push rsi
        push rdi
        mov ecx, eax
        add ecx, edi            ; cy+y
        add ebx, esi            ; cx+x becomes temp
        push rbx
        sub ebx, esi            ; restore cx
        pop rdx
        ; record (edx, ecx) = (cx+x, cy+y)
        push rax
        push rbx
        mov eax, edx
        call record_stroke
        pop rbx
        pop rax

        ; (cx-x, cy+y)
        mov edx, ebx
        sub edx, esi
        mov ecx, eax
        add ecx, edi
        push rax
        push rbx
        mov eax, edx
        call record_stroke
        pop rbx
        pop rax

        ; (cx+x, cy-y)
        mov edx, ebx
        add edx, esi
        mov ecx, eax
        sub ecx, edi
        push rax
        push rbx
        mov eax, edx
        call record_stroke
        pop rbx
        pop rax

        ; (cx-x, cy-y)
        mov edx, ebx
        sub edx, esi
        mov ecx, eax
        sub ecx, edi
        push rax
        push rbx
        mov eax, edx
        call record_stroke
        pop rbx
        pop rax

        ; (cx+y, cy+x)
        mov edx, ebx
        add edx, edi
        mov ecx, eax
        add ecx, esi
        push rax
        push rbx
        mov eax, edx
        call record_stroke
        pop rbx
        pop rax

        ; (cx-y, cy+x)
        mov edx, ebx
        sub edx, edi
        mov ecx, eax
        add ecx, esi
        push rax
        push rbx
        mov eax, edx
        call record_stroke
        pop rbx
        pop rax

        ; (cx+y, cy-x)
        mov edx, ebx
        add edx, edi
        mov ecx, eax
        sub ecx, esi
        push rax
        push rbx
        mov eax, edx
        call record_stroke
        pop rbx
        pop rax

        ; (cx-y, cy-x)
        mov edx, ebx
        sub edx, edi
        mov ecx, eax
        sub ecx, esi
        push rax
        push rbx
        mov eax, edx
        call record_stroke
        pop rbx
        pop rax

        pop rdi
        pop rsi
        pop rcx

        ; Update d
        cmp ecx, 0
        jl .circle_d_neg
        ; d >= 0: d = d + 2*(x-y) + 5, y--
        mov eax, esi
        sub eax, edi
        shl eax, 1
        add eax, 5
        add ecx, eax
        dec edi
        jmp .circle_inc_x
.circle_d_neg:
        ; d < 0: d = d + 2*x + 3
        mov eax, esi
        shl eax, 1
        add eax, 3
        add ecx, eax
.circle_inc_x:
        inc esi
        jmp .circle_loop

.circle_done:
        pop rdx                 ; cy
        pop rbx                 ; cx
        ret

;----------- REPEAT -----------
do_repeat:
        pop rsi
        call skip_spaces
        call eval_expr          ; count
        push rax

        ; Expect [ ... ]
        call skip_spaces
        cmp byte [esi], '['
        jne .rep_err
        inc esi

        ; Find the block content (up to matching ])
        mov [repeat_body], esi
        ; Find matching ]
        xor ecx, ecx           ; depth
        mov edi, esi
.rep_find_end:
        movzx eax, byte [edi]
        cmp al, 0
        je .rep_err
        cmp al, '['
        jne .rep_not_open
        inc ecx
.rep_not_open:
        cmp al, ']'
        jne .rep_not_close
        cmp ecx, 0
        je .rep_found_end
        dec ecx
.rep_not_close:
        inc edi
        jmp .rep_find_end

.rep_found_end:
        mov byte [edi], 0      ; temporarily null-terminate
        mov [repeat_end], edi

        pop rcx                 ; iteration count
.rep_exec:
        cmp ecx, 0
        jle .rep_restore
        push rcx

        ; Execute each command in the block
        mov esi, [repeat_body]
.rep_cmd_loop:
        call skip_spaces
        cmp byte [esi], 0
        je .rep_cmd_done

        ; Execute one command
        push rsi
        call exec_line_inner
        pop rsi

        ; Advance past current command (find next space or end)
        call skip_to_next_command

        jmp .rep_cmd_loop

.rep_cmd_done:
        pop rcx
        dec ecx
        jmp .rep_exec

.rep_restore:
        mov edi, [repeat_end]
        mov byte [edi], ']'     ; restore the ]
        ret

.rep_err:
        pop rax
        ret

;----------- MAKE -----------
do_make:
        pop rsi
        call skip_spaces
        ; MAKE "varname value
        cmp byte [esi], '"'
        jne .mk_bare
        inc esi
.mk_bare:
        movzx eax, byte [esi]
        call to_upper_al
        cmp al, 'A'
        jl .mk_done
        cmp al, 'Z'
        jg .mk_done
        sub al, 'A'
        movzx ebx, al
        inc esi
        ; Skip closing " if present
        cmp byte [esi], '"'
        jne .mk_val
        inc esi
.mk_val:
        call skip_spaces
        push rbx
        call eval_expr
        pop rbx
        shl ebx, 2
        mov [variables + ebx], eax
.mk_done:
        ret

;----------- SETXY -----------
do_setxy:
        pop rsi
        call skip_spaces
        call eval_expr          ; x
        push rax
        call skip_spaces
        cmp byte [esi], ','
        jne .sxy_no_comma
        inc esi
        call skip_spaces
.sxy_no_comma:
        call eval_expr          ; y
        mov edx, eax
        pop rax

        ; Scale and store
        shl eax, 10
        shl edx, 10
        mov [turtle_x], eax
        mov [turtle_y], edx
        ret

;=======================================================================
; PILOT COMMANDS
;=======================================================================

;----------- T: Type text -----------
exec_pilot_type:
        call skip_spaces
        ; Print the rest of the line with variable interpolation
        mov edi, print_buf
        mov dword [print_pos], 0

.pt_loop:
        movzx eax, byte [esi]
        cmp al, 0
        je .pt_done
        cmp al, '*'
        je .pt_var
        mov ecx, [print_pos]
        cmp ecx, 70
        jge .pt_skip
        mov [print_buf + ecx], al
        inc dword [print_pos]
.pt_skip:
        inc esi
        jmp .pt_loop

.pt_var:
        inc esi
        movzx eax, byte [esi]
        call to_upper_al
        cmp al, 'A'
        jl .pt_loop
        cmp al, 'Z'
        jg .pt_loop
        sub al, 'A'
        movzx ebx, al
        shl ebx, 2
        mov eax, [variables + ebx]
        inc esi
        cmp byte [esi], '*'
        jne .pt_no_close
        inc esi
.pt_no_close:
        push rsi
        mov edi, int_buf
        call int_to_str
        mov esi, int_buf
        call append_print_buf
        pop rsi
        jmp .pt_loop

.pt_done:
        mov ecx, [print_pos]
        mov byte [print_buf + ecx], 0
        mov esi, print_buf
        call output_add_line
        POPALL
        ret

;----------- A: Accept input -----------
exec_pilot_accept:
        ; Store prompt text (if any) and get input
        call skip_spaces
        ; Save variable name from arg
        movzx eax, byte [esi]
        call to_upper_al
        cmp al, 'A'
        jl .pa_default
        cmp al, 'Z'
        jg .pa_default

        sub al, 'A'
        movzx ebx, al
        mov [input_var], ebx
        jmp .pa_get
.pa_default:
        mov dword [input_var], 8        ; default to 'I' (8th variable)

.pa_get:
        mov byte [input_waiting], 1
        mov byte [input_done], 0
        mov dword [input_len], 0
        mov byte [input_buf], 0

        mov esi, str_input_prompt
        call output_add_line

.pa_wait:
        call gui_compose
        call draw_all

        cmp dword [input_len], 0
        je .pa_no_text
        mov eax, [win_id]
        mov ebx, OUT_X + 20
        mov ecx, OUT_Y + OUT_H - 18
        mov esi, input_buf
        mov edi, 0x00FFFFFF
        call gui_draw_text
.pa_no_text:
        call gui_flip
        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .pa_abort
        cmp eax, EVT_KEY_PRESS
        jne .pa_wait
        mov eax, ebx
        call handle_keypress
        cmp byte [input_done], 1
        jne .pa_wait

        ; Parse value
        mov esi, input_buf
        call parse_number
        mov ebx, [input_var]
        shl ebx, 2
        mov [variables + ebx], eax
        mov byte [input_waiting], 0
        POPALL
        ret

.pa_abort:
        mov byte [input_waiting], 0
        mov byte [running], 0
        POPALL
        ret

;----------- M: Match -----------
exec_pilot_match:
        ; Simple: check if last input matches patterns
        ; For now, just set match_flag based on simple compare
        call skip_spaces
        ; Get last input value and compare with comma-separated patterns
        ; Simple implementation: check if variables[8] ('I') matches val
        mov byte [match_flag], 0
        ; Parse comma-separated values
.pm_loop:
        call skip_spaces
        cmp byte [esi], 0
        je .pm_done
        push rsi
        call eval_expr
        pop rsi
        mov ebx, [variables + 8*4]     ; variable I
        cmp eax, ebx
        jne .pm_no_match
        mov byte [match_flag], 1
        jmp .pm_done
.pm_no_match:
        ; Skip to next comma
.pm_skip:
        cmp byte [esi], ','
        je .pm_comma
        cmp byte [esi], 0
        je .pm_done
        inc esi
        jmp .pm_skip
.pm_comma:
        inc esi
        jmp .pm_loop

.pm_done:
        POPALL
        ret

;----------- Y: Yes (if match) -----------
exec_pilot_yes:
        cmp byte [match_flag], 1
        jne .py_skip
        call exec_line_inner
.py_skip:
        POPALL
        ret

;----------- N: No (if not match) -----------
exec_pilot_no:
        cmp byte [match_flag], 0
        jne .pn_skip
        call exec_line_inner
.pn_skip:
        POPALL
        ret

;----------- J: Jump -----------
exec_pilot_jump:
        call skip_spaces
        ; Skip leading *
        cmp byte [esi], '*'
        jne .pj_find
        inc esi
.pj_find:
        call find_label
        cmp eax, -1
        je .pj_err
        mov [jump_target], eax
        mov byte [jump_flag], 1
        POPALL
        ret
.pj_err:
        mov esi, str_label_err
        call output_add_line
        POPALL
        ret

;----------- U: Use (set variable) -----------
exec_pilot_use:
        call skip_spaces
        ; U:X=5
        movzx eax, byte [esi]
        call to_upper_al
        cmp al, 'A'
        jl .pu_done
        cmp al, 'Z'
        jg .pu_done
        sub al, 'A'
        movzx ebx, al
        inc esi
        call skip_spaces
        cmp byte [esi], '='
        jne .pu_done
        inc esi
        call skip_spaces
        push rbx
        call eval_expr
        pop rbx
        shl ebx, 2
        mov [variables + ebx], eax
.pu_done:
        POPALL
        ret

;----------- E: End -----------
exec_pilot_end:
        mov byte [running], 0
        POPALL
        ret

;----------- C: Compute -----------
exec_pilot_compute:
        call skip_spaces
        ; C:X=5+3  or C:*label
        cmp byte [esi], '*'
        je exec_pilot_jump      ; treat as subroutine call
        ; Otherwise: assignment like U:
        jmp exec_pilot_use

;=======================================================================
; EXPRESSION EVALUATOR (Integer)
;=======================================================================
eval_expr:
        ; Evaluate integer expression at ESI, return in EAX
        call skip_spaces
        call eval_add_sub
        ret

eval_add_sub:
        call eval_mul_div
        push rax
.eas_loop:
        call skip_spaces
        cmp byte [esi], '+'
        je .eas_add
        cmp byte [esi], '-'
        je .eas_sub
        pop rax
        ret
.eas_add:
        inc esi
        call eval_mul_div
        pop rbx
        add eax, ebx
        push rax
        jmp .eas_loop
.eas_sub:
        inc esi
        call eval_mul_div
        pop rbx
        sub ebx, eax
        mov eax, ebx
        push rax
        jmp .eas_loop

eval_mul_div:
        call eval_unary
        push rax
.emd_loop:
        call skip_spaces
        cmp byte [esi], '*'
        je .emd_mul
        cmp byte [esi], '/'
        je .emd_div
        cmp byte [esi], '%'
        je .emd_mod
        pop rax
        ret
.emd_mul:
        inc esi
        call eval_unary
        pop rbx
        imul eax, ebx
        push rax
        jmp .emd_loop
.emd_div:
        inc esi
        call eval_unary
        test eax, eax
        jz .emd_divzero
        mov ecx, eax
        pop rax
        cdq
        idiv ecx
        push rax
        jmp .emd_loop
.emd_mod:
        inc esi
        call eval_unary
        test eax, eax
        jz .emd_divzero
        mov ecx, eax
        pop rax
        cdq
        idiv ecx
        mov eax, edx
        push rax
        jmp .emd_loop
.emd_divzero:
        pop rax
        xor eax, eax
        push rax
        jmp .emd_loop

eval_unary:
        call skip_spaces
        cmp byte [esi], '-'
        je .eu_neg
        cmp byte [esi], '('
        je .eu_paren
        jmp eval_atom

.eu_neg:
        inc esi
        call eval_unary
        neg eax
        ret

.eu_paren:
        inc esi
        call eval_expr
        call skip_spaces
        cmp byte [esi], ')'
        jne .eu_done
        inc esi
.eu_done:
        ret

eval_atom:
        call skip_spaces
        movzx eax, byte [esi]

        ; Number?
        cmp al, '0'
        jl .ea_check_var
        cmp al, '9'
        jg .ea_check_var
        jmp parse_number

.ea_check_var:
        ; Variable A-Z?
        call to_upper_al
        cmp al, 'A'
        jl .ea_check_special
        cmp al, 'Z'
        jg .ea_check_special

        ; Check for :VAR (Logo variable reference)
        cmp byte [esi], ':'
        je .ea_logo_var

        sub al, 'A'
        movzx eax, al
        shl eax, 2
        mov eax, [variables + eax]
        inc esi
        ret

.ea_logo_var:
        inc esi                 ; skip :
        movzx eax, byte [esi]
        call to_upper_al
        sub al, 'A'
        movzx eax, al
        shl eax, 2
        mov eax, [variables + eax]
        inc esi
        ret

.ea_check_special:
        ; RND function
        cmp byte [esi], 'R'
        jne .ea_zero
        cmp byte [esi+1], 'N'
        jne .ea_zero
        cmp byte [esi+2], 'D'
        jne .ea_zero
        add esi, 3
        ; Optional (max)
        call skip_spaces
        cmp byte [esi], '('
        jne .ea_rnd_plain
        inc esi
        call eval_expr
        push rax
        call skip_spaces
        cmp byte [esi], ')'
        jne .ea_rnd_close
        inc esi
.ea_rnd_close:
        pop rcx
        ; Random 1..ecx
        push rcx
        rdtsc
        pop rcx
        test ecx, ecx
        jz .ea_rnd_plain2
        xor edx, edx
        div ecx
        mov eax, edx
        inc eax
        ret
.ea_rnd_plain:
        rdtsc
        and eax, 0x7FFF
        ret
.ea_rnd_plain2:
        rdtsc
        and eax, 0x7FFF
        ret

.ea_zero:
        xor eax, eax
        ret

;=======================================================================
; UTILITY FUNCTIONS
;=======================================================================

; Parse decimal number from ESI -> EAX, advance ESI
parse_number:
        xor eax, eax
        xor ecx, ecx           ; sign flag
        cmp byte [esi], '-'
        jne .pn_loop
        mov ecx, 1
        inc esi
.pn_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jl .pn_done
        cmp dl, '9'
        jg .pn_done
        imul eax, 10
        sub dl, '0'
        movzx edx, dl
        add eax, edx
        inc esi
        jmp .pn_loop
.pn_done:
        test ecx, ecx
        jz .pn_ret
        neg eax
.pn_ret:
        ret

; Skip whitespace at ESI
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

; Skip to next command in REPEAT block
skip_to_next_command:
        ; Skip past current command (letters/numbers/operators)
        ; Stop at space, null, [, ]
.stnc_loop:
        movzx eax, byte [esi]
        cmp al, 0
        je .stnc_done
        cmp al, ' '
        jne .stnc_not_space

        ; At space: check if next word is a command keyword
        call skip_spaces
        ; Check first char of next word - if uppercase, probably a command
        movzx eax, byte [esi]
        cmp al, 'A'
        jl .stnc_done
        cmp al, 'z'
        jg .stnc_done
        ret                     ; at start of next command

.stnc_not_space:
        inc esi
        jmp .stnc_loop
.stnc_done:
        ret

; Uppercase AL
to_upper_al:
        cmp al, 'a'
        jl .tua_done
        cmp al, 'z'
        jg .tua_done
        sub al, 32
.tua_done:
        ret

; Match keyword: ESI=input, EDI=keyword (uppercase, null-terminated)
; Sets CF if match (ESI advanced past keyword+space), clears CF if no match
match_keyword:
        push rbx
        push rdx
        mov ebx, esi            ; save start
.mk_loop:
        movzx eax, byte [edi]
        test al, al
        jz .mk_matched
        movzx edx, byte [esi]
        cmp dl, 'a'
        jl .mk_cmp
        cmp dl, 'z'
        jg .mk_cmp
        sub dl, 32
.mk_cmp:
        cmp dl, al
        jne .mk_fail
        inc esi
        inc edi
        jmp .mk_loop

.mk_matched:
        ; Check that next char is space, null, (, =, or other delimiter
        movzx eax, byte [esi]
        cmp al, ' '
        je .mk_ok
        cmp al, 0
        je .mk_ok
        cmp al, '('
        je .mk_ok
        cmp al, '='
        je .mk_ok
        cmp al, '"'
        je .mk_ok
        cmp al, ':'
        je .mk_ok
        ; Check for digit following keyword like GOTO10 - not a match
        ; Only match if delimiter follows
        jmp .mk_fail

.mk_ok:
        ; Skip trailing space
        cmp byte [esi], ' '
        jne .mk_success
        inc esi
.mk_success:
        stc                     ; set carry = match
        pop rdx
        pop rbx
        ret

.mk_fail:
        mov esi, ebx            ; restore ESI
        clc                     ; clear carry = no match
        pop rdx
        pop rbx
        ret

; String length: ESI -> EAX
strlen:
        push rsi
        xor eax, eax
.sl_loop:
        cmp byte [esi], 0
        je .sl_done
        inc eax
        inc esi
        jmp .sl_loop
.sl_done:
        pop rsi
        ret

; Compare strings ESI and EDI until null/space, return EAX=1 if equal
str_equal:
        push rsi
        push rdi
.se_loop:
        movzx eax, byte [esi]
        movzx ebx, byte [edi]
        ; Check terminators
        cmp al, ' '
        je .se_check_end
        cmp al, 0
        je .se_check_end2
        cmp bl, 0
        je .se_fail
        cmp al, bl
        jne .se_fail
        inc esi
        inc edi
        jmp .se_loop

.se_check_end:
        cmp bl, 0
        je .se_match
        jmp .se_fail
.se_check_end2:
        cmp bl, 0
        jne .se_fail
.se_match:
        mov eax, 1
        pop rdi
        pop rsi
        ret
.se_fail:
        xor eax, eax
        pop rdi
        pop rsi
        ret

; Integer to string: EAX -> buffer at EDI (null-terminated)
int_to_str:
        PUSHALL
        test eax, eax
        jns .its_pos
        mov byte [edi], '-'
        inc edi
        neg eax
.its_pos:
        ; Push digits
        xor ecx, ecx
        mov ebx, 10
.its_push:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        test eax, eax
        jnz .its_push
.its_pop:
        pop rax
        add al, '0'
        mov [edi], al
        inc edi
        dec ecx
        jnz .its_pop
        mov byte [edi], 0
        POPALL
        ret

; Integer to string at EDI, advance EDI past digits
int_to_str_at:
        push rax
        push rbx
        push rcx
        push rdx
        test eax, eax
        jns .itsa_pos
        mov byte [edi], '-'
        inc edi
        neg eax
.itsa_pos:
        xor ecx, ecx
        mov ebx, 10
.itsa_push:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        test eax, eax
        jnz .itsa_push
.itsa_pop:
        pop rax
        add al, '0'
        mov [edi], al
        inc edi
        dec ecx
        jnz .itsa_pop
        pop rdx
        pop rcx
        pop rbx
        pop rax
        ret

; Append string from ESI to print_buf
append_print_buf:
        push rax
        push rcx
.apb_loop:
        movzx eax, byte [esi]
        test al, al
        jz .apb_done
        mov ecx, [print_pos]
        cmp ecx, 70
        jge .apb_done
        mov [print_buf + ecx], al
        inc dword [print_pos]
        inc esi
        jmp .apb_loop
.apb_done:
        pop rcx
        pop rax
        ret

; Record a pixel stroke: EAX=canvas_x, ECX=canvas_y
record_stroke:
        PUSHALL
        ; Bounds check
        cmp eax, 0
        jl .rs_done
        cmp eax, CV_W
        jge .rs_done
        cmp ecx, 0
        jl .rs_done
        cmp ecx, CV_H
        jge .rs_done

        mov edx, [stroke_count]
        cmp edx, 16000
        jge .rs_done

        imul edx, 8
        mov [stroke_buf + edx], ax
        mov [stroke_buf + edx + 2], cx
        mov ebx, [turtle_color]
        mov [stroke_buf + edx + 4], ebx
        inc dword [stroke_count]
.rs_done:
        POPALL
        ret

; Draw line using Bresenham's algorithm
; Parameters on stack: x1, y1, x2, y2
draw_line_bresenham:
        push rbp
        mov rbp, rsp
        PUSHALL

        mov eax, [ebp + 8]      ; x1
        mov ebx, [ebp + 12]     ; y1
        mov ecx, [ebp + 16]     ; x2
        mov edx, [ebp + 20]     ; y2

        ; Store in local vars
        mov [bres_x1], eax
        mov [bres_y1], ebx
        mov [bres_x2], ecx
        mov [bres_y2], edx

        ; dx = abs(x2-x1), dy = -abs(y2-y1)
        sub ecx, eax            ; dx
        mov esi, 1              ; sx
        cmp ecx, 0
        jge .bres_dx_pos
        neg ecx
        neg esi
.bres_dx_pos:
        mov [bres_dx], ecx
        mov [bres_sx], esi

        sub edx, ebx            ; dy
        mov edi, 1              ; sy
        cmp edx, 0
        jge .bres_dy_pos
        neg edx
        neg edi
.bres_dy_pos:
        neg edx                 ; dy = -abs(dy)
        mov [bres_dy], edx
        mov [bres_sy], edi

        ; err = dx + dy
        mov eax, [bres_dx]
        add eax, [bres_dy]
        mov [bres_err], eax

        mov eax, [bres_x1]
        mov ebx, [bres_y1]

.bres_loop:
        ; Record pixel
        push rax
        push rbx
        mov ecx, ebx
        call record_stroke
        pop rbx
        pop rax

        ; Check if done
        cmp eax, [bres_x2]
        jne .bres_continue
        cmp ebx, [bres_y2]
        je .bres_done

.bres_continue:
        mov ecx, [bres_err]
        shl ecx, 1              ; e2 = 2*err

        ; if e2 >= dy
        cmp ecx, [bres_dy]
        jl .bres_check_dx
        mov edx, [bres_dy]
        add [bres_err], edx
        add eax, [bres_sx]

.bres_check_dx:
        ; if e2 <= dx
        cmp ecx, [bres_dx]
        jg .bres_loop
        mov edx, [bres_dx]
        add [bres_err], edx
        add ebx, [bres_sy]

        jmp .bres_loop

.bres_done:
        POPALL
        pop rbp
        ret

; Trigonometry: get_sin(ECX=degrees) -> EAX = sin*1024
; Uses pre-computed lookup table for quadrant 1, derives rest
get_sin:
        push rbx
        push rcx
        push rdx

        ; Normalize to 0-359
.gs_norm:
        cmp ecx, 360
        jl .gs_norm2
        sub ecx, 360
        jmp .gs_norm
.gs_norm2:
        cmp ecx, 0
        jge .gs_calc
        add ecx, 360
        jmp .gs_norm2

.gs_calc:
        ; Determine quadrant
        cmp ecx, 90
        jle .gs_q1
        cmp ecx, 180
        jle .gs_q2
        cmp ecx, 270
        jle .gs_q3
        jmp .gs_q4

.gs_q1: ; 0-90: sin(x) = table[x]
        mov eax, ecx
        mov eax, [sin_table + eax * 4]
        jmp .gs_ret

.gs_q2: ; 91-180: sin(x) = sin(180-x)
        mov eax, 180
        sub eax, ecx
        mov eax, [sin_table + eax * 4]
        jmp .gs_ret

.gs_q3: ; 181-270: sin(x) = -sin(x-180)
        mov eax, ecx
        sub eax, 180
        mov eax, [sin_table + eax * 4]
        neg eax
        jmp .gs_ret

.gs_q4: ; 271-359: sin(x) = -sin(360-x)
        mov eax, 360
        sub eax, ecx
        mov eax, [sin_table + eax * 4]
        neg eax

.gs_ret:
        pop rdx
        pop rcx
        pop rbx
        ret

; get_cos(ECX=degrees) -> EAX = cos*1024
get_cos:
        push rcx
        add ecx, 90            ; cos(x) = sin(x+90)
        call get_sin
        pop rcx
        ret

;=======================================================================
; DATA SECTION
;=======================================================================
section .data

title_str:      db "Time Warp", 0
str_run:        db "Run", 0
str_stop:       db "Stop", 0
str_clear:      db "Clear", 0
str_new:        db " New", 0
str_title_bar:  db "Time Warp for Mellivora", 0
str_ready:      db "Ready", 0
str_running:    db "Running...", 0
str_hint:       db "F5=Run  ESC=Stop  Ctrl+S=Save", 0
str_overflow:   db "Error: max iterations reached", 0
str_label_err:  db "Error: label not found", 0
str_gosub_err:  db "Error: GOSUB stack overflow", 0
str_input_prompt: db ">> Enter value:", 0
str_saved:      db "File saved.", 0
str_no_name:    db "No filename (use arg)", 0
str_load_err:   db "Error: cannot load file", 0
str_empty:      db " ", 0

; Keywords
kw_print:       db "PRINT", 0
kw_let:         db "LET", 0
kw_if:          db "IF", 0
kw_for:         db "FOR", 0
kw_next:        db "NEXT", 0
kw_goto:        db "GOTO", 0
kw_gosub:       db "GOSUB", 0
kw_return:      db "RETURN", 0
kw_input:       db "INPUT", 0
kw_dim:         db "DIM", 0
kw_end:         db "END", 0
kw_then:        db "THEN", 0
kw_to:          db "TO", 0

; Logo keywords
kw_forward:     db "FORWARD", 0
kw_fd:          db "FD", 0
kw_back:        db "BACK", 0
kw_bk:          db "BK", 0
kw_left:        db "LEFT", 0
kw_lt:          db "LT", 0
kw_right:       db "RIGHT", 0
kw_rt:          db "RT", 0
kw_penup:       db "PENUP", 0
kw_pu:          db "PU", 0
kw_pendown:     db "PENDOWN", 0
kw_pd:          db "PD", 0
kw_home:        db "HOME", 0
kw_clearscreen: db "CLEARSCREEN", 0
kw_cs:          db "CS", 0
kw_setcolor:    db "SETCOLOR", 0
kw_circle:      db "CIRCLE", 0
kw_repeat:      db "REPEAT", 0
kw_make:        db "MAKE", 0
kw_setxy:       db "SETXY", 0

; Sin lookup table (0-90 degrees, scaled by 1024)
; sin(0)=0, sin(30)=512, sin(45)=724, sin(60)=887, sin(90)=1024
sin_table:
        dd 0, 18, 36, 54, 71, 89, 107, 125, 143, 160          ; 0-9
        dd 178, 195, 213, 230, 248, 265, 282, 299, 316, 333   ; 10-19
        dd 350, 367, 384, 400, 416, 433, 449, 465, 481, 497   ; 20-29
        dd 512, 527, 543, 558, 573, 587, 602, 616, 631, 644   ; 30-39
        dd 658, 672, 685, 699, 711, 724, 737, 749, 761, 773   ; 40-49
        dd 784, 796, 807, 818, 828, 839, 849, 859, 868, 878   ; 50-59
        dd 887, 896, 904, 913, 920, 928, 936, 943, 950, 956   ; 60-69
        dd 962, 968, 974, 979, 984, 989, 994, 998, 1002, 1005 ; 70-79
        dd 1009, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024 ; 80-89
        dd 1024                                                 ; 90

section .bss

; Window
win_id:         resd 1
has_arg:        resb 1

; Editor state
text_buf:       resb MAX_LINES * LINE_LEN       ; 12800 bytes
cur_line:       resd 1
cur_col:        resd 1
scroll_y:       resd 1
num_lines:      resd 1

; Interpreter state
variables:      resd VAR_COUNT                   ; 26 integer variables A-Z
labels:         resb MAX_LABELS * (LABEL_NAME_LEN + 4) ; label table
label_count:    resd 1
gosub_stack:    resd GOSUB_DEPTH
gosub_sp:       resd 1
for_stack:      resd FOR_DEPTH * 3               ; var_idx, end_val, line
for_sp:         resd 1
interp_line:    resd 1
iteration:      resd 1
running:        resb 1
jump_flag:      resb 1
jump_target:    resd 1
match_flag:     resb 1

; Turtle state
turtle_x:       resd 1          ; scaled by TRIG_SCALE
turtle_y:       resd 1
turtle_heading: resd 1          ; degrees, 0=North
turtle_pen:     resb 1          ; 1=down, 0=up
turtle_visible: resb 1
turtle_color:   resd 1

; Stroke buffer for canvas
stroke_buf:     resb 16000 * 8  ; canvas pixel strokes
stroke_count:   resd 1

; Output panel
output_buf:     resb OUT_LINES * OUT_LINE_LEN
out_count:      resd 1
out_scroll:     resd 1

; Input handling
input_buf:      resb 64
input_len:      resd 1
input_waiting:  resb 1
input_done:     resb 1
input_var:      resd 1

; File I/O
file_buf:       resb 16384
file_size:      resd 1
arg_buf:        resb 64

; Temp buffers
print_buf:      resb 80
print_pos:      resd 1
int_buf:        resb 16
line_num_buf:   resb 8
char_tmp:       resb 4
status_buf:     resb 80

; Bresenham line drawing
bres_x1:        resd 1
bres_y1:        resd 1
bres_x2:        resd 1
bres_y2:        resd 1
bres_dx:        resd 1
bres_dy:        resd 1
bres_sx:        resd 1
bres_sy:        resd 1
bres_err:       resd 1

; REPEAT state
repeat_body:    resd 1
repeat_end:     resd 1
