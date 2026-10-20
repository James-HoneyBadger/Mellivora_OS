; fs_mkdir_test.asm — metadata churn for the crash-consistency harness
;
; Creates, chmods and deletes a file in a loop, so a random SIGKILL from
; tools/crash_inject.py is very likely to land in the middle of a journaled
; metadata transaction (bitmap + superblock + directory sector).
;
; Prints CRASH_LOOP_OK when every iteration completed.
; The name is historical: it is the workload `make crash-test` drives.

%include "syscalls.inc"

ITERATIONS equ 40

start:
        mov dword [count], ITERATIONS
.loop:
        ; create
        mov eax, SYS_FWRITE
        mov ebx, name_file
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne fail

        ; metadata change (exercises the directory entry write path)
        mov eax, SYS_CHMOD
        mov ebx, name_file
        mov ecx, 0644o
        int 0x80
        cmp eax, 0
        jne fail

        ; remove (exercises the block-free path)
        mov eax, SYS_DELETE
        mov ebx, name_file
        int 0x80
        cmp eax, 0
        jne fail

        dec dword [count]
        mov eax, SYS_PRINT
        mov ebx, msg_dot
        int 0x80
        cmp dword [count], 0
        jnz .loop

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

name_file:   db "crashme.tmp", 0
payload:     db "crash-consistency-payload", 0
payload_len  equ 25

msg_ok:   db "CRASH_LOOP_OK", 10, 0
msg_fail: db "CRASH_LOOP_FAIL", 10, 0
msg_dot:  db ".", 0

count:    dd 0
