;=======================================================================
; sett.inc - the Sett: the hierarchical configuration store (M4, phase 1)
;
; docs/spec/CONFIG.md section 1: one tree of keys, each carrying named, typed
; values, with \Registry\Machine for the machine and \Registry\User\<sid> per
; account. Section 11 says none of it exists yet and that the Sett is the
; first thing M4 builds; this is the first slice of it.
;
; Built here:
;   * a key tree with U32, STRING and BINARY values, names compared without
;     regard to case and stored as typed (section 2);
;   * `sett set/get/del/delkey/tree` from the shell;
;   * `sett save` / `sett load`, persisting the tree as one file on HBFS;
;   * the environment compatibility view (conformance item 6): `sett env
;     import` seeds \Registry\Machine\Environment from the shell's variables
;     and `sett env apply` writes them back.
;
; Not here, and named so the slice is not mistaken for the whole: the
; journaled multi-value commit, per-user hives and logon, key ACLs, change
; notification, migration, rollback, text export and SECRET values with the
; credential store. Those are the rest of M4, and section 11 is the list.
;
; The file is replaced whole, so a crash between two saves leaves the older
; file rather than a torn one; that is weaker than section 5's atomic commit
; and is not claimed to be it.
;
; Strings are UTF-8 bytes: the kernel has no UTF-16 yet and M5 owns "Unicode
; end to end", so storing UTF-8 and saying so beats a half-UTF-16 no reader
; could handle.
;=======================================================================

SETT_MAGIC        equ 0x54544553        ; 'SETT'
SETT_VERSION      equ 2                 ; +generation and checksum over the image
SETT_VERSION_1    equ 1                 ; an image written before M4's commit work

SETT_MAX_KEYS     equ 64
SETT_KEY_PATH     equ 128               ; including the null
SETT_KEY_ENTRY    equ 136               ; +0 parent (dword), +4 path, +132 spare

SETT_MAX_VALUES   equ 64
SETT_VAL_ENTRY    equ 48                ; +0 key, +4 type, +8 offset, +12 len,
                                        ; +16 name (32 bytes)
SETT_VAL_NAME     equ 32
SETT_ARENA        equ 16384

SETT_HEADER       equ 28                ; magic, version, keys, values, arena,
                                        ; generation, checksum
SETT_HEADER_V1    equ 20                ; the same without the last two
SETT_HDR_KEYS     equ 8
SETT_HDR_VALS     equ 12
SETT_HDR_ARENA    equ 16
SETT_HDR_GEN      equ 20
SETT_HDR_SUM      equ 24
SETT_IMAGE_SIZE   equ (SETT_HEADER + SETT_MAX_KEYS * SETT_KEY_ENTRY \
                       + SETT_MAX_VALUES * SETT_VAL_ENTRY + SETT_ARENA)

SETT_T_U32        equ 1
SETT_T_STRING     equ 2
SETT_T_BINARY     equ 3
SETT_T_SECRET     equ 4                 ; held in the credential store, never read back

SETT_KEY_SPARE    equ 132               ; (mode << 24) | owner uid; 0 = default
SETT_MODE_DEFAULT equ 0x0F              ; owner read/write, world read/write
SETT_MODE_OWN_RD  equ 0x08
SETT_MODE_OWN_WR  equ 0x04
SETT_MODE_WLD_RD  equ 0x02
SETT_MODE_WLD_WR  equ 0x01

sett_file_name:   db "sett.bin", 0
sett_backup_name: db "sett.bak", 0
sett_file_path:   db "/sett.bin", 0
sett_bak_path:    db "/sett.bak", 0
sett_root_path:   db "/", 0
sett_env_prefix:  db "\Registry\Machine\Environment", 0

align 4
sett_keys:        times SETT_MAX_KEYS * SETT_KEY_ENTRY db 0
sett_vals:        times SETT_MAX_VALUES * SETT_VAL_ENTRY db 0
sett_arena:       times SETT_ARENA db 0
sett_image:       times SETT_IMAGE_SIZE db 0
sett_backup_buf:  times SETT_IMAGE_SIZE db 0
sett_pathbuf:     times SETT_KEY_PATH db 0
sett_key_count:   dd 0
sett_val_count:   dd 0
sett_arena_used:  dd 0
sett_keep:        times SETT_MAX_KEYS db 0
sett_remap:       times SETT_MAX_KEYS dd 0
sett_cur_key:     dd 0
sett_image_len:   dd 0
sett_load_err:    dd 0
sett_generation:  dd 0                  ; bumped by every committed save
sett_hdr_off:     dd SETT_HEADER        ; the header size the loaded image used
sett_load_name:   dd sett_file_name     ; which file sett_load reads

SETT_ERR_OPEN     equ 1                 ; no /sett.bin to read
SETT_ERR_IMAGE    equ 2                 ; there, but not a Sett image
sett_tmp_type:    dd 0
sett_tmp_len:     dd 0
sett_tmp_data:    dd 0

sett_tok:         times 24 db 0
sett_path_tok:    times SETT_KEY_PATH db 0
sett_name_tok:    times SETT_VAL_NAME db 0
sett_type_tok:    times 8 db 0
sett_val_tok:     times 200 db 0
sett_envbuf:      times 256 db 0
sett_secbytes:    times 32 db 0
sett_hexstart:    dd 0

;=======================================================================
; String and index helpers
;=======================================================================

; AL uppercased in place. Leaves the caller's flags alone.
sett_upper_al:
        cmp al, 'a'
        jb .sua_done
        cmp al, 'z'
        ja .sua_done
        sub al, 32
.sua_done:
        ret

; CF set when the null-terminated strings at ESI and EDI are equal, ASCII
; case-insensitively. ESI, EDI, EBX and ECX are preserved.
sett_ci_equal:
        push esi
        push edi
        push ebx
        push ecx
.sce_loop:
        mov bl, [esi]
        mov cl, [edi]
        mov al, bl
        call sett_upper_al
        mov bl, al
        mov al, cl
        call sett_upper_al
        mov cl, al
        cmp bl, cl
        jne .sce_no
        test bl, bl
        jz .sce_yes
        inc esi
        inc edi
        jmp .sce_loop
.sce_yes:
        pop ecx
        pop ebx
        pop edi
        pop esi
        stc
        ret
.sce_no:
        pop ecx
        pop ebx
        pop edi
        pop esi
        clc
        ret

; CF set when the string at ESI starts with the one at EDI (case-insensitive).
sett_prefix:
        push esi
        push edi
        push ebx
        push ecx
.spx_loop:
        mov bl, [edi]
        test bl, bl
        jz .spx_yes
        mov cl, [esi]
        test cl, cl
        jz .spx_no
        mov al, bl
        call sett_upper_al
        mov bl, al
        mov al, cl
        call sett_upper_al
        mov cl, al
        cmp bl, cl
        jne .spx_no
        inc esi
        inc edi
        jmp .spx_loop
.spx_yes:
        pop ecx
        pop ebx
        pop edi
        pop esi
        stc
        ret
.spx_no:
        pop ecx
        pop ebx
        pop edi
        pop esi
        clc
        ret

; EAX = index -> EBX = key entry
sett_key_base:
        imul eax, eax, SETT_KEY_ENTRY
        add eax, sett_keys
        mov ebx, eax
        ret

