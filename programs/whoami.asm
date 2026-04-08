; whoami.asm - Print current user (always "root" in Mellivora) [HBU]
; Usage: whoami
%include "syscalls.inc"

start:
        mov eax, SYS_PRINT
        mov ebx, msg
        int 0x80

        mov eax, SYS_EXIT
        int 0x80

msg:    db "root", 10, 0
