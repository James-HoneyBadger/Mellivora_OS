; false.asm - Exit with failure code [HBU]
; Usage: false
%include "syscalls.inc"

start:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
