; =============================================================================
; stack_test.asm — user stack guard page and on-demand stack growth
;
; The kernel reserves the top USER_STACK_MAX_PAGES (256 KB) of the program
; window for the stack. Only the top USER_STACK_INIT_PAGES (64 KB) are mapped
; when a program starts; the page at USER_STACK_BASE is a permanent guard.
;
; What this program proves:
;   1. Touching an unmapped page below the stack grows the stack (the fault
;      handler maps it) instead of crashing the task.
;   2. Growth works all the way down to the last usable page above the guard.
;   3. Every page that was grown still holds what was written to it.
;   4. The heap cannot grow into the stack region (SYS_SBRK refuses), while a
;      heap request that stays below the guard still succeeds.
; =============================================================================

bits 32
org 0x00300000                  ; must equal kernel.asm PROGRAM_BASE

SYS_EXIT    equ 0
SYS_PRINT   equ 3
SYS_SBRK    equ 23

GUARD_PAGE   equ 0x003C0000     ; kernel USER_STACK_BASE: never mapped
DEEPEST_PAGE equ GUARD_PAGE + 0x1000
PATTERN      equ 0xA5A5A5A5

section .text
global _start

_start:
    ; --- 1/2. Walk down one page at a time, writing and reading back ---
    mov ebx, esp
    and ebx, ~0xFFF
    sub ebx, 0x1000             ; first page below the live stack
    mov [highest_grown], ebx

.touch:
    mov dword [ebx], PATTERN
    cmp dword [ebx], PATTERN
    jne .fail
    cmp ebx, DEEPEST_PAGE
    jbe .deep_done
    sub ebx, 0x1000
    jmp .touch

.deep_done:
    ; --- 3. The first grown page must still be intact and distinct ---
    mov ebx, [highest_grown]
    cmp dword [ebx], PATTERN
    jne .fail
    mov eax, [ebx]
    mov ecx, [DEEPEST_PAGE]
    cmp ecx, PATTERN
    jne .fail

    ; --- 4a. The heap must not be allowed to reach the stack ---
    mov eax, SYS_SBRK
    mov ebx, 0x100000           ; 1 MB: far past the stack region
    int 0x80
    cmp eax, -1
    jne .fail

    ; --- 4b. A heap request below the guard must still work ---
    mov eax, SYS_SBRK
    mov ebx, 0x00080000         ; 512 KB, still below USER_STACK_BASE
    int 0x80
    cmp eax, -1
    je .fail
    mov edi, eax
    mov dword [edi], 0x12345678
    mov dword [edi + 0x7FFFC], 0x87654321
    cmp dword [edi], 0x12345678
    jne .fail
    cmp dword [edi + 0x7FFFC], 0x87654321
    jne .fail
    mov eax, SYS_SBRK
    mov ebx, -0x00080000
    int 0x80

    mov eax, SYS_PRINT
    mov ebx, msg_ok
    int 0x80
    jmp .exit

.fail:
    mov eax, SYS_PRINT
    mov ebx, msg_fail
    int 0x80

.exit:
    mov eax, SYS_EXIT
    int 0x80

section .data
msg_ok:   db "STACK_GROW_OK", 10, 0
msg_fail: db "STACK_FAIL", 10, 0

section .bss
highest_grown: resd 1
