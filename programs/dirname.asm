; dirname.asm - Print directory from path [HBU]
; Usage: dirname PATH
%include "syscalls.inc"

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .done

        ; Skip leading spaces
        mov esi, args_buf
.skip_spaces:
        cmp byte [esi], ' '
        jne .check_tab
        inc esi
        jmp .skip_spaces
.check_tab:
        cmp byte [esi], 9
        jne .find_slash
        inc esi
        jmp .skip_spaces

        ; Find last slash
.find_slash:
        mov edi, 0              ; last_slash position (-1 means none)
        xor ecx, ecx           ; current position
.scan:
        mov al, [esi+ecx]
        cmp al, 0
        je .check_slash
        cmp al, ' '
        je .check_slash
        cmp al, '/'
        jne .scan_next
        mov edi, ecx            ; record slash position
.scan_next:
        inc ecx
        jmp .scan

.check_slash:
        cmp edi, 0
        jne .print_dir

        ; No slash found - print "."
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        jmp .newline

.print_dir:
        ; Print from start to position edi (exclusive)
        xor ecx, ecx
.print_loop:
        cmp ecx, edi
        jge .newline

        mov eax, SYS_PUTCHAR
        movzx ebx, byte [esi+ecx]
        int 0x80

        inc ecx
        jmp .print_loop

.newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.done:
        mov eax, SYS_EXIT
        int 0x80

section .bss
args_buf:       resb 256
