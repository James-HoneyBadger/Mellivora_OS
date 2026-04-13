; echo.asm - Print arguments to stdout (like Unix echo)
; Usage: echo [text...]

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        cmp eax, 0
        je .newline

        ; Print args
        mov eax, SYS_PRINT
        mov ebx, argbuf
        int 0x80

.newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

argbuf:         times 256 db 0
