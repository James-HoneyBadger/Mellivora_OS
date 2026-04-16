; edit.asm - Full-screen text editor for Mellivora OS v2.2
;
; Controls:
;   Arrow keys  : Move cursor (with line-wrap)
;   Ctrl+A      : Beginning of line
;   Ctrl+E      : End of line
;   Ctrl+U      : Page Up
;   Ctrl+D      : Page Down
;   Ctrl+K      : Kill (delete) to end of line
;   Ctrl+L      : Force full redraw
;   Ctrl+S      : Save file
;   Ctrl+Q/ESC  : Quit
;   Enter       : Insert newline
;   Backspace   : Delete char before cursor
;   0x7F        : Delete char at cursor
;
; Usage: edit [filename]     (defaults to scratch.txt)

%include "syscalls.inc"

section .text

;=== Layout constants ===
MAX_LINES       equ 500
MAX_LINE_LEN    equ 240
EDIT_ROWS       equ 23
TEXT_START      equ 1
STATUS_ROW      equ 24
FILE_BUF_SIZE   equ 65536

;=== Color attributes ===
COL_HEADER      equ 0x1F
COL_STATUS      equ 0x70
COL_TEXT        equ 0x07
COL_SAVE_OK     equ 0x2F
COL_WARN        equ 0x4F

;=== Control key codes ===
KEY_CTRL_A      equ 0x01
KEY_CTRL_D      equ 0x04
KEY_CTRL_E      equ 0x05
KEY_CTRL_K      equ 0x0B
KEY_CTRL_L      equ 0x0C
KEY_CTRL_Q      equ 0x11
KEY_CTRL_S      equ 0x13
KEY_CTRL_U      equ 0x15
KEY_ENTER       equ 0x0D
KEY_BACKSPACE   equ 0x08
KEY_DEL         equ 0x7F
KEY_ESC         equ 0x1B

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .use_default
        mov esi, arg_buf
        mov edi, filename
        mov ecx, 63
.copy_fn:
        lodsb
        test al, al
        jz .fn_done
        cmp al, ' '
        je .fn_done
        stosb
        dec ecx
        jnz .copy_fn
.fn_done:
        mov byte [edi], 0
        jmp .init
.use_default:
        mov esi, str_default_fn
        mov edi, filename
.ud_loop:
        lodsb
        stosb
        test al, al
        jnz .ud_loop
.init:
        xor eax, eax
        mov [cursor_x],  eax
        mov [cursor_y],  eax
        mov [scroll_x],  eax
        mov [scroll_y],  eax
        mov [num_lines], dword 1
        mov [modified],  byte 0
        mov edi, text_buf
        mov ecx, (MAX_LINES * MAX_LINE_LEN) / 4
        rep stosd
        call load_file
        call full_redraw

.main_loop:
        call draw_status
        call set_hw_cursor
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, KEY_ESC
        je .do_quit
        cmp al, KEY_CTRL_Q
        je .do_quit
        cmp al, KEY_CTRL_S
        je .do_save
        cmp al, KEY_CTRL_K
        je .do_kill
        cmp al, KEY_CTRL_L
        je .do_refresh
        cmp al, KEY_CTRL_A
        je .do_home
        cmp al, KEY_CTRL_E
        je .do_end
        cmp al, KEY_CTRL_U
        je .do_pgup
        cmp al, KEY_CTRL_D
        je .do_pgdn
        cmp al, KEY_UP
        je .do_up
        cmp al, KEY_DOWN
        je .do_down
        cmp al, KEY_LEFT
        je .do_left
        cmp al, KEY_RIGHT
        je .do_right
        cmp al, KEY_ENTER
        je .do_enter
        cmp al, KEY_BACKSPACE
        je .do_backspace
        cmp al, KEY_DEL
        je .do_del
        cmp al, 0x20
        jb .main_loop
        cmp al, 0x7E
        ja .main_loop
        call insert_char
        call draw_cur_line
        jmp .main_loop

.do_up:
        cmp dword [cursor_y], 0
        je .main_loop
        dec dword [cursor_y]
        call clamp_x
        call adjust_scroll_y
        call draw_text_area
        jmp .main_loop
