; badptr_test.asm - every syscall that takes a caller pointer must say no
;
; Each call below hands the kernel an address no task maps (0xD0000000) and
; requires a clean error return. Before the pointer checks existed, the kernel
; dereferenced the argument itself: the fault happened in ring 0, so the shell
; restarted and every task in the system died with it. That is a denial of
; service any ring-3 program could trigger with one instruction.
;
; Prints BADPTR_OK when every call returned its documented failure value.
%include "syscalls.inc"

BAD equ 0xD0000000

%macro FAILS 0
        int 0x80
        cmp eax, -1
        jne .fail
%endmacro

%macro RETURNS_ZERO 0
        int 0x80
        test eax, eax
        jnz .fail
%endmacro

start:
        mov eax, SYS_PRINT
        mov ebx, msg_start
        int 0x80

        mov eax, SYS_RENAME           ; old name unmapped
        mov ebx, BAD
        mov ecx, good
        FAILS

        mov eax, SYS_RENAME           ; new name unmapped
        mov ebx, good
        mov ecx, BAD
        FAILS

        mov eax, SYS_RMDIR
        mov ebx, BAD
        FAILS

        mov eax, SYS_TRUNCATE
        mov ebx, BAD
        xor ecx, ecx
        FAILS

        mov eax, SYS_SYMLINK          ; link name unmapped
        mov ebx, BAD
        mov ecx, good
        FAILS

        mov eax, SYS_SYMLINK          ; target unmapped
        mov ebx, good
        mov ecx, BAD
        FAILS

        mov eax, SYS_READLINK         ; link name unmapped
        mov ebx, BAD
        mov ecx, buf
        FAILS

        mov eax, SYS_READLINK         ; destination unmapped
        mov ebx, good
        mov ecx, BAD
        FAILS

        mov eax, SYS_GETMTIME         ; "not found" is 0
        mov ebx, BAD
        RETURNS_ZERO

        mov eax, SYS_SETMTIME
        mov ebx, BAD
        xor ecx, ecx
        FAILS

        mov eax, SYS_MKDIR
        mov ebx, BAD
        FAILS

        mov eax, SYS_FREAD            ; path unmapped
        mov ebx, BAD
        mov ecx, buf
        RETURNS_ZERO

        mov eax, SYS_FREAD            ; destination unmapped
        mov ebx, good
        mov ecx, BAD
        RETURNS_ZERO

        mov eax, SYS_TASKNAME
        mov ebx, BAD
        RETURNS_ZERO

        mov eax, SYS_SETDATE
        mov ebx, BAD
        FAILS

        mov eax, SYS_OPEN             ; path unmapped
        mov ebx, BAD
        mov ecx, 1
        FAILS

        mov eax, SYS_DELETE
        mov ebx, BAD
        FAILS

        mov eax, SYS_STAT
        mov ebx, BAD
        xor ecx, ecx
        FAILS

        mov eax, SYS_CHDIR
        mov ebx, BAD
        FAILS

        mov eax, SYS_SELECT           ; fd_set unmapped
        mov ebx, 8
        mov ecx, BAD
        xor edx, edx
        xor esi, esi
        xor edi, edi
        FAILS

        mov eax, SYS_CLIPBOARD_COPY
        mov ebx, BAD
        mov ecx, 8
        FAILS

        mov eax, SYS_CLIPBOARD_PASTE
        mov ebx, BAD
        mov ecx, 8
        RETURNS_ZERO

        mov eax, SYS_PIPE_WRITE
        xor ebx, ebx
        mov ecx, BAD
        mov edx, 8
        FAILS

        mov eax, SYS_PIPE_READ
        xor ebx, ebx
        mov ecx, BAD
        mov edx, 8
        FAILS

        ; A valid pointer must still be accepted: the checks are a guard, not
        ; a wall. Copy eight bytes out of this program and read them back.
        mov eax, SYS_CLIPBOARD_COPY
        mov ebx, payload
        mov ecx, 8
        int 0x80
        cmp eax, -1
        je .fail

        mov eax, SYS_CLIPBOARD_PASTE
        mov ebx, buf
        mov ecx, 8
        int 0x80
        cmp eax, 8
        jne .fail
        mov eax, [buf]
        cmp eax, [payload]
        jne .fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

good:    db "badptr_never_created", 0
msg_start: db "BADPTR_BEGIN", 10, 0
msg_ok:    db "BADPTR_OK", 10, 0
msg_fail:  db "BADPTR_FAIL", 10, 0
payload:   db "TSTDATA!", 0
buf:       times 16 db 0
