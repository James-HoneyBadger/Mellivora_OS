; bedit.asm - BEdit - Burrows Text Editor
; Simple text editor with load/save, cursor movement, and basic editing.

%include "syscalls.inc"
%include "lib/gui.inc"

MAX_LINES       equ 100
LINE_LEN        equ 80
VISIBLE_LINES   equ 18
UNDO_LEVELS     equ 4           ; Ctrl+Z / Ctrl+Y depth (full-buffer snapshots)
UNDO_SLOT       equ 12 + MAX_LINES * LINE_LEN   ; num_lines, line, col + text

start:
        ; Create window
        mov eax, 60
        mov ebx, 30
        mov ecx, 520
        mov edx, 360
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        ; Initialize
        mov dword [cur_line], 0
        mov dword [cur_col], 0
        mov dword [scroll_y], 0
        mov dword [num_lines], 1
        ; Clear text buffer
        mov edi, text_buf
        xor eax, eax
        mov ecx, MAX_LINES * LINE_LEN / 4
        rep stosd
        call undo_init

        ; Check if filename was passed via args
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        jle .main_loop
        ; Load file
        mov esi, arg_buf
        call load_file

.main_loop:
        call gui_compose
        call draw_editor
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        jne .main_loop

        ; Handle key
        cmp bl, 27              ; ESC
        je .close
        cmp bl, KEY_UP
        je .key_up
        cmp bl, KEY_DOWN
        je .key_down
        cmp bl, KEY_LEFT
        je .key_left
        cmp bl, KEY_RIGHT
        je .key_right
        cmp bl, 13              ; Enter
        je .key_enter
        cmp bl, 8               ; Backspace
        je .key_bs
        cmp bl, 19              ; Ctrl+S
        je .key_save
        cmp bl, 15              ; Ctrl+O
        je .key_open
        cmp bl, 26              ; Ctrl+Z
        je .key_undo
        cmp bl, 25              ; Ctrl+Y
        je .key_redo
        cmp bl, 32
        jl .main_loop
        cmp bl, 126
        jg .main_loop

        ; Insert character
        call insert_char
        jmp .main_loop

.key_up:
        cmp dword [cur_line], 0
        je .main_loop
        dec dword [cur_line]
        call adjust_scroll
        jmp .main_loop

.key_down:
        mov eax, [cur_line]
        inc eax
        cmp eax, [num_lines]
        jge .main_loop
        mov [cur_line], eax
        call adjust_scroll
        jmp .main_loop

.key_left:
        cmp dword [cur_col], 0
        je .main_loop
        dec dword [cur_col]
        jmp .main_loop

.key_right:
        inc dword [cur_col]
        ; Clamp to line length
        mov eax, [cur_line]
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        call strlen
        cmp [cur_col], eax
        jle .main_loop
        mov [cur_col], eax
        jmp .main_loop

.key_enter:
        call insert_newline
        jmp .main_loop

.key_bs:
        call delete_char
        jmp .main_loop

.key_save:
        ; If no filename, pop Save dialog
        cmp byte [arg_buf], 0
        jne .key_save_go
        mov eax, SYS_FILE_SAVE_DLG
        mov ebx, .dlg_save_title
        xor edx, edx            ; all types
        int 0x80
        test eax, eax
        jz .main_loop           ; cancelled
        ; Copy chosen name into arg_buf
        mov esi, ecx
        mov edi, arg_buf
        mov ecx, 63
.save_cp:
        lodsb
        stosb
        test al, al
        jz .key_save_go
        dec ecx
        jnz .save_cp
        mov byte [edi], 0
.key_save_go:
        call save_file
        jmp .main_loop

.key_undo:
        call edit_undo
        jmp .main_loop

.key_redo:
        call edit_redo
        jmp .main_loop

.key_open:
        mov eax, SYS_FILE_OPEN_DLG
        mov ebx, .dlg_open_title
        xor edx, edx            ; all types
        int 0x80
        test eax, eax
        jz .main_loop           ; cancelled
        ; Copy chosen name into arg_buf
        mov esi, ecx
        mov edi, arg_buf
        mov ecx, 63
.open_cp:
        lodsb
        stosb
        test al, al
        jz .open_load
        dec ecx
        jnz .open_cp
        mov byte [edi], 0
.open_load:
        ; Clear text buf and reload
        mov edi, text_buf
        mov ecx, (MAX_LINES * LINE_LEN) / 4
        xor eax, eax
        rep stosd
        mov dword [num_lines], 1
        mov dword [cur_line], 0
        mov dword [cur_col], 0
        mov dword [scroll_y], 0
        call undo_reset
        call load_file
        jmp .main_loop

.dlg_open_title: db "Open File", 0
.dlg_save_title: db "Save As", 0

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; Undo / redo: full-buffer snapshots in two SYS_SBRK rings
;---------------------------------------