; EAX = index -> EBX = value entry
sett_val_base:
        imul eax, eax, SETT_VAL_ENTRY
        add eax, sett_vals
        mov ebx, eax
        ret

; ESI = path as typed -> sett_pathbuf, canonical "\A\B".
; Leading and trailing separators are dropped and runs collapsed, so "A\\B\"
; and "\A\B" are one key. The root is the single character "\".
; CF set when the path does not fit.
sett_path_norm:
        push esi
        push edi
        push ecx
        lea edi, [sett_pathbuf]
        mov byte [edi], '\'
        inc edi
        mov ecx, SETT_KEY_PATH - 2
.spn_lead:
        cmp byte [esi], '\'
        jne .spn_copy
        inc esi
        jmp .spn_lead
.spn_copy:
        mov al, [esi]
        test al, al
        jz .spn_end
        cmp al, '\'
        je .spn_sep
        mov [edi], al
        inc edi
        inc esi
        dec ecx
        jz .spn_fail
        jmp .spn_copy
.spn_sep:
        inc esi
        cmp byte [esi], '\'
        je .spn_sep
        cmp byte [esi], 0
        je .spn_end
        mov byte [edi], '\'
        inc edi
        dec ecx
        jz .spn_fail
        jmp .spn_copy
.spn_end:
        mov byte [edi], 0
        pop ecx
        pop edi
        pop esi
        clc
        ret
.spn_fail:
        mov byte [edi], 0
        pop ecx
        pop edi
        pop esi
        stc
        ret

; ESI = source, EDI = destination, EDX = room in characters.
; ESI is left past the word; CF set when there is no word.
sett_word:
        push eax
        push ecx
        push edi
.sw_skip:
        cmp byte [esi], ' '
        jne .sw_start
        inc esi
        jmp .sw_skip
.sw_start:
        cmp byte [esi], 0
        je .sw_none
        mov ecx, edx
        test ecx, ecx
        jz .sw_none
.sw_copy:
        mov al, [esi]
        test al, al
        jz .sw_end
        cmp al, ' '
        je .sw_end
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jnz .sw_copy
.sw_end:
        mov byte [edi], 0
        pop edi
        pop ecx
        pop eax
        clc
        ret
.sw_none:
        pop edi
        pop ecx
        pop eax
        stc
        ret

; One hex digit from ESI -> AL, ESI advanced. CF set when it is not a digit.
sett_nibble:
        mov al, [esi]
        inc esi
        cmp al, '0'
        jb .sn_bad
        cmp al, '9'
        jbe .sn_digit
        cmp al, 'A'
        jb .sn_lower
        cmp al, 'F'
        jbe .sn_upper
.sn_lower:
        cmp al, 'a'
        jb .sn_bad
        cmp al, 'f'
        ja .sn_bad
        sub al, 'a' - 10
        clc
        ret
.sn_digit:
        sub al, '0'
        clc
        ret
.sn_upper:
        sub al, 'A' - 10
        clc
        ret
.sn_bad:
        stc
        ret

; ESI = hex text, EDI = output, EDX = most bytes to write.
; ECX = bytes written; CF set on a bad digit, an odd count or an overflow.
sett_hex:
        push eax
        push esi
        push edi
        xor ecx, ecx
.sh_loop:
        cmp byte [esi], 0
        je .sh_done
        cmp ecx, edx
        jae .sh_bad
        call sett_nibble
        jc .sh_bad
        shl al, 4
        mov ah, al
        call sett_nibble
        jc .sh_bad
        or al, ah
        mov [edi], al
        inc edi
        inc ecx
        jmp .sh_loop
.sh_done:
        pop edi
        pop esi
        pop eax
        clc
        ret
.sh_bad:
        pop edi
        pop esi
        pop eax
        stc
        ret

;=======================================================================
; Keys
;=======================================================================

; ESI = path -> EAX = key index; CF set when there is no such key
sett_key_find:
        push esi
        push edi
        push ebx
        push ecx
        mov [.skf_path], esi
        xor ecx, ecx
.skf_loop:
        cmp ecx, [sett_key_count]
        jae .skf_none
        mov eax, ecx
        call sett_key_base
        mov esi, [.skf_path]
        lea edi, [ebx + 4]
        call sett_ci_equal
        jc .skf_found
        inc ecx
        jmp .skf_loop
.skf_found:
        mov [.skf_idx], ecx
        pop ecx
        pop ebx
        pop edi
        pop esi
        mov eax, [.skf_idx]
        clc
        ret
.skf_none:
        pop ecx
        pop ebx
        pop edi
        pop esi
        stc
        ret
.skf_path: dd 0
.skf_idx:  dd 0

; ESI = canonical path, EDX = parent index -> EAX = index; CF set when full
sett_key_create:
        push esi
        push edi
        push ecx
        push ebx
        mov [.skc_parent], edx
        mov [.skc_src], esi
        mov eax, [sett_key_count]
        cmp eax, SETT_MAX_KEYS
        jae .skc_full
        mov [.skc_idx], eax
        call sett_key_base
        mov edx, [.skc_parent]
        mov [ebx], edx
        mov dword [ebx + SETT_KEY_SPARE], SETT_MODE_DEFAULT << 24
        lea edi, [ebx + 4]
        mov esi, [.skc_src]
        mov ecx, SETT_KEY_PATH - 1
.skc_copy:
        lodsb
        stosb
        test al, al
        jz .skc_stored
        dec ecx
        jnz .skc_copy
        ; Ran out of room with characters left: refuse rather than truncate.
        mov byte [ebx + 4], 0
        jmp .skc_full
.skc_stored:
        inc dword [sett_key_count]
        mov eax, [.skc_idx]
        push eax
        mov al, SETT_OP_NEWKEY
        mov esi, [.skc_src]
        mov edi, sett_empty_name
        call sett_notify
        pop eax
        pop ebx
        pop ecx
        pop edi
        pop esi
        clc
        ret
.skc_full:
        pop ebx
        pop ecx
        pop edi
        pop esi
        stc
        ret
.skc_idx:    dd 0
.skc_parent: dd 0
.skc_src:    dd 0

; ESI = path -> EAX = key index, created if it did not exist.
; CF set when the tree is full or the path is too long.
sett_ensure_path:
        push esi
        push edi
        push ebx
        push edx
        call sett_path_norm
        jc .sep_fail
        lea esi, [sett_pathbuf]
        call sett_key_find
        jnc .sep_done
        lea esi, [sett_pathbuf]
        inc esi                         ; past the leading separator
        mov dword [.sep_parent], -1
.sep_next:
        mov ebx, esi
.sep_scan:
        mov al, [ebx]
        test al, al
        jz .sep_component
        cmp al, '\'
        je .sep_component
        inc ebx
        jmp .sep_scan
.sep_component:
        mov [.sep_term], al
        mov byte [ebx], 0
        lea esi, [sett_pathbuf]
        call sett_key_find
        jnc .sep_have
        lea esi, [sett_pathbuf]
        mov edx, [.sep_parent]
        call sett_key_create
        jc .sep_fail
.sep_have:
        mov [.sep_parent], eax
        mov al, [.sep_term]
        mov [ebx], al
        test al, al
        jz .sep_built
        lea esi, [ebx + 1]
        jmp .sep_next
.sep_built:
        mov eax, [.sep_parent]
