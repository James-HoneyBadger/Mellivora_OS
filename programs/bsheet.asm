; bsheet.asm - Spreadsheet for Mellivora OS (GUI)
; Usage: bsheet [filename.csv]
;
; VisiCalc-style spreadsheet with 26 columns (A-Z) × 50 rows.
; Supports formulas: =A1+B2, =SUM(A1:A10), numbers, and text.
; Save/load in CSV format.

%include "syscalls.inc"
%include "lib/gui.inc"

WIN_W           equ 580
WIN_H           equ 400

MAX_COLS        equ 26          ; A-Z
MAX_ROWS        equ 50
CELL_W          equ 72
CELL_H          equ 16
HEADER_H        equ 18         ; column header height
ROW_HDR_W       equ 32         ; row number width
CELL_TEXT_LEN   equ 32         ; max text per cell
FORMULA_BAR_H   equ 24
VISIBLE_COLS    equ 7
VISIBLE_ROWS    equ 20

COL_BG          equ 0x00F0F0F0
COL_GRID        equ 0x00BBBBBB
COL_HEADER_BG   equ 0x00DDDDEE
COL_HEADER_TEXT equ 0x00333344
COL_CELL_BG     equ 0x00FFFFFF
COL_CELL_TEXT   equ 0x00111111
COL_SELECT_BG   equ 0x00BBDDFF
COL_SELECT_BRD  equ 0x003366CC
COL_FORMULA_BG  equ 0x00FFFFFF
COL_FORMULA_TXT equ 0x00222222
COL_STATUS      equ 0x00666688
COL_BAR_BG      equ 0x00EEEEF0

; Cell value types
TYPE_EMPTY      equ 0
TYPE_TEXT       equ 1
TYPE_NUMBER     equ 2
TYPE_FORMULA    equ 3

start:
        ; Get arguments
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80

        ; Create window
        mov eax, 30
        mov ebx, 40
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, title_str
        call gui_create_window
        mov [win_id], eax

        ; Initialize
        mov dword [cur_col], 0
        mov dword [cur_row], 0
        mov dword [scroll_col], 0
        mov dword [scroll_row], 0
        mov byte [editing], 0
        mov dword [edit_len], 0

        ; Clear all cells
        mov edi, cells
        xor eax, eax
        mov ecx, (MAX_COLS * MAX_ROWS * CELL_TEXT_LEN) / 4
        rep stosd
        mov edi, cell_types
        mov ecx, MAX_COLS * MAX_ROWS
        rep stosb
        mov edi, cell_values
        mov ecx, (MAX_COLS * MAX_ROWS * 4) / 4
        rep stosd

        ; Load file if given
        cmp byte [arg_buf], 0
        je main_loop
        call load_csv

main_loop:
        call draw_sheet
        call gui_compose
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je exit_app
        cmp eax, EVT_KEY_PRESS
        je handle_key
        cmp eax, EVT_MOUSE_CLICK
        je handle_click
        jmp main_loop

exit_app:
        mov eax, [win_id]
        call gui_destroy_window
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80


;=======================================================================
; Drawing
;=======================================================================
draw_sheet:
        PUSHALL

        ; Background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, WIN_H
        mov edi, COL_BG
        call gui_fill_rect

        ; Formula bar background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, FORMULA_BAR_H
        mov edi, COL_BAR_BG
        call gui_fill_rect

        ; Cell reference in formula bar
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, 6
        ; Build "A1:" style reference
        mov esi, cell_ref_buf
        mov al, [cur_col]
        add al, 'A'
        mov [esi], al
        mov eax, [cur_row]
        inc eax                 ; 1-based
        push rsi
        inc esi
        call itoa_to_buf        ; writes digits to esi
        pop rsi
        mov edi, COL_HEADER_TEXT
        call gui_draw_text

        ; Formula bar: show cell content or edit buffer
        mov eax, [win_id]
        mov ebx, 40
        mov ecx, 4
        mov edx, WIN_W - 44
        mov esi, 18
        mov edi, COL_FORMULA_BG
        call gui_fill_rect

        mov eax, [win_id]
        mov ebx, 44
        mov ecx, 6
        cmp byte [editing], 0
        jne .draw_edit_buf
        ; Show cell content
        call get_cur_cell_text
        mov edi, COL_FORMULA_TXT
        call gui_draw_text
        jmp .draw_headers
.draw_edit_buf:
        mov esi, edit_buf
        mov edi, COL_FORMULA_TXT
        call gui_draw_text

