; quota_limit_test.asm - a ceiling refuses a write, and only past the ceiling
;
; The quota ledger is charged to the writing task's effective uid, so proving a
; ceiling from the shell cannot work: the shell is the kernel context and root,
; and root is deliberately not held to a quota (every shipped program and
; document is uid 0, so the first write by the shell would otherwise be
; refused). The check therefore runs where it applies - in a task that has
; dropped to an ordinary uid.
;
; As root, from "/", it builds a writable directory owned by uid 2200, then
; drops to that uid and requires:
;
;   1. a three-block file is refused while the ceiling is one block;
;   2. a one-block file is accepted, so the refusal above is the ceiling and
;      not a broken directory or a task that cannot write at all;
;   3. the refusal left no file behind - a write refused before anything is
;      allocated creates nothing.
;
; The ceiling is set by the caller (`quota set 2200 1`), because the ledger is
; the machine's, not the program's. Prints QUOTA_LIMIT_OK or
; QUOTA_LIMIT_FAIL stage <letter>.
;
; The uid is one no other case uses. The ledger charges a file to its owner and
; a directory to whoever owns it, so a uid the rest of the matrix already writes
; as (2000, which owns the leftovers of the directory-permission fixtures) does
; not start at zero blocks and a one-block ceiling would refuse the control
; write in step 2 as well as the three-block one in step 1.

%include "syscalls.inc"

set_stage:
        mov [stage], al
        ret

fail:
        push eax
        mov eax, SYS_CHDIR
        mov ebx, root_path
        int 0x80
        pop eax
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, stage
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

start:
        ; --- fixtures, as root ---
        mov eax, SYS_DELETE
        mov ebx, name_dir
        int 0x80                        ; may not exist
        mov eax, SYS_MKDIR
        mov ebx, name_dir
        int 0x80
        mov eax, SYS_CHMOD
        mov ebx, name_dir
        mov ecx, 0777o
        int 0x80

        ; File names are relative to the current directory, so stand in the
        ; directory the ceiling applies to before dropping privileges.
        mov eax, SYS_CHDIR
        mov ebx, name_dir
        int 0x80
        cmp eax, 0
        jne fail

        mov al, '1'
        call set_stage
        mov eax, SYS_SETUID
        mov ebx, 2200
        int 0x80
        cmp eax, 0
        jne fail
        mov eax, SYS_GETUID
        int 0x80
        cmp eax, 2200
        jne fail

        ; --- 1. three blocks against a one-block ceiling ---
        mov al, '2'
        call set_stage
        mov eax, SYS_DELETE
        mov ebx, name_big
        int 0x80                        ; may not exist
        mov eax, SYS_FWRITE
        mov ebx, name_big
        mov ecx, payload
        mov edx, payload_len            ; three blocks of data
        xor esi, esi
        int 0x80
        cmp eax, 0
        je fail                        ; it should have been refused

        ; --- 3. and nothing was created by the refusal ---
        mov al, '3'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, name_big
        mov ecx, io_buf
        int 0x80
        ; SYS_FREAD answers 0 for a file it cannot read, so a zero is the
        ; "not there" this checks for; anything else means the refused write
        ; left a file behind.
        cmp eax, 0
        jne fail

        ; --- 2. one block is inside the ceiling ---
        mov al, '4'
        call set_stage
        mov eax, SYS_DELETE
        mov ebx, name_small
        int 0x80
        mov eax, SYS_FWRITE
        mov ebx, name_small
        mov ecx, payload
        mov edx, 1000                   ; one block
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne fail

        mov al, '5'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, name_small
        mov ecx, io_buf
        int 0x80
        cmp eax, 1000
        jne fail

        ; --- tidy up, as the user who made them ---
        mov al, '6'
        call set_stage
        mov eax, SYS_DELETE
        mov ebx, name_small
        int 0x80
        cmp eax, 0
        jne fail

        mov eax, SYS_SETUID
        xor ebx, ebx
        int 0x80
        mov eax, SYS_CHDIR
        mov ebx, root_path
        int 0x80
        mov eax, SYS_DELETE
        mov ebx, name_dir
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

name_dir:   db "qdir", 0
name_big:   db "qbig.txt", 0
name_small: db "qsmall.txt", 0
root_path:  db "/", 0
msg_ok:     db "QUOTA_LIMIT_OK", 10, 0
msg_fail:   db "QUOTA_LIMIT_FAIL stage ", 0
msg_newline: db 10, 0
stage:      db '0', 0
io_buf:     times 4096 db 0
payload:    times 3 * 4096 db 'Q'
payload_len equ 3 * 4096
