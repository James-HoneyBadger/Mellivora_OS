; fpu_test.asm - x87 state must survive a context switch
;
; The x87 register stack is per-CPU, not per-task. Unless the scheduler saves
; the outgoing task's state and restores the incoming one, two programs doing
; floating point corrupt each other's results with no visible cause.
;
; This program forks, so two tasks run the same code at the same time, each
; pushing its own value on the x87 stack and yielding in a loop. Every task
; converts its value back to an integer and requires the exact number it put
; there: a clobbered stack gives a different value, and an emptied one gives
; the indefinite integer.
;
; Prints FPU_OK when both tasks agree, FPU_FAIL otherwise.

%include "syscalls.inc"

YIELDS equ 64                  ; enough switches that a shared stack cannot survive

start:
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail
        test eax, eax
        jz .child

        ; parent
        mov [child_pid], eax
        call fpu_check
        jc .fail
        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        test eax, eax                  ; child exits 0 only when it agreed
        jnz .fail
        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        jmp .exit

.child:
        call fpu_check
        jc .child_fail
        xor ebx, ebx
        jmp .exit
.child_fail:
        mov ebx, 1
        jmp .exit

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
.exit:
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; fpu_check - put two exact values on the x87 stack, switch tasks, take them
;             back, and require the same two values.
;   Out: CF clear on agreement, CF set on corruption
;   Clobbers : nothing the caller needs; preserves EBP/EBX/ESI/EDI by using
;              only EAX/ECX/EDX and the x87 stack.
;-----------------------------------------------------------------------
fpu_check:
        ; The seed depends on the PID, so the two tasks hold different values
        ; and swapping one for the other cannot pass by accident.
        mov eax, SYS_GETPID
        int 0x80
        imul eax, 131
        add eax, 7
        mov [value_lo], eax
        add eax, 4242
        mov [value_hi], eax

        fild dword [value_lo]          ; st0 = value_lo
        fild dword [value_hi]          ; st0 = value_hi, st1 = value_lo

        mov ecx, YIELDS
.yield_loop:
        push ecx
        mov eax, SYS_YIELD
        int 0x80
        pop ecx
        dec ecx
        jnz .yield_loop

        fistp dword [got_hi]           ; pop value_hi first
        fistp dword [got_lo]

        mov eax, [got_hi]
        cmp eax, [value_hi]
        jne .bad
        mov eax, [got_lo]
        cmp eax, [value_lo]
        jne .bad
        clc
        ret
.bad:
        stc
        ret

msg_ok:   db "FPU_OK", 10, 0
msg_fail: db "FPU_FAIL", 10, 0

child_pid:  dd 0
value_lo:   dd 0
value_hi:   dd 0
got_lo:     dd 0
got_hi:     dd 0
