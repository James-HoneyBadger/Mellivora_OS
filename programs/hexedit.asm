; ==========================================================================
; hexedit - Interactive hex viewer/editor for Mellivora OS
;
; Usage: hexedit <filename>
;
; Displays file contents in hex + ASCII format (16 bytes per line).
; Navigation: Arrow Up/Down, PgUp/PgDn, Home/End.
; Press 'q' or ESC to quit.
; ==========================================================================
%include "syscalls.inc"

BYTES_PER_LINE equ 16
LINES_PER_PAGE equ 20
PAGE_SIZE      equ BYTES_PER_LINE * LINES_PER_PAGE  ; 320 bytes per screen
MAX_FILE_SIZE  equ 32768

start:
        ; Get filename from args
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        ; Read the file
        mov eax, SYS_FREAD
        mov ebx, arg_buf
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .file_err
        cmp eax, 0
        je .file_err
        mov [file_size], eax

        ; Start at offset 0
        mov dword [view_offset], 0

.display:
        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Print header
        mov eax, SYS_PRINT
        mov ebx, msg_header
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, arg_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_size_pre
        int 0x80
        mov eax, [file_size]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_size_post
        int 0x80

        ; Display hex lines
        mov dword [cur_line], 0
.draw_line:
        mov eax, [cur_line]
        cmp eax, LINES_PER_PAGE
        jge .draw_done

        ; Calculate offset for this line
        mov eax, [cur_line]
        imul eax, BYTES_PER_LINE
        add eax, [view_offset]
        cmp eax, [file_size]
        jge .draw_done
        mov [line_offset], eax

        ; Print offset (8 hex digits)
        call print_offset

        ; Print separator
        mov eax, SYS_PRINT
        mov ebx, msg_sep
        int 0x80

        ; Print hex bytes
        xor ecx, ecx
.hex_byte:
        cmp ecx, BYTES_PER_LINE
        jge .hex_done
        mov eax, [line_offset]
        add eax, ecx
        cmp eax, [file_size]
        jge .hex_pad

        push rcx
        movzx eax, byte [file_buf + eax]
        call print_hex_byte
        ; Space after byte
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        ; Extra space at midpoint
        pop rcx
        cmp ecx, 7
        jne .hex_next
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        jmp .hex_next

.hex_pad:
        ; Pad with spaces for past-end bytes
        push rcx
        mov eax, SYS_PRINT
        mov ebx, msg_pad
        int 0x80
        pop rcx
        cmp ecx, 7
        jne .hex_next
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
.hex_next:
        inc ecx
        jmp .hex_byte

.hex_done:
        ; Print separator
        mov eax, SYS_PRINT
        mov ebx, msg_ascii_sep
        int 0x80

        ; Print ASCII representation
        xor ecx, ecx
.ascii_byte:
        cmp ecx, BYTES_PER_LINE
        jge .ascii_done
        mov eax, [line_offset]
        add eax, ecx
        cmp eax, [file_size]
        jge .ascii_done

        movzx ebx, byte [file_buf + eax]
        cmp ebx, 32
        jl .ascii_dot
        cmp ebx, 126
        jg .ascii_dot
        jmp .ascii_print
.ascii_dot:
        mov ebx, '.'
.ascii_print:
        push rcx
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        inc ecx
        jmp .ascii_byte
.ascii_done:
        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        inc dword [cur_line]
        jmp .draw_line

.draw_done:
        ; Print footer with navigation help
        mov eax, SYS_PRINT
        mov ebx, msg_footer
        int 0x80

        ; Wait for key
        mov eax, SYS_READ_KEY
        int 0x80

        cmp al, 'q'
        je .quit
        cmp al, 27             ; ESC
        je .quit
        cmp al, KEY_UP
        je .scroll_up
        cmp al, KEY_DOWN
        je .scroll_down
        cmp ah, 0x49           ; PgUp scancode
        je .page_up
        cmp ah, 0x51           ; PgDn scancode
        je .page_down
        cmp ah, 0x47           ; Home scancode
        je .go_home
        cmp ah, 0x4F           ; End scancode
        je .go_end
        ; Also handle 'k'/'j' for vi-style navigation
        cmp al, 'k'
        je .scroll_up
        cmp al, 'j'
        je .scroll_down
        jmp .display

