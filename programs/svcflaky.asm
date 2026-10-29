; svcflaky.asm - a service fixture that fails twice and then keeps running.
;
; The reset period needs a service that fails and then succeeds, because a
; streak that never stops failing is the crash loop the attempt count already
; covers. The count of runs lives in a file, so it survives the restarts the
; SCM makes: the third run finds a count of three and settles into the sleep
; loop svcrun uses, which is what lets the shell have the CPU back.
;
; Prints nothing on a failing run and "svcflaky: up" once it stays up.
%include "syscalls.inc"

O_RDONLY equ 0

start:
        ; Read the run counter, or start one at '0'.
        mov byte [cnt], '0'
        mov eax, SYS_OPEN
        mov ebx, path
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je .have
        mov [fd], eax
        mov ebx, [fd]
        mov eax, SYS_READ
        mov ecx, cnt
        mov edx, 1
        int 0x80
        mov ebx, [fd]
        mov eax, SYS_CLOSE
        int 0x80

.have:
        inc byte [cnt]
        mov eax, SYS_FWRITE
        mov ebx, path
        mov ecx, cnt
        mov edx, 1
        xor esi, esi
        int 0x80

        cmp byte [cnt], '3'
        jb .fail

        mov eax, SYS_PRINT
        mov ebx, msg_up
        int 0x80
.loop:
        mov eax, SYS_SLEEP
        mov ebx, 50
        int 0x80
        jmp .loop

.fail:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

path:   db "flaky.cnt", 0
msg_up: db "svcflaky: up", 0x0A, 0
fd:     dd 0
cnt:    db '0'
