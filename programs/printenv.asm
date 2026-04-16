; printenv.asm - Print environment variables [HBU]
; Usage: printenv [NAME]
; With no args: prints all environment variables
; With NAME: prints the value of that variable
;
%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .show_all

        ; Single variable lookup
        mov eax, SYS_GETENV
        mov ebx, args_buf
        int 0x80
        or eax, eax
        jz .not_found
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.not_found:
        mov eax, SYS_PRINT
        mov ebx, msg_not_set
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, args_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.show_all:
        ; List all environment variables
        mov eax, SYS_LISTENV
        mov ebx, env_buf
        mov ecx, ENV_BUF_SIZE
        int 0x80
        cmp eax, 0
        jle .empty

        ; Null-terminate and print
        mov byte [env_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, env_buf
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.empty:
        mov eax, SYS_PRINT
        mov ebx, msg_empty
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

msg_not_set:    db "Variable not set: ", 0
msg_empty:      db "No environment variables set.", 0x0A, 0
newline:        db 0x0A, 0

args_buf:       times 256 db 0

ENV_BUF_SIZE    equ 4096
env_buf:        times ENV_BUF_SIZE db 0
