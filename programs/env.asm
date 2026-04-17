; env.asm - Print or modify environment [HBU]
; Usage: env                    (print all variables)
;        env NAME=VALUE... CMD  (set vars, then run CMD)
;        env -u NAME CMD       (unset NAME, then run CMD - not impl)
;
%include "syscalls.inc"

MAX_ENV_BUF     equ 4096

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .print_env

        ; Parse: look for NAME=VALUE pairs, then a command
        mov esi, args_buf
.parse:
        cmp byte [esi], 0
        je .print_env           ; no command, just print
        cmp byte [esi], ' '
        jne .check_arg
        inc esi
        jmp .parse
.check_arg:
        ; Is this a NAME=VALUE? (contains '=' before space/null)
        push rsi
        mov edi, esi
.scan_eq:
        cmp byte [edi], 0
        je .no_eq
        cmp byte [edi], ' '
        je .no_eq
        cmp byte [edi], '='
        je .has_eq
        inc edi
        jmp .scan_eq

.has_eq:
        pop rsi                 ; restore start of this arg
        ; Find end of this arg, null-terminate
        mov edi, esi
.find_end:
        cmp byte [edi], 0
        je .set_last
        cmp byte [edi], ' '
        je .set_term
        inc edi
        jmp .find_end
.set_term:
        mov byte [edi], 0
        push rdi
        ; Set the variable
        mov eax, SYS_SETENV
        mov ebx, esi
        int 0x80
        pop rdi
        lea esi, [edi + 1]
        jmp .parse

.set_last:
        ; Last arg is NAME=VALUE with no command after
        mov eax, SYS_SETENV
        mov ebx, esi
        int 0x80
        jmp .print_env

.no_eq:
        pop rsi                 ; this arg is the command
        ; Execute the command: copy remaining args as program name
        mov eax, SYS_EXEC
        mov ebx, esi
        int 0x80
        ; If exec returns, command not found
        mov eax, SYS_PRINT
        mov ebx, err_exec
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 127
        int 0x80

.print_env:
        ; List all environment variables
        mov eax, SYS_LISTENV
        mov ebx, env_buf
        mov ecx, MAX_ENV_BUF
        int 0x80
        cmp eax, 0
        je .done
        ; Null-terminate the buffer
        mov byte [env_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, env_buf
        int 0x80

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

section .data
err_exec:   db "env: cannot execute ", 0

section .bss
args_buf:   resb 512
env_buf:    resb MAX_ENV_BUF + 1
