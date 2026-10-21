;=======================================================================
; lm_hello32.asm - a 32-bit program, /hello32 on the HBFS volume
;
; Runs in compatibility mode under the 64-bit nucleus and reaches the kernel
; the way the 32-bit tree does: INT 0x80, number in EAX, arguments in
; EBX/ECX/EDX, result in EAX. It is the proof that a legacy binary's entry
; path still works.
;=======================================================================

[BITS 32]
[ORG 0x40000000]        ; the address the nucleus maps the code page at

SYS_EXIT   equ 0
SYS_WRITE  equ 7
SYS_GETPID equ 54

start:
        mov eax, SYS_WRITE
        mov ebx, 1
        mov ecx, msg
        mov edx, msg_end - msg
        int 0x80
        cmp eax, msg_end - msg
        jne fail

        mov eax, SYS_GETPID
        int 0x80
        cmp eax, 1              ; task 1 runs this program
        jne fail

        mov eax, SYS_EXIT
        mov ebx, 0x32
        int 0x80

fail:
        mov eax, SYS_EXIT
        mov ebx, 0xBAD
        int 0x80
.hang:
        jmp .hang

msg:    db "hello from 32-bit mode", 10
msg_end:
