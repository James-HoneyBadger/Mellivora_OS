; vmtest.asm - virtual memory conformance test (docs/spec/VMM.md)
;
; Covers:
;   1. mmap returns page-aligned demand-zone memory, zero-filled and writable
;   2. W^X: a writable+executable mapping is refused
;   3. mprotect(PROT_READ) makes a write fault in another task
;   4. munmap makes a later access fault
; Each faulting case runs in a forked child so the parent can assert the
; system contained the fault; prints VM_OK on success.

%include "syscalls.inc"

CRASH_STATUS equ 0x80000000

start:
        ; --- 1. Anonymous RW mapping ---
        mov eax, SYS_MMAP
        xor ebx, ebx
        mov ecx, 8192
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        cmp eax, -1
        je .fail
        test eax, 0xFFF                 ; page aligned?
        jnz .fail
        cmp eax, 0x08000000             ; inside the demand zone?
        jb .fail
        cmp eax, 0x20000000
        jae .fail
        mov [map1], eax
        cmp dword [eax], 0              ; fresh pages read as zero
        jne .fail
        mov dword [eax], 0x11111111
        cmp dword [eax], 0x11111111
        jne .fail
        mov dword [eax + 4096], 0x22222222
        cmp dword [eax + 4096], 0x22222222
        jne .fail

        ; --- 2. W^X: RWX must be refused ---
        mov eax, SYS_MMAP
        xor ebx, ebx
        mov ecx, 4096
        mov edx, PROT_READ | PROT_WRITE | PROT_EXEC
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- 3. mprotect to read-only, then a child write must fault ---
        mov eax, SYS_MMAP
        xor ebx, ebx
        mov ecx, 4096
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        cmp eax, -1
        je .fail
        mov [ro_page], eax
        mov dword [eax], 5

        mov eax, SYS_MPROTECT
        mov ebx, [ro_page]
        mov ecx, 4096
        mov edx, PROT_READ
        int 0x80
        test eax, eax
        jnz .fail

        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail
        test eax, eax
        jz .child_ro_write
        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        cmp eax, CRASH_STATUS
        jne .fail

        ; --- 4. munmap, then a child access must fault ---
        mov eax, SYS_MUNMAP
        mov ebx, [ro_page]
        mov ecx, 4096
        int 0x80
        test eax, eax
        jnz .fail

        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail
        test eax, eax
        jz .child_read_unmapped
        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        cmp eax, CRASH_STATUS
        jne .fail

        ; --- 5. A second mapping must not overlap the first ---
        mov eax, SYS_MMAP
        xor ebx, ebx
        mov ecx, 4096
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        cmp eax, -1
        je .fail
        mov ecx, [map1]
        cmp eax, ecx
        je .fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        jmp .exit

.child_ro_write:
        mov eax, [ro_page]
        mov dword [eax], 7              ; must fault: page is read-only
        mov eax, SYS_PRINT
        mov ebx, msg_fail_ro
        int 0x80
        jmp .exit

.child_read_unmapped:
        mov eax, [ro_page]
        mov ebx, [eax]                  ; must fault: page was unmapped
        mov eax, SYS_PRINT
        mov ebx, msg_fail_munmap
        int 0x80
        jmp .exit

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

section .data
msg_ok:            db "VM_OK", 10, 0
msg_fail:          db "VMTEST_FAIL", 10, 0
msg_fail_ro:       db "VMTEST_FAIL_NO_MPROTECT", 10, 0
msg_fail_munmap:   db "VMTEST_FAIL_NO_MUNMAP", 10, 0

section .bss
map1:    resd 1
ro_page: resd 1
