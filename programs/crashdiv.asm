; crashdiv.asm - a task that dies on purpose.
;
; SERVICES.md section 6 item 6: a logon screen that crashes must not hand the
; session back. Demonstrating that needs a task that really faults rather than
; one that returns, so this prints a line and then divides by zero. It is a
; fixture for the runtime gate, not a diagnostic a user runs.
%include "syscalls.inc"

        jmp start

msg_about:  db "CRASHDIV_ABOUT_TO_FAULT", 0x0A, 0

start:
        mov eax, SYS_PRINT
        mov ebx, msg_about
        int 0x80

        mov eax, 10
        xor ecx, ecx
        xor edx, edx
        div ecx                         ; divide error: the task is killed

        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
