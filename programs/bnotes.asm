; bnotes.asm - Sticky Notes for Mellivora OS (Burrows GUI)
; Create and edit small sticky notes in a GUI window.
; Notes can be saved to and loaded from disk.

%include "syscalls.inc"
%include "lib/gui.inc"

; Window dimensions
WIN_W           equ 250
WIN_H           equ 200

; Note area
NOTE_X          equ 4
NOTE_Y          equ 28
NOTE_W          equ 242
NOTE_H          equ 168
CHARS_PER_LINE  equ 29          ; ~242/8 chars at 8px font
MAX_LINES       equ 20          ; ~168/8 lines at 8px font
MAX_TEXT        equ 600         ; max note text bytes

; Button bar
BTN_Y           equ 4
BTN_W           equ 50
BTN_H           equ 18

; Colors - yellow sticky note
COL_NOTE_BG     equ 0x00FFFF88
COL_NOTE_TEXT   equ 0x00333300
COL_BTN_NEW     equ 0x0088CC88
COL_BTN_SAVE    equ 0x008888CC
COL_BTN_LOAD    equ 0x00CC8888
COL_BTN_CLR     equ 0x00AAAAAA
COL_BTN_TEXT    equ 0x00000000
COL_CURSOR      equ 0x00FF0000
COL_TOOLBAR     equ 0x00DDDDAA
COL_STATUS      equ 0x00666644

start:
        ; Create window
        mov eax, 150
        mov ebx, 80
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, win_title
        call gui_create_window
        mov [win_id], eax

        ; Initialize
        call clear_note

.main_loop:
        call draw_all

        ; Poll events
        mov eax, [win_id]
        call gui_poll_event

        cmp eax, EVT_CLOSE
        je .quit

        cmp eax, EVT_KEY_PRESS
        je .handle_key

        cmp eax, EVT_MOUSE_CLICK
        je .handle_click

        jmp .main_loop

.handle_key:
        ; EBX = keycode
        cmp ebx, 27             ; ESC
        je .quit

        cmp ebx, 8              ; Backspace
        je .do_backspace

        cmp ebx, 13             ; Enter
        je .do_enter

        cmp ebx, 3              ; Ctrl+C (copy line)
        je .do_copy

        cmp ebx, 22             ; Ctrl+V (paste)
        je .do_paste

        ; Printable chars only
        cmp ebx, 32
        jb .main_loop
        cmp ebx, 126
        ja .main_loop

        ; Insert character
        mov ecx, [cursor_pos]
        cmp ecx, MAX_TEXT - 1
        jge .main_loop
        mov [note_text + ecx], bl
        inc dword [cursor_pos]
        jmp .main_loop

.do_backspace:
        mov ecx, [cursor_pos]
        test ecx, ecx
        jz .main_loop
        dec dword [cursor_pos]
        ; Shift text left from cursor_pos
        mov ecx, [cursor_pos]
.bs_shift:
        mov al, [note_text + ecx + 1]
        mov [note_text + ecx], al
        test al, al
        jz .main_loop
        inc ecx
        jmp .bs_shift

.do_enter:
        mov ecx, [cursor_pos]
        cmp ecx, MAX_TEXT - 1
        jge .main_loop
        mov byte [note_text + ecx], 10  ; newline
        inc dword [cursor_pos]
        jmp .main_loop

.do_copy:
        ; Copy from cursor to next newline (or end) to clipboard
        mov ecx, [cursor_pos]
        lea ebx, [note_text + ecx]
        ; Find length to next newline or end
        xor ecx, ecx
.copy_len:
        cmp byte [ebx + ecx], 0
        je .copy_do
        cmp byte [ebx + ecx], 10
        je .copy_do
        inc ecx
        cmp ecx, MAX_TEXT
        jl .copy_len
.copy_do:
        mov eax, SYS_CLIPBOARD_COPY
        int 0x80
        jmp .main_loop

.do_paste:
        ; Paste clipboard at cursor position
        mov eax, SYS_CLIPBOARD_PASTE
        mov ebx, clip_buf
        mov ecx, MAX_TEXT
        int 0x80
        ; EAX = bytes pasted
        test eax, eax
        jz .main_loop
        ; Insert characters at cursor
        mov esi, clip_buf
        mov ecx, eax
