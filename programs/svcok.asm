; svcok.asm - a service fixture that starts, says so and exits cleanly.
;
; The boot-service case needs a program that proves it ran and then leaves no
; recovery behind: exit zero is a clean stop, so the service reaches STOPPED
; and the SCM does not restart it.
%include "syscalls.inc"

start:
        mov eax, SYS_PRINT
        mov ebx, msg_done
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

msg_done: db "svcok: done", 0x0A, 0
