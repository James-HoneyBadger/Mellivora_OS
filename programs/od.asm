; od.asm - Octal/hex dump of a file [HBU]
; Usage: od FILE
%include "syscalls.inc"

start:
        ; Get filename
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Skip spaces
        mov esi, args_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Read entire file
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .file_err
        mov [file_size], eax

        ; Dump 16 bytes per line
        mov esi, file_buf
        xor edx, edx           ; offset

.line_loop:
        cmp edx, [file_size]
        jge .done

        ; Print up to 16 bytes
        xor ecx, ecx           ; byte counter within line
.byte_loop:
        cmp ecx, 16
        jge .line_end
        mov eax, edx
        add eax, ecx
        cmp eax, [file_size]
        jge .line_end

        ; Print hex byte (eax = edx + ecx already computed above)
        movzx ebx, byte [esi + eax]
        call print_hex_byte

        ; Space separator
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        inc ecx
        jmp .byte_loop

.line_end:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        add edx, 16
        jmp .line_loop

.done:
        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; Print byte in BL as two hex digits
print_hex_byte:
        push ebx
        mov al, bl
        shr al, 4
        call print_hex_digit
        pop ebx
        mov al, bl
        and al, 0x0F
        call print_hex_digit
        ret

print_hex_digit:
        cmp al, 10
        jl .digit
        add al, 'a' - 10
        jmp .print_it
.digit:
        add al, '0'
.print_it:
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        ret

skip_spaces:
.loop:
        cmp byte [esi], ' '
        je .skip
        cmp byte [esi], 9
        je .skip
        ret
.skip:
        inc esi
        jmp .loop

msg_usage:      db "Usage: od FILE", 10, 0
msg_err:        db "od: cannot open file", 10, 0

section .bss
args_buf:       resb 256
file_size:      resd 1
file_buf:       resb 32768
