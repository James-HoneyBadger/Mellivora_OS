; id.asm - Print user and group IDs [HBU]
; Usage: id
%include "syscalls.inc"

start:
        mov eax, SYS_PRINT
        mov ebx, msg
        int 0x80

        mov eax, SYS_EXIT
        int 0x80

msg:    db "uid=0(root) gid=0(root) groups=0(root)", 10, 0
