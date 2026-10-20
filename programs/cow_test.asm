; cow_test.asm - copy-on-write fork regression test
;
; fork() shares the parent's user frames and marks them read-only in both
; address spaces; the first write in either process takes a private copy. This
; checks the three things that must hold:
;
;   1. the child starts out seeing the parent's values (shared, not copied)
;   2. the child's writes to data and to the stack stay private
;   3. the parent's values are untouched afterwards
;
; Prints COW_OK on success, COW_FAIL otherwise.

%include "syscalls.inc"

WAIT_SPINS  equ 4000

PARENT_DATA equ 0x1111
CHILD_DATA  equ 0x2222
PARENT_STK  equ 0x11111111
CHILD_STK   equ 0x22222222

start:
        ; Reserve a stack slot and park a sentinel in it, so the child's write
        ; to "the same" stack address must break the page copy.
        sub esp, 8
        mov dword [esp], PARENT_STK
        mov [stack_ptr], esp

        mov dword [shared_value], PARENT_DATA

        mov eax, SYS_FORK
        int 0x80
        cmp eax, 0
        je child_main
        cmp eax, -1
        je fail_main
        mov [child_pid], eax

        ; Wait for the child and check how it exited
        mov ecx, WAIT_SPINS
.wait_child:
        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        cmp eax, -1
        jne .child_done
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .wait_child
        jmp fail_main
.child_done:
        test eax, eax
        jnz fail_main               ; the child reported its own failure

        ; --- the child's writes must not be visible here ---
        mov eax, [stack_ptr]
        cmp dword [eax], PARENT_STK
        jne fail_main
        cmp dword [shared_value], PARENT_DATA
        jne fail_main

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; Child: verify inheritance, then write and verify isolation.
; Exits 0 on success, or a distinguishing code on failure.
;-----------------------------------------------------------------------
child_main:
        cmp dword [shared_value], PARENT_DATA
        jne .fail1
        mov eax, [stack_ptr]
        cmp dword [eax], PARENT_STK
        jne .fail2

        ; Write to both; each must take a private frame
        mov dword [shared_value], CHILD_DATA
        mov eax, [stack_ptr]
        mov dword [eax], CHILD_STK

        cmp dword [shared_value], CHILD_DATA
        jne .fail3
        mov eax, [stack_ptr]
        cmp dword [eax], CHILD_STK
        jne .fail4

        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.fail1:
        mov ebx, 1
        jmp .exit
.fail2:
        mov ebx, 2
        jmp .exit
.fail3:
        mov ebx, 3
        jmp .exit
.fail4:
        mov ebx, 4
.exit:
        mov eax, SYS_EXIT
        int 0x80

fail_main:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

msg_ok:   db "COW_OK", 10, 0
msg_fail: db "COW_FAIL", 10, 0

child_pid:    dd 0
stack_ptr:    dd 0
shared_value: dd 0