; undo_init - point the ring pointers at their static buffers
undo_init:
        pushad
        mov dword [undo_ptr], undo_buf
        mov dword [redo_ptr], redo_buf
        popad
        ret

; undo_reset - forget all history (after loading a file)
undo_reset:
        pushad
        mov dword [undo_head], 0
        mov dword [undo_count], 0
        mov dword [redo_head], 0
        mov dword [redo_count], 0
        popad
        ret

; edit_store_state - copy the current document into a ring slot
;   EAX = ring base, EBX = slot, ECX = slot count for the "full" clamp
edit_store_state:
        pushad
        imul ebx, UNDO_SLOT
        add eax, ebx
        mov edi, eax
        mov eax, [num_lines]
        stosd
        mov eax, [cur_line]
        stosd
        mov eax, [cur_col]
        stosd
        mov esi, text_buf
        mov ecx, (MAX_LINES * LINE_LEN) / 4
        cld
        rep movsd
        popad
        ret

; edit_load_state - restore a ring slot into the document
;   EAX = ring base, EBX = slot
edit_load_state:
        pushad
        imul ebx, UNDO_SLOT
        add eax, ebx
        mov esi, eax
        lodsd
        mov [num_lines], eax
        lodsd
        mov [cur_line], eax
        lodsd
        mov [cur_col], eax
        mov edi, text_buf
        mov ecx, (MAX_LINES * LINE_LEN) / 4
        cld
        rep movsd
        call adjust_scroll
        popad
        ret

; edit_push_state - snapshot before an edit and invalidate the redo ring
edit_push_state:
        pushad
        cmp dword [undo_ptr], 0
        je .eps_done
        mov dword [redo_head], 0
        mov dword [redo_count], 0
        mov eax, [undo_ptr]
        mov ebx, [undo_head]
        call edit_store_state
        mov eax, [undo_head]
        inc eax
        and eax, UNDO_LEVELS - 1
        mov [undo_head], eax
        mov eax, [undo_count]
        cmp eax, UNDO_LEVELS
        jae .eps_done
        inc dword [undo_count]
.eps_done:
        popad
        ret

; edit_undo - restore the previous snapshot
edit_undo:
        pushad
        cmp dword [undo_ptr], 0
        je .eu_done
        cmp dword [undo_count], 0
        je .eu_done
        ; Keep the current state so Ctrl+Y can come back to it.
        mov eax, [redo_ptr]
        mov ebx, [redo_head]
        call edit_store_state
        mov eax, [redo_head]
        inc eax
        and eax, UNDO_LEVELS - 1
        mov [redo_head], eax
        mov eax, [redo_count]
        cmp eax, UNDO_LEVELS
        jae .eu_pop
        inc dword [redo_count]
.eu_pop:
        ; Pop the newest undo slot.
        mov eax, [undo_head]
        dec eax
        and eax, UNDO_LEVELS - 1
        mov [undo_head], eax
        mov ebx, eax
        dec dword [undo_count]
        mov eax, [undo_ptr]
        call edit_load_state
.eu_done:
        popad
        ret

; edit_redo - reapply the last undone edit
edit_redo:
        pushad
        cmp dword [redo_ptr], 0
        je .er_done
        cmp dword [redo_count], 0
        je .er_done
        mov eax, [undo_ptr]
        mov ebx, [undo_head]
        call edit_store_state
        mov eax, [undo_head]
        inc eax
        and eax, UNDO_LEVELS - 1
        mov [undo_head], eax
        mov eax, [undo_count]
        cmp eax, UNDO_LEVELS
        jae .er_pop
        inc dword [undo_count]
.er_pop:
        mov eax, [redo_head]
        dec eax
        and eax, UNDO_LEVELS - 1
        mov [redo_head], eax
        mov ebx, eax
        dec dword [redo_count]
        mov eax, [redo_ptr]
        call edit_load_state
.er_done:
        popad
        ret

;---------------------------------------
; draw_editor
;---------------------------------------
draw_editor:
        pushad
        ; Background
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 0
        mov edx, 520
        mov esi, 360
        mov edi, 0x00FFFFFF
        call gui_fill_rect

        ; Status bar
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 340
        mov edx, 520
        mov esi, 20
        mov edi, 0x00E0E0E0
        call gui_fill_rect

        ; Status text
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 344
        mov esi, status_str
        mov edi, 0x00404040
        call gui_draw_text

        ; Draw text lines
        xor ecx, ecx
