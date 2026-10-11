; notify.asm - Desktop notification syscall example
; Build:
;   nasm -f bin -Iprograms/ -o notify.bin samples/notify.asm

%include "syscalls.inc"

section .data
message db "Sample notification: hello from ring 3!", 0

section .text
start:
        mov eax, SYS_NOTIFY
        mov ebx, message
        mov edx, 0x004A90E2
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, message
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
