;=======================================================================
; lm_spin.asm - /spin, a program that never finishes
;
; Used to prove two things at once: that a program can run in the background
; while the shell keeps reading commands, and that another task can stop it.
; It does nothing but pause, so the timer has to be doing the preempting.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT equ 3

start:
.forever:
        pause
        jmp .forever

        mov edi, 0
        mov eax, SYS_EXIT
        syscall