.do_down:
        mov eax, [cursor_y]
        inc eax
        cmp eax, [num_lines]
        jge .main_loop
        mov [cursor_y], eax
        call clamp_x
        call adjust_scroll_y
        call draw_text_area
        jmp .main_loop
.do_left:
        cmp dword [cursor_x], 0
        jne .left_step
        cmp dword [cursor_y], 0
        je .main_loop
        dec dword [cursor_y]
        call line_len
        mov [cursor_x], eax
        call adjust_scroll_y
        call adjust_scroll_x
        call draw_text_area
        jmp .main_loop
.left_step:
        dec dword [cursor_x]
        call adjust_scroll_x
        jmp .main_loop
.do_right:
        call line_len
        cmp [cursor_x], eax
        jb .right_step
        mov eax, [cursor_y]
        inc eax
        cmp eax, [num_lines]
        jge .main_loop
        mov [cursor_y], eax
        mov dword [cursor_x], 0
        mov dword [scroll_x], 0
        call adjust_scroll_y
        call draw_text_area
        jmp .main_loop
.right_step:
        inc dword [cursor_x]
        call adjust_scroll_x
        jmp .main_loop
.do_home:
        mov dword [cursor_x], 0
        mov dword [scroll_x], 0
        jmp .main_loop
.do_end:
        call line_len
        mov [cursor_x], eax
        call adjust_scroll_x
        jmp .main_loop
.do_pgup:
        mov eax, [cursor_y]
        cmp eax, EDIT_ROWS
        jle .pgup_zero
        sub eax, EDIT_ROWS
        mov [cursor_y], eax
        mov [scroll_y], eax
        jmp .pgup_done
.pgup_zero:
        mov dword [cursor_y], 0
        mov dword [scroll_y], 0
.pgup_done:
        call clamp_x
        call draw_text_area
        jmp .main_loop
.do_pgdn:
        mov eax, [num_lines]
        dec eax
        mov ebx, [cursor_y]
        add ebx, EDIT_ROWS
        cmp ebx, eax
        jle .pgdn_ok
        mov ebx, eax
.pgdn_ok:
        mov [cursor_y], ebx
        call clamp_x
        call adjust_scroll_y
        call draw_text_area
        jmp .main_loop
.do_refresh:
        call full_redraw
        jmp .main_loop
.do_kill:
        call kill_line
        call draw_cur_line
        jmp .main_loop
.do_enter:
        call split_line
        call draw_text_area
        jmp .main_loop
.do_backspace:
        cmp dword [cursor_x], 0
        jne .bs_inline
        cmp dword [cursor_y], 0
        je .main_loop
        call join_up
        call draw_text_area
        jmp .main_loop
.bs_inline:
        dec dword [cursor_x]
        call delete_at_cursor
        call draw_cur_line
        call adjust_scroll_x
        jmp .main_loop
.do_del:
        call line_len
        cmp [cursor_x], eax
        jb .del_char
        mov eax, [cursor_y]
        inc eax
        cmp eax, [num_lines]
        jge .main_loop
        call join_down
        call draw_text_area
        jmp .main_loop
.del_char:
        call delete_at_cursor
        call draw_cur_line
        jmp .main_loop
.do_save:
        call save_file
        call draw_header
        jmp .main_loop
.do_quit:
        cmp byte [modified], 0
        je .exit_clean
        call draw_quit_prompt
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je .exit_clean
        cmp al, 'Y'
        je .exit_clean
        call full_redraw
        jmp .main_loop
.exit_clean:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TEXT
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; get_line_ptr: EAX=line# -> ESI=pointer
get_line_ptr:
        push rdx
        imul eax, MAX_LINE_LEN
        lea esi, [text_buf + eax]
        pop rdx
        ret

; get_line_len: EAX=line# -> EAX=length
get_line_len:
        call get_line_ptr
        xor ecx, ecx
.gll_scan:
        cmp byte [esi + ecx], 0
        je .gll_done
        inc ecx
        cmp ecx, MAX_LINE_LEN - 1
        jge .gll_done
        jmp .gll_scan
.gll_done:
        mov eax, ecx
        ret