.draw_headers:
        ; Column headers
        xor ecx, ecx           ; visible col index
.draw_col_headers:
        cmp ecx, VISIBLE_COLS
        jge .draw_row_headers

        push rcx
        mov eax, [win_id]
        mov ebx, ecx
        imul ebx, CELL_W
        add ebx, ROW_HDR_W
        mov ecx, FORMULA_BAR_H
        mov edx, CELL_W
        mov esi, HEADER_H
        mov edi, COL_HEADER_BG
        call gui_fill_rect
        pop rcx

        ; Column letter
        push rcx
        mov eax, ecx
        add eax, [scroll_col]
        add al, 'A'
        mov [col_letter_buf], al
        mov byte [col_letter_buf + 1], 0

        mov eax, [win_id]
        mov ebx, ecx
        imul ebx, CELL_W
        add ebx, ROW_HDR_W + CELL_W/2 - 4
        mov ecx, FORMULA_BAR_H + 3
        mov esi, col_letter_buf
        mov edi, COL_HEADER_TEXT
        call gui_draw_text
        pop rcx

        inc ecx
        jmp .draw_col_headers

.draw_row_headers:
        ; Row headers
        xor ecx, ecx
.draw_row_hdrs:
        cmp ecx, VISIBLE_ROWS
        jge .draw_cells

        push rcx
        mov eax, [win_id]
        xor ebx, ebx
        mov edx, ecx
        imul edx, CELL_H
        mov ecx, edx
        add ecx, FORMULA_BAR_H + HEADER_H
        mov edx, ROW_HDR_W
        mov esi, CELL_H
        mov edi, COL_HEADER_BG
        call gui_fill_rect
        pop rcx

        ; Row number
        push rcx
        mov eax, ecx
        add eax, [scroll_row]
        inc eax                 ; 1-based
        push rcx
        mov esi, row_num_buf
        call itoa_to_buf

        pop rcx
        mov eax, [win_id]
        mov ebx, 4
        mov edx, ecx
        imul edx, CELL_H
        mov ecx, edx
        add ecx, FORMULA_BAR_H + HEADER_H + 2
        mov esi, row_num_buf
        mov edi, COL_HEADER_TEXT
        call gui_draw_text
        pop rcx

        inc ecx
        jmp .draw_row_hdrs

.draw_cells:
        ; Draw visible cells
        xor edx, edx           ; row index
.dc_row:
        cmp edx, VISIBLE_ROWS
        jge .draw_done
        xor ecx, ecx           ; col index
.dc_col:
        cmp ecx, VISIBLE_COLS
        jge .dc_next_row

        push rcx
        push rdx

        ; Calculate actual cell coords
        mov eax, ecx
        add eax, [scroll_col]  ; actual col
        mov ebx, edx
        add ebx, [scroll_row]  ; actual row

        ; Cell screen position
        mov [.tmp_acol], eax
        mov [.tmp_arow], ebx
        mov eax, ecx
        imul eax, CELL_W
        add eax, ROW_HDR_W
        mov [.tmp_x], eax
        mov ebx, edx
        imul ebx, CELL_H
        add ebx, FORMULA_BAR_H + HEADER_H
        mov [.tmp_y], ebx

        ; Is this the selected cell?
        mov eax, [.tmp_acol]
        cmp eax, [cur_col]
        jne .dc_normal_bg
        mov eax, [.tmp_arow]
        cmp eax, [cur_row]
        jne .dc_normal_bg

        ; Selected cell background
        mov eax, [win_id]
        mov ebx, [.tmp_x]
        mov ecx, [.tmp_y]
        mov edx, CELL_W
        mov esi, CELL_H
        mov edi, COL_SELECT_BG
        call gui_fill_rect
        jmp .dc_text

.dc_normal_bg:
        mov eax, [win_id]
        mov ebx, [.tmp_x]
        mov ecx, [.tmp_y]
        mov edx, CELL_W
        mov esi, CELL_H
        mov edi, COL_CELL_BG
        call gui_fill_rect

