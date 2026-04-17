; yes.asm - Output a string (or "y") repeatedly until interrupted [HBU]
; Usage: yes [STRING]
; Default: outputs "y" repeatedly
%include "syscalls.inc"

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .use_default

        ; Skip leading spaces
        mov esi, args_buf
.skip_spaces:
        cmp byte [esi], ' '
        jne .check_tab
        inc esi
        jmp .skip_spaces
.check_tab:
        cmp byte [esi], 9
        jne .check_end
        inc esi
        jmp .skip_spaces
.check_end:
        cmp byte [esi], 0
        je .use_default

        mov [str_ptr], rsi
        jmp .output_loop

.use_default:
        mov qword [str_ptr], default_str

.output_loop:
        ; Print string char by char
        mov rsi, [str_ptr]
.print_loop:
        cmp byte [esi], 0
        je .newline
        cmp byte [esi], ' '
        je .newline
        cmp byte [esi], 9
        je .newline

        mov eax, SYS_PUTCHAR
        movzx ebx, byte [esi]
        int 0x80

        inc esi
        jmp .print_loop

.newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .output_loop

default_str:    db "y", 0

section .bss
args_buf:       resb 256
str_ptr:        resq 1