.sep_done:
        pop edx
        pop ebx
        pop edi
        pop esi
        clc
        ret
.sep_fail:
        pop edx
        pop ebx
        pop edi
        pop esi
        stc
        ret
.sep_parent: dd 0
.sep_term:   db 0

; ESI = path -> CF set when there is no such key.
;
; The key, every key below it, and their values go. The tables are rebuilt in
; order with the parent indices remapped, so a compacted slot never leaves a
; child pointing at a stranger.
sett_key_delete:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        push esi
        call sett_path_norm
        jc .skd_fail
        lea esi, [sett_pathbuf]
        call sett_key_find
        jc .skd_fail

        xor ecx, ecx
.skd_mark:
        cmp ecx, [sett_key_count]
        jae .skd_rebuild
        mov eax, ecx
        call sett_key_base
        lea esi, [ebx + 4]
        lea edi, [sett_pathbuf]
        call sett_prefix
        jnc .skd_keep
        ; It starts with the target: the target itself, or a child of it.
        lea esi, [sett_pathbuf]
        xor edx, edx
.skd_tlen:
        cmp byte [esi + edx], 0
        je .skd_tlen_done
        inc edx
        jmp .skd_tlen
.skd_tlen_done:
        lea esi, [ebx + 4]
        cmp byte [esi + edx], 0
        je .skd_drop
        cmp byte [esi + edx], '\'
        je .skd_drop
        ; "\Registry\Machine2" is not below "\Registry\Machine".
        jmp .skd_keep
.skd_drop:
        mov byte [sett_keep + ecx], 0
        jmp .skd_next
.skd_keep:
        mov byte [sett_keep + ecx], 1
.skd_next:
        inc ecx
        jmp .skd_mark

.skd_rebuild:
        xor ecx, ecx
        xor edx, edx                    ; new index
.skd_compact:
        cmp ecx, [sett_key_count]
        jae .skd_parents
        cmp byte [sett_keep + ecx], 0
        je .skd_skip
        mov [sett_remap + ecx*4], edx
        cmp ecx, edx
        je .skd_same
        push ecx
        push edx
        mov eax, ecx
        call sett_key_base
        mov esi, ebx
        mov eax, edx
        call sett_key_base
        mov edi, ebx
        mov ecx, SETT_KEY_ENTRY
        cld
        rep movsb
        pop edx
        pop ecx
.skd_same:
        inc edx
.skd_skip:
        inc ecx
        jmp .skd_compact

.skd_parents:
        mov [sett_key_count], edx
        xor ecx, ecx
.skd_par_loop:
        cmp ecx, [sett_key_count]
        jae .skd_values
        mov eax, ecx
        call sett_key_base
        mov eax, [ebx]                  ; the old parent
        cmp eax, -1
        je .skd_par_next
        mov eax, [sett_remap + eax*4]
        mov [ebx], eax
.skd_par_next:
        inc ecx
        jmp .skd_par_loop

.skd_values:
        xor ecx, ecx                    ; read index
        xor edx, edx                    ; write index
.skd_val_loop:
        cmp ecx, [sett_val_count]
        jae .skd_val_done
        mov eax, ecx
        call sett_val_base
        mov eax, [ebx]                  ; key
        cmp byte [sett_keep + eax], 0
        je .skd_val_skip
        mov eax, [sett_remap + eax*4]
        mov [ebx], eax
        cmp ecx, edx
        je .skd_val_same
        push ecx
        push edx
        mov esi, ebx
        mov eax, edx
        call sett_val_base
        mov edi, ebx
        mov ecx, SETT_VAL_ENTRY
        cld
        rep movsb
        pop edx
        pop ecx
.skd_val_same:
        inc edx
.skd_val_skip:
        inc ecx
        jmp .skd_val_loop
.skd_val_done:
        mov [sett_val_count], edx
        push eax
        mov al, SETT_OP_DELKEY
        lea esi, [sett_pathbuf]
        mov edi, sett_empty_name
        call sett_notify
        pop eax

        pop esi
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        clc
        ret
.skd_fail:
        pop esi
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        stc
        ret

;=======================================================================
; Values
;=======================================================================

; EAX = key index, ESI = value name -> EAX = value index; CF when absent
sett_value_find:
        push esi
        push edi
        push ebx
        push ecx
        push edx
        mov [.svf_key], eax
        mov [.svf_name], esi
        xor ecx, ecx
.svf_loop:
        cmp ecx, [sett_val_count]
        jae .svf_none
        mov eax, ecx
        call sett_val_base
        mov edx, [.svf_key]
        cmp [ebx], edx
        jne .svf_next
        mov esi, [.svf_name]
        lea edi, [ebx + 16]
        call sett_ci_equal
        jc .svf_found
.svf_next:
        inc ecx
        jmp .svf_loop
.svf_found:
        mov [.svf_idx], ecx
        pop edx
        pop ecx
        pop ebx
        pop edi
        pop esi
        mov eax, [.svf_idx]
        clc
        ret
.svf_none:
        pop edx
        pop ecx
        pop ebx
        pop edi
        pop esi
        stc
        ret
.svf_key:  dd 0
.svf_name: dd 0
.svf_idx:  dd 0

; EAX = key index, ESI = name, EDX = type, ECX = byte count, EBX = data.
; CF set when the tree is full, the name is too long or the arena is full.
sett_value_set:
        push eax
        push esi
        push edi
        push ebx
        push ecx
        push edx
        mov [.svs_key], eax
        mov [.svs_name], esi
        mov [.svs_type], edx
        mov [.svs_len], ecx
        mov [.svs_data], ebx
        mov dword [.svs_alloc], 1
        mov dword [.svs_new], 0

        call sett_value_find
        jnc .svs_existing

        mov eax, [sett_val_count]
        cmp eax, SETT_MAX_VALUES
        jae .svs_fail
        mov [.svs_slot], eax
        mov dword [.svs_new], 1
        mov edx, [.svs_key]
        mov eax, [.svs_slot]
        call sett_val_base
        mov [ebx], edx
        mov esi, [.svs_name]
        lea edi, [ebx + 16]
        mov ecx, SETT_VAL_NAME - 1
.svs_name_copy:
        lodsb
        stosb
        test al, al
        jz .svs_place
        dec ecx
        jnz .svs_name_copy
        jmp .svs_fail                ; a truncated name would collide

.svs_existing:
        mov [.svs_slot], eax
        mov eax, [.svs_slot]
        call sett_val_base
        mov eax, [.svs_len]
        cmp eax, [ebx + 12]
        ja .svs_place                 ; bigger than the slot: move it
        mov eax, [ebx + 8]
        mov [.svs_off], eax
        mov dword [.svs_alloc], 0

.svs_place:
        cmp dword [.svs_alloc], 0
        je .svs_have_off
        mov eax, [sett_arena_used]
        mov edx, [.svs_len]
        add eax, edx
        jc .svs_fail
        cmp eax, SETT_ARENA
        ja .svs_fail
        mov eax, [sett_arena_used]
        mov [.svs_off], eax
        add [sett_arena_used], edx
.svs_have_off:
        mov esi, [.svs_data]
        mov edi, sett_arena
        add edi, [.svs_off]
        mov ecx, [.svs_len]
        cld
        rep movsb

        mov eax, [.svs_slot]
        call sett_val_base
        mov eax, [.svs_type]
        mov [ebx + 4], eax
        mov eax, [.svs_len]
        mov [ebx + 12], eax
        mov eax, [.svs_off]
        mov [ebx + 8], eax

        cmp dword [.svs_new], 0
        je .svs_ok
        inc dword [sett_val_count]
