; crash_acl_test.asm - churn extended attributes, which is where an ACL lives
;
; The other power-cut workloads churn directory entries and data runs. This one
; churns the block that holds a file's extended attributes: it is allocated the
; first time an ACE is set on a file and freed when the file goes, and nothing in
; the directory entry's run describes it - only a field does. That is the shape a
; checker is most likely to enumerate wrongly.
;
; Each round creates a file, grows its xattr block one ACE at a time, rewrites an
; ACE that is already there, changes the mode, and deletes every third file so
; the block has to be freed as well as allocated. Killed at any point, the volume
; has to come back consistent - the journal decides whether the round happened,
; not how far into it the machine got.
;
; Prints CRASH_ACL_OK when it runs to the end.

%include "syscalls.inc"

ROUNDS      equ 9
FILE_SLOTS  equ 3

start:
        mov dword [round], 0
.round_loop:
        mov eax, [round]
        cmp eax, ROUNDS
        jae .done

        ; A name that rotates over three files, so later rounds rewrite blocks
        ; that earlier rounds allocated.
        mov eax, [round]
        xor edx, edx
        mov ecx, FILE_SLOTS
        div ecx                         ; EDX = slot
        call build_name                 ; fills name_buf

        ; 1. the file (creating or replacing it)
        mov eax, SYS_FWRITE
        mov ebx, name_buf
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne .fail
        call dot

        ; 2. the first ACE allocates the block
        mov eax, SYS_SETXATTR
        mov ebx, name_buf
        mov ecx, key_other
        mov edx, val_7
        int 0x80
        cmp eax, 0
        jne .fail
        call dot

        ; 3. growth inside the block
        mov eax, SYS_SETXATTR
        mov ebx, name_buf
        mov ecx, key_user
        mov edx, val_6
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_SETXATTR
        mov ebx, name_buf
        mov ecx, key_group
        mov edx, val_4
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_SETXATTR
        mov ebx, name_buf
        mov ecx, key_mask
        mov edx, val_6
        int 0x80
        cmp eax, 0
        jne .fail
        call dot

        ; 4. rewriting an ACE that is already there
        mov eax, SYS_SETXATTR
        mov ebx, name_buf
        mov ecx, key_other
        mov edx, val_0
        int 0x80
        cmp eax, 0
        jne .fail

        mov eax, SYS_CHMOD
        mov ebx, name_buf
        mov ecx, 0o640
        int 0x80
        cmp eax, 0
        jne .fail
        call dot

        ; 5. every third round frees it again
        mov eax, [round]
        xor edx, edx
        mov ecx, 3
        div ecx
        test edx, edx
        jnz .next
        mov eax, SYS_DELETE
        mov ebx, name_buf
        int 0x80
        cmp eax, 0
        jne .fail
        call dot

.next:
        inc dword [round]
        jmp .round_loop

.done:
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

; EDX = slot -> "xa.txt" .. "xc.txt"
build_name:
        pushad
        mov edi, name_buf
        mov byte [edi], 'x'
        mov al, 'a'
        add al, dl
        mov [edi + 1], al
        mov byte [edi + 2], '.'
        mov byte [edi + 3], 't'
        mov byte [edi + 4], 'x'
        mov byte [edi + 5], 't'
        mov byte [edi + 6], 0
        popad
        ret

dot:
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ret

name_buf:    times 16 db 0
round:       dd 0
payload:     db "xattr workload", 10
payload_len  equ $ - payload

key_other:   db "acl:o", 0
key_user:    db "acl:u2000", 0
key_group:   db "acl:g1000", 0
key_mask:    db "acl:m", 0
val_7:       db "7", 0
val_6:       db "6", 0
val_4:       db "4", 0
val_0:       db "0", 0
msg_ok:      db 10, "CRASH_ACL_OK", 10, 0
msg_fail:    db 10, "CRASH_ACL_FAIL", 10, 0
