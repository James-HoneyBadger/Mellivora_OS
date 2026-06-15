; filepick.asm - File Picker API demonstration
;
; Shows how to use SYS_FILE_OPEN_DLG and SYS_FILE_SAVE_DLG to drive
; the unified file-dialog service introduced in vNext Phase 2.
; Demonstrates reading the chosen file, displaying its first bytes,
; then saving a transformed copy to a user-chosen path.
;
; Intermediate sample — assumes you understand syscalls.inc basics.
; Q or ESC to quit at any menu prompt.
%include "syscalls.inc"

FILE_BUF_SIZE   equ 65536       ; 64 KB read buffer
NAME_BUF_SIZE   equ 256
COLOR_TITLE     equ 0x0F
COLOR_DIM       equ 0x07
COLOR_GOOD      equ 0x0A
COLOR_ERROR     equ 0x0C
COLOR_INFO      equ 0x0B
COLOR_KEY       equ 0x0E
COLOR_DATA      equ 0x09

start:
        call print_header

; ---- open dialog ----
.open_phase:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_INFO
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_open_prompt
        int 0x80

        mov eax, SYS_FILE_OPEN_DLG
        mov ebx, dlg_open_title
        mov ecx, open_name_buf
        mov edx, dlg_filter_text
        int 0x80

        test eax, eax
        jz .dialog_cancelled

        ; EAX=1, open_name_buf has the chosen path
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_GOOD
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_selected
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, open_name_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80

; ---- read the file ----
        mov eax, SYS_FREAD
        mov ebx, open_name_buf
        mov ecx, file_buf
        int 0x80

        cmp eax, 0
        jl .read_error

        mov [file_size], eax

        ; Print first 64 bytes as hex preview
        call print_hex_preview

; ---- save dialog ----
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_INFO
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_save_prompt
        int 0x80

        mov eax, SYS_FILE_SAVE_DLG
        mov ebx, dlg_save_title
        mov ecx, save_name_buf
        mov edx, dlg_filter_text
        int 0x80

        test eax, eax
        jz .dialog_cancelled

        ; Write the same data back (demo: identity copy)
        mov eax, SYS_FWRITE
        mov ebx, save_name_buf
        mov ecx, file_buf
        mov edx, [file_size]
        mov esi, 1              ; FTYPE_TEXT
        int 0x80

        test eax, eax
        js .write_error

        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_GOOD
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_saved
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, save_name_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        jmp .done

.dialog_cancelled:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DIM
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_cancelled
        int 0x80
        jmp .done

.read_error:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_ERROR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_read_error
        int 0x80
        jmp .done

.write_error:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_ERROR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_write_error
        int 0x80

.done:
        call wait_key
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DIM
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;-------------------------------------------
print_header:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_TITLE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80
        ret

;-------------------------------------------
; print_hex_preview: dump first 64 bytes of file_buf as hex
print_hex_preview:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_INFO
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_preview_hdr
        int 0x80

        mov esi, file_buf
        mov ecx, [file_size]
        cmp ecx, 64
        jle .hp_go
        mov ecx, 64
.hp_go:
        mov edi, hex_line
        mov ebp, 0              ; byte counter per row
.hp_loop:
        test ecx, ecx
        jz .hp_done

        ; Write byte as two hex digits
        xor eax, eax
        lodsb
        push eax
        shr eax, 4
        call nib2hex
        mov [edi], al
        inc edi
        pop eax
        and eax, 0x0F
        call nib2hex
        mov [edi], al
        inc edi
        mov byte [edi], ' '
        inc edi
        dec ecx
        inc ebp

        ; New line every 16 bytes
        cmp ebp, 16
        jne .hp_loop
        mov byte [edi], 0x0A
        inc edi
        mov byte [edi], 0
        mov eax, SYS_PRINT
        mov ebx, COLOR_DATA
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DATA
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hex_line
        int 0x80
        mov edi, hex_line
        mov ebp, 0
        jmp .hp_loop

.hp_done:
        cmp ebp, 0
        je .hp_end
        mov byte [edi], 0x0A
        inc edi
        mov byte [edi], 0
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DATA
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hex_line
        int 0x80
.hp_end:
        popad
        ret

nib2hex:
        cmp al, 10
        jl .digit
        add al, 'a' - 10
        ret
.digit:
        add al, '0'
        ret

;-------------------------------------------
wait_key:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_KEY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_press_key
        int 0x80
.wk:    mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .wk
        ret

; ---------------------------------------------------------------------------
msg_title:        db "=== File Picker API Demo ===", 0x0A, 0
msg_open_prompt:  db 0x0A, "Opening file picker dialog...", 0x0A, 0
msg_selected:     db "  Selected: ", 0
msg_preview_hdr:  db 0x0A, "  First 64 bytes (hex):", 0x0A, 0
msg_save_prompt:  db 0x0A, "Opening save dialog...", 0x0A, 0
msg_saved:        db "  Saved to: ", 0
msg_cancelled:    db "  Dialog cancelled.", 0x0A, 0
msg_read_error:   db "  Error: could not read file.", 0x0A, 0
msg_write_error:  db "  Error: could not save file.", 0x0A, 0
msg_newline:      db 0x0A, 0
msg_press_key:    db 0x0A, "  [Press any key] ", 0

dlg_open_title:   db "Open a text file", 0
dlg_save_title:   db "Save copy to...", 0
dlg_filter_text:  db "*.txt", 0

open_name_buf:    times NAME_BUF_SIZE db 0
save_name_buf:    times NAME_BUF_SIZE db 0
hex_line:         times 64 db 0
file_size:        dd 0
file_buf:         times FILE_BUF_SIZE db 0