.dc_text:
        ; Get cell display text
        mov eax, [.tmp_acol]
        mov ebx, [.tmp_arow]
        call get_cell_display

        ; Draw text
        mov eax, [win_id]
        mov ebx, [.tmp_x]
        add ebx, 2
        mov ecx, [.tmp_y]
        add ecx, 2
        ; esi already set by get_cell_display
        mov edi, COL_CELL_TEXT
        call gui_draw_text

        ; Grid line (right edge)
        mov eax, [win_id]
        mov ebx, [.tmp_x]
        add ebx, CELL_W - 1
        mov ecx, [.tmp_y]
        mov edx, 1
        mov esi, CELL_H
        mov edi, COL_GRID
        call gui_fill_rect

        ; Grid line (bottom edge)
        mov eax, [win_id]
        mov ebx, [.tmp_x]
        mov ecx, [.tmp_y]
        add ecx, CELL_H - 1
        mov edx, CELL_W
        mov esi, 1
        mov edi, COL_GRID
        call gui_fill_rect

        pop rdx
        pop rcx
        inc ecx
        jmp .dc_col

.dc_next_row:
        pop rdx
        pop rcx
        ; ecx/edx are from the inner push
        ; We need to properly restore — the inner loop pushes ecx,edx
        ; Actually the flow is: we push rcx,edx at start of per-cell, pop at .dc_text/normal_bg end
        ; When we get to .dc_next_row, ecx,edx were already popped
        inc edx
        jmp .dc_row

.draw_done:
        ; Status bar
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, WIN_H - 16
        mov esi, str_status
        mov edi, COL_STATUS
        call gui_draw_text

        POPALL
        ret

.tmp_x:    dd 0
.tmp_y:    dd 0
.tmp_acol: dd 0
.tmp_arow: dd 0


;=======================================================================
; Input handling
;=======================================================================
handle_key:
        ; EBX = key code
        cmp byte [editing], 1
        je handle_edit_key

        ; Navigation
        cmp ebx, 0x83           ; KEY_RIGHT
        je .move_right
        cmp ebx, 0x82           ; KEY_LEFT
        je .move_left
        cmp ebx, 0x81           ; KEY_DOWN
        je .move_down
        cmp ebx, 0x80           ; KEY_UP
        je .move_up
        cmp ebx, 0x0D           ; Enter - start editing
        je start_editing
        cmp ebx, 0x7F           ; Delete - clear cell
        je clear_cell
        cmp ebx, 0x1B           ; Escape
        je exit_app

        ; Ctrl+S = save
        cmp ebx, 0x13
        je save_csv

        ; Ctrl+C = copy cell to clipboard
        cmp ebx, 0x03
        je .copy_cell

        ; Ctrl+V = paste clipboard into cell
        cmp ebx, 0x16
        je .paste_cell

        ; Tab - move right
        cmp ebx, 0x09
        je .move_right

        ; Printable char starts editing
        cmp ebx, 32
        jb .nav_done
        cmp ebx, 126
        ja .nav_done
        ; Start editing with this char
        mov dword [edit_len], 0
        mov byte [editing], 1
        mov eax, ebx
        mov [edit_buf], al
        mov dword [edit_len], 1
        mov byte [edit_buf + 1], 0
        jmp main_loop

.move_right:
        cmp dword [cur_col], MAX_COLS - 1
        jge .nav_done
        inc dword [cur_col]
        call adjust_scroll
        jmp main_loop
.move_left:
        cmp dword [cur_col], 0
        je .nav_done
        dec dword [cur_col]
        call adjust_scroll
        jmp main_loop
.move_down:
        cmp dword [cur_row], MAX_ROWS - 1
        jge .nav_done
        inc dword [cur_row]
        call adjust_scroll
        jmp main_loop
.move_up:
        cmp dword [cur_row], 0
        je .nav_done
        dec dword [cur_row]
        call adjust_scroll
        jmp main_loop
.nav_done:
        jmp main_loop

.copy_cell:
        PUSHALL
        call get_cur_cell_text  ; ESI = cell text
        ; Get length
        xor ecx, ecx
.cc_len:
        cmp byte [esi + ecx], 0
        je .cc_do
        inc ecx
        jmp .cc_len
.cc_do:
        mov ebx, esi
        mov eax, SYS_CLIPBOARD_COPY
        int 0x80
        POPALL
        jmp main_loop

.paste_cell:
        PUSHALL
        mov ebx, clip_buf
        mov ecx, CELL_TEXT_LEN
        mov eax, SYS_CLIPBOARD_PASTE
        int 0x80
        ; Null-terminate
        cmp eax, CELL_TEXT_LEN
        jl .pv_ok
        mov eax, CELL_TEXT_LEN - 1
.pv_ok:
        mov byte [clip_buf + eax], 0
        ; Copy to cell
        mov eax, [cur_col]
        mov ebx, [cur_row]
        call get_cell_ptr
        mov esi, clip_buf
        xor ecx, ecx
