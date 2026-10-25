; perm_test.asm - the permission bits have to deny, not decorate
;
; A 2000-class system refuses an access the mode word forbids. This checks the
; three cases that matter:
;   * root passes a mode it does not own a bit for;
;   * the owner is held to the owner bits (read allowed, write refused);
;   * a stranger is held to the world bits.
;
; Prints PERM_OK when every case behaves and PERM_FAIL otherwise. Each stage
; prints a progress dot so a failure says which case broke.

%include "syscalls.inc"

O_RDONLY equ 0
O_WRONLY equ 1
O_RDWR   equ 2

start:
        ; --- as root: two fixtures, both created through the file writer ---
        mov eax, SYS_FWRITE
        mov ebx, own_file
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne .fail

        mov eax, SYS_FWRITE
        mov ebx, other_file
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne .fail

        ; One belongs to the caller-to-be and is read-only for everyone; the
        ; other belongs to a stranger and is private.
        mov eax, SYS_CHOWN
        mov ebx, own_file
        mov ecx, 1000
        mov edx, 1000
        int 0x80
        cmp eax, 0
        jne .fail
        call progress

        mov eax, SYS_CHMOD
        mov ebx, own_file
        mov ecx, 0o444
        int 0x80
        cmp eax, 0
        jne .fail

        mov eax, SYS_CHOWN
        mov ebx, other_file
        mov ecx, 2000
        mov edx, 2000
        int 0x80
        cmp eax, 0
        jne .fail

        mov eax, SYS_CHMOD
        mov ebx, other_file
        mov ecx, 0o600
        int 0x80
        cmp eax, 0
        jne .fail
        call progress

        ; --- root is not held to the mode: 0444 may still be written ---
        mov eax, SYS_OPEN
        mov ebx, own_file
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        je .fail
        mov ebx, eax
        mov eax, SYS_CLOSE
        int 0x80
        call progress

        ; --- become the owner ---
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
        ; The effective ID is the one access checks read.
        mov eax, SYS_GETEUID
        int 0x80
        cmp eax, 1000
        jne .fail
        call progress

        ; access(W_OK) asks the same question through a different path; using
        ; both here separates "the mode is wrong" from "open ignored it".
        mov eax, SYS_ACCESS
        mov ebx, own_file
        mov ecx, 2                      ; W_OK
        int 0x80
        cmp eax, -1
        jne .fail

        ; Owner may read a 0444 file...
        mov eax, SYS_OPEN
        mov ebx, own_file
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je .fail
        mov ebx, eax
        mov eax, SYS_CLOSE
        int 0x80
        call progress

        ; ...and may not write it: the owner triplet has no write bit.
        mov eax, SYS_OPEN
        mov ebx, own_file
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        jne .fail
        call progress

        ; A private file owned by somebody else is refused even for reading.
        mov eax, SYS_OPEN
        mov ebx, other_file
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        jne .fail
        call progress

        ; The owner may widen its own mode, and the wider mode takes effect.
        mov eax, SYS_CHMOD
        mov ebx, own_file
        mov ecx, 0o644
        int 0x80
        cmp eax, 0
        jne .fail
        call progress

        mov eax, SYS_OPEN
        mov ebx, own_file
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        je .fail
        mov ebx, eax
        mov eax, SYS_CLOSE
        int 0x80

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

progress:
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ret

own_file:    db "perm_own.txt", 0
other_file:  db "perm_other.txt", 0
payload:     db "permission fixture", 10
payload_len  equ $ - payload
msg_ok:      db 10, "PERM_OK", 10, 0
msg_fail:    db 10, "PERM_FAIL", 10, 0
