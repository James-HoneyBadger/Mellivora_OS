; svcstub.asm - a service that ignores STOP.
;
; SERVICES.md section 1.2 and section 6 item 3: a service that does not report
; STOPPED within its timeout is killed, and the kill is recorded. Proving that
; needs a service that never looks at its Control value - this one sleeps in a
; loop and answers nothing - so the SCM has to reach the end of its patience.
%include "syscalls.inc"

        jmp start

msg_up:      db "svcstub: up, and ignoring STOP", 0x0A, 0

start:
        mov eax, SYS_PRINT
        mov ebx, msg_up
        int 0x80
.loop:
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        jmp .loop