.svs_ok:
        ; Nothing changes a setting without a watcher being able to hear it.
        mov eax, [.svs_key]
        call sett_key_base
        lea esi, [ebx + 4]
        mov al, SETT_OP_SET
        cmp dword [.svs_new], 0
        je .svs_notify
        mov al, SETT_OP_NEWVAL
.svs_notify:
        mov edi, [.svs_name]
        call sett_notify
        pop edx
        pop ecx
        pop ebx
        pop edi
        pop esi
        pop eax
        clc
        ret
.svs_fail:
        pop edx
        pop ecx
        pop ebx
        pop edi
        pop esi
        pop eax
        stc
        ret
.svs_key:   dd 0
.svs_name:  dd 0
.svs_type:  dd 0
.svs_len:   dd 0
.svs_data:  dd 0
.svs_slot:  dd 0
.svs_off:   dd 0
.svs_alloc: dd 0
.svs_new:   dd 0

; EAX = key index, ESI = name -> CF set when there was no such value
sett_value_del:
        push eax
        push ebx
        push ecx
        push edi
        push esi
        mov [.svd_key], eax
        mov [.svd_name], esi
        call sett_value_find
        jc .svd_none
        mov [.svd_idx], eax
        mov ecx, [sett_val_count]
        dec ecx
        mov eax, ecx
        call sett_val_base
        mov edx, ebx                    ; the last entry, if it is another one
        mov eax, [.svd_idx]
        call sett_val_base
        cmp eax, edx
        je .svd_drop
        mov edi, ebx                    ; the slot being given up
        mov esi, edx                    ; the last entry moves into it
        mov ecx, SETT_VAL_ENTRY
        cld
        rep movsb
.svd_drop:
        dec dword [sett_val_count]
        ; The key is still there (only the value went), so the event names both.
        mov eax, [.svd_key]
        call sett_key_base
        lea esi, [ebx + 4]
        mov edi, [.svd_name]
        mov al, SETT_OP_DEL
        call sett_notify
        pop esi
        pop edi
        pop ecx
        pop ebx
        pop eax
        clc
        ret
.svd_none:
        pop esi
        pop edi
        pop ecx
        pop ebx
        pop eax
        stc
        ret
.svd_idx: dd 0
.svd_key: dd 0
.svd_name: dd 0

; Pack the arena in table order, so a value that was moved or deleted does not
; hold a hole for the life of the boot. Destinations are always at or below
; the sources, which rep movsb handles in place.
; Pack every value's bytes end to end in slot order.
;
; The pack is done through a scratch buffer rather than in place. A value that
; grows is moved to the end of the arena, so its bytes can sit *after* those of
; a value that comes later in slot order. Copying in slot order then reads a
; source region that an earlier copy has already overwritten: the neighbour
; came back holding the tail of the grown value. Two passes make the order
; irrelevant. sett_image is the scratch, and it is free here - only save and
; load use it, and save builds the image after this returns.
sett_compact:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        xor ecx, ecx
        xor edx, edx                    ; new arena used
.sco_loop:
        cmp ecx, [sett_val_count]
        jae .sco_back
        mov eax, ecx
        call sett_val_base
        push ecx
        push edx
        mov edi, sett_image
        add edi, edx
        mov esi, sett_arena
        add esi, [ebx + 8]
        mov ecx, [ebx + 12]
        cld
        rep movsb
        pop edx
        pop ecx
        mov [ebx + 8], edx
        add edx, [ebx + 12]
        inc ecx
        jmp .sco_loop
.sco_back:
        mov [sett_arena_used], edx
        mov esi, sett_image
        mov edi, sett_arena
        mov ecx, edx
        cld
        rep movsb
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

sett_reset:
        mov dword [sett_key_count], 0
        mov dword [sett_val_count], 0
        mov dword [sett_arena_used], 0
        ret

;=======================================================================
; Persistence: one file, replaced whole
;=======================================================================


; Pin the current directory to the root for a read or a write, and make the
; directory cache a miss so the next lookup reads the root and not whatever
; the shell was listing. file_save_cwd / file_restore_cwd bracket the calls.
sett_pin_root:
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        mov byte [current_dir_name], '/'
        mov byte [current_dir_name + 1], 0
        mov dword [dir_depth], 0
        mov dword [dir_cache_lba], 0xFFFFFFFF
        ret

; ECX = image length, ESI = file name -> CF set when the write failed.
; The caller has pinned the root, so this is the half a transaction uses to
; write more than one file inside one directory bracket.
sett_write_named_pinned:
        push eax
        push ecx
        push edx
        push edi
        mov [sett_image_len], ecx       ; the create clobbers ECX
        mov edi, sett_image
        mov ecx, [sett_image_len]
        mov edx, FTYPE_FILE
        call hbfs_create_file
        jc .swn_fail
        mov dword [dir_cache_lba], 0xFFFFFFFF
        pop edi
        pop edx
        pop ecx
        pop eax
        clc
        ret
.swn_fail:
        pop edi
        pop edx
        pop ecx
        pop eax
        stc
        ret

; ECX = image length -> CF set when the write failed
sett_write_root:
        push eax
        push ebx
        push edx
        push edi
        push esi
        mov [sett_image_len], ecx       ; the create clobbers ECX
        call file_save_cwd
        call sett_pin_root
        mov esi, sett_file_name
        call sett_write_named_pinned
        jc .swr_restore_fail
        call file_restore_cwd
        pop esi
        pop edi
        pop edx
        pop ebx
        pop eax
        clc
        ret
.swr_restore_fail:
        call file_restore_cwd
.swr_fail:
        pop esi
        pop edi
        pop edx
        pop ebx
        pop eax
        stc
        ret

; Copy the current /sett.bin to /sett.bak, so a generation that turns out to be
; unusable can be rolled back to. The root is pinned by the caller. A missing
; or unreadable file is not an error: there is nothing to keep.
sett_backup_pinned:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        push esi
        mov esi, sett_file_name
        mov ecx, FD_FLAG_READ
        call fd_open
        cmp eax, -1
        je .sbp_done
        mov [.sbp_fd], eax
        mov ebx, eax
        mov ecx, sett_backup_buf
        mov edx, SETT_IMAGE_SIZE
        call fd_read
        mov [.sbp_len], eax
        mov ebx, [.sbp_fd]
        call fd_close
        cmp dword [.sbp_len], 0
        jle .sbp_done
        mov esi, sett_backup_name
        mov edi, sett_backup_buf
        mov ecx, [.sbp_len]
        mov edx, FTYPE_FILE
        call hbfs_create_file
.sbp_done:
        mov dword [dir_cache_lba], 0xFFFFFFFF
        pop esi
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret
.sbp_fd:  dd 0
.sbp_len: dd 0

