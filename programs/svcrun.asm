; svcrun.asm - a service fixture that keeps running.
;
; It announces itself on the console once, then sleeps in a loop. Sleeping is
; what lets the shell have the CPU back: a service that merely spun would be
; the only task the cooperative scheduler ever picked.
%include "syscalls.inc"

start:
        mov eax, SYS_PRINT
        mov ebx, msg_up
        int 0x80
.loop:
        mov eax, SYS_SLEEP
        mov ebx, 50
        int 0x80
        jmp .loop

msg_up: db "svcrun: running", 0x0A, 0
