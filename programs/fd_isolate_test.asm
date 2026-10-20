; fd_isolate_test.asm - regression test for per-task file descriptor views
;
; The VFS virtual-fd table used to be global, so a forked child closing a
; descriptor also closed it for its parent: two processes shared one fd space.
; Each task now keeps its own copy in the private fd page, swapped on every
; context switch.
;
; Verifies:
;   1. the parent opens /proc/version and gets a readable fd
;   2. the child closes that same fd number and exits
;   3. the parent's fd is still open afterwards (read returns data)
;
; Prints FDS_ISOLATED on success, FDS_FAIL on any failure.

%include "syscalls.inc"

O_RDONLY        equ 0

start:
        ; --- 1. open a file in the parent ---
        mov eax, SYS_OPEN
        mov ebx, proc_path
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, 0
        jl .fail
        mov [file_fd], eax

        ; --- 2. fork; the child closes the parent's fd number ---
        mov eax, SYS_FORK
        int 0x80
        cmp eax, 0
        je .child
        cmp eax, -1
        je .fail
        mov [child_pid], eax

        ; Parent: wait for the child to close "its" copy of the fd
        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        cmp eax, -1
        je .fail

        ; --- 3. the parent's fd must still be open ---
        mov eax, SYS_READ
        mov ebx, [file_fd]
        mov ecx, buf
        mov edx, 32
        int 0x80
        cmp eax, 0
        jle .fail

        mov eax, SYS_CLOSE
        mov ebx, [file_fd]
        int 0x80
        cmp eax, 0
        jne .fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.child:
        mov eax, SYS_CLOSE
        mov ebx, [file_fd]
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

proc_path:  db "/proc/version", 0
msg_ok:     db "FDS_ISOLATED", 10, 0
msg_fail:   db "FDS_FAIL", 10, 0

file_fd:    dd 0
child_pid:  dd 0
buf:        times 64 db 0
