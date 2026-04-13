; xxd.asm - Hex dump with ASCII (like Unix xxd)
; Usage: xxd <filename>

%include "syscalls.inc"

BUF_SIZE        equ 32768

start:
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        cmp eax, 0
        je .usage

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, argbuf
        mov ecx, filebuf
        int 0x80
        cmp eax, -1
        je .err
        cmp eax, 0
        je .done
        mov [file_len], eax

        ; Display hex dump
        xor esi, esi            ; offset
.row_loop:
        cmp esi, [file_len]
        jge .done

        ; Print offset (8 hex digits)
        mov eax, esi
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, str_colon
        int 0x80

        ; Print 16 hex bytes
        xor ecx, ecx
.hex_loop:
        cmp ecx, 16
        jge .ascii_part
        mov eax, esi
        add eax, ecx
        cmp eax, [file_len]
        jge .hex_pad
        movzx eax, byte [filebuf + eax]
        call print_hex8
        ; Space after every byte, extra space after 8
        cmp ecx, 7
        jne .hex_sp
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
.hex_sp:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc ecx
        jmp .hex_loop

.hex_pad:
        ; Pad with spaces
        mov eax, SYS_PRINT
        mov ebx, str_3sp
        int 0x80
        cmp ecx, 7
        jne .hp_noex
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
.hp_noex:
        inc ecx
        jmp .hex_loop

.ascii_part:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Print ASCII representation
        xor ecx, ecx
.asc_loop:
        cmp ecx, 16
        jge .asc_end
        mov eax, esi
        add eax, ecx
        cmp eax, [file_len]
        jge .asc_end
        movzx ebx, byte [filebuf + eax]
        ; Printable?
        cmp bl, 32
        jl .asc_dot
        cmp bl, 126
        jg .asc_dot
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .asc_next
.asc_dot:
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
.asc_next:
        inc ecx
        jmp .asc_loop

.asc_end:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        add esi, 16
        jmp .row_loop

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .done

.err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        jmp .done

;---------------------------------------
print_hex32:
        ; EAX = 32-bit value, prints 8 hex digits
        pushad
        mov edx, eax
        mov eax, edx
        shr eax, 28
        and eax, 0xF
        call print_nybble
        mov eax, edx
        shr eax, 24
        and eax, 0xF
        call print_nybble
        mov eax, edx
        shr eax, 20
        and eax, 0xF
        call print_nybble
        mov eax, edx
        shr eax, 16
        and eax, 0xF
        call print_nybble
        mov eax, edx
        shr eax, 12
        and eax, 0xF
        call print_nybble
        mov eax, edx
        shr eax, 8
        and eax, 0xF
        call print_nybble
        mov eax, edx
        shr eax, 4
        and eax, 0xF
        call print_nybble
        mov eax, edx
        and eax, 0xF
        call print_nybble
        popad
        ret

print_hex8:
        ; AL = byte, prints 2 hex digits
        pushad
        movzx edx, al
        mov eax, edx
        shr eax, 4
        call print_nybble
        mov eax, edx
        and eax, 0xF
        call print_nybble
        popad
        ret

print_nybble:
        ; EAX = 0-15, prints one hex digit
        pushad
        cmp eax, 10
        jge .pn_af
        add eax, '0'
        jmp .pn_out
.pn_af:
        add eax, 'a' - 10
.pn_out:
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        popad
        ret

;=======================================
msg_usage:      db "Usage: xxd <filename>", 10, 0
msg_err:        db "Error: cannot read file", 10, 0
str_colon:      db ": ", 0
str_3sp:        db "   ", 0

argbuf:         times 256 db 0
file_len:       dd 0
filebuf:        times BUF_SIZE db 0