.draw_line:
        cmp ecx, VISIBLE_LINES
        jge .draw_cursor
        mov eax, ecx
        add eax, [scroll_y]
        cmp eax, [num_lines]
        jge .draw_cursor

        push ecx
        ; Get line
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]

        ; Draw it
        mov eax, [win_id]
        mov ebx, 8
        imul ecx, 16
        add ecx, 4
        mov edi, 0x00000000
        call gui_draw_text
        pop ecx
        inc ecx
        jmp .draw_line

.draw_cursor:
        ; Draw cursor block
        mov eax, [cur_line]
        sub eax, [scroll_y]
        cmp eax, 0
        jl .draw_end
        cmp eax, VISIBLE_LINES
        jge .draw_end

        push eax
        mov eax, [win_id]
        mov ebx, [cur_col]
        shl ebx, 3             ; * 8
        add ebx, 8
        pop eax
        imul eax, 16
        add eax, 4
        mov ecx, eax
        mov edx, 2             ; width
        mov esi, 16            ; height
        mov edi, 0x00000000
        call gui_fill_rect

.draw_end:
        popad
        ret

;---------------------------------------
; insert_char - Insert BL at cursor position
;---------------------------------------
insert_char:
        pushad
        mov eax, [cur_line]
        imul eax, LINE_LEN
        add eax, [cur_col]
        lea edi, [text_buf + eax]
        ; Shift right
        call strlen_from
        mov ecx, eax
        cmp ecx, LINE_LEN - 2
        jge .ic_done
        call edit_push_state
        lea esi, [edi + ecx]
        lea edx, [edi + ecx + 1]
        std
.ic_shift:
        cmp ecx, 0
        jl .ic_insert
        mov al, [esi]
        mov [esi + 1], al
        dec esi
        dec ecx
        jmp .ic_shift
.ic_insert:
        cld
        mov [edi], bl
        inc dword [cur_col]
.ic_done:
        popad
        ret

;---------------------------------------
; delete_char - Backspace at cursor
;---------------------------------------
delete_char:
        pushad
        cmp dword [cur_col], 0
        je .dc_done
        call edit_push_state
        dec dword [cur_col]
        mov eax, [cur_line]
        imul eax, LINE_LEN
        add eax, [cur_col]
        lea edi, [text_buf + eax]
        ; Shift left
        lea esi, [edi + 1]
.dc_shift:
        mov al, [esi]
        mov [edi], al
        cmp al, 0
        je .dc_done
        inc esi
        inc edi
        jmp .dc_shift
.dc_done:
        popad
        ret

;---------------------------------------
; insert_newline
;---------------------------------------
insert_newline:
        pushad
        mov eax, [num_lines]
        cmp eax, MAX_LINES - 1
        jge .nl_done
        call edit_push_state

        ; Shift lines [cur_line+1 .. num_lines-1] down by one LINE_LEN
        ; src = text_buf + (num_lines-1)*LINE_LEN
        ; dst = text_buf + num_lines*LINE_LEN
        ; count = (num_lines - cur_line - 1) lines
        cld
        mov ecx, [num_lines]
        sub ecx, [cur_line]
        dec ecx                 ; lines to move
        cmp ecx, 0
        jle .nl_no_shift
        ; Move from bottom up (use backwards copy to avoid overlap)
        ; Last source line: (num_lines - 1) * LINE_LEN
        mov eax, [num_lines]
        dec eax
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]
        lea edi, [text_buf + eax + LINE_LEN]
.nl_shift:
        cmp ecx, 0
        jle .nl_no_shift
        ; Copy one line (LINE_LEN bytes)
        push ecx
        push esi
        push edi
        mov ecx, LINE_LEN / 4
        rep movsd
        pop edi
        pop esi
        pop ecx
        sub esi, LINE_LEN
        sub edi, LINE_LEN
        dec ecx
        jmp .nl_shift

.nl_no_shift:
        ; Copy text after cur_col from cur_line to new line below
        mov eax, [cur_line]
        imul eax, LINE_LEN
        lea esi, [text_buf + eax]       ; current line base

        ; New line = cur_line + 1
        lea edi, [text_buf + eax + LINE_LEN]
        ; Clear new line first
        push edi
        push eax
        xor eax, eax
        mov ecx, LINE_LEN / 4
        rep stosd
        pop eax
        pop edi

        ; Copy text from cur_col onward to new line
        mov ecx, [cur_col]
        push esi
        add esi, ecx            ; src = cur_line + cur_col
        mov ecx, LINE_LEN
        sub ecx, [cur_col]     ; bytes to copy
.nl_copy:
        cmp ecx, 0
        jle .nl_copy_done
        lodsb
        stosb
        dec ecx
        jmp .nl_copy
.nl_copy_done:
        pop esi                 ; restore current line base

        ; Null out text after cur_col on current line
        mov edi, esi
        add edi, [cur_col]
        mov ecx, LINE_LEN
        sub ecx, [cur_col]
        xor al, al
        rep stosb

        inc dword [num_lines]
        inc dword [cur_line]
        mov dword [cur_col], 0
        call adjust_scroll