; line_len: -> EAX=length of cursor_y line
line_len:
        push rdx
        mov eax, [cursor_y]
        call get_line_len
        pop rdx
        ret

; clamp_x: clamp cursor_x to <= current line length
clamp_x:
        push rax
        push rdx
        call line_len
        cmp [cursor_x], eax
        jle .cx_ok
        mov [cursor_x], eax
.cx_ok:
        pop rdx
        pop rax
        ret

; adjust_scroll_y
adjust_scroll_y:
        push rax
        push rbx
        mov eax, [cursor_y]
        cmp eax, [scroll_y]
        jge .asy_down
        mov [scroll_y], eax
        jmp .asy_done
.asy_down:
        mov ebx, [scroll_y]
        add ebx, EDIT_ROWS - 1
        cmp eax, ebx
        jle .asy_done
        sub eax, EDIT_ROWS - 1
        mov [scroll_y], eax
.asy_done:
        pop rbx
        pop rax
        ret

; adjust_scroll_x
adjust_scroll_x:
        push rax
        push rbx
        mov eax, [cursor_x]
        cmp eax, [scroll_x]
        jge .asx_right
        mov [scroll_x], eax
        jmp .asx_done
.asx_right:
        mov ebx, [scroll_x]
        add ebx, 79
        cmp eax, ebx
        jle .asx_done
        mov ebx, eax
        sub ebx, 79
        mov [scroll_x], ebx
.asx_done:
        pop rbx
        pop rax
        ret

full_redraw:
        mov eax, SYS_CLEAR
        int 0x80
        call draw_header
        call draw_text_area
        ret

; draw_header: SYS_SETCURSOR EBX=col, ECX=row
draw_header:
        PUSHALL
        mov edi, line_buf
        mov al, ' '
        mov ecx, 80
        rep stosb
        mov byte [line_buf + 80], 0
        mov edi, line_buf + 1
        mov esi, str_hdr_prefix
        call strcat_edi
        mov esi, filename
        call strcat_edi
        cmp byte [modified], 0
        je .dh_no_mod
        mov esi, str_modified
        call strcat_edi
.dh_no_mod:
        mov edi, line_buf + 55
        mov esi, str_hdr_hints
        call strcat_edi
        mov byte [line_buf + 80], 0
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_HEADER
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line_buf
        int 0x80
        POPALL
        ret

; draw_text_area: render all EDIT_ROWS text lines
draw_text_area:
        PUSHALL
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, TEXT_START
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TEXT
        int 0x80
        xor edx, edx
.dta_row:
        cmp edx, EDIT_ROWS
        jge .dta_done
        mov eax, [scroll_y]
        add eax, edx
        cmp eax, [num_lines]
        jge .dta_empty
        push rdx
        push rax
        call get_line_ptr
        add esi, [scroll_x]
        mov edi, line_buf
        mov ecx, 80
.dta_copy:
        mov al, [esi]
        test al, al
        jz .dta_pad
        cmp al, 0x20
        jb .dta_ctrl
        stosb
        inc esi
        dec ecx
        jnz .dta_copy
        jmp .dta_print
.dta_ctrl:
        mov byte [edi], ' '
        inc edi
        inc esi
        dec ecx
        jnz .dta_copy
        jmp .dta_print
.dta_pad:
        mov al, ' '
        rep stosb
        jmp .dta_print
.dta_empty:
        push rdx
        push rax
        mov edi, line_buf
        mov byte [edi], '~'
        inc edi
        mov al, ' '
        mov ecx, 79
        rep stosb
.dta_print:
        mov byte [line_buf + 80], 0
        mov eax, SYS_PRINT
        mov ebx, line_buf
        int 0x80
        pop rax
        pop rdx
        inc edx
        jmp .dta_row
.dta_done:
        POPALL
        ret

; draw_cur_line: redraw only the cursor_y line
draw_cur_line:
        PUSHALL
        mov ecx, [cursor_y]
        sub ecx, [scroll_y]
        cmp ecx, 0
        jl .dcl_skip
        cmp ecx, EDIT_ROWS - 1
        jg .dcl_skip
        add ecx, TEXT_START
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TEXT
        int 0x80
        mov eax, [cursor_y]
        call get_line_ptr
        add esi, [scroll_x]
        mov edi, line_buf
        mov ecx, 80
