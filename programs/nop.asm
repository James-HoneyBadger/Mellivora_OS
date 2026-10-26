; nop.asm - the smallest program that succeeds
;
; perf_test.asm times fork + exec + waitpid of a program whose only job is to
; exit, so the measurement is the kernel's work and not the program's. A
; program that printed would put its output in the middle of the perf numbers
; and make them unreadable to the gate that parses them.

%include "syscalls.inc"

start:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