.nl_done:
        popad
        ret

;---------------------------------------
; adjust_scroll
;---------------------------------------
adjust_scroll:
        pushad
        mov eax, [cur_line]
        cmp eax, [scroll_y]
        jge .check_bottom
        mov [scroll_y], eax
        jmp .as_done
.check_bottom:
        mov ebx, [scroll_y]
        add ebx, VISIBLE_LINES
        cmp eax, ebx
        jl .as_done
        mov ebx, eax
        sub ebx, VISIBLE_LINES - 1
        mov [scroll_y], ebx
.as_done:
        popad
        ret

;---------------------------------------
; load_file
;---------------------------------------
load_file:
        pushad
        mov eax, SYS_FREAD
        mov ebx, arg_buf
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .lf_done
        ; Parse into lines
        mov esi, file_buf
        mov edi, text_buf
        mov dword [num_lines], 1
        xor ecx, ecx           ; col
.lf_parse:
        lodsb
        cmp al, 0
        je .lf_done
        cmp al, 10             ; newline
        je .lf_newline
        cmp ecx, LINE_LEN - 1
        jge .lf_parse
        stosb
        inc ecx
        jmp .lf_parse
.lf_newline:
        ; Pad rest of line with zeros
        push ecx
        mov eax, LINE_LEN
        sub eax, ecx
        mov ecx, eax
        xor al, al
        rep stosb
        pop ecx
        xor ecx, ecx
        inc dword [num_lines]
        cmp dword [num_lines], MAX_LINES
        jge .lf_done
        jmp .lf_parse
.lf_done:
        popad
        ret

;---------------------------------------
; save_file
;---------------------------------------
save_file:
        pushad
        ; Check if we have a filename
        cmp byte [arg_buf], 0
        je .sf_done
        ; Flatten text_buf into file_buf
        mov esi, text_buf
        mov edi, file_buf
        xor edx, edx           ; total bytes
        xor ecx, ecx           ; line counter
.sf_line:
        cmp ecx, [num_lines]
        jge .sf_write
        push ecx
        push esi
        ; Find line end (skip trailing zeros)
        mov ebx, LINE_LEN - 1
.sf_find_end:
        cmp ebx, 0
        jl .sf_empty
        cmp byte [esi + ebx], 0
        jne .sf_copy
        dec ebx
        jmp .sf_find_end
.sf_empty:
        ; Empty line, just add newline
        mov byte [edi], 10
        inc edi
        inc edx
        pop esi
        add esi, LINE_LEN
        pop ecx
        inc ecx
        jmp .sf_line
.sf_copy:
        inc ebx                ; length
        push ecx
        mov ecx, ebx
.sf_cp:
        lodsb
        stosb
        inc edx
        dec ecx
        jnz .sf_cp
        pop ecx
        ; Add newline
        mov byte [edi], 10
        inc edi
        inc edx
        pop esi
        add esi, LINE_LEN
        pop ecx
        inc ecx
        jmp .sf_line
.sf_write:
        mov byte [edi], 0
        mov eax, SYS_FWRITE
        mov ebx, arg_buf
        mov ecx, file_buf
        ; EDX = size (already set)
        xor esi, esi            ; type = text
        int 0x80
.sf_done:
        popad
        ret

;---------------------------------------
; strlen - Length of string at ESI
;---------------------------------------
strlen:
        push esi
        xor eax, eax
.sl:
        cmp byte [esi], 0
        je .sl_done
        inc eax
        inc esi
        jmp .sl
.sl_done:
        pop esi
        ret

;---------------------------------------
; strlen_from - Length of string at EDI
;---------------------------------------
strlen_from:
        push edi
        xor eax, eax
.sf:
        cmp byte [edi], 0
        je .sf_done
        inc eax
        inc edi
        jmp .sf
.sf_done:
        pop edi
        ret

; Data
title_str:      db "BEdit", 0
status_str:     db "^O:Open ^S:Save ^Z:Undo ^Y:Redo ESC:Exit", 0

win_id:         dd 0
cur_line:       dd 0
cur_col:        dd 0
scroll_y:       dd 0
num_lines:      dd 1

undo_ptr:       dd 0            ; snapshot ring base (static buffers below)
redo_ptr:       dd 0
undo_head:      dd 0
undo_count:     dd 0
redo_head:      dd 0
redo_count:     dd 0

undo_buf:       times UNDO_LEVELS * UNDO_SLOT db 0
redo_buf:       times UNDO_LEVELS * UNDO_SLOT db 0

arg_buf:        times 64 db 0
file_buf:       times 8192 db 0
text_buf:       times MAX_LINES * LINE_LEN db 0