.dcl_copy:
        mov al, [esi]
        test al, al
        jz .dcl_pad
        cmp al, 0x20
        jb .dcl_ctrl
        stosb
        inc esi
        dec ecx
        jnz .dcl_copy
        jmp .dcl_print
.dcl_ctrl:
        mov byte [edi], ' '
        inc edi
        inc esi
        dec ecx
        jnz .dcl_copy
        jmp .dcl_print
.dcl_pad:
        mov al, ' '
        rep stosb
.dcl_print:
        mov byte [line_buf + 80], 0
        mov eax, SYS_PRINT
        mov ebx, line_buf
        int 0x80
.dcl_skip:
        POPALL
        ret

; draw_status: render status bar at row 24
draw_status:
        PUSHALL
        mov edi, line_buf
        mov al, ' '
        mov ecx, 80
        rep stosb
        mov edi, line_buf + 1
        mov esi, str_ln
        call strcat_edi
        mov eax, [cursor_y]
        inc eax
        call fmt_dec
        mov esi, str_col
        call strcat_edi
        mov eax, [cursor_x]
        inc eax
        call fmt_dec
        mov esi, str_of
        call strcat_edi
        mov eax, [num_lines]
        call fmt_dec
        mov esi, str_lines
        call strcat_edi
        mov esi, str_bar
        call strcat_edi
        mov esi, filename
        call strcat_edi
        mov byte [line_buf + 79], 0
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, STATUS_ROW
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_STATUS
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line_buf
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TEXT
        int 0x80
        POPALL
        ret

; draw_quit_prompt
draw_quit_prompt:
        PUSHALL
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, STATUS_ROW
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_WARN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_quit_prompt
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TEXT
        int 0x80
        POPALL
        ret

; set_hw_cursor: position hardware cursor
set_hw_cursor:
        PUSHALL
        mov eax, SYS_SETCURSOR
        mov ebx, [cursor_x]
        sub ebx, [scroll_x]
        mov ecx, [cursor_y]
        sub ecx, [scroll_y]
        add ecx, TEXT_START
        int 0x80
        POPALL
        ret

; insert_char: insert AL at cursor position
insert_char:
        PUSHALL
        mov bl, al
        mov eax, [cursor_y]
        call get_line_ptr
        xor ecx, ecx
.ic_len:
        cmp byte [esi + ecx], 0
        je .ic_got_len
        inc ecx
        cmp ecx, MAX_LINE_LEN - 2
        jge .ic_full
        jmp .ic_len
.ic_full:
        POPALL
        ret
.ic_got_len:
        mov edx, [cursor_x]
.ic_shift:
        cmp ecx, edx
        jl .ic_place
        mov al, [esi + ecx]
        mov [esi + ecx + 1], al
        dec ecx
        jmp .ic_shift
.ic_place:
        mov [esi + edx], bl
        inc dword [cursor_x]
        mov byte [modified], 1
        call adjust_scroll_x
        POPALL
        ret

; delete_at_cursor
delete_at_cursor:
        PUSHALL
        mov eax, [cursor_y]
        call get_line_ptr
        xor ecx, ecx
.dac_len:
        cmp byte [esi + ecx], 0
        je .dac_len_done
        inc ecx
        jmp .dac_len
.dac_len_done:
        mov edx, [cursor_x]
        cmp edx, ecx
        jge .dac_exit
.dac_shift:
        mov al, [esi + edx + 1]
        mov [esi + edx], al
        test al, al
        jz .dac_done
        inc edx
        jmp .dac_shift
.dac_done:
        mov byte [modified], 1
.dac_exit:
        POPALL
        ret

; split_line: Enter key
split_line:
        PUSHALL
        cmp dword [num_lines], MAX_LINES - 1
        jge .sl_done
        mov eax, [num_lines]
        dec eax
