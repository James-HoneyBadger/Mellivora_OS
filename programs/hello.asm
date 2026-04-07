; hello.asm - Hello World program for Mellivora OS
; Uses INT 0x80 syscalls
; Loaded at 0x00200000
%include "syscalls.inc"

start:
        ; Set bright white color
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; Bright white on black
        int 0x80

        ; Print hello message
        mov eax, SYS_PRINT
        mov ebx, msg_hello
        int 0x80

        ; Set green color
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; Light green
        int 0x80

        ; Print second line
        mov eax, SYS_PRINT
        mov ebx, msg_running
        int 0x80

        ; Reset color
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Exit
        mov eax, SYS_EXIT
        int 0x80

msg_hello:      db "Hello, World! Welcome to Mellivora OS!", 0x0A, 0
msg_running:    db "This program is running in 32-bit protected mode.", 0x0A, 0