; CF set when the tree could not be written
sett_save:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        push esi
        call sett_compact
        mov edi, sett_image
        mov eax, SETT_MAGIC
        stosd
        mov eax, SETT_VERSION
        stosd
        mov eax, [sett_key_count]
        stosd
        mov eax, [sett_val_count]
        stosd
        mov eax, [sett_arena_used]
        stosd
        mov eax, [sett_generation]
        inc eax
        mov [sett_generation], eax
        stosd
        xor eax, eax
        stosd                           ; the checksum, filled in below

        mov esi, sett_keys
        mov ecx, [sett_key_count]
        imul ecx, ecx, SETT_KEY_ENTRY
        cld
        rep movsb

        mov esi, sett_vals
        mov ecx, [sett_val_count]
        imul ecx, ecx, SETT_VAL_ENTRY
        rep movsb

        mov esi, sett_arena
        mov ecx, [sett_arena_used]
        rep movsb

        mov ecx, edi
        sub ecx, sett_image
        mov [sett_image_len], ecx
        ; The checksum covers the image with its own field zeroed, so a torn
        ; write is refused by the load rather than half trusted.
        mov eax, sett_image
        call crc32c
        mov [sett_image + SETT_HDR_SUM], eax

        call file_save_cwd
        call sett_pin_root
        call sett_backup_pinned
        mov esi, sett_file_name
        mov ecx, [sett_image_len]
        call sett_write_named_pinned
        jc .sav_fail
        call file_restore_cwd
        clc
        jmp .sav_out
.sav_fail:
        call file_restore_cwd
        stc
.sav_out:
        pop esi
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret
; CF set when the file is missing, unreadable or not a Sett image. On any
; failure the tree is emptied rather than left half loaded.
sett_load:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        push esi
        ; fd_open matches a name in the current directory, so the read is
        ; pinned to the root the file lives in, as sett_save's write is.
        mov dword [sett_load_err], SETT_ERR_OPEN
        call file_save_cwd
        call sett_pin_root
        mov esi, [sett_load_name]
        mov ecx, FD_FLAG_READ
        call fd_open
        cmp eax, -1
        je .sld_fail_restore
        mov [.sld_fd], eax
        mov ebx, eax
        mov ecx, sett_image
        mov edx, SETT_IMAGE_SIZE
        call fd_read
        mov [.sld_len], eax
        mov ebx, [.sld_fd]
        call fd_close
        call file_restore_cwd
        mov dword [sett_load_err], SETT_ERR_IMAGE
        cmp dword [.sld_len], SETT_HEADER_V1
        jb .sld_fail

        cmp dword [sett_image + 0], SETT_MAGIC
        jne .sld_fail
        ; Two header sizes are read: an image written before the commit work
        ; (version 1) has no generation or checksum, and refusing it would be a
        ; machine that cannot read the settings it saved yesterday.
        mov eax, [sett_image + 4]
        cmp eax, SETT_VERSION
        je .sld_v2
        cmp eax, SETT_VERSION_1
        jne .sld_fail
        mov dword [.sld_hdr], SETT_HEADER_V1
        mov dword [.sld_gen], 0
        jmp .sld_header
.sld_v2:
        mov dword [.sld_hdr], SETT_HEADER
        cmp dword [.sld_len], SETT_HEADER
        jb .sld_fail
        ; A torn or corrupted write is refused here, by checksum, before a
        ; single key is trusted.
        mov eax, [sett_image + SETT_HDR_SUM]
        mov [.sld_sum], eax
        mov dword [sett_image + SETT_HDR_SUM], 0
        mov eax, sett_image
        mov ecx, [.sld_len]
        call crc32c
        cmp eax, [.sld_sum]
        jne .sld_fail
        mov eax, [sett_image + SETT_HDR_GEN]
        mov [.sld_gen], eax
        mov eax, [sett_image + SETT_HDR_SUM]
        mov dword [sett_image + SETT_HDR_SUM], eax
.sld_header:
        mov eax, [sett_image + SETT_HDR_KEYS]
        cmp eax, SETT_MAX_KEYS
        ja .sld_fail
        mov [.sld_keys], eax
        mov eax, [sett_image + SETT_HDR_VALS]
        cmp eax, SETT_MAX_VALUES
        ja .sld_fail
        mov [.sld_vals], eax
        mov eax, [sett_image + SETT_HDR_ARENA]
        cmp eax, SETT_ARENA
        ja .sld_fail
        mov [.sld_arena], eax

        ; The header has to describe exactly the bytes that arrived.
        mov eax, [.sld_keys]
        imul eax, eax, SETT_KEY_ENTRY
        add eax, [.sld_hdr]
        mov edx, [.sld_vals]
        imul edx, edx, SETT_VAL_ENTRY
        add eax, edx
        add eax, [.sld_arena]
        cmp eax, [.sld_len]
        jne .sld_fail

        ; Every key must be terminated and name a parent that exists.
        xor ecx, ecx
.sld_key_check:
        cmp ecx, [.sld_keys]
        jae .sld_val_start
        mov eax, ecx
        imul eax, eax, SETT_KEY_ENTRY
        add eax, sett_image
        add eax, [.sld_hdr]
        cmp byte [eax + 4 + SETT_KEY_PATH - 1], 0
        jne .sld_fail
        mov edx, [eax]
        cmp edx, -1
        je .sld_key_next
        cmp edx, [.sld_keys]
        jae .sld_fail
.sld_key_next:
        inc ecx
        jmp .sld_key_check

.sld_val_start:
        xor ecx, ecx
.sld_val_check:
        cmp ecx, [.sld_vals]
        jae .sld_accept
        mov eax, [.sld_keys]
        imul eax, eax, SETT_KEY_ENTRY
        add eax, [.sld_hdr]
        mov edx, ecx
        imul edx, edx, SETT_VAL_ENTRY
        add eax, edx
        add eax, sett_image
        mov edx, [eax]                  ; key index
        cmp edx, [.sld_keys]
        jae .sld_fail
        mov edx, [eax + 4]              ; type
        cmp edx, SETT_T_U32
        jb .sld_fail
        cmp edx, SETT_T_SECRET
        ja .sld_fail
        mov edx, [eax + 8]              ; offset
        add edx, [eax + 12]             ; plus length
        jc .sld_fail
        cmp edx, [.sld_arena]
        ja .sld_fail
        cmp byte [eax + 16 + SETT_VAL_NAME - 1], 0
        jne .sld_fail
        inc ecx
        jmp .sld_val_check

.sld_accept:
        call sett_reset
        mov eax, [.sld_keys]
        mov [sett_key_count], eax
        mov eax, [.sld_vals]
        mov [sett_val_count], eax
        mov eax, [.sld_arena]
        mov [sett_arena_used], eax
        mov eax, [.sld_gen]
        mov [sett_generation], eax

        mov esi, sett_image
        add esi, [.sld_hdr]
        mov edi, sett_keys
        mov ecx, [.sld_keys]
        imul ecx, ecx, SETT_KEY_ENTRY
        cld
        rep movsb

        mov edi, sett_vals
        mov ecx, [.sld_vals]
        imul ecx, ecx, SETT_VAL_ENTRY
        rep movsb

        mov edi, sett_arena
        mov ecx, [.sld_arena]
        rep movsb

        pop esi
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        clc
        ret
.sld_fail_restore:
        call file_restore_cwd
.sld_fail:
        call sett_reset
        pop esi
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        stc
        ret
.sld_fd:    dd 0
.sld_len:   dd 0
.sld_keys:  dd 0
.sld_vals:  dd 0
.sld_arena: dd 0
.sld_hdr:   dd SETT_HEADER
.sld_gen:   dd 0
.sld_sum:   dd 0