.sl_shift:
        cmp eax, [cursor_y]
        jle .sl_copy
        push rax
        call get_line_ptr
        mov edi, esi
        add edi, MAX_LINE_LEN
        mov ecx, MAX_LINE_LEN
        rep movsb
        pop rax
        dec eax
        jmp .sl_shift
.sl_copy:
        mov eax, [cursor_y]
        call get_line_ptr
        lea edi, [esi + MAX_LINE_LEN]
        mov edx, [cursor_x]
        xor ecx, ecx
.sl_cp:
        mov al, [esi + edx]
        mov [edi + ecx], al
        test al, al
        jz .sl_trunc
        inc edx
        inc ecx
        jmp .sl_cp
.sl_trunc:
        mov edx, [cursor_x]
        mov byte [esi + edx], 0
        inc dword [cursor_y]
        mov dword [cursor_x], 0
        mov dword [scroll_x], 0
        inc dword [num_lines]
        mov byte [modified], 1
        call adjust_scroll_y
.sl_done:
        POPALL
        ret

; join_up: backspace at col 0
join_up:
        PUSHALL
        mov eax, [cursor_y]
        dec eax
        call get_line_ptr
        mov ebp, esi
        xor ecx, ecx
.ju_scan:
        cmp byte [ebp + ecx], 0
        je .ju_found
        inc ecx
        jmp .ju_scan
.ju_found:
        mov [cursor_x], ecx
        mov eax, [cursor_y]
        call get_line_ptr
        lea edi, [ebp + ecx]
        xor edx, edx
.ju_append:
        lea eax, [ecx + edx]
        cmp eax, MAX_LINE_LEN - 2
        jge .ju_done
        mov al, [esi + edx]
        mov [edi + edx], al
        test al, al
        jz .ju_done
        inc edx
        jmp .ju_append
.ju_done:
        call delete_cur_line
        dec dword [cursor_y]
        mov byte [modified], 1
        call adjust_scroll_y
        call adjust_scroll_x
        POPALL
        ret

; join_down: del at EOL
join_down:
        PUSHALL
        mov eax, [cursor_y]
        call get_line_ptr
        xor ecx, ecx
.jd_scan:
        cmp byte [esi + ecx], 0
        je .jd_found
        inc ecx
        jmp .jd_scan
.jd_found:
        lea edi, [esi + ecx]
        mov eax, [cursor_y]
        inc eax
        call get_line_ptr
        xor edx, edx
.jd_append:
        lea eax, [ecx + edx]
        cmp eax, MAX_LINE_LEN - 2
        jge .jd_done
        mov al, [esi + edx]
        mov [edi + edx], al
        test al, al
        jz .jd_done
        inc edx
        jmp .jd_append
.jd_done:
        inc dword [cursor_y]
        call delete_cur_line
        dec dword [cursor_y]
        mov byte [modified], 1
        POPALL
        ret

; kill_line: Ctrl+K
kill_line:
        PUSHALL
        mov eax, [cursor_y]
        call get_line_ptr
        mov edx, [cursor_x]
        mov byte [esi + edx], 0
        mov byte [modified], 1
        POPALL
        ret

; delete_cur_line
delete_cur_line:
        PUSHALL
        mov edx, [cursor_y]
.dcl2_loop:
        mov ebx, [num_lines]
        dec ebx
        cmp edx, ebx
        jge .dcl2_clear
        mov eax, edx
        inc eax
        call get_line_ptr
        lea edi, [esi - MAX_LINE_LEN]
        mov ecx, MAX_LINE_LEN
        rep movsb
        inc edx
        jmp .dcl2_loop
.dcl2_clear:
        mov eax, edx
        call get_line_ptr
        mov edi, esi
        xor al, al
        mov ecx, MAX_LINE_LEN
        rep stosb
        dec dword [num_lines]
        cmp dword [num_lines], 1
        jge .dcl2_done
        mov dword [num_lines], 1
.dcl2_done:
        POPALL
        ret

; load_file
load_file:
        PUSHALL
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_io_buf
        int 0x80
        test eax, eax
        jz .lf_empty
        mov [file_size], eax
        mov esi, file_io_buf
        mov ecx, [file_size]
        xor edx, edx
        xor ebx, ebx