.pv_copy:
        mov al, [esi + ecx]
        mov [edi + ecx], al
        inc ecx
        cmp al, 0
        jne .pv_copy
        call classify_cell
        call recalc_all
        POPALL
        jmp main_loop

handle_edit_key:
        ; Editing mode
        cmp ebx, 0x0D           ; Enter - confirm
        je confirm_edit
        cmp ebx, 0x1B           ; Escape - cancel
        je cancel_edit
        cmp ebx, 0x08           ; Backspace
        je .edit_bs

        ; Printable
        cmp ebx, 32
        jb main_loop
        cmp ebx, 126
        ja main_loop
        cmp dword [edit_len], CELL_TEXT_LEN - 1
        jge main_loop
        mov eax, [edit_len]
        mov [edit_buf + eax], bl
        inc dword [edit_len]
        mov eax, [edit_len]
        mov byte [edit_buf + eax], 0
        jmp main_loop

.edit_bs:
        cmp dword [edit_len], 0
        je main_loop
        dec dword [edit_len]
        mov eax, [edit_len]
        mov byte [edit_buf + eax], 0
        jmp main_loop

start_editing:
        mov byte [editing], 1
        ; Copy current cell text to edit buffer
        call get_cur_cell_text
        mov edi, edit_buf
        xor ecx, ecx
.se_copy:
        mov al, [esi + ecx]
        mov [edi + ecx], al
        inc ecx
        cmp al, 0
        jne .se_copy
        dec ecx
        mov [edit_len], ecx
        jmp main_loop

confirm_edit:
        mov byte [editing], 0
        ; Write edit buffer to cell
        mov eax, [cur_col]
        mov ebx, [cur_row]
        call get_cell_ptr       ; edi = cell text ptr
        mov esi, edit_buf
        xor ecx, ecx
.ce_copy:
        mov al, [esi + ecx]
        mov [edi + ecx], al
        inc ecx
        cmp al, 0
        jne .ce_copy

        ; Determine cell type
        call classify_cell
        ; Recalculate all formulas
        call recalc_all
        jmp main_loop

cancel_edit:
        mov byte [editing], 0
        jmp main_loop

clear_cell:
        mov eax, [cur_col]
        mov ebx, [cur_row]
        call get_cell_ptr
        mov byte [edi], 0
        ; Set type to empty
        mov eax, [cur_col]
        imul eax, MAX_ROWS
        add eax, [cur_row]
        mov byte [cell_types + eax], TYPE_EMPTY
        call recalc_all
        jmp main_loop

handle_click:
        ; EBX = x, ECX = y (window-relative)
        ; Determine which cell was clicked
        sub ecx, FORMULA_BAR_H + HEADER_H
        js main_loop
        sub ebx, ROW_HDR_W
        js main_loop

        ; Col = x / CELL_W + scroll_col
        push rcx
        mov eax, ebx
        xor edx, edx
        mov ecx, CELL_W
        div ecx
        add eax, [scroll_col]
        cmp eax, MAX_COLS
        jge .click_oob
        mov [cur_col], eax
        pop rcx

        ; Row = y / CELL_H + scroll_row
        mov eax, ecx
        xor edx, edx
        mov ecx, CELL_H
        div ecx
        add eax, [scroll_row]
        cmp eax, MAX_ROWS
        jge main_loop
        mov [cur_row], eax
        mov byte [editing], 0
        jmp main_loop

.click_oob:
        pop rcx
        jmp main_loop


;=======================================================================
; Cell data helpers
;=======================================================================

; get_cell_ptr - Get pointer to cell text
;  EAX = col, EBX = row
; Returns: EDI = pointer to cell text buffer
get_cell_ptr:
        push rax
        imul eax, MAX_ROWS
        add eax, ebx
        imul eax, CELL_TEXT_LEN
        lea edi, [cells + eax]
        pop rax
        ret

; get_cur_cell_text - Get ESI pointing to current cell's text
; Returns: ESI = pointer
get_cur_cell_text:
        push rax
        push rbx
        mov eax, [cur_col]
        mov ebx, [cur_row]
        call get_cell_ptr
        mov esi, edi
        pop rbx
        pop rax
        ret