;=======================================================================
; The environment compatibility view (conformance item 6)
;=======================================================================

; Copy every NAME=VALUE into \Registry\Machine\Environment\NAME as a STRING.
; CF set when the tree could not hold them.
sett_env_import:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        push esi
        mov dword [.sei_i], 0
.sei_loop:
        mov ebx, [.sei_i]
        cmp ebx, ENV_MAX
        jae .sei_done
        mov eax, ebx
        imul eax, ENV_ENTRY_SIZE
        lea esi, [env_table + eax]
        cmp byte [esi], 0
        je .sei_next
        ; The variable name, up to '=', bounded so that prefix + separator +
        ; name still fits a key path. A longer name is skipped, not cut.
        lea edi, [sett_envbuf]
        mov ecx, SETT_KEY_PATH - 32
.sei_name:
        mov al, [esi]
        test al, al
        jz .sei_next                    ; no '=': not a variable
        cmp al, '='
        je .sei_split
        mov [edi], al
        inc edi
        inc esi
        dec ecx
        jnz .sei_name
        jmp .sei_next
.sei_split:
        mov byte [edi], 0
        inc esi                         ; ESI = the value
        mov [.sei_val], esi
        ; One key, \Registry\Machine\Environment, with one value per variable:
        ; a name is a value name, not a key of its own.
        lea esi, [sett_env_prefix]
        call sett_ensure_path
        jc .sei_fail
        mov [.sei_key], eax
        ; Length of the value including its terminator.
        mov esi, [.sei_val]
        xor ecx, ecx
.sei_len:
        cmp byte [esi + ecx], 0
        je .sei_len_done
        inc ecx
        jmp .sei_len
.sei_len_done:
        inc ecx
        mov eax, [.sei_key]
        mov edx, SETT_T_STRING
        lea esi, [sett_envbuf]
        mov ebx, [.sei_val]
        call sett_value_set             ; EAX = key, ESI = name, EDX/ECX/EBX
        jc .sei_fail
.sei_next:
        inc dword [.sei_i]
        jmp .sei_loop
.sei_done:
        pop esi
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        clc
        ret
.sei_fail:
        pop esi
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        stc
        ret
.sei_i:   dd 0
.sei_key: dd 0
.sei_val: dd 0

; Write every value of \Registry\Machine\Environment back into the shell's
; variable table as NAME=VALUE. This is the view the existing programs read.
sett_env_apply:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        push esi
        lea esi, [sett_env_prefix]
        call sett_key_find
        jc .sea_done                    ; no tree yet: nothing to write back
        mov [.sea_key], eax
        mov dword [.sea_i], 0
.sea_loop:
        mov ecx, [.sea_i]
        cmp ecx, [sett_val_count]
        jae .sea_done
        mov eax, ecx
        call sett_val_base
        mov edx, [.sea_key]
        cmp [ebx], edx
        jne .sea_next
        ; "NAME=" then the value.
        lea edi, [sett_envbuf]
        lea esi, [ebx + 16]
        call str_copy
        dec edi
        mov byte [edi], '='
        inc edi
        mov ecx, [ebx + 12]
        mov esi, sett_arena
        add esi, [ebx + 8]
        ; The whole variable has to fit one entry.
        mov edx, edi
        sub edx, sett_envbuf
        neg edx
        add edx, ENV_ENTRY_SIZE - 1
        cmp ecx, edx
        jbe .sea_copy
        mov ecx, edx
.sea_copy:
        cld
        rep movsb
        mov byte [edi], 0
        lea esi, [sett_envbuf]
        call env_set_str
.sea_next:
        inc dword [.sea_i]
        jmp .sea_loop
.sea_done:
        pop esi
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret
.sea_i:   dd 0
.sea_key: dd 0

;=======================================================================
; The shell command
;=======================================================================

sett_word_set:   db "set", 0
sett_word_get:   db "get", 0
sett_word_del:   db "del", 0
sett_word_delkey: db "delkey", 0
sett_word_tree:  db "tree", 0
sett_word_save:  db "save", 0
sett_word_load:  db "load", 0
sett_word_env:   db "env", 0
sett_word_import: db "import", 0
sett_word_apply: db "apply", 0
sett_word_u32:   db "u32", 0
sett_word_str:   db "str", 0
sett_word_bin:   db "bin", 0
sett_word_secret: db "secret", 0
sett_empty_name: db 0

msg_sett_stored:   db "sett: stored", 0x0A, 0
msg_sett_deleted:  db "sett: deleted", 0x0A, 0
msg_sett_keygone:  db "sett: key removed", 0x0A, 0
msg_sett_saved:    db "sett: written to /sett.bin", 0x0A, 0
msg_sett_loaded:   db "sett: read from /sett.bin", 0x0A, 0
msg_sett_envin:    db "sett: the environment is in the tree", 0x0A, 0
msg_sett_envout:   db "sett: the environment was written back", 0x0A, 0
msg_sett_empty:    db "sett: the tree is empty", 0x0A, 0
msg_sett_equals:   db " = ", 0
msg_sett_quote:    db 0x22, 0
msg_sett_u32t:     db "u32 ", 0
msg_sett_strt:     db "str ", 0
msg_sett_bint:     db "bin ", 0
msg_sett_nokey:    db "sett: no such key", 0x0A, 0
msg_sett_noval:    db "sett: no such value", 0x0A, 0
msg_sett_secret:   db "secret: held by the credential store, not readable", 0x0A, 0
msg_sett_secretword: db "secret", 0
msg_sett_sec_audit: db "sett: a SECRET value was read back and refused", 0
msg_sett_full:     db "sett: the tree is full", 0x0A, 0
msg_sett_badtype:  db "sett: type is u32, str or bin", 0x0A, 0
msg_sett_badval:   db "sett: bad value for the type", 0x0A, 0
msg_sett_nofile:   db "sett: no /sett.bin to read", 0x0A, 0
msg_sett_noimage:  db "sett: /sett.bin is not a Sett image", 0x0A, 0
msg_sett_iowrite:  db "sett: could not write /sett.bin", 0x0A, 0
msg_sett_usage:
        db "sett set PATH NAME u32|str|bin|secret VALUE | sett get PATH NAME", 0x0A
        db "sett del PATH NAME | sett delkey PATH | sett tree [PATH]", 0x0A
        db "sett save | sett load | sett rollback | sett gen", 0x0A
        db "sett begin | sett commit | sett abort - one write, or none", 0x0A
        db "sett watch PATH | sett events | sett acl PATH [UID MODE]", 0x0A
        db "sett hive save PATH FILE | sett hive load FILE", 0x0A
        db "sett export [PATH] | sett import FILE | sett env import|apply", 0x0A
        db 0

align 4
sett_u32buf: times 4 db 0

; ESI -> the first argument after "sett <verb>"
sett_args:
        push edi
        push edx
        mov esi, line_buffer
        mov edi, cmd_sett_str
        call str_starts_with
        mov edi, sett_tok
        mov edx, 16
        call sett_word
        pop edx
        pop edi
        ret

; Read PATH then NAME into the token buffers. CF set when either is missing.
sett_two_words:
        mov edi, sett_path_tok
        mov edx, SETT_KEY_PATH - 1
        call sett_word
        jc .stw_fail
        mov edi, sett_name_tok
        mov edx, SETT_VAL_NAME - 1
        call sett_word
        jc .stw_fail
        clc
        ret
