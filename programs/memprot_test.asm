; memprot_test.asm - memory-protection conformance test
;
; Proves the isolation rules from docs/spec/VMM.md:
;   * ring 3 can read and write its own program window and heap
;   * ring 3 cannot write kernel memory (0x00100000): the offending task dies
;   * the parent survives its child's fault and sees the fault exit status
;
; Prints MEMPROT_OK or MEMPROT_FAIL.

%include "syscalls.inc"

CRASH_STATUS equ 0x80000000     ; exit status recorded by fault_kill_current_task

start:
        ; --- 1. Our own window is writable and readable ---
        mov edi, PROGRAM_WINDOW_PROBE
        mov dword [edi], 0xA5A5A5A5
        cmp dword [edi], 0xA5A5A5A5
        jne .fail

        ; --- 2. The heap is writable and zero-filled ---
        mov eax, SYS_SBRK
        mov ebx, 4096
        int 0x80
        cmp eax, -1
        je .fail
        cmp dword [eax], 0
        jne .fail
        mov dword [eax], 0x5A5A5A5A
        cmp dword [eax], 0x5A5A5A5A
        jne .fail

        ; --- 3. A write to kernel memory must fault in a child, not succeed ---
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail
        test eax, eax
        jz .child_escape

        ; Parent: wait for the child and inspect how it ended.
        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        cmp eax, CRASH_STATUS
        jne .fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        jmp .exit

.child_escape:
        ; If this write does not fault, isolation is broken. Print a marker and
        ; exit with 0 so the parent can tell the two cases apart.
        mov eax, KERNEL_RAM_PROBE
        mov dword [eax], 0xDEADBEEF
        mov eax, SYS_PRINT
        mov ebx, msg_escaped
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

KERNEL_RAM_PROBE   equ 0x00100000       ; inside the kernel image
PROGRAM_WINDOW_PROBE equ 0x00310000     ; scratch inside the program window

section .data
msg_ok:      db "MEMPROT_OK", 10, 0
msg_fail:    db "MEMPROT_FAIL", 10, 0
msg_escaped: db "MEMPROT_FAIL_ESCAPED", 10, 0
