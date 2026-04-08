; basename.asm - Print filename from path [HBU]
; Usage: basename PATH
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
        mov edi, esi            ; last_slash = start (no slash case)
        mov edx, esi            ; scan pointer
.scan:
        cmp byte [edx], 0
        je .print_it
        cmp byte [edx], ' '
        je .print_it
        cmp byte [edx], '/'
        jne .scan_next
        lea edi, [edx+1]       ; point after the slash
.scan_next:
        inc edx
        jmp .scan

.print_it:
        ; edi = pointer to basename
        mov byte [edx], 0      ; null terminate at space/end
.print_loop:
        cmp byte [edi], 0
        je .newline

        mov eax, SYS_PUTCHAR
        movzx ebx, byte [edi]
        int 0x80

        inc edi
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
