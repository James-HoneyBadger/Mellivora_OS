;=======================================================================
; HBFS v5 - access control lists
;
; The v3 permission word answers one question per file: owner, group, world.
; That is not enough to hand a single file to one other user without handing it
; to their whole group, so a file may carry an ACL instead - and when it does,
; the ACL decides and the mode word no longer does.
;
; An ACL lives in the file's existing xattr block, one entry per access control
; entry (ACE):
;
;   key          value    meaning
;   acl:u<uid>   '0'-'7'  named user
;   acl:g<gid>   '0'-'7'  named group
;   acl:m        '0'-'7'  mask over named users and groups
;   acl:o        '0'-'7'  everyone else
;
; `acl:o` is what makes an ACL "present": a file without it has none and is
; read through the mode word exactly as before. One ACE is one xattr entry, so a
; file carries HBFS_XATTR_PER_BLOCK - 1 named ACEs at most, which is the same
; order of magnitude as the ACLs files here have.
;
; Evaluation order (POSIX's, without inheritance or group-class unions):
;   1. root is granted everything before any of this is consulted;
;   2. the owner, through `acl:u<owner>` when present, else the owner triplet;
;   3. a named user ACE for the caller, narrowed by `acl:m`;
;   4. the caller's group ACE, narrowed by `acl:m`;
;   5. `acl:o`.
;
; The value is a permission digit rather than rwx bits: it is what chmod speaks
; and what an operator reads back out of a hexdump.
;=======================================================================

;-----------------------------------------------------------------------
; acl_key - build the xattr key for one ACE
;   In : AL = 'u' or 'g' (ignored when AH is 1 or 2)
;        AH = 0 named user/group, 1 mask, 2 other
;        EBX = id, for a named ACE
;        EDI = destination, at least 16 bytes
;   Out: key written and null-terminated at EDI
;-----------------------------------------------------------------------
acl_key:
        pushad
        mov ebp, edi
        mov byte [ebp], 'a'
        mov byte [ebp + 1], 'c'
        mov byte [ebp + 2], 'l'
        mov byte [ebp + 3], ':'
        add ebp, 4

        cmp ah, 1
        jne .ak_not_mask
        mov byte [ebp], 'm'
        inc ebp
        jmp .ak_terminate
.ak_not_mask:
        cmp ah, 2
        jne .ak_named
        mov byte [ebp], 'o'
        inc ebp
        jmp .ak_terminate

.ak_named:
        mov [ebp], al
        inc ebp
        mov eax, ebx
        mov ecx, 10
        xor edi, edi                    ; digit count
.ak_divide:
        xor edx, edx
        div ecx
        push edx
        inc edi
        test eax, eax
        jnz .ak_divide
.ak_emit:
        pop eax
        add al, '0'
        mov [ebp], al
        inc ebp
        dec edi
        jnz .ak_emit

.ak_terminate:
        mov byte [ebp], 0
        popad
        ret

;-----------------------------------------------------------------------
; acl_get - read one ACE's permission digit
;   In : EDI = entry ptr, AL = 'u' | 'g', EBX = id, AH = 0/1/2
;   Out: EAX = 0..7, or -1 when the ACE is absent
;-----------------------------------------------------------------------
acl_get:
        pushad
        mov [.acg_entry], edi
        mov [.acg_prefix], al
        mov [.acg_kind], ah
        mov [.acg_num], ebx
        mov edi, acl_key_buf
        mov al, [.acg_prefix]
        mov ah, [.acg_kind]
        mov ebx, [.acg_num]
        call acl_key

        mov ebx, [.acg_entry]
        mov ecx, acl_key_buf
        mov edx, acl_val_buf
        mov esi, HBFS_XATTR_VAL_LEN - 1
        call hbfs_xattr_get             ; EAX = length, or -1
        test eax, eax
        js .acg_absent
        movzx eax, byte [acl_val_buf]
        sub eax, '0'
        cmp eax, 7
        jbe .acg_return
        mov eax, -1                     ; a corrupt digit denies, never grants
        jmp .acg_return
.acg_absent:
        mov eax, -1
.acg_return:
        mov [esp + 28], eax
        popad
        ret

.acg_entry:  dd 0
.acg_prefix: db 0
.acg_kind:   db 0
.acg_num:    dd 0

;-----------------------------------------------------------------------
; acl_perm - the permissions an ACL grants a caller, if the file has one
;   In : EDI = entry ptr, ECX = caller uid, EDX = caller gid
;   Out: EAX = rwx bits, or -1 when the file carries no ACL
;-----------------------------------------------------------------------
acl_perm:
        pushad
        mov [.acp_entry], edi
        mov [.acp_uid], ecx
        mov [.acp_gid], edx

        ; `acl:o` present is what "this file has an ACL" means
        xor al, al
        xor ebx, ebx
        mov ah, 2
        call acl_get
        cmp eax, -1
        je .acp_no_acl
        mov [.acp_other], eax

        ; 2. the owner
        mov edi, [.acp_entry]
        movzx ebx, word [edi + HBFS_DE_UID]
        cmp ebx, [.acp_uid]
        jne .acp_named_user
        mov al, 'u'
        xor ah, ah
        call acl_get
        cmp eax, -1
        jne .acp_grant
        movzx eax, word [edi + HBFS_DE_MODE]
        shr eax, 6
        and eax, 7
        jmp .acp_grant

.acp_named_user:
        ; 3. a named user ACE, narrowed by the mask
        mov ebx, [.acp_uid]
        mov edi, [.acp_entry]
        mov al, 'u'
        xor ah, ah
        call acl_get
        cmp eax, -1
        je .acp_group
        mov ebx, eax
        call .acp_mask
        mov eax, ebx
        jmp .acp_grant

.acp_group:
        ; 4. the caller's group
        mov ebx, [.acp_gid]
        mov edi, [.acp_entry]
        mov al, 'g'
        xor ah, ah
        call acl_get
        cmp eax, -1
        je .acp_other_ace
        mov ebx, eax
        call .acp_mask
        mov eax, ebx
        jmp .acp_grant

.acp_other_ace:
        ; 5. everyone else
        mov eax, [.acp_other]
        jmp .acp_grant

.acp_no_acl:
        mov dword [esp + 28], -1
        popad
        ret
.acp_grant:
        and eax, 7
        mov [esp + 28], eax
        popad
        ret

; EBX = perms in, EBX = perms narrowed by `acl:m` out
.acp_mask:
        push eax
        mov [.acp_perms], ebx
        xor al, al
        mov ah, 1
        xor ebx, ebx
        call acl_get
        cmp eax, -1
        je .acp_mask_absent
        and [.acp_perms], eax
.acp_mask_absent:
        mov ebx, [.acp_perms]
        pop eax
        ret

.acp_entry: dd 0
.acp_uid:   dd 0
.acp_gid:   dd 0
.acp_other: dd 0
.acp_perms: dd 0

;-----------------------------------------------------------------------
; acl_show_entry - print the ACEs carried by one entry
;   In : EDI = entry ptr
;   Out: EAX = number of ACEs printed
;
; The xattr block is read through hbfs_block_buf: like hbfs_xattr_get, this is
; only called from a shell command, where nothing else holds that buffer.
;-----------------------------------------------------------------------
acl_show_entry:
        pushad
        mov [.ase_entry], edi
        xor ebp, ebp                    ; EBP = printed count

        movzx eax, word [edi + HBFS_DE_XATTR_COUNT]
        test eax, eax
        jz .ase_done
        mov [.ase_count], eax
        mov eax, [edi + HBFS_DE_XATTR_LBA]
        test eax, eax
        jz .ase_done
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        jc .ase_done

        mov edi, hbfs_block_buf
        xor ebx, ebx                    ; EBX = slot index
.ase_loop:
        cmp ebx, [.ase_count]
        jge .ase_done
        cmp byte [edi], 0
        je .ase_next                    ; free slot: no key
        cmp dword [edi], 'acl:'         ; "acl:" little-endian
        jne .ase_next
        ; key, then " = ", then the value
        push edi
        mov esi, edi
        call vga_print
        mov esi, msg_acl_equals
        call vga_print
        pop edi
        lea esi, [edi + HBFS_XATTR_KEY_LEN]
        call vga_print
        mov al, 0x0A
        call vga_putchar
        inc ebp
.ase_next:
        add edi, HBFS_XATTR_ENTRY_SIZE
        inc ebx
        jmp .ase_loop

.ase_done:
        mov [esp + 28], ebp
        popad
        ret

.ase_entry: dd 0
.ase_count: dd 0

;-----------------------------------------------------------------------
; acl_command - the `acl` shell command
;   acl show <name>              list a file's ACEs
;   acl set <name> u<id> <perm>  named user
;   acl set <name> g<id> <perm>  named group
;   acl set <name> mask <perm>   mask
;   acl set <name> other <perm>  everyone else
;
; The mode word is left alone: an ACL'd file is decided by its ACEs, and
; `chmod` on one changes what tools that only read the mode word see.
;-----------------------------------------------------------------------
acl_command:
        pushad
        call skip_spaces
        cmp byte [esi], 0
        je .ac_usage

        mov edi, acl_verb_show
        call str_starts_with            ; CF set = match, ESI past the verb
        jc .ac_show
        mov edi, acl_verb_set
        call str_starts_with
        jc .ac_set
        jmp .ac_usage

.ac_show:
        cmp byte [esi], 0
        je .ac_usage
        mov edi, path_base_buf
        mov ecx, 255
        call copy_word_n
        mov edi, path_base_buf
        call acl_load_entry             ; EDI = entry ptr
        jc .ac_missing
        call acl_show_entry
        test eax, eax
        jnz .ac_done
        mov esi, msg_acl_none
        call vga_print
        jmp .ac_done

.ac_set:
        cmp byte [esi], 0
        je .ac_usage
        mov edi, path_base_buf
        mov ecx, 255
        call copy_word_n                ; the file
        cmp byte [path_base_buf], 0
        je .ac_usage
        call skip_spaces
        mov edi, acl_spec_buf
        mov ecx, 15
        call copy_word_n                ; the ACE spec
        cmp byte [acl_spec_buf], 0
        je .ac_usage
        call skip_spaces
        mov edi, acl_val_buf
        mov ecx, 3
        call copy_word_n                ; the permission digit
        cmp byte [acl_val_buf], 0
        je .ac_usage

        movzx eax, byte [acl_val_buf]
        sub eax, '0'
        cmp eax, 7
        ja .ac_bad_perm

        ; classify the spec: u<id>, g<id>, mask, other
        mov esi, acl_spec_buf
        movzx eax, byte [esi]
        xor ebx, ebx                    ; kind: 0 named, 1 mask, 2 other
        xor ecx, ecx                    ; prefix char for a named ACE
        cmp al, 'm'
        jne .ac_not_mask
        mov ebx, 1
        jmp .ac_have_kind
.ac_not_mask:
        cmp al, 'o'
        jne .ac_named_spec
        mov ebx, 2
        jmp .ac_have_kind
.ac_named_spec:
        cmp al, 'u'
        je .ac_prefix_ok
        cmp al, 'g'
        jne .ac_bad_spec
.ac_prefix_ok:
        movzx ecx, al
        inc esi
        call parse_decimal              ; EAX = id
        mov [acl_key_id], eax
.ac_have_kind:
        mov [acl_key_kind], ebx
        mov [acl_key_prefix], ecx

        mov edi, path_base_buf
        call acl_load_entry
        jc .ac_missing
        mov [acl_entry_ptr], edi
        mov edi, acl_key_buf
        mov al, [acl_key_prefix]
        mov ah, [acl_key_kind]
        mov ebx, [acl_key_id]
        call acl_key
        mov ebx, [acl_entry_ptr]
        mov ecx, acl_key_buf
        mov edx, acl_val_buf
        call hbfs_xattr_set
        test eax, eax
        js .ac_write_fail
        call hbfs_save_root_dir
        mov esi, msg_acl_set
        call vga_print
        jmp .ac_done

.ac_missing:
        call vga_set_error
        mov esi, msg_not_found
        call vga_print
        call vga_reset_color
        jmp .ac_done
.ac_write_fail:
        call vga_set_error
        mov esi, msg_write_err
        call vga_print
        call vga_reset_color
        jmp .ac_done
.ac_bad_perm:
        call vga_set_error
        mov esi, msg_acl_bad_perm
        call vga_print
        call vga_reset_color
        jmp .ac_done
.ac_bad_spec:
        call vga_set_error
        mov esi, msg_acl_bad_spec
        call vga_print
        call vga_reset_color
        jmp .ac_done
.ac_usage:
        mov esi, msg_acl_usage
        call vga_print
.ac_done:
        popad
        ret

acl_key_id:     dd 0
acl_key_kind:   dd 0
acl_key_prefix: dd 0
acl_entry_ptr:  dd 0

;-----------------------------------------------------------------------
; acl_load_entry - find an entry by name in the current directory
;   In : EDI = name
;   Out: EDI = entry ptr, CF set when there is no such entry
;-----------------------------------------------------------------------
acl_load_entry:
        push ebx
        mov ebx, edi
        call hbfs_load_root_dir
        call hbfs_find_entry            ; EBX = name -> EAX = index, EDI = entry
        test eax, eax
        js .ale_missing
        pop ebx
        clc
        ret
.ale_missing:
        pop ebx
        stc
        ret