.paste_loop:
        cmp ecx, 0
        jle .main_loop
        mov edx, [cursor_pos]
        cmp edx, MAX_TEXT - 1
        jge .main_loop
        mov al, [esi]
        cmp al, 0
        je .main_loop
        mov [note_text + edx], al
        inc dword [cursor_pos]
        inc esi
        dec ecx
        jmp .paste_loop

.handle_click:
        ; EBX = x, ECX = y (relative to client area)
        ; Check button bar
        cmp ecx, BTN_Y
        jb .main_loop
        mov eax, ecx
        sub eax, BTN_Y
        cmp eax, BTN_H
        jg .click_note

        ; Which button? x ranges:
        ; New:  4..54    Save: 58..108   Load: 112..162   Clear: 166..216
        cmp ebx, 4
        jb .main_loop
        cmp ebx, 54
        jle .btn_new
        cmp ebx, 108
        jle .btn_save
        cmp ebx, 162
        jle .btn_load
        cmp ebx, 216
        jle .btn_clear
        jmp .main_loop

.btn_new:
        call clear_note
        jmp .main_loop
.btn_save:
        call save_note
        jmp .main_loop
.btn_load:
        call load_note
        jmp .main_loop
.btn_clear:
        call clear_note
        jmp .main_loop

.click_note:
        ; Clicked in note area — set cursor to approximate position
        ; Row = (y - NOTE_Y) / 8, Col = (x - NOTE_X) / 8
        sub ecx, NOTE_Y
        js .main_loop
        shr ecx, 3              ; row
        sub ebx, NOTE_X
        js .main_loop
        shr ebx, 3              ; col

        ; Walk text to find position at (row, col)
        xor edx, edx            ; current text index
        xor esi, esi            ; current row
        xor edi, edi            ; current col

.click_scan:
        cmp byte [note_text + edx], 0
        je .click_found
        cmp esi, ecx
        jg .click_found
        cmp esi, ecx
        jl .click_next
        ; Same row - check if col matches
        cmp edi, ebx
        jge .click_found
.click_next:
        cmp byte [note_text + edx], 10
        jne .click_not_nl
        inc esi
        xor edi, edi
        inc edx
        jmp .click_scan
.click_not_nl:
        inc edi
        cmp edi, CHARS_PER_LINE
        jl .click_no_wrap
        xor edi, edi
        inc esi
.click_no_wrap:
        inc edx
        jmp .click_scan
.click_found:
        mov [cursor_pos], edx
        jmp .main_loop

.quit:
        mov eax, [win_id]
        call gui_destroy_window
        mov eax, SYS_EXIT
        int 0x80


; ─── draw_all ────────────────────────────────────────────────
draw_all:
        PUSHALL

        ; Toolbar background
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 0
        mov edx, WIN_W
        mov esi, NOTE_Y - 2
        mov edi, COL_TOOLBAR
        call gui_fill_rect

        ; Buttons
        ; New
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, BTN_Y
        mov edx, BTN_W
        mov esi, BTN_H
        mov edi, COL_BTN_NEW
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 12
        mov ecx, BTN_Y + 4
        mov esi, str_new
        mov edi, COL_BTN_TEXT
        call gui_draw_text

        ; Save
        mov eax, [win_id]
        mov ebx, 58
        mov ecx, BTN_Y
        mov edx, BTN_W
        mov esi, BTN_H
        mov edi, COL_BTN_SAVE
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 66
        mov ecx, BTN_Y + 4
        mov esi, str_save
        mov edi, COL_BTN_TEXT
        call gui_draw_text

        ; Load
        mov eax, [win_id]
        mov ebx, 112
        mov ecx, BTN_Y
        mov edx, BTN_W
        mov esi, BTN_H
        mov edi, COL_BTN_LOAD
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 120
        mov ecx, BTN_Y + 4
        mov esi, str_load
        mov edi, COL_BTN_TEXT
        call gui_draw_text

        ; Clear
        mov eax, [win_id]
        mov ebx, 166
        mov ecx, BTN_Y
        mov edx, BTN_W
        mov esi, BTN_H
        mov edi, COL_BTN_CLR
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 170
        mov ecx, BTN_Y + 4
        mov esi, str_clear
        mov edi, COL_BTN_TEXT
        call gui_draw_text

        ; Note background
        mov eax, [win_id]
        mov ebx, NOTE_X
        mov ecx, NOTE_Y
        mov edx, NOTE_W
        mov esi, NOTE_H
        mov edi, COL_NOTE_BG
        call gui_fill_rect

        ; Draw note text with cursor
        xor ecx, ecx            ; text index
        mov ebx, NOTE_X         ; x
        mov edx, NOTE_Y         ; y
        mov dword [draw_col], 0

