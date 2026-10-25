; dirperm_test.asm - the directory is an object with rights of its own
;
; Adding a name to a directory is a write to the directory, so the directory's
; own write bit decides it - the file's mode does not exist yet. This program
; proves it from the one place the check cannot be confused with anything else:
; a user who owns the directory outright.
;
; As root, from "/", it builds two directories owned by uid 2000 - HBFS calls
; take names relative to the current directory, not paths:
;
;   dperm_ro   0555  - ours, but not writable
;   dperm_rw   0755  - ours, and writable
;
; then drops to uid 2000 and requires:
;
;   1. creating a file in the unwritable one is refused
;   2. creating in the writable one works (so the refusal is the mode, not a
;      dead directory)
;   3. the file it just made can be renamed ... and
;   4. a file belonging to somebody else cannot be
;   5. discarding a file's contents behind its owner's back is refused
;
; It prints DIRPERM_OK when every case holds, DIRPERM_FAIL otherwise, and
; leaves the shell in "/" so the next command is unaffected.

%include "syscalls.inc"

O_RDONLY equ 0

start:
        ; --- fixtures, as root ---
        mov eax, SYS_MKDIR
        mov ebx, dir_ro
        int 0x80
        mov eax, SYS_MKDIR
        mov ebx, dir_rw
        int 0x80

        ; Both belong to the test user ...
        mov eax, SYS_CHOWN
        mov ebx, dir_ro
        mov ecx, 2000
        mov edx, 2000
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_CHOWN
        mov ebx, dir_rw
        mov ecx, 2000
        mov edx, 2000
        int 0x80
        cmp eax, 0
        jne .fail

        ; ... and differ only in the write bit.
        mov eax, SYS_CHMOD
        mov ebx, dir_ro
        mov ecx, 0o555
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_CHMOD
        mov ebx, dir_rw
        mov ecx, 0o755
        int 0x80
        cmp eax, 0
        jne .fail

        ; A file belonging to somebody else, planted inside the directory we
        ; own and may write: then a refusal on it can only be the file's mode.
        mov eax, SYS_CHDIR
        mov ebx, dir_rw
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_DELETE
        mov ebx, renamed
        int 0x80
        mov eax, SYS_DELETE
        mov ebx, stolen_name
        int 0x80
        call make_victim
        jc .fail
        mov eax, SYS_CHDIR
        mov ebx, root_path
        int 0x80
        cmp eax, 0
        jne .fail

        ; --- become the owner of both directories ---
        mov eax, SYS_SETGID
        mov ebx, 2000
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_SETUID
        mov ebx, 2000
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_GETUID
        int 0x80
        cmp eax, 2000
        jne .fail

        ; --- 1: no name may be added to a directory we cannot write ---
        mov eax, SYS_CHDIR
        mov ebx, dir_ro
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_FWRITE
        mov ebx, new_name
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; --- 2: the writable one accepts the same call ---
        ; chdir takes a name relative to the current directory, so come back to
        ; the root before entering a sibling.
        mov eax, SYS_CHDIR
        mov ebx, root_path
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_CHDIR
        mov ebx, dir_rw
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_FWRITE
        mov ebx, new_name
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne .fail
        call dot

        ; --- 3: our own file in our own directory renames ---
        mov eax, SYS_RENAME
        mov ebx, new_name
        mov ecx, renamed
        int 0x80
        cmp eax, 0
        jne .fail
        call dot

        ; --- 4: somebody else's file in *our* directory does not rename ---
        mov eax, SYS_RENAME
        mov ebx, victim_file
        mov ecx, stolen_name
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; --- 5: nor may its contents be discarded ---
        mov eax, SYS_TRUNCATE
        mov ebx, victim_file
        xor ecx, ecx
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; --- leave the shell in "/" for the next command ---
        mov eax, SYS_CHDIR
        mov ebx, root_path
        int 0x80
        cmp eax, 0
        jne .fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.fail:
        ; Leave the shell somewhere sane before reporting.
        mov eax, SYS_CHDIR
        mov ebx, root_path
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

dot:
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ret

;-----------------------------------------------------------------------
; make_victim - a file owned by uid 2000, mode 0600
;   Out: CF clear on success
;-----------------------------------------------------------------------
make_victim:
        mov eax, SYS_FWRITE
        mov ebx, victim_file
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne .mv_bad
        mov eax, SYS_CHOWN
        mov ebx, victim_file
        mov ecx, 1000
        mov edx, 1000
        int 0x80
        cmp eax, 0
        jne .mv_bad
        mov eax, SYS_CHMOD
        mov ebx, victim_file
        mov ecx, 0o600
        int 0x80
        cmp eax, 0
        jne .mv_bad
        clc
        ret
.mv_bad:
        stc
        ret

root_path:   db "/", 0
dir_ro:      db "dperm_ro", 0
dir_rw:      db "dperm_rw", 0
victim_file: db "other.txt", 0
new_name:    db "fresh.txt", 0
renamed:     db "renamed.txt", 0
stolen_name: db "stolen.txt", 0
payload:     db "directory rights fixture", 10
payload_len  equ $ - payload
msg_ok:      db 10, "DIRPERM_OK", 10, 0
msg_fail:    db 10, "DIRPERM_FAIL", 10, 0
