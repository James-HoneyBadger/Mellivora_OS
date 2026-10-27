; acl_test.asm - an ACL decides, and the mode word does not
;
; The fixture is a file owned by 1000 with mode 0600: as it stands, nobody but
; the owner and root may write it. The test then hangs an ACL on it and proves
; that the ACEs - not the mode word - answer every question.
;
; Each question is asked by a child that drops to the uid in question and exits
; 0 or 1: a process cannot setuid its way back up, so the parent stays root and
; the children are one-way trips.
;
;   1. a named user (2000) writes a file the mode word makes read-only for them;
;   2. a stranger (3000) is denied, because `acl:o` is 0;
;   3. the mask narrows 2000's grant to read-only without touching their ACE;
;   4. the owner is held to their own ACE, not to the owner triplet;
;   5. root passes what every ACE denies.
;
; Prints ACL_OK when every case behaves, ACL_FAIL otherwise, with a progress dot
; per stage so a failure says which case broke.

%include "syscalls.inc"

O_RDONLY     equ 0
O_WRONLY     equ 1
WAIT_SPINS   equ 20000

start:
        ; --- as root: a fixture nobody but 1000 may write ---
        mov eax, SYS_FWRITE
        mov ebx, fixture
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne test_fail

        mov eax, SYS_CHOWN
        mov ebx, fixture
        mov ecx, 1000
        mov edx, 1000
        int 0x80
        cmp eax, 0
        jne test_fail

        mov eax, SYS_CHMOD
        mov ebx, fixture
        mov ecx, 0o600
        int 0x80
        cmp eax, 0
        jne test_fail

        ; --- the ACL: owner 1000 rw, user 2000 rw, everyone else nothing ---
        mov ecx, key_u1000
        mov edx, val_7
        call set_ace
        cmp eax, 0
        jne test_fail
        mov ecx, key_u2000
        mov edx, val_7
        call set_ace
        cmp eax, 0
        jne test_fail
        mov ecx, key_other
        mov edx, val_0
        call set_ace
        cmp eax, 0
        jne test_fail
        mov ecx, key_mask
        mov edx, val_7
        call set_ace
        cmp eax, 0
        jne test_fail

        ; 1. the mode word denies 2000, the ACL grants
        mov eax, SYS_FORK
        int 0x80
        test eax, eax
        jz stage_user_granted
        cmp eax, -1
        je test_fail
        mov [child_pid], eax
        call wait_child
        call progress

        ; 2. a stranger falls to acl:o, which is 0
        mov eax, SYS_FORK
        int 0x80
        test eax, eax
        jz stage_stranger_denied
        cmp eax, -1
        je test_fail
        mov [child_pid], eax
        call wait_child
        call progress

        ; 3. narrow the mask to read-only: the ACE stays 7, the grant does not
        mov ecx, key_mask
        mov edx, val_4
        call set_ace
        cmp eax, 0
        jne test_fail
        mov eax, SYS_FORK
        int 0x80
        test eax, eax
        jz stage_mask_narrows
        cmp eax, -1
        je test_fail
        mov [child_pid], eax
        call wait_child
        call progress

        ; 4. name the owner: their access is the ACE's, not the owner triplet
        mov ecx, key_u1000
        mov edx, val_4
        call set_ace
        cmp eax, 0
        jne test_fail
        mov eax, SYS_FORK
        int 0x80
        test eax, eax
        jz stage_owner_held
        cmp eax, -1
        je test_fail
        mov [child_pid], eax
        call wait_child
        call progress

        ; 5. root passes everything, ACL or not
        mov eax, SYS_FORK
        int 0x80
        test eax, eax
        jz stage_root_passes
        cmp eax, -1
        je test_fail
        mov [child_pid], eax
        call wait_child

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        mov eax, SYS_DELETE
        mov ebx, fixture
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; Stages. Each exits 0 when its case behaves and 1 when it does not.
;-----------------------------------------------------------------------
stage_user_granted:
        mov ebx, 2000
        call become
        jc stage_fail
        mov eax, SYS_OPEN
        mov ebx, fixture
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        je stage_fail
        mov ebx, eax
        mov eax, SYS_CLOSE
        int 0x80
        call stage_ok

stage_stranger_denied:
        mov ebx, 3000
        call become
        jc stage_fail
        mov eax, SYS_ACCESS
        mov ebx, fixture
        mov ecx, ACC_R_OK
        int 0x80
        cmp eax, -1
        jne stage_fail
        call stage_ok

stage_mask_narrows:
        mov ebx, 2000
        call become
        jc stage_fail
        mov eax, SYS_OPEN
        mov ebx, fixture
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je stage_fail
        mov ebx, eax
        mov eax, SYS_CLOSE
        int 0x80
        mov eax, SYS_OPEN
        mov ebx, fixture
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        jne stage_fail
        call stage_ok

stage_owner_held:
        mov ebx, 1000
        call become
        jc stage_fail
        mov eax, SYS_OPEN
        mov ebx, fixture
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je stage_fail
        mov ebx, eax
        mov eax, SYS_CLOSE
        int 0x80
        mov eax, SYS_OPEN
        mov ebx, fixture
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        jne stage_fail
        call stage_ok

stage_root_passes:
        mov eax, SYS_OPEN
        mov ebx, fixture
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        je stage_fail
        mov ebx, eax
        mov eax, SYS_CLOSE
        int 0x80
        call stage_ok

stage_ok:
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

stage_fail:
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80
        jmp stage_fail

; --- helpers -----------------------------------------------------------

; EBX = uid to become; CF set when the kernel refused
become:
        mov eax, SYS_SETGID
        int 0x80
        cmp eax, 0
        jne .bec_bad
        mov eax, SYS_SETUID
        int 0x80
        cmp eax, 0
        jne .bec_bad
        clc
        ret
.bec_bad:
        stc
        ret

; ECX = key, EDX = value, fixture implied
set_ace:
        mov eax, SYS_SETXATTR
        mov ebx, fixture
        int 0x80
        ret

wait_child:
        mov ecx, WAIT_SPINS
.wait_loop:
        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        cmp eax, -1
        jne .wait_done
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .wait_loop
        jmp test_fail
.wait_done:
        test eax, eax
        jnz test_fail
        ret

progress:
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ret

test_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

child_pid:   dd 0
fixture:     db "acl_fix.txt", 0
key_u1000:   db "acl:u1000", 0
key_u2000:   db "acl:u2000", 0
key_mask:    db "acl:m", 0
key_other:   db "acl:o", 0
val_7:       db "7", 0
val_4:       db "4", 0
val_0:       db "0", 0
payload:     db "acl fixture", 10
payload_len  equ $ - payload
msg_ok:      db 10, "ACL_OK", 10, 0
msg_fail:    db 10, "ACL_FAIL", 10, 0
