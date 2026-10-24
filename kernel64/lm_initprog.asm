;=======================================================================
; lm_initprog.asm - /init, the first program the 64-bit nucleus runs
;
; It prints one line and execs /hello: the smallest possible demonstration
; that a loaded program can replace itself with another file off the disk.
;=======================================================================

[BITS 64]
[ORG 0]

SYS_WRITE equ 6
SYS_EXIT  equ 3
SYS_EXEC  equ 12

start:
        mov eax, SYS_WRITE
        mov edi, 1
        lea rsi, [rel msg]
        mov edx, msg_end - msg
        syscall

        lea rdi, [rel path]
        lea rsi, [rel args]             ; the command line /hello will see
        mov eax, SYS_EXEC
        syscall

        ; exec only returns on failure.
        mov eax, SYS_EXIT
        mov edi, 0xBAD
        syscall
.hang:
        jmp .hang

msg:     db "init", 10
msg_end:
path:    db "/hello", 0
args:    db "alpha beta", 0
