; sigret_test.asm - does a signal handler that RETURNS work?
;
; sigterm_test covers a handler that exits. Job control needs the other shape:
; a handler that runs and returns, leaving the interrupted code to continue
; (the kernel redirects `ret` to the read-only sigreturn trampoline).
;
; The task signals itself, then yields: pending signals are dispatched on the
; yield path. Prints SIGRET_OK when the handler ran exactly once and the code
; after the yield still executes.

%include "syscalls.inc"

WAIT_SPINS equ 2000

start:
        mov eax, SYS_SIGACTION
        mov ebx, SIGINT
        mov ecx, int_handler
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne fail

        ; Signal ourselves: a RUNNING target is queued, not torn down
        mov eax, SYS_GETPID
        int 0x80
        mov ebx, eax
        mov ecx, SIGINT
        mov eax, SYS_KILL
        int 0x80
        cmp eax, 0
        jne fail

        ; Give the delivery a chance (it happens on the yield path)
        mov ecx, WAIT_SPINS
.spin:
        cmp dword [hits], 1
        je .ran
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .spin
        jmp fail

.ran:
        ; We are executing again after the handler returned
        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

int_handler:
        inc dword [hits]
        ret                     ; returns through the sigreturn trampoline

fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

msg_ok:   db "SIGRET_OK", 10, 0
msg_fail: db "SIGRET_FAIL", 10, 0

hits:     dd 0
