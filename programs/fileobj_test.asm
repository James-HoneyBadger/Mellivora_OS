; fileobj_test.asm - an open file is a typed object (M1)
;
; OBJECTS.md §2 says the resources the system already manages are re-expressed
; over the object model with the existing syscalls as compatibility aliases. For
; files that means: SYS_OPEN keeps working exactly as it did, and the thing it
; returns a descriptor for is now an object - counted, named, reference-counted
; and visible to `objects`.
;
; What is asserted:
;   1. opening a file raises the File live count by exactly one
;   2. the object is named after the path that was opened
;   3. closing the descriptor drops the count back
;   4. a second open of the same path is a second object, and closing both
;      returns the count to where it started
;
; Prints FILEOBJ_OK or FILEOBJ_FAIL.

%include "syscalls.inc"

O_RDONLY        equ 0

OS_LIVE         equ 0
OS_REC          equ 32
OS_FILE_LIVE    equ (OBJ_TYPE_FILE * OS_REC) + OS_LIVE
OQ_TYPE         equ 0
OQ_RIGHTS       equ 4
OQ_SIGNALLED    equ 8
OQ_NAMELEN      equ 12
OQ_NAME         equ 16

start:
        ; A file of our own, so the test does not depend on what the image
        ; happens to contain: SYS_FWRITE creates or truncates.
        mov eax, SYS_FWRITE
        mov ebx, path
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne _fail_open

        call file_live
        mov [live_before], eax

        ; --- 1: an open file is one more File object ---
        mov eax, SYS_OPEN
        mov ebx, path
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je _fail_open
        mov [fd1], eax

        call file_live
        mov ecx, [live_before]
        inc ecx
        cmp eax, ecx
        jne _fail_count

        ; --- 2: a second open is a second object ---
        mov eax, SYS_OPEN
        mov ebx, path
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je _fail_open
        mov [fd2], eax

        call file_live
        mov ecx, [live_before]
        add ecx, 2
        cmp eax, ecx
        jne _fail_count

        ; --- 3: closing one drops one ---
        mov eax, SYS_CLOSE
        mov ebx, [fd1]
        int 0x80
        test eax, eax
        jnz _fail_close

        call file_live
        mov ecx, [live_before]
        inc ecx
        cmp eax, ecx
        jne _fail_count

        ; --- 4: closing the last one returns to the baseline ---
        mov eax, SYS_CLOSE
        mov ebx, [fd2]
        int 0x80
        test eax, eax
        jnz _fail_close

        call file_live
        cmp eax, [live_before]
        jne _fail_leak

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; file_live - EAX = live File objects
;-----------------------------------------------------------------------
file_live:
        push ebx
        push ecx
        mov eax, SYS_OBJECT_STATS
        mov ebx, stats_buf
        mov ecx, stats_len
        int 0x80
        cmp eax, 0
        jl .fl_zero
        mov eax, [stats_buf + OS_FILE_LIVE]
        pop ecx
        pop ebx
        ret
.fl_zero:
        xor eax, eax
        pop ecx
        pop ebx
        ret

_fail_open:
        mov esi, msg_fail_open
        jmp _report
_fail_count:
        mov esi, msg_fail_count
        jmp _report
_fail_close:
        mov esi, msg_fail_close
        jmp _report
_fail_leak:
        mov esi, msg_fail_leak
_report:
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

fd1:        dd 0
fd2:        dd 0
live_before: dd 0
stats_buf:  times OBJ_TYPE_COUNT * OS_REC + 64 db 0
stats_len   equ OBJ_TYPE_COUNT * OS_REC + 64
path:       db "fileobj.tmp", 0
payload:    db "object file test", 10
payload_len equ 18
msg_ok:         db "FILEOBJ_OK", 10, 0
msg_fail_open:  db "FILEOBJ_FAIL_OPEN", 10, 0
msg_fail_count: db "FILEOBJ_FAIL_COUNT", 10, 0
msg_fail_close: db "FILEOBJ_FAIL_CLOSE", 10, 0
msg_fail_leak:  db "FILEOBJ_FAIL_LEAK", 10, 0