; get_cell_display - Get display text for cell (col=EAX, row=EBX)
; Returns: ESI = display text
get_cell_display:
        push rcx
        push rdx
        push rax
        imul eax, MAX_ROWS
        add eax, ebx
        ; Check type
        movzx ecx, byte [cell_types + eax]
        cmp ecx, TYPE_NUMBER
        je .gcd_num
        cmp ecx, TYPE_FORMULA
        je .gcd_formula
        ; Text or empty — return raw text
        imul eax, CELL_TEXT_LEN
        lea esi, [cells + eax]
        pop rax
        pop rdx
        pop rcx
        ret

.gcd_num:
.gcd_formula:
        ; For numbers and formulas, show computed value
        mov edx, [cell_values + eax * 4]
        mov esi, display_num_buf
        mov eax, edx
        call int_to_str
        pop rax
        pop rdx
        pop rcx
        ret

; classify_cell - Determine type of current cell and compute value
classify_cell:
        PUSHALL
        mov eax, [cur_col]
        mov ebx, [cur_row]
        call get_cell_ptr
        mov esi, edi

        ; Empty?
        cmp byte [esi], 0
        jne .cc_not_empty
        mov eax, [cur_col]
        imul eax, MAX_ROWS
        add eax, [cur_row]
        mov byte [cell_types + eax], TYPE_EMPTY
        mov dword [cell_values + eax * 4], 0
        POPALL
        ret

.cc_not_empty:
        ; Formula? (starts with '=')
        cmp byte [esi], '='
        je .cc_formula

        ; Try to parse as number
        push rsi
        call parse_number       ; eax = number, carry = success
        pop rsi
        jc .cc_text

        ; It's a number
        mov edx, eax
        mov eax, [cur_col]
        imul eax, MAX_ROWS
        add eax, [cur_row]
        mov byte [cell_types + eax], TYPE_NUMBER
        mov [cell_values + eax * 4], edx
        POPALL
        ret

.cc_formula:
        mov eax, [cur_col]
        imul eax, MAX_ROWS
        add eax, [cur_row]
        mov byte [cell_types + eax], TYPE_FORMULA
        ; Evaluate formula
        inc esi                 ; skip '='
        call eval_formula
        mov edx, eax
        mov eax, [cur_col]
        imul eax, MAX_ROWS
        add eax, [cur_row]
        mov [cell_values + eax * 4], edx
        POPALL
        ret

.cc_text:
        mov eax, [cur_col]
        imul eax, MAX_ROWS
        add eax, [cur_row]
        mov byte [cell_types + eax], TYPE_TEXT
        POPALL
        ret

; parse_number - Parse decimal number from ESI
; Returns: EAX = value, carry set on failure
parse_number:
        push rbx
        push rcx
        push rdx
        xor eax, eax
        xor ecx, ecx           ; sign flag
        cmp byte [esi], '-'
        jne .pn_loop
        mov ecx, 1
        inc esi
.pn_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .pn_end
        cmp dl, '9'
        ja .pn_end
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pn_loop
.pn_end:
        cmp byte [esi], 0
        jne .pn_fail            ; trailing non-digits
        test ecx, ecx
        jz .pn_ok
        neg eax
.pn_ok:
        pop rdx
        pop rcx
        pop rbx
        clc
        ret
.pn_fail:
        pop rdx
        pop rcx
        pop rbx
        stc
        ret