.stw_fail:
        stc
        ret

; Print the value named by sett_path_tok / sett_name_tok, or say why not.
sett_print_value:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        lea esi, [sett_path_tok]
        call sett_key_find
        jc .spv_nokey
        lea esi, [sett_name_tok]
        call sett_value_find
        jc .spv_noval
        call sett_val_base
        lea esi, [sett_path_tok]
        call vga_print
        mov esi, msg_sett_equals
        call vga_print
        mov eax, [ebx + 4]
        cmp eax, SETT_T_SECRET
        je .spv_secret
        cmp eax, SETT_T_U32
        je .spv_u32
        cmp eax, SETT_T_STRING
        je .spv_str
        mov esi, msg_sett_bint
        call vga_print
        mov esi, sett_arena
        add esi, [ebx + 8]
        mov ecx, [ebx + 12]
        call sett_print_hex
        jmp .spv_done
.spv_u32:
        mov esi, sett_arena
        add esi, [ebx + 8]
        mov eax, [esi]
        call vga_print_dec
        jmp .spv_done
.spv_str:
        mov esi, sett_arena
        add esi, [ebx + 8]
        call vga_print
        jmp .spv_done
.spv_nokey:
        mov esi, msg_sett_nokey
        call vga_print
        jmp .spv_done
.spv_secret:
        ; The value never leaves the store: the attempt is recorded instead.
        mov esi, msg_sett_secret
        call vga_print
        LOGBOOT LOG_WARN, msg_sett_sec_audit
        jmp .spv_done
.spv_noval:
        mov esi, msg_sett_noval
        call vga_print
.spv_done:
        call vga_newline
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; ESI = bytes, ECX = count -> uppercase hex
sett_print_hex:
        push eax
        push ecx
        push esi
        cld
.sph_loop:
        test ecx, ecx
        jz .sph_done
        lodsb
        push eax
        shr al, 4
        call sett_put_hex
        pop eax
        and al, 0x0F
        call sett_put_hex
        dec ecx
        jmp .sph_loop
.sph_done:
        pop esi
        pop ecx
        pop eax
        ret

sett_put_hex:
        cmp al, 10
        jb .sph_digit
        add al, 'A' - 10
        jmp .sph_out
.sph_digit:
        add al, '0'
.sph_out:
        call vga_putchar
        ret

; EAX = key index -> print "    name = value" for each of its values
sett_print_values:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        mov [.spl_key], eax
        xor ecx, ecx
.spl_loop:
        cmp ecx, [sett_val_count]
        jae .spl_done
        mov eax, ecx
        call sett_val_base
        mov edx, [.spl_key]
        cmp [ebx], edx
        jne .spl_next
        push ecx
        mov al, ' '
        call vga_putchar
        call vga_putchar
        call vga_putchar
        call vga_putchar
        lea esi, [ebx + 16]
        call vga_print
        mov esi, msg_sett_equals
        call vga_print
        mov eax, [ebx + 4]
        cmp eax, SETT_T_U32
        je .spl_u32
        cmp eax, SETT_T_STRING
        je .spl_str
        cmp eax, SETT_T_SECRET
        je .spl_secret
        mov esi, sett_arena
        add esi, [ebx + 8]
        mov ecx, [ebx + 12]
        call sett_print_hex
        jmp .spl_nl
.spl_u32:
        mov esi, sett_arena
        add esi, [ebx + 8]
        mov eax, [esi]
        call vga_print_dec
        jmp .spl_nl
.spl_str:
        mov esi, sett_arena
        add esi, [ebx + 8]
        call vga_print
        jmp .spl_nl
.spl_secret:
        mov esi, msg_sett_secretword
        call vga_print
.spl_nl:
        call vga_newline
        pop ecx
.spl_next:
        inc ecx
        jmp .spl_loop
.spl_done:
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret
.spl_key: dd 0

; Indent two spaces per level, then the path.
sett_print_key:
        push eax
        push ecx
        push edx
        push esi
        mov edx, esi
        xor ecx, ecx
.spk_depth:
        mov al, [edx]
        test al, al
        jz .spk_indent
        cmp al, '\'
        jne .spk_next
        inc ecx
.spk_next:
        inc edx
        jmp .spk_depth
.spk_indent:
        dec ecx                         ; the leading separator is level 0
        jle .spk_path
        mov al, ' '
        call vga_putchar
        call vga_putchar
        dec ecx
        jmp .spk_indent
.spk_path:
        call vga_print
        call vga_newline
        pop esi
        pop edx
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; sett_command - `sett <verb> ...`
;-----------------------------------------------------------------------
sett_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_sett_str
        call str_starts_with
        jc .scl_have
        jmp .scl_usage
.scl_have:
        mov edi, sett_tok
        mov edx, 16
        call sett_word
        jc .scl_usage

        mov esi, sett_tok
        mov edi, sett_word_set
        call sett_ci_equal
        jc .scl_set
        mov esi, sett_tok
        mov edi, sett_word_get
        call sett_ci_equal
        jc .scl_get
        mov esi, sett_tok
        mov edi, sett_word_delkey
        call sett_ci_equal
        jc .scl_delkey
        mov esi, sett_tok
        mov edi, sett_word_del
        call sett_ci_equal
        jc .scl_del
        mov esi, sett_tok
        mov edi, sett_word_tree
        call sett_ci_equal
        jc .scl_tree
        mov esi, sett_tok
        mov edi, sett_word_save
        call sett_ci_equal
        jc .scl_save
        mov esi, sett_tok
        mov edi, sett_word_load
        call sett_ci_equal
        jc .scl_load
        mov esi, sett_tok
        mov edi, sett_word_env
        call sett_ci_equal
        jc .scl_env
        jmp .scl_usage

;--- set ---------------------------------------------------------------
.scl_set:
        call sett_args
        jc .scl_usage
        call sett_two_words
        jc .scl_usage
        mov edi, sett_type_tok
        mov edx, 7
        call sett_word
        jc .scl_usage
        ; Where the value starts. The type compares below restore ESI, so the
        ; cursor has to be kept somewhere the compares cannot reach.
        mov [sett_tmp_data], esi

        mov esi, sett_type_tok
        mov edi, sett_word_u32
        call sett_ci_equal
        jc .scl_set_u32
        mov esi, sett_type_tok
        mov edi, sett_word_str
        call sett_ci_equal
        jc .scl_set_str
        mov esi, sett_type_tok
        mov edi, sett_word_bin
        call sett_ci_equal
        jc .scl_set_bin
        mov esi, sett_type_tok
        mov edi, sett_word_secret
        call sett_ci_equal
        jc .scl_set_secret
        jmp .scl_badtype

.scl_set_u32:
        mov esi, [sett_tmp_data]
        mov edi, sett_val_tok
        mov edx, 12
        call sett_word
        jc .scl_usage
        cmp byte [sett_val_tok], 0
        je .scl_badval
        lea esi, [sett_val_tok]
        call parse_decimal
        mov [sett_u32buf], eax
        mov dword [sett_tmp_type], SETT_T_U32
        mov dword [sett_tmp_len], 4
        mov dword [sett_tmp_data], sett_u32buf
        jmp .scl_store

