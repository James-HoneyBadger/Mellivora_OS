; hexdump.asm - Hex dump utility for Mellivora OS
; Usage: hexdump <filename>
; Displays file contents in hex + ASCII format
;
%include "syscalls.inc"

BYTES_PER_LINE  equ 16

start:
        ; Get filename
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, args_buf
        mov ecx, file_buffer
        int 0x80
        cmp eax, 0
        jl .file_err
        mov [file_size], eax

        ; Display hex dump
        xor esi, esi            ; offset

.dump_loop:
        cmp esi, [file_size]
        jge .done

        ; Print offset in hex (8 digits)
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; dark gray
        int 0x80

        mov eax, esi
        call print_hex_dword

        ; Print separator
        mov eax, SYS_PRINT
        mov ebx, sep_colon
        int 0x80

        ; Print hex bytes
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; cyan
        int 0x80

        xor ecx, ecx           ; byte counter
.hex_loop:
        cmp ecx, BYTES_PER_LINE
        jge .ascii_part
        mov eax, esi
        add eax, ecx
        cmp eax, [file_size]
        jge .hex_pad

        ; Print hex byte
        movzx eax, byte [file_buffer + eax]
        call print_hex_byte

        ; Space separator (extra space after 8th byte)
        cmp ecx, 7
        jne .no_extra_sp
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
.no_extra_sp:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        inc ecx
        jmp .hex_loop

.hex_pad:
        ; Pad with spaces for short last line
        mov eax, SYS_PRINT
        mov ebx, spaces_3
        int 0x80
        cmp ecx, 7
        jne .no_pad_extra
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
.no_pad_extra:
        inc ecx
        jmp .hex_loop

.ascii_part:
        ; Print ASCII representation
        mov eax, SYS_PRINT
        mov ebx, sep_bar
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; green
        int 0x80

        xor ecx, ecx
.ascii_loop:
        cmp ecx, BYTES_PER_LINE
        jge .ascii_done
        mov eax, esi
        add eax, ecx
        cmp eax, [file_size]
        jge .ascii_done

        movzx eax, byte [file_buffer + eax]
        ; Printable? (0x20-0x7E)
        cmp eax, 0x20
        jl .non_printable
        cmp eax, 0x7E
        jg .non_printable
        mov ebx, eax
        jmp .print_ascii_char

.non_printable:
        mov ebx, '.'

.print_ascii_char:
        mov eax, SYS_PUTCHAR
        int 0x80
        inc ecx
        jmp .ascii_loop

.ascii_done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80

        ; Newline
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        add esi, BYTES_PER_LINE
        jmp .dump_loop

.done:
        ; Print total size
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, [file_size]
        call print_hex_dword
        mov eax, SYS_PRINT
        mov ebx, msg_total
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, [file_size]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_bytes
        int 0x80

        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.file_err:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_file_err
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; Print EAX as 8-digit hex
;=======================================================================
print_hex_dword:
        PUSHALL
        mov ecx, 8
        mov edx, eax
.phd_loop:
        rol edx, 4
        mov eax, edx
        and eax, 0x0F
        cmp eax, 10
        jl .phd_digit
        add eax, 'a' - 10
        jmp .phd_put
.phd_digit:
        add eax, '0'
.phd_put:
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .phd_loop
        POPALL
        ret

;=======================================================================
; Print AL as 2-digit hex
;=======================================================================
print_hex_byte:
        PUSHALL
        mov edx, eax
        ; High nibble
        shr eax, 4
        and eax, 0x0F
        cmp eax, 10
        jl .phb_d1
        add eax, 'a' - 10
        jmp .phb_p1
.phb_d1:
        add eax, '0'
.phb_p1:
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        ; Low nibble
        mov eax, edx
        and eax, 0x0F
        cmp eax, 10
        jl .phb_d2
        add eax, 'a' - 10
        jmp .phb_p2
.phb_d2:
        add eax, '0'
.phb_p2:
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        POPALL
        ret

; Data
msg_usage:      db "Usage: hexdump <filename>", 0x0A, 0
msg_file_err:   db "Error: Cannot open file", 0x0A, 0
msg_total:      db " total (", 0
msg_bytes:      db " bytes)", 0x0A, 0
sep_colon:      db ": ", 0
sep_bar:        db " |", 0
spaces_3:       db "   ", 0
args_buf:       times 256 db 0
file_size:      dd 0
file_buffer:    times 32768 db 0
