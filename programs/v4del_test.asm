; v4del_test.asm - delete an extent file and prove it is gone.
;
; frag.txt lives in three runs plus an extent block. Deleting it has to free
; every run and the extent block, restamp the bitmap and the directory, and
; leave the volume consistent - tests/test_v4_mount.py checks that with fsck.
;
; Prints V4DEL_OK or V4DEL_FAIL-<stage>.
%include "syscalls.inc"

O_RDONLY equ 0

start:
        mov eax, SYS_DELETE
        mov ebx, name
        int 0x80
        cmp eax, 0
        jne .fail_delete

        ; It must not open any more.
        mov eax, SYS_OPEN
        mov ebx, name
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        jne .fail_still

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor eax, eax
        int 0x80

.fail_still:
        mov ebx, eax
        mov eax, SYS_CLOSE
        int 0x80
        mov ebx, msg_still
        jmp .fail
.fail_delete:
        mov ebx, msg_delete
.fail:
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

name:       db "frag.txt", 0
msg_ok:     db 10, "V4DEL_OK", 10, 0
msg_delete: db 10, "V4DEL_FAIL-DELETE", 10, 0
msg_still:  db 10, "V4DEL_FAIL-STILL-THERE", 10, 0
