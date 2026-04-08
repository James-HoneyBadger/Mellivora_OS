; rev.asm - Reverse each line of a file [HBU]
; Usage: rev FILE
%include "syscalls.inc"

start:
        ; Get filename
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Skip spaces to find filename
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

        ; Process line by line
        mov esi, file_buf
.next_line:
        mov eax, esi
        sub eax, file_buf
        cmp eax, [file_size]
        jge .done

        ; Find end of line
        mov edi, esi
.find_eol:
        mov eax, edi
        sub eax, file_buf
        cmp eax, [file_size]
        jge .found_eol
        cmp byte [edi], 0x0A
        je .found_eol
        inc edi
        jmp .find_eol

.found_eol:
        ; esi = line start, edi = one past last char (or at newline)
        ; Print chars from edi-1 back to esi
        mov edx, edi
        dec edx
.rev_print:
        cmp edx, esi
        jl .print_nl
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [edx]
        int 0x80
        dec edx
        jmp .rev_print

.print_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Advance past newline
        mov esi, edi
        cmp byte [esi], 0x0A
        jne .next_line
        inc esi
        jmp .next_line

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

msg_usage:      db "Usage: rev FILE", 10, 0
msg_err:        db "rev: cannot open file", 10, 0

section .bss
args_buf:       resb 256
file_size:      resd 1
file_buf:       resb 32768