.draw_text:
        ; Check cursor position - draw cursor indicator
        cmp ecx, [cursor_pos]
        jne .draw_no_cursor
        ; Draw cursor (red bar at current position)
        push rcx
        push rbx
        push rdx
        mov eax, [win_id]
        mov ecx, edx            ; y
        mov edx, 2              ; w
        mov esi, 8              ; h
        mov edi, COL_CURSOR
        ; ebx already = x
        call gui_fill_rect
        pop rdx
        pop rbx
        pop rcx

.draw_no_cursor:
        cmp byte [note_text + ecx], 0
        je .draw_done

        cmp byte [note_text + ecx], 10   ; newline
        je .draw_newline

        ; Draw character
        push rcx
        push rbx
        push rdx
        movzx eax, byte [note_text + ecx]
        mov [char_buf], al
        mov byte [char_buf + 1], 0
        mov eax, [win_id]
        mov ecx, edx            ; y
        mov esi, char_buf
        mov edi, COL_NOTE_TEXT
        ; ebx already = x
        call gui_draw_text
        pop rdx
        pop rbx
        pop rcx

        add ebx, 8              ; advance x
        inc dword [draw_col]
        cmp dword [draw_col], CHARS_PER_LINE
        jl .draw_next_char
        ; Line wrap
        mov ebx, NOTE_X
        add edx, 8
        mov dword [draw_col], 0
        jmp .draw_next_char

.draw_newline:
        mov ebx, NOTE_X
        add edx, 8
        mov dword [draw_col], 0

.draw_next_char:
        inc ecx
        ; Check if we've gone past visible area
        cmp edx, NOTE_Y + NOTE_H - 8
        jg .draw_done
        jmp .draw_text

.draw_done:
        mov eax, [win_id]
        call gui_compose
        mov eax, [win_id]
        call gui_flip

        POPALL
        ret


; ─── clear_note ──────────────────────────────────────────────
clear_note:
        PUSHALL
        mov edi, note_text
        mov ecx, MAX_TEXT
        xor eax, eax
        rep stosb
        mov dword [cursor_pos], 0
        POPALL
        ret


; ─── save_note ───────────────────────────────────────────────
; Save note text to file "note.txt"
save_note:
        PUSHALL
        ; Calculate text length
        mov esi, note_text
        xor ecx, ecx
.save_len:
        cmp byte [esi + ecx], 0
        je .save_write
        inc ecx
        cmp ecx, MAX_TEXT
        jl .save_len
.save_write:
        ; SYS_FWRITE: EBX=name, ECX=buf, EDX=size, ESI=type(0=text)
        mov eax, SYS_FWRITE
        mov edx, ecx             ; size
        mov ecx, note_text       ; buf
        mov ebx, save_filename   ; name
        xor esi, esi             ; type = text
        int 0x80

        POPALL
        ret


; ─── load_note ───────────────────────────────────────────────
; Load note text from file "note.txt"
load_note:
        PUSHALL
        ; First clear
        call clear_note
        ; SYS_FREAD: EBX=name, ECX=buf -> EAX=bytes read
        mov eax, SYS_FREAD
        mov ebx, save_filename
        mov ecx, note_text
        int 0x80

        ; Null-terminate
        cmp eax, 0
        jle .load_fail
        cmp eax, MAX_TEXT - 1
        jl .load_ok
        mov eax, MAX_TEXT - 1
.load_ok:
        mov byte [note_text + eax], 0
.load_fail:
        POPALL
        ret


; ═════════════════════════════════════════════════════════════
; DATA
; ═════════════════════════════════════════════════════════════

win_title:      db "BNotes", 0
save_filename:  db "note.txt", 0
str_new:        db "New", 0
str_save:       db "Save", 0
str_load:       db "Load", 0
str_clear:      db "Clear", 0
char_buf:       db 0, 0

; ═════════════════════════════════════════════════════════════
; BSS
; ═════════════════════════════════════════════════════════════

section .bss

win_id:         resd 1
cursor_pos:     resd 1
draw_col:       resd 1
note_text:      resb MAX_TEXT
clip_buf:       resb MAX_TEXT
