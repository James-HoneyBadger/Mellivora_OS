; true.asm - Exit with success code [HBU]
; Usage: true
%include "syscalls.inc"

start:
        mov eax, SYS_EXIT
        int 0x80