; eval_formula - Evaluate simple formula from ESI
; Supports: cell refs (A1), +, -, *, SUM(A1:A5)
; Returns: EAX = result
eval_formula:
        push rbx
        push rcx
        push rdx

        ; Check for SUM(
        cmp dword [esi], 'SUM('
        je .ef_sum

        ; Simple expression: val op val op val ...
        call eval_term          ; eax = first value
.ef_loop:
        cmp byte [esi], 0
        je .ef_done
        cmp byte [esi], '+'
        je .ef_add
        cmp byte [esi], '-'
        je .ef_sub
        cmp byte [esi], '*'
        je .ef_mul
        jmp .ef_done

.ef_add:
        inc esi
        push rax
        call eval_term
        mov ebx, eax
        pop rax
        add eax, ebx
        jmp .ef_loop
.ef_sub:
        inc esi
        push rax
        call eval_term
        mov ebx, eax
        pop rax
        sub eax, ebx
        jmp .ef_loop
.ef_mul:
        inc esi
        push rax
        call eval_term
        mov ebx, eax
        pop rax
        imul eax, ebx
        jmp .ef_loop

.ef_done:
        pop rdx
        pop rcx
        pop rbx
        ret

.ef_sum:
        ; SUM(A1:B5) — sum range
        add esi, 4             ; skip "SUM("
        ; Parse start ref
        call parse_cell_ref     ; eax=col, ebx=row
        mov [.sum_sc], eax
        mov [.sum_sr], ebx
        cmp byte [esi], ':'
        jne .ef_sum_end
        inc esi
        call parse_cell_ref
        mov [.sum_ec], eax
        mov [.sum_er], ebx
        cmp byte [esi], ')'
        jne .ef_sum_end
        inc esi

        ; Sum all cells in range
        xor edx, edx           ; accumulator
        mov eax, [.sum_sc]
.sum_col:
        cmp eax, [.sum_ec]
        jg .sum_done
        mov ebx, [.sum_sr]
.sum_row:
        cmp ebx, [.sum_er]
        jg .sum_next_col
        push rax
        push rbx
        ; Get cell value
        imul eax, MAX_ROWS
        add eax, ebx
        add edx, [cell_values + eax * 4]
        pop rbx
        pop rax
        inc ebx
        jmp .sum_row
.sum_next_col:
        inc eax
        jmp .sum_col
.sum_done:
        mov eax, edx
        pop rdx
        pop rcx
        pop rbx
        ret

.ef_sum_end:
        xor eax, eax
        pop rdx
        pop rcx
        pop rbx
        ret

.sum_sc: dd 0
.sum_sr: dd 0
.sum_ec: dd 0
.sum_er: dd 0

; eval_term - Evaluate a single term (number or cell ref)
; Returns: EAX = value
eval_term:
        ; Is it a cell ref? (letter followed by digit)
        movzx eax, byte [esi]
        or al, 0x20            ; lowercase
        cmp al, 'a'
        jb .et_number
        cmp al, 'z'
        ja .et_number
        movzx ebx, byte [esi + 1]
        cmp bl, '0'
        jb .et_number
        cmp bl, '9'
        ja .et_number

        ; Cell reference
        call parse_cell_ref
        ; eax=col, ebx=row — look up value
        imul eax, MAX_ROWS
        add eax, ebx
        mov eax, [cell_values + eax * 4]
        ret

.et_number:
        ; Parse decimal number
        call parse_inline_num
        ret

; parse_cell_ref - Parse "A1" style reference from ESI
; Returns: EAX = col (0-25), EBX = row (0-49), ESI advanced
parse_cell_ref:
        movzx eax, byte [esi]
        or al, 0x20
        sub al, 'a'
        inc esi
        ; Parse row number
        xor ebx, ebx
.pcr_loop:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jb .pcr_done
        cmp cl, '9'
        ja .pcr_done
        imul ebx, 10
        sub cl, '0'
        movzx ecx, cl
        add ebx, ecx
        inc esi
        jmp .pcr_loop
.pcr_done:
        dec ebx                 ; convert 1-based to 0-based
        ; Clamp
        cmp eax, MAX_COLS
        jb .pcr_col_ok
        xor eax, eax
.pcr_col_ok:
        cmp ebx, MAX_ROWS
        jb .pcr_row_ok
        xor ebx, ebx
.pcr_row_ok:
        ret

; parse_inline_num - Parse number from ESI (stops at operator/end)
; Returns: EAX = value
parse_inline_num:
        push rdx
        push rcx
        xor eax, eax
        xor ecx, ecx
        cmp byte [esi], '-'
        jne .pin_loop
        mov ecx, 1
        inc esi
.pin_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .pin_done
        cmp dl, '9'
        ja .pin_done
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pin_loop
.pin_done:
        test ecx, ecx
        jz .pin_pos
        neg eax
.pin_pos:
        pop rcx
        pop rdx
        ret

; recalc_all - Recalculate all formula cells
recalc_all:
        PUSHALL
        xor eax, eax           ; col
.ra_col:
        cmp eax, MAX_COLS
        jge .ra_done
        xor ebx, ebx           ; row
.ra_row:
        cmp ebx, MAX_ROWS
        jge .ra_next_col
        push rax
        push rbx
        imul eax, MAX_ROWS
        add eax, ebx
        cmp byte [cell_types + eax], TYPE_FORMULA
        jne .ra_skip
        ; Re-evaluate
        imul eax, CELL_TEXT_LEN
        lea esi, [cells + eax]
        inc esi                 ; skip '='
        call eval_formula
        mov edx, eax
        pop rbx
        pop rax
        push rax
        push rbx
        imul eax, MAX_ROWS
        add eax, ebx
        mov [cell_values + eax * 4], edx
.ra_skip:
        pop rbx
        pop rax
        inc ebx
        jmp .ra_row
.ra_next_col:
        inc eax
        jmp .ra_col
.ra_done:
        POPALL
        ret


;=======================================================================
; Scrolling
;=======================================================================
adjust_scroll:
        ; Ensure cursor is visible
        mov eax, [cur_col]
        sub eax, [scroll_col]
        cmp eax, VISIBLE_COLS
        jl .as_col_max_ok
        mov eax, [cur_col]
        sub eax, VISIBLE_COLS - 1
        mov [scroll_col], eax
.as_col_max_ok:
        mov eax, [cur_col]
        cmp eax, [scroll_col]
        jge .as_col_min_ok
        mov [scroll_col], eax
.as_col_min_ok:
        mov eax, [cur_row]
        sub eax, [scroll_row]
        cmp eax, VISIBLE_ROWS
        jl .as_row_max_ok
        mov eax, [cur_row]
        sub eax, VISIBLE_ROWS - 1
        mov [scroll_row], eax
.as_row_max_ok:
        mov eax, [cur_row]
        cmp eax, [scroll_row]
        jge .as_done
        mov [scroll_row], eax
.as_done:
        ret


;=======================================================================
; Number to string conversion
;=======================================================================

; int_to_str - Convert signed EAX to string at ESI
int_to_str:
        PUSHALL
        mov edi, esi
        test eax, eax
        jns .its_pos
        mov byte [edi], '-'
        inc edi
        neg eax
.its_pos:
        mov ebx, 10
        xor ecx, ecx
        test eax, eax
        jnz .its_push
        mov byte [edi], '0'
        inc edi
        jmp .its_end
.its_push:
        test eax, eax
        jz .its_pop
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        jmp .its_push
.its_pop:
        test ecx, ecx
        jz .its_end
        pop rax
        add al, '0'
        mov [edi], al
        inc edi
        dec ecx
        jmp .its_pop
.its_end:
        mov byte [edi], 0
        POPALL
        ret

; itoa_to_buf - Convert EAX to string at ESI, NUL terminate
itoa_to_buf:
        PUSHALL
        mov edi, esi
        mov ebx, 10
        xor ecx, ecx
        test eax, eax
        jnz .itb_push
        mov byte [edi], '0'
        inc edi
        jmp .itb_end
.itb_push:
        test eax, eax
        jz .itb_pop
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        jmp .itb_push
.itb_pop:
        test ecx, ecx
        jz .itb_end
        pop rax
        add al, '0'
        mov [edi], al
        inc edi
        dec ecx
        jmp .itb_pop
.itb_end:
        mov byte [edi], 0
        POPALL
        ret


;=======================================================================
; CSV Load/Save
;=======================================================================
load_csv:
        PUSHALL
        mov ebx, arg_buf
        mov ecx, csv_buf
        mov eax, SYS_FREAD
        int 0x80
        cmp eax, 0
        jle .lc_done

        mov [csv_len], eax
        mov esi, csv_buf
        xor eax, eax           ; col
        xor ebx, ebx           ; row

.lc_parse:
        cmp esi, csv_buf
        jb .lc_done
        mov ecx, esi
        sub ecx, csv_buf
        cmp ecx, [csv_len]
        jge .lc_done

        ; Get cell pointer
        push rax
        push rbx
        call get_cell_ptr       ; edi = cell text ptr
        pop rbx
        pop rax

        ; Copy until comma, newline, or end
        xor ecx, ecx
.lc_cell:
        mov dl, [esi]
        cmp dl, ','
        je .lc_comma
        cmp dl, 0x0A
        je .lc_newline
        cmp dl, 0x0D
        je .lc_cr
        cmp dl, 0
        je .lc_end_cell
        mov edx, esi
        sub edx, csv_buf
        cmp edx, [csv_len]
        jge .lc_end_cell
        mov dl, [esi]
        cmp ecx, CELL_TEXT_LEN - 1
        jge .lc_skip_char
        mov [edi + ecx], dl
        inc ecx
.lc_skip_char:
        inc esi
        jmp .lc_cell

.lc_comma:
        mov byte [edi + ecx], 0
        inc esi
        inc eax
        cmp eax, MAX_COLS
        jl .lc_parse
        jmp .lc_done

.lc_cr:
        inc esi
        cmp byte [esi], 0x0A
        jne .lc_newline
        inc esi
.lc_newline:
        mov byte [edi + ecx], 0
        cmp byte [esi - 1], 0x0A
        jne .lc_nl_skip
        ; was already incremented
.lc_nl_skip:
        xor eax, eax
        inc ebx
        cmp ebx, MAX_ROWS
        jl .lc_parse
        jmp .lc_done

.lc_end_cell:
        mov byte [edi + ecx], 0

.lc_done:
        ; Classify all non-empty cells
        call classify_all_cells
        call recalc_all
        POPALL
        ret

; Classify all cells
classify_all_cells:
        PUSHALL
        xor eax, eax
.cac_col:
        cmp eax, MAX_COLS
        jge .cac_done
        xor ebx, ebx
.cac_row:
        cmp ebx, MAX_ROWS
        jge .cac_next_col
        push rax
        push rbx
        mov [cur_col], eax
        mov [cur_row], ebx
        call classify_cell
        pop rbx
        pop rax
        inc ebx
        jmp .cac_row
.cac_next_col:
        inc eax
        jmp .cac_col
.cac_done:
        POPALL
        ret

save_csv:
        PUSHALL
        mov edi, csv_buf

        xor ebx, ebx           ; row
.sv_row:
        cmp ebx, MAX_ROWS
        jge .sv_write
        ; Check if row has any content
        xor eax, eax
        mov ecx, 0             ; has_content flag
.sv_check:
        cmp eax, MAX_COLS
        jge .sv_row_check
        push rax
        push rbx
        call get_cell_ptr
        cmp byte [edi], 0      ; wait, edi is csv_buf here, conflict!
        pop rbx
        pop rax
        ; Use a temp approach
        push rax
        imul eax, MAX_ROWS
        add eax, ebx
        imul eax, CELL_TEXT_LEN
        cmp byte [cells + eax], 0
        pop rax
        je .sv_check_next
        mov ecx, 1
.sv_check_next:
        inc eax
        jmp .sv_check
.sv_row_check:
        test ecx, ecx
        jz .sv_next_row

        ; Write this row
        xor eax, eax
.sv_col:
        cmp eax, MAX_COLS
        jge .sv_row_end
        push rax
        imul eax, MAX_ROWS
        add eax, ebx
        imul eax, CELL_TEXT_LEN
        lea esi, [cells + eax]
        pop rax
        ; Copy cell text
.sv_copy:
        mov cl, [esi]
        test cl, cl
        jz .sv_cell_done
        mov [edi], cl
        inc edi
        inc esi
        jmp .sv_copy
.sv_cell_done:
        ; Add comma (except after last col with content)
        mov byte [edi], ','
        inc edi
        inc eax
        jmp .sv_col

.sv_row_end:
        ; Replace trailing comma with newline
        dec edi
        mov byte [edi], 0x0A
        inc edi

.sv_next_row:
        inc ebx
        jmp .sv_row

.sv_write:
        ; Calculate length
        mov edx, edi
        sub edx, csv_buf

        ; Save to file
        mov ebx, save_filename
        mov ecx, csv_buf
        mov esi, 0              ; type
        mov eax, SYS_FWRITE
        int 0x80

        POPALL
        jmp main_loop


;=======================================================================
; Data
;=======================================================================
title_str:       db "BSheet - Spreadsheet", 0
str_status:      db "Arrows:Nav Enter:Edit ^S:Save ^C:Copy ^V:Paste Del:Clear", 0
save_filename:   db "sheet.csv", 0

win_id:          dd 0
cur_col:         dd 0
cur_row:         dd 0
scroll_col:      dd 0
scroll_row:      dd 0
editing:         db 0
edit_buf:        times (CELL_TEXT_LEN + 1) db 0
edit_len:        dd 0

col_letter_buf:  db 0, 0
row_num_buf:     times 8 db 0
cell_ref_buf:    times 8 db 0
display_num_buf: times 16 db 0

arg_buf:         times 256 db 0

; Cell storage: 26 cols × 50 rows × 32 bytes each = 41600 bytes
cells:           times (MAX_COLS * MAX_ROWS * CELL_TEXT_LEN) db 0
cell_types:      times (MAX_COLS * MAX_ROWS) db 0
cell_values:     times (MAX_COLS * MAX_ROWS) dd 0

; CSV buffer
csv_buf:         times 65536 db 0
csv_len:         dd 0
clip_buf:        times (CELL_TEXT_LEN + 1) db 0
