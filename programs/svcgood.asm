; svcgood.asm - a service that stops when it is asked to.
;
; SERVICES.md section 1.2: Stop sends STOP and the service reports STOPPED
; within its timeout. The request and the answer are the `Control` value in the
; service's own record, so this program polls SYS_SVC_CONTROL and answers
; through it rather than exiting at a signal it cannot catch.
%include "syscalls.inc"

        jmp start

msg_up:      db "svcgood: up", 0x0A, 0
msg_down:    db "svcgood: stopped on request", 0x0A, 0

start:
        mov eax, SYS_PRINT
        mov ebx, msg_up
        int 0x80
.poll:
        mov eax, SYS_SVC_CONTROL
        xor ebx, ebx                    ; 0 = what was I asked?
        int 0x80
        cmp eax, 1                      ; stop requested
        je .leave
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        jmp .poll

.leave:
        mov eax, SYS_SVC_CONTROL
        mov ebx, 1                      ; 1 = I have stopped
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_down
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