.scroll_up:
        cmp dword [view_offset], 0
        je .display
        sub dword [view_offset], BYTES_PER_LINE
        jmp .display

.scroll_down:
        mov eax, [view_offset]
        add eax, PAGE_SIZE
        cmp eax, [file_size]
        jge .display
        add dword [view_offset], BYTES_PER_LINE
        jmp .display

.page_up:
        mov eax, [view_offset]
        cmp eax, PAGE_SIZE
        jl .go_home
        sub dword [view_offset], PAGE_SIZE
        jmp .display

.page_down:
        mov eax, [view_offset]
        add eax, PAGE_SIZE
        cmp eax, [file_size]
        jge .display
        add dword [view_offset], PAGE_SIZE
        jmp .display

.go_home:
        mov dword [view_offset], 0
        jmp .display

.go_end:
        mov eax, [file_size]
        sub eax, PAGE_SIZE
        cmp eax, 0
        jge .ge_ok
        xor eax, eax
.ge_ok:
        ; Align to line boundary
        xor edx, edx
        mov ecx, BYTES_PER_LINE
        div ecx
        imul eax, BYTES_PER_LINE
        mov [view_offset], eax
        jmp .display

.quit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, arg_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; -------------------------------------------------------------------
; print_offset - Print [line_offset] as 8 hex digits
; -------------------------------------------------------------------
print_offset:
        PUSHALL
        mov eax, [line_offset]
        ; Print 8 nibbles
        mov ecx, 8
        mov edx, 28            ; shift amount (starts at bit 28)
.po_loop:
        push rcx
        push rdx
        mov ebx, eax
        mov cl, dl
        shr ebx, cl
        and ebx, 0x0F
        movzx ebx, byte [hex_chars + ebx]
        push rax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rax
        pop rdx
        pop rcx
        sub edx, 4
        dec ecx
        jnz .po_loop
        POPALL
        ret

; -------------------------------------------------------------------
; print_hex_byte - Print AL as 2 hex digits
; -------------------------------------------------------------------
print_hex_byte:
        PUSHALL
        mov ecx, eax
        ; High nibble
        shr eax, 4
        and eax, 0x0F
        movzx ebx, byte [hex_chars + eax]
        mov eax, SYS_PUTCHAR
        int 0x80
        ; Low nibble
        mov eax, ecx
        and eax, 0x0F
        movzx ebx, byte [hex_chars + eax]
        mov eax, SYS_PUTCHAR
        int 0x80
        POPALL
        ret

; -------------------------------------------------------------------
; Data
; -------------------------------------------------------------------
hex_chars:      db "0123456789abcdef"

msg_usage:      db "Usage: hexedit <filename>", 0x0A
                db "Interactive hex viewer. Navigate with arrows/pgup/pgdn.", 0x0A, 0
msg_err:        db "Error: cannot read file: ", 0
msg_header:     db "=== hexedit: ", 0
msg_size_pre:   db " (", 0
msg_size_post:  db " bytes) ===", 0x0A, 0
msg_sep:        db "  ", 0
msg_ascii_sep:  db " |", 0
msg_pad:        db "   ", 0
msg_footer:     db 0x0A, "[Up/Down/PgUp/PgDn/Home/End] Navigate  [q/ESC] Quit", 0x0A, 0

; BSS
view_offset:    dd 0
file_size:      dd 0
cur_line:       dd 0
line_offset:    dd 0
arg_buf:        times 256 db 0
file_buf:       times MAX_FILE_SIZE db 0
