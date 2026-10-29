; svcfail.asm - a service fixture that dies at once with a non-zero code.
;
; The service manager's recovery policy needs a failure it can count, and the
; shortest honest one is a program that exits 1. `make check-runtime` creates a
; service over this image and watches the restarts and the FAILED report.
%include "syscalls.inc"

start:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