.lf_parse:
        cmp ecx, 0
        jle .lf_parse_done
        mov al, [esi]
        inc esi
        dec ecx
        cmp al, 0x0D
        je .lf_parse
        cmp al, 0x0A
        je .lf_newline
        cmp ebx, MAX_LINE_LEN - 2
        jge .lf_parse
        push rax
        mov eax, edx
        imul eax, MAX_LINE_LEN
        lea edi, [text_buf + eax + ebx]
        pop rax
        mov [edi], al
        inc ebx
        jmp .lf_parse
.lf_newline:
        push rax
        mov eax, edx
        imul eax, MAX_LINE_LEN
        lea edi, [text_buf + eax + ebx]
        mov byte [edi], 0
        pop rax
        inc edx
        xor ebx, ebx
        cmp edx, MAX_LINES - 1
        jge .lf_parse_done
        jmp .lf_parse
.lf_parse_done:
        push rax
        mov eax, edx
        imul eax, MAX_LINE_LEN
        lea edi, [text_buf + eax + ebx]
        mov byte [edi], 0
        pop rax
        inc edx
        cmp edx, 1
        jge .lf_set_count
        mov edx, 1
.lf_set_count:
        mov [num_lines], edx
        jmp .lf_done
.lf_empty:
        mov dword [num_lines], 1
.lf_done:
        POPALL
        ret

; save_file
save_file:
        PUSHALL
        mov edi, file_io_buf
        xor edx, edx
.sf_loop:
        cmp edx, [num_lines]
        jge .sf_write
        mov eax, edx
        imul eax, MAX_LINE_LEN
        lea esi, [text_buf + eax]
.sf_chars:
        lodsb
        test al, al
        jz .sf_nl
        stosb
        jmp .sf_chars
.sf_nl:
        mov al, 0x0A
        stosb
        inc edx
        jmp .sf_loop
.sf_write:
        mov eax, edi
        sub eax, file_io_buf
        mov [file_size], eax
        mov eax, SYS_FWRITE
        mov ebx, filename
        mov ecx, file_io_buf
        mov edx, [file_size]
        int 0x80
        mov byte [modified], 0
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, STATUS_ROW
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_SAVE_OK
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_saved
        int 0x80
        mov eax, SYS_SLEEP
        mov ebx, 80
        int 0x80
        POPALL
        ret

; strcat_edi: append ESI string to [EDI], advance EDI
strcat_edi:
.sce_loop:
        lodsb
        test al, al
        jz .sce_done
        stosb
        jmp .sce_loop
.sce_done:
        ret

; fmt_dec: write EAX as decimal digits at [EDI], advance EDI
fmt_dec:
        push rcx
        push rbx
        mov ebx, 10
        xor ecx, ecx
        test eax, eax
        jnz .fd_push
        mov byte [edi], '0'
        inc edi
        jmp .fd_done
.fd_push:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        test eax, eax
        jnz .fd_push
.fd_pop:
        pop rdx
        add dl, '0'
        mov [edi], dl
        inc edi
        dec ecx
        jnz .fd_pop
.fd_done:
        pop rbx
        pop rcx
        ret

section .data

; Data
str_default_fn:  db "scratch.txt", 0
str_hdr_prefix:  db " EDIT: ", 0
str_modified:    db " [Modified]", 0
str_hdr_hints:   db "^S Save  ^Q Quit", 0
str_ln:          db " Ln ", 0
str_col:         db ", Col ", 0
str_of:          db " / ", 0
str_lines:       db " lines  ", 0
str_bar:         db "| ", 0
str_saved:
        db " Saved!                                                                ", 0
str_quit_prompt:
        db " Unsaved changes! Quit without saving? (y/N)                           ", 0

section .bss

; BSS
cursor_x:    resd 1
cursor_y:    resd 1
scroll_x:    resd 1
scroll_y:    resd 1
num_lines:   resd 1
modified:    resb 1
file_size:   resd 1
arg_buf:     resb 512
filename:    resb 64
line_buf:    resb 84
file_io_buf: resb FILE_BUF_SIZE
text_buf:    resb (MAX_LINES * MAX_LINE_LEN)
