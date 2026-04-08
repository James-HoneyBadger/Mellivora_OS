; sleep.asm - Sleep for N seconds [HBU]
; Usage: sleep SECONDS
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
        jne .parse_num
        inc esi
        jmp .skip_spaces

        ; Parse number
.parse_num:
        xor ecx, ecx           ; accumulator
.parse_loop:
        movzx eax, byte [esi]
        cmp al, '0'
        jb .do_sleep
        cmp al, '9'
        ja .do_sleep
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_loop

.do_sleep:
        ; Use SYS_SLEEP syscall (ticks, ~18.2 ticks/sec)
        ; ecx = seconds, convert to ticks: seconds * 18
        cmp ecx, 0
        jle .done
        imul ebx, ecx, 18
        mov eax, SYS_SLEEP
        int 0x80

.done:
        mov eax, SYS_EXIT
        int 0x80

section .bss
args_buf:       resb 256
