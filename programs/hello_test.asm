bits 32
org 0x00200000

SYS_EXIT    equ 0
SYS_PRINT   equ 3

section .text
global _start

_start:
    mov eax, SYS_PRINT
    mov ebx, msg_hello
    int 0x80

    mov eax, SYS_EXIT
    int 0x80

section .data
    msg_hello db 'Hello, world!', 0