.scl_set_str:
        ; Everything after the type is the value, spaces included.
        mov esi, [sett_tmp_data]
.scl_str_skip:
        cmp byte [esi], ' '
        jne .scl_str_start
        inc esi
        jmp .scl_str_skip
.scl_str_start:
        mov edx, -1
.scl_str_len:
        inc edx
        cmp byte [esi + edx], 0
        jne .scl_str_len
        test edx, edx
        jz .scl_badval
        inc edx                         ; the terminator is part of the value
        mov dword [sett_tmp_type], SETT_T_STRING
        mov [sett_tmp_len], edx
        mov [sett_tmp_data], esi
        jmp .scl_store

.scl_set_bin:
        mov esi, [sett_tmp_data]
.scl_bin_skip:
        cmp byte [esi], ' '
        jne .scl_bin_start
        inc esi
        jmp .scl_bin_skip
.scl_bin_start:
        mov edi, sett_val_tok
        mov edx, 159
        call sett_hex
        jc .scl_badval
        test ecx, ecx
        jz .scl_badval
        mov dword [sett_tmp_type], SETT_T_BINARY
        mov [sett_tmp_len], ecx
        mov dword [sett_tmp_data], sett_val_tok

.scl_store:
        lea esi, [sett_path_tok]
        call sett_ensure_path
        jc .scl_full
        mov edx, [sett_tmp_type]
        mov ecx, [sett_tmp_len]
        mov ebx, [sett_tmp_data]
        lea esi, [sett_name_tok]
        call sett_value_set
        jc .scl_full
        mov esi, msg_sett_stored
        call vga_print
        jmp .scl_done

.scl_set_secret:
        ; The plaintext goes to the credential store and the value records only
        ; that a secret is there; nothing in the tree holds the bytes.
        mov esi, [sett_tmp_data]
.scl_sec_skip:
        cmp byte [esi], ' '
        jne .scl_sec_start
        inc esi
        jmp .scl_sec_skip
.scl_sec_start:
        ; The store wants the 64 hex characters, so they are validated into a
        ; throwaway buffer and the text itself is what is handed over.
        mov [sett_hexstart], esi
        mov edi, sett_secbytes
        mov edx, 32
        call sett_hex
        jc .scl_badval
        cmp ecx, 32
        jne .scl_badval                 ; a secret is 32 bytes, like the store's
        lea esi, [sett_path_tok]
        call sett_ensure_path
        jc .scl_full
        mov esi, [sett_hexstart]
        lea edi, [sett_name_tok]
        call sett_secret_store          ; EAX = key, ESI = hex, EDI = name
        jc .scl_full
        mov esi, msg_sett_stored
        call vga_print
        jmp .scl_done

;--- get ---------------------------------------------------------------
.scl_get:
        call sett_args
        jc .scl_usage
        call sett_two_words
        jc .scl_usage
        call sett_print_value
        jmp .scl_done

;--- del ---------------------------------------------------------------
.scl_del:
        call sett_args
        jc .scl_usage
        call sett_two_words
        jc .scl_usage
        lea esi, [sett_path_tok]
        call sett_key_find
        jc .scl_nokey
        lea esi, [sett_name_tok]
        call sett_value_del
        jc .scl_noval
        mov esi, msg_sett_deleted
        call vga_print
        jmp .scl_done

;--- delkey ------------------------------------------------------------
.scl_delkey:
        call sett_args
        jc .scl_usage
        mov edi, sett_path_tok
        mov edx, SETT_KEY_PATH - 1
        call sett_word
        jc .scl_usage
        lea esi, [sett_path_tok]
        call sett_key_delete
        jc .scl_nokey
        mov esi, msg_sett_keygone
        call vga_print
        jmp .scl_done

;--- tree --------------------------------------------------------------
.scl_tree:
        ; The path is optional: with none, the whole tree is shown.
        call sett_args
        jc .scl_tree_all
        mov edi, sett_path_tok
        mov edx, SETT_KEY_PATH - 1
        call sett_word
        jc .scl_tree_all
        lea esi, [sett_path_tok]
        call sett_path_norm
        jc .scl_usage
        jmp .scl_tree_go
.scl_tree_all:
        mov byte [sett_pathbuf], '\'
        mov byte [sett_pathbuf + 1], 0
.scl_tree_go:
        cmp dword [sett_key_count], 0
        je .scl_empty
        mov dword [sett_tmp_len], 0     ; the key cursor
.scl_tree_loop:
        mov ecx, [sett_tmp_len]
        cmp ecx, [sett_key_count]
        jae .scl_done
        mov eax, ecx
        call sett_key_base
        lea esi, [ebx + 4]
        lea edi, [sett_pathbuf]
        call sett_prefix
        jnc .scl_tree_next
        mov eax, [sett_tmp_len]
        call sett_key_base
        lea esi, [ebx + 4]
        call sett_print_key
        mov eax, [sett_tmp_len]
        call sett_print_values
.scl_tree_next:
        inc dword [sett_tmp_len]
        jmp .scl_tree_loop

;--- save / load -------------------------------------------------------
.scl_save:
        call sett_save
        jc .scl_iowrite
        mov esi, msg_sett_saved
        call vga_print
        jmp .scl_done
.scl_load:
        call sett_load
        jc .scl_nofile
        mov esi, msg_sett_loaded
        call vga_print
        jmp .scl_done

;--- env ---------------------------------------------------------------
.scl_env:
        call sett_args
        jc .scl_usage
        mov edi, sett_tok
        mov edx, 16
        call sett_word
        jc .scl_usage
        mov esi, sett_tok
        mov edi, sett_word_import
        call sett_ci_equal
        jc .scl_env_import
        mov esi, sett_tok
        mov edi, sett_word_apply
        call sett_ci_equal
        jc .scl_env_apply
        jmp .scl_usage
.scl_env_import:
        call sett_env_import
        jc .scl_full
        mov esi, msg_sett_envin
        call vga_print
        jmp .scl_done
.scl_env_apply:
        call sett_env_apply
        mov esi, msg_sett_envout
        call vga_print
        jmp .scl_done

;--- messages ----------------------------------------------------------
.scl_nokey:
        mov esi, msg_sett_nokey
        call vga_print
        jmp .scl_done
.scl_noval:
        mov esi, msg_sett_noval
        call vga_print
        jmp .scl_done
.scl_full:
        mov esi, msg_sett_full
        call vga_print
        jmp .scl_done
.scl_badtype:
        mov esi, msg_sett_badtype
        call vga_print
        jmp .scl_done
.scl_badval:
        mov esi, msg_sett_badval
        call vga_print
        jmp .scl_done
.scl_nofile:
        mov esi, msg_sett_noimage
        cmp dword [sett_load_err], SETT_ERR_OPEN
        jne .scl_nofile_print
        mov esi, msg_sett_nofile
.scl_nofile_print:
        call vga_print
        jmp .scl_done
.scl_iowrite:
        mov esi, msg_sett_iowrite
        call vga_print
        jmp .scl_done
.scl_empty:
        mov esi, msg_sett_empty
        call vga_print
        jmp .scl_done
.scl_usage:
        ; The extended verbs (transactions, watchers, ACLs, hives, export) live
        ; in settx.inc; it prints the combined usage when it knows none of them.
        call sett_ext_command
.scl_done:
        popad
        ret
