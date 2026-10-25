; hostile_test.asm - the M1 conformance gate: a program that misbehaves and
; is refused
;
; Run as an unprivileged user, it tries everything it is not entitled to and
; requires every attempt to fail:
;
;   * read, write, truncate-by-rewrite and remove a file owned by someone else
;   * change that file's mode, or take ownership of it
;   * pass a kernel address where a user pointer belongs
;
; and it requires two things it *is* entitled to still work, so the gate cannot
; pass by denying everything. A final fault-containment case lives in
; tests/test_runtime.sh: a task that writes through a guard page must die while
; the machine keeps running.
;
; Prints HOSTILE_OK when every attempt was refused and both controls worked,
; HOSTILE_FAIL otherwise. A dot per check shows how far it got.

%include "syscalls.inc"

O_RDONLY equ 0
O_WRONLY equ 1
R_OK     equ 4
W_OK     equ 2

start:
        ; --- fixtures, as root ---
        ; victim: owned by somebody else, private
        call make_victim
        jc .fail
        ; public: owned by somebody else, world-readable
        mov eax, SYS_FWRITE
        mov ebx, public_file
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_CHOWN
        mov ebx, public_file
        mov ecx, 2000
        mov edx, 2000
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_CHMOD
        mov ebx, public_file
        mov ecx, 0o644
        int 0x80
        cmp eax, 0
        jne .fail

        ; --- become an ordinary user ---
        mov eax, SYS_SETGID
        mov ebx, 1000
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_SETUID
        mov ebx, 1000
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_GETUID
        int 0x80
        cmp eax, 1000
        jne .fail

        ; 1: a private file cannot be read
        mov eax, SYS_OPEN
        mov ebx, victim_file
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; 2: ...nor written
        mov eax, SYS_OPEN
        mov ebx, victim_file
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; 3: ...nor asked about
        mov eax, SYS_ACCESS
        mov ebx, victim_file
        mov ecx, R_OK | W_OK
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; 4: ...nor removed
        mov eax, SYS_DELETE
        mov ebx, victim_file
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; 5: ...nor replaced by writing over it
        mov eax, SYS_FWRITE
        mov ebx, victim_file
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; 6: its mode is not ours to change
        mov eax, SYS_CHMOD
        mov ebx, victim_file
        mov ecx, 0o777
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; 7: and ownership is not ours to take
        mov eax, SYS_CHOWN
        mov ebx, victim_file
        mov ecx, 1000
        mov edx, 1000
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; 8: a kernel address is not a path
        mov eax, SYS_OPEN
        mov ebx, 0x00100000
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; 9: a kernel buffer is not a message
        mov eax, SYS_WRITE
        mov ebx, 1
        mov ecx, 0x00100000
        mov edx, 16
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        ; --- controls: what it *is* entitled to still works ---
        ; 10: a world-readable file can be read
        mov eax, SYS_OPEN
        mov ebx, public_file
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je .fail
        mov ebx, eax
        mov eax, SYS_CLOSE
        int 0x80
        call dot

        ; 11: ...and cannot be written
        mov eax, SYS_OPEN
        mov ebx, public_file
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        mov eax, SYS_PRINT
        mov ebx, msg_ok
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

;-----------------------------------------------------------------------
; make_victim - create the private file: uid 2000, mode 0600
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
        mov ecx, 2000
        mov edx, 2000
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

dot:
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ret

victim_file: db "/hostile_victim.txt", 0
public_file: db "/hostile_public.txt", 0
payload:     db "hostile fixture", 10
payload_len  equ $ - payload
msg_ok:      db 10, "HOSTILE_OK", 10, 0
msg_fail:    db 10, "HOSTILE_FAIL", 10, 0
