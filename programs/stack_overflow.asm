; =============================================================================
; stack_overflow.asm — the guard page must kill only this task
;
; The page at USER_STACK_BASE (0x3C0000) is the bottom of the reserved stack
; region and is never mapped. Writing to it must fault, and the kernel must
; contain that fault: this program dies with a one-line report and the shell
; keeps running. Reaching the failure message means the guard page was
; mapped, which would defeat the whole scheme.
; =============================================================================

bits 32
org 0x00300000                  ; must equal kernel.asm PROGRAM_BASE

SYS_EXIT    equ 0
SYS_PRINT   equ 3

GUARD_PAGE  equ 0x003C0000      ; kernel USER_STACK_BASE

section .text
global _start

_start:
    mov eax, SYS_PRINT
    mov ebx, msg_start
    int 0x80

    ; Below the guard: this must fault and terminate the task.
    mov dword [GUARD_PAGE], 0xDEADBEEF

    ; Reached only if the guard page was writable.
    mov eax, SYS_PRINT
    mov ebx, msg_fail
    int 0x80
    mov eax, SYS_EXIT
    int 0x80

section .data
msg_start: db "STACK_OVERFLOW_START", 10, 0
msg_fail:  db "STACK_FAIL: guard page is writable", 10, 0
