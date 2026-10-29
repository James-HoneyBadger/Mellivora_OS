;=======================================================================
; settx.inc - the rest of the Sett (M4, CONFIG.md sections 3, 5, 7, 8)
;
; sett.inc builds the tree: keys, typed values, one file, the environment
; view. This file is what turns that tree into the store the specification
; describes:
;
;   * transactions - `sett begin` snapshots the tree, `sett abort` puts it
;     back, and `sett commit` writes it once. A multi-value change is therefore
;     one file write, and the write carries a generation and a checksum so a
;     torn one is refused by the next load instead of half trusted;
;   * a last-known-good generation - every committed save copies the previous
;     /sett.bin to /sett.bak first, and `sett rollback` reads it back;
;   * change notification - one bounded ring of create/set/delete events, in
;     commit order, with consecutive duplicates coalesced rather than grown;
;   * per-key access - an owner and a mode word, checked before a caller that
;     is not SYSTEM may change a key;
;   * SECRET values - the plaintext goes to the credential store, the tree
;     keeps only the store entry's name, and every read attempt is audited;
;   * hives - a subtree written to its own file and merged back, which is what
;     a per-user hive is at logon;
;   * text export and import - `path|type|name|value`, one value per line.
;
; The snapshot is the whole table, not an undo log: the tree is capped at 64
; keys and 64 values, so 28 KiB of copy is cheaper than a log that has to be
; replayed in the right order. That is a decision this size of store can make
; and a larger one could not.
;=======================================================================

SETT_OP_NEWKEY    equ 1
SETT_OP_NEWVAL    equ 2
SETT_OP_SET       equ 3
SETT_OP_DEL       equ 4
SETT_OP_DELKEY    equ 5
SETT_OP_STATE     equ 6

SETT_EV_MAX       equ 16
SETT_EV_ENTRY     equ 168               ; op, count, path, name
SETT_EV_PATH      equ 8
SETT_EV_NAME      equ (8 + SETT_KEY_PATH)
SETT_WATCH_MAX    equ 4

align 4
sett_ev_ring:     times SETT_EV_MAX * SETT_EV_ENTRY db 0
sett_ev_head:     dd 0
sett_ev_count:    dd 0
sett_ev_coalesced: dd 0
sett_ev_dropped:  dd 0

sett_watches:     times SETT_WATCH_MAX * SETT_KEY_PATH db 0
sett_watch_count: dd 0

sett_txn_depth:   dd 0
sett_snap_keys:   times SETT_MAX_KEYS * SETT_KEY_ENTRY db 0
sett_snap_vals:   times SETT_MAX_VALUES * SETT_VAL_ENTRY db 0
sett_snap_arena:  times SETT_ARENA db 0
sett_snap_key_count: dd 0
sett_snap_val_count: dd 0
sett_snap_arena_used: dd 0

; A hive is built in its own buffers, not in the transaction snapshot: a logon
; that loads a user hive must not destroy a transaction somebody else has open.
sett_hive_keys_buf:  times SETT_MAX_KEYS * SETT_KEY_ENTRY db 0
sett_hive_vals_buf:  times SETT_MAX_VALUES * SETT_VAL_ENTRY db 0
sett_hive_arena_buf: times SETT_ARENA db 0

sett_hive_path:   times SETT_KEY_PATH db 0
sett_hive_file:   times 96 db 0
sett_hive_keys:   dd 0
sett_hive_vals:   dd 0
sett_hive_arena:  dd 0
sett_hive_hdr:    dd SETT_HEADER
sett_hive_keep:   times SETT_MAX_KEYS db 0
sett_hive_map:    times SETT_MAX_KEYS dd 0
sett_secname:     times 16 db 0
sett_hexword:     times 80 db 0
sett_expline:     times 256 db 0
sett_lineptr:     dd 0
sett_argp:        dd 0

;-----------------------------------------------------------------------
; sett_ci_len - length of a NUL-terminated string
;   In : ESI   Out: EAX   Preserves everything else.
;-----------------------------------------------------------------------
sett_ci_len:
        push esi
        xor eax, eax
.scl_len:
        cmp byte [esi + eax], 0
        je .scl_done
        inc eax
        jmp .scl_len
.scl_done:
        pop esi
        ret

;-----------------------------------------------------------------------
; sett_copy_bounded - copy a string into a fixed field, zero-filling the rest
;   In : ESI = source, EDI = destination, ECX = field size
;   Out: EDI advanced past the field; ESI, ECX, EAX clobbered.
;-----------------------------------------------------------------------
sett_copy_bounded:
        push ecx
        push edi
.scb_loop:
        test ecx, ecx
        jz .scb_out
        mov al, [esi]
        mov [edi], al
        inc edi
        dec ecx
        test al, al
        jz .scb_zero
        inc esi
        jmp .scb_loop
.scb_zero:
        xor eax, eax
        rep stosb
.scb_out:
        pop edi
        pop ecx
        ret

;-----------------------------------------------------------------------
; sett_notify - record a change for the watchers
;   In : AL = SETT_OP_*, ESI = key path, EDI = value name ("" for a key event)
;   Out: nothing. Everything is preserved.
;
; Consecutive events with the same op, key and name fold into one record with a
; count: a program that writes the same setting a hundred times is one thing
; that happened, not a hundred, and the ring stays bounded either way.
;-----------------------------------------------------------------------
sett_notify:
        pushad
        movzx eax, al
        mov [.sn_op], eax
        mov [.sn_key], esi
        mov [.sn_name], edi

        cmp dword [sett_ev_count], 0
        je .sn_append
        mov eax, [sett_ev_head]
        dec eax
        jns .sn_lastslot
        mov eax, SETT_EV_MAX - 1
.sn_lastslot:
        imul eax, SETT_EV_ENTRY
        add eax, sett_ev_ring
        mov [.sn_last], eax
        mov ebx, [.sn_op]
        cmp dword [eax], ebx
        jne .sn_append
        lea esi, [eax + SETT_EV_PATH]
        mov edi, [.sn_key]
        call sett_ci_equal
        jnc .sn_append
        mov eax, [.sn_last]
        lea esi, [eax + SETT_EV_NAME]
        mov edi, [.sn_name]
        call sett_ci_equal
        jnc .sn_append
        mov eax, [.sn_last]
        inc dword [eax + 4]
        inc dword [sett_ev_coalesced]
        jmp .sn_done

.sn_append:
        cmp dword [sett_ev_count], SETT_EV_MAX
        jb .sn_room
        inc dword [sett_ev_dropped]
        jmp .sn_done
.sn_room:
        mov eax, [sett_ev_head]
        imul eax, SETT_EV_ENTRY
        add eax, sett_ev_ring
        mov [.sn_slot], eax
        mov ebx, [.sn_op]
        mov [eax], ebx
        mov dword [eax + 4], 1

        mov eax, [.sn_slot]
        lea edi, [eax + SETT_EV_PATH]
        mov esi, [.sn_key]
        mov ecx, SETT_KEY_PATH
        call sett_copy_bounded
        mov eax, [.sn_slot]
        lea edi, [eax + SETT_EV_NAME]
        mov esi, [.sn_name]
        mov ecx, SETT_VAL_NAME
        call sett_copy_bounded

        inc dword [sett_ev_count]
        mov eax, [sett_ev_head]
        inc eax
        cmp eax, SETT_EV_MAX
        jb .sn_head_ok
        xor eax, eax
.sn_head_ok:
        mov [sett_ev_head], eax
.sn_done:
        popad
        ret
.sn_op:   dd 0
.sn_key:  dd 0
.sn_name: dd 0
.sn_last: dd 0
.sn_slot: dd 0

;=======================================================================
; Key access
;=======================================================================

; EAX = key index -> EAX = (mode << 24) | owner, with the default filled in.
sett_acl_word:
        push ebx
        push edx
        call sett_key_base
        mov eax, [ebx + SETT_KEY_SPARE]
        mov edx, eax
        shr edx, 24
        test edx, edx
        jnz .saw_have
        and eax, 0x00FFFFFF
        or eax, SETT_MODE_DEFAULT << 24
.saw_have:
        pop edx
        pop ebx
        ret

; EAX = key index -> CF set when the caller may not change it.
; SYSTEM (uid 0) holds everything, which is the compatibility identity; any
; other uid is checked against the owner and the world bits.
sett_acl_may_write:
        pushad
        mov [.sam_key], eax
        call obj_identity                ; EAX = uid, ESI = 1 for SYSTEM
        test esi, esi
        jnz .sam_yes
        mov [.sam_uid], eax
        mov eax, [.sam_key]
        call sett_acl_word
        mov ecx, eax
        shr ecx, 24                      ; mode
        and eax, 0x00FFFFFF              ; owner
        cmp eax, [.sam_uid]
        jne .sam_world
        test ecx, SETT_MODE_OWN_RD
        jz .sam_no
        test ecx, SETT_MODE_OWN_WR
        jz .sam_no
        jmp .sam_yes
.sam_world:
        test ecx, SETT_MODE_WLD_RD
        jz .sam_no
        test ecx, SETT_MODE_WLD_WR
        jz .sam_no
.sam_yes:
        popad
        clc
        ret
.sam_no:
        popad
        stc
        ret
.sam_uid: dd 0
.sam_key: dd 0

;=======================================================================
; Value helpers for the rest of the kernel (services, accounts, tasks)
;=======================================================================

; EDI = key path, ESI = value name -> EAX = value index, CF when absent
sett_kv_lookup:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.kvl_name], esi
        mov esi, edi
        call sett_key_find
        jc .kvl_no
        mov [.kvl_key], eax
        mov esi, [.kvl_name]
        call sett_value_find
        jc .kvl_no
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        clc
        ret
.kvl_no:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        stc
        ret
.kvl_name: dd 0
.kvl_key:  dd 0

; EDI = key path, ESI = name -> EAX = U32 value, CF when absent or not a U32
sett_kv_get_u32:
        call sett_kv_lookup
        jc .kgu_no
        call sett_val_base
        cmp dword [ebx + 4], SETT_T_U32
        jne .kgu_no
        mov edx, sett_arena
        add edx, [ebx + 8]
        mov eax, [edx]
        clc
        ret
.kgu_no:
        stc
        ret

; EDI = key path, ESI = name -> ESI = string, CF when absent or not a STRING
sett_kv_get_str:
        call sett_kv_lookup
        jc .kgs_no
        call sett_val_base
        cmp dword [ebx + 4], SETT_T_STRING
        jne .kgs_no
        mov esi, sett_arena
        add esi, [ebx + 8]
        clc
        ret
.kgs_no:
        stc
        ret

; EDI = key path, ESI = name, EDX = type, ECX = bytes, EBX = data
sett_kv_set:
        push ebx
        push ecx
        push edx
        mov [.kvs_name], esi
        mov esi, edi
        call sett_key_find
        jnc .kvs_have
        mov esi, edi
        call sett_ensure_path
        jc .kvs_fail3
.kvs_have:
        pop edx
        pop ecx
        pop ebx
        mov esi, [.kvs_name]
        call sett_value_set
        jc .kvs_fail
        clc
        ret
.kvs_fail3:
        pop edx
        pop ecx
        pop ebx
.kvs_fail:
        stc
        ret
.kvs_path: dd 0
.kvs_name: dd 0

; EDI = key path, ESI = name, EBX = value
sett_kv_set_u32:
        mov [sett_u32buf], ebx
        mov edx, SETT_T_U32
        mov ecx, 4
        mov ebx, sett_u32buf
        jmp sett_kv_set

; EDI = key path, ESI = name, EDX = text (NUL-terminated)
sett_kv_set_str:
        push ebx
        mov ebx, edx
        mov ecx, 1
        mov eax, edx
.kvss_len:
        cmp byte [eax], 0
        je .kvss_go
        inc ecx
        inc eax
        jmp .kvss_len
.kvss_go:
        mov edx, SETT_T_STRING
        call sett_kv_set
        pop ebx
        ret

; EDI = key path, ESI = name, EDX = data, ECX = bytes
sett_kv_set_bin:
        mov ebx, edx
        mov edx, SETT_T_BINARY
        jmp sett_kv_set

; EDI = key path, ESI = name -> CF when there was no such value
sett_kv_del:
        push esi
        mov esi, edi
        call sett_key_find
        jc .kvd_no
        pop esi
        call sett_value_del
        ret
.kvd_no:
        pop esi
        stc
        ret

; EDI = key path -> CF when there was no such key
sett_kv_delkey:
        mov esi, edi
        jmp sett_key_delete

;=======================================================================
; Transactions
;=======================================================================

; Snapshot the tree. A nested begin joins the outer transaction.
sett_txn_begin_internal:
        pushad
        cmp dword [sett_txn_depth], 0
        jne .stb_nested
        mov eax, [sett_key_count]
        mov [sett_snap_key_count], eax
        mov eax, [sett_val_count]
        mov [sett_snap_val_count], eax
        mov eax, [sett_arena_used]
        mov [sett_snap_arena_used], eax

        mov esi, sett_keys
        mov edi, sett_snap_keys
        mov ecx, [sett_key_count]
        imul ecx, ecx, SETT_KEY_ENTRY
        cld
        rep movsb
        mov esi, sett_vals
        mov edi, sett_snap_vals
        mov ecx, [sett_val_count]
        imul ecx, ecx, SETT_VAL_ENTRY
        rep movsb
        mov esi, sett_arena
        mov edi, sett_snap_arena
        mov ecx, [sett_arena_used]
        rep movsb
        mov dword [sett_txn_depth], 1
        jmp .stb_done
.stb_nested:
        inc dword [sett_txn_depth]
.stb_done:
        popad
        ret

; Put the snapshot back and end the transaction.
sett_txn_abort_internal:
        pushad
        mov eax, [sett_snap_key_count]
        mov [sett_key_count], eax
        mov eax, [sett_snap_val_count]
        mov [sett_val_count], eax
        mov eax, [sett_snap_arena_used]
        mov [sett_arena_used], eax
        mov esi, sett_snap_keys
        mov edi, sett_keys
        mov ecx, [sett_key_count]
        imul ecx, ecx, SETT_KEY_ENTRY
        cld
        rep movsb
        mov esi, sett_snap_vals
        mov edi, sett_vals
        mov ecx, [sett_val_count]
        imul ecx, ecx, SETT_VAL_ENTRY
        rep movsb
        mov esi, sett_snap_arena
        mov edi, sett_arena
        mov ecx, [sett_arena_used]
        rep movsb
        mov dword [sett_txn_depth], 0
        popad
        ret

; Drop the snapshot without writing anything.
sett_txn_end_internal:
        mov dword [sett_txn_depth], 0
        ret

;=======================================================================
; SECRET values
;=======================================================================

; Build the credential-store name for a key path plus value name: "S" and the
; CRC-32C of each folded together, in hex. Sixteen bytes, which is what the
; store keeps.
;   In : ESI = key path, EDI = value name
;   Out: sett_secname filled, ESI = sett_secname
sett_secret_name:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        mov [.ssn_path], esi
        mov [.ssn_name], edi

        mov eax, [.ssn_path]
        xor ecx, ecx
.ssn_len1:
        cmp byte [eax + ecx], 0
        je .ssn_crc1
        inc ecx
        jmp .ssn_len1
.ssn_crc1:
        call crc32c
        mov [.ssn_sum], eax

        mov eax, [.ssn_name]
        xor ecx, ecx
.ssn_len2:
        cmp byte [eax + ecx], 0
        je .ssn_crc2
        inc ecx
        jmp .ssn_len2
.ssn_crc2:
        call crc32c
        xor eax, [.ssn_sum]

        mov byte [sett_secname], 'S'
        mov ecx, 8
        lea edx, [sett_secname + 8]
.ssn_hex:
        mov ebx, eax
        and ebx, 0x0F
        cmp bl, 10
        jb .ssn_digit
        add bl, 'A' - 10
        jmp .ssn_store
.ssn_digit:
        add bl, '0'
.ssn_store:
        mov [edx], bl
        dec edx
        shr eax, 4
        dec ecx
        jnz .ssn_hex
        mov byte [sett_secname + 9], 0
        mov esi, sett_secname
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret
.ssn_path: dd 0
.ssn_name: dd 0
.ssn_sum:  dd 0

; EAX = key index, ESI = 64 hex characters, EDI = value name
; Stores the secret in the credential store and the entry's name in the tree.
sett_secret_store:
        pushad
        mov [.sst_key], eax
        mov [.sst_hex], esi
        mov [.sst_name], edi
        ; A value that is already a SECRET replaces its store entry.
        mov eax, [.sst_key]
        mov esi, [.sst_name]
        call sett_value_find
        jc .sst_new
        call sett_val_base
        cmp dword [ebx + 4], SETT_T_SECRET
        jne .sst_new
        mov esi, sett_arena
        add esi, [ebx + 8]
        call key_del
.sst_new:
        mov eax, [.sst_key]
        call sett_key_base
        lea esi, [ebx + 4]
        mov edi, [.sst_name]
        call sett_secret_name            ; ESI = the store name
        mov [.sst_store], esi
        call key_del                     ; a collision is replaced, not trusted
        mov esi, [.sst_store]
        mov edi, [.sst_hex]
        call key_add
        jc .sst_addfail
        ; The tree records the store name, never the secret.
        mov eax, [.sst_key]
        mov esi, [.sst_name]
        mov edx, SETT_T_SECRET
        mov ecx, 16
        mov ebx, [.sst_store]
        call sett_value_set
        jc .sst_setfail
        popad
        clc
        ret
.sst_addfail:
        mov esi, msg_sx_secadd
        call vga_print
        popad
        stc
        ret
.sst_setfail:
        mov esi, msg_sx_secset
        call vga_print
        popad
        stc
        ret
.sst_fail:
        popad
        stc
        ret
.sst_key:   dd 0
.sst_hex:   dd 0
.sst_name:  dd 0
.sst_store: dd 0

;=======================================================================
; Image validation (used by hive load)
;=======================================================================

; Validate the image in sett_image with length sett_image_len.
;   Out: CF set when it is not a Sett image; otherwise sett_hive_hdr/keys/vals/
;        arena describe it.
sett_image_check:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        cmp dword [sett_image_len], SETT_HEADER_V1
        jb .sic_no
        cmp dword [sett_image], SETT_MAGIC
        jne .sic_no
        mov eax, [sett_image + 4]
        cmp eax, SETT_VERSION
        je .sic_v2
        cmp eax, SETT_VERSION_1
        jne .sic_no
        mov dword [sett_hive_hdr], SETT_HEADER_V1
        jmp .sic_header
.sic_v2:
        mov dword [sett_hive_hdr], SETT_HEADER
        cmp dword [sett_image_len], SETT_HEADER
        jb .sic_no
        mov eax, [sett_image + SETT_HDR_SUM]
        mov [.sic_sum], eax
        mov dword [sett_image + SETT_HDR_SUM], 0
        mov eax, sett_image
        mov ecx, [sett_image_len]
        call crc32c
        mov edx, [sett_image + SETT_HDR_SUM]
        mov [sett_image + SETT_HDR_SUM], edx
        cmp eax, [.sic_sum]
        jne .sic_no
.sic_header:
        mov eax, [sett_image + SETT_HDR_KEYS]
        cmp eax, SETT_MAX_KEYS
        ja .sic_no
        mov [sett_hive_keys], eax
        mov eax, [sett_image + SETT_HDR_VALS]
        cmp eax, SETT_MAX_VALUES
        ja .sic_no
        mov [sett_hive_vals], eax
        mov eax, [sett_image + SETT_HDR_ARENA]
        cmp eax, SETT_ARENA
        ja .sic_no
        mov [sett_hive_arena], eax

        mov eax, [sett_hive_keys]
        imul eax, eax, SETT_KEY_ENTRY
        add eax, [sett_hive_hdr]
        mov edx, [sett_hive_vals]
        imul edx, edx, SETT_VAL_ENTRY
        add eax, edx
        add eax, [sett_hive_arena]
        cmp eax, [sett_image_len]
        jne .sic_no

        xor ecx, ecx
.sic_key_loop:
        cmp ecx, [sett_hive_keys]
        jae .sic_val_start
        mov eax, ecx
        imul eax, eax, SETT_KEY_ENTRY
        add eax, [sett_hive_hdr]
        add eax, sett_image
        cmp byte [eax + 4 + SETT_KEY_PATH - 1], 0
        jne .sic_no
        mov edx, [eax]
        cmp edx, -1
        je .sic_key_next
        cmp edx, [sett_hive_keys]
        jae .sic_no
.sic_key_next:
        inc ecx
        jmp .sic_key_loop

.sic_val_start:
        xor ecx, ecx
.sic_val_loop:
        cmp ecx, [sett_hive_vals]
        jae .sic_yes
        mov eax, [sett_hive_keys]
        imul eax, eax, SETT_KEY_ENTRY
        add eax, [sett_hive_hdr]
        mov edx, ecx
        imul edx, edx, SETT_VAL_ENTRY
        add eax, edx
        add eax, sett_image
        mov edx, [eax]
        cmp edx, [sett_hive_keys]
        jae .sic_no
        mov edx, [eax + 4]
        cmp edx, SETT_T_U32
        jb .sic_no
        cmp edx, SETT_T_SECRET
        ja .sic_no
        mov edx, [eax + 8]
        add edx, [eax + 12]
        jc .sic_no
        cmp edx, [sett_hive_arena]
        ja .sic_no
        cmp byte [eax + 16 + SETT_VAL_NAME - 1], 0
        jne .sic_no
        inc ecx
        jmp .sic_val_loop
.sic_yes:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        clc
        ret
.sic_no:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        stc
        ret
.sic_sum: dd 0

;=======================================================================
; Hives: a subtree in its own file, merged back
;=======================================================================

; ESI = prefix (as typed), EDI = file name -> CF set on failure
sett_hive_save:
        pushad
        mov [.hsv_file], edi
        call sett_path_norm
        jc .hsv_fail
        lea esi, [sett_pathbuf]
        lea edi, [sett_hive_path]
        call str_copy
        lea esi, [sett_hive_path]
        call sett_ci_len
        mov [.hsv_plen], eax

        mov dword [.hsv_nk], 0
        mov dword [.hsv_nv], 0
        mov dword [.hsv_arena], 0
        mov dword [.hsv_i], 0

        ; Keys: include the prefix and everything below it, remapping parents
        ; into the smaller table as they are copied.
.hsv_key_loop:
        mov ecx, [.hsv_i]
        cmp ecx, [sett_key_count]
        jae .hsv_keys_done
        mov eax, ecx
        call sett_key_base
        lea esi, [ebx + 4]
        lea edi, [sett_hive_path]
        call sett_ci_equal
        jc .hsv_include
        ; The root hive is the whole tree; any other path needs the prefix and
        ; a separator after it, so "\A\B2" is not inside "\A\B".
        cmp dword [.hsv_plen], 1
        jbe .hsv_include
        lea esi, [ebx + 4]
        lea edi, [sett_hive_path]
        call sett_prefix
        jnc .hsv_exclude
        mov eax, [.hsv_plen]
        cmp byte [ebx + 4 + eax], '\'
        jne .hsv_exclude
.hsv_include:
        mov ecx, [.hsv_i]
        mov byte [sett_hive_keep + ecx], 1
        mov eax, [.hsv_nk]
        mov [sett_hive_map + ecx*4], eax
        ; copy the entry into the hive table
        mov eax, ecx
        call sett_key_base
        mov esi, ebx
        mov eax, [.hsv_nk]
        imul eax, eax, SETT_KEY_ENTRY
        add eax, sett_hive_keys_buf
        mov edi, eax
        mov ecx, SETT_KEY_ENTRY
        cld
        rep movsb
        ; the parent must land in the hive table too
        mov eax, [.hsv_nk]
        imul eax, eax, SETT_KEY_ENTRY
        add eax, sett_hive_keys_buf
        mov edx, [eax]
        cmp edx, -1
        je .hsv_parent_root
        mov edx, [sett_hive_map + edx*4]
        mov [eax], edx
        jmp .hsv_parent_done
.hsv_parent_root:
        mov dword [eax], -1
.hsv_parent_done:
        inc dword [.hsv_nk]
        jmp .hsv_next
.hsv_exclude:
        mov ecx, [.hsv_i]
        mov byte [sett_hive_keep + ecx], 0
.hsv_next:
        inc dword [.hsv_i]
        jmp .hsv_key_loop

.hsv_keys_done:
        mov dword [.hsv_i], 0
.hsv_val_loop:
        mov ecx, [.hsv_i]
        cmp ecx, [sett_val_count]
        jae .hsv_vals_done
        mov eax, ecx
        call sett_val_base
        mov [.hsv_entry], ebx
        mov eax, [ebx]                   ; the key it belongs to
        cmp byte [sett_hive_keep + eax], 0
        je .hsv_val_next
        mov eax, [sett_hive_map + eax*4]
        mov [.hsv_newkey], eax
        ; copy the entry, then point it at the hive's key and arena
        mov esi, [.hsv_entry]
        mov eax, [.hsv_nv]
        imul eax, eax, SETT_VAL_ENTRY
        add eax, sett_hive_vals_buf
        mov [.hsv_newent], eax
        mov edi, eax
        mov ecx, SETT_VAL_ENTRY
        cld
        rep movsb
        mov edi, [.hsv_newent]
        mov eax, [.hsv_newkey]
        mov [edi], eax
        mov eax, [.hsv_arena]
        mov [edi + 8], eax
        ; copy the bytes
        mov esi, [.hsv_entry]
        mov edx, sett_arena
        add edx, [esi + 8]
        mov esi, edx
        mov edi, sett_hive_arena_buf
        add edi, [.hsv_arena]
        mov ecx, [.hsv_entry]
        mov ecx, [ecx + 12]
        cld
        rep movsb
        mov ecx, [.hsv_entry]
        mov eax, [ecx + 12]
        add [.hsv_arena], eax
        inc dword [.hsv_nv]
.hsv_val_next:
        inc dword [.hsv_i]
        jmp .hsv_val_loop

.hsv_vals_done:
        ; Assemble the image: header, keys, values, arena.
        mov edi, sett_image
        mov eax, SETT_MAGIC
        stosd
        mov eax, SETT_VERSION
        stosd
        mov eax, [.hsv_nk]
        stosd
        mov eax, [.hsv_nv]
        stosd
        mov eax, [.hsv_arena]
        stosd
        xor eax, eax
        stosd                            ; a hive has no generation of its own
        stosd                            ; checksum

        mov esi, sett_hive_keys_buf
        mov ecx, [.hsv_nk]
        imul ecx, ecx, SETT_KEY_ENTRY
        cld
        rep movsb
        mov esi, sett_hive_vals_buf
        mov ecx, [.hsv_nv]
        imul ecx, ecx, SETT_VAL_ENTRY
        rep movsb
        mov esi, sett_hive_arena_buf
        mov ecx, [.hsv_arena]
        rep movsb

        mov ecx, edi
        sub ecx, sett_image
        mov [sett_image_len], ecx
        mov eax, sett_image
        call crc32c
        mov [sett_image + SETT_HDR_SUM], eax

        call file_save_cwd
        call sett_pin_root
        mov esi, [.hsv_file]
        mov ecx, [sett_image_len]
        call sett_write_named_pinned
        jc .hsv_io_fail
        call file_restore_cwd
        popad
        clc
        ret
.hsv_io_fail:
        call file_restore_cwd
.hsv_fail:
        popad
        stc
        ret
.hsv_file:   dd 0
.hsv_plen:   dd 0
.hsv_nk:     dd 0
.hsv_nv:     dd 0
.hsv_arena:  dd 0
.hsv_i:      dd 0
.hsv_entry:  dd 0
.hsv_newent: dd 0
.hsv_newkey: dd 0

; ESI = file name -> CF set on failure. The keys keep their full paths; a hive
; merges into the tree and its keys must already be inside it.
sett_hive_load:
        pushad
        mov [.hld_file], esi
        call file_save_cwd
        call sett_pin_root
        mov esi, [.hld_file]
        mov ecx, FD_FLAG_READ
        call fd_open
        cmp eax, -1
        je .hld_restore_fail
        mov [.hld_fd], eax
        mov ebx, eax
        mov ecx, sett_image
        mov edx, SETT_IMAGE_SIZE
        call fd_read
        mov [sett_image_len], eax
        mov ebx, [.hld_fd]
        call fd_close
        call file_restore_cwd

        call sett_image_check
        jc .hld_fail

        mov dword [.hld_began], 0
        cmp dword [sett_txn_depth], 0
        jne .hld_txn
        call sett_txn_begin_internal
        mov dword [.hld_began], 1
.hld_txn:
        mov dword [.hld_i], 0
.hld_key_loop:
        mov ecx, [.hld_i]
        cmp ecx, [sett_hive_keys]
        jae .hld_val_start
        mov eax, ecx
        imul eax, eax, SETT_KEY_ENTRY
        add eax, [sett_hive_hdr]
        add eax, sett_image
        lea esi, [eax + 4]
        call sett_ensure_path
        jc .hld_abort
        inc dword [.hld_i]
        jmp .hld_key_loop

.hld_val_start:
        mov dword [.hld_i], 0
.hld_val_loop:
        mov ecx, [.hld_i]
        cmp ecx, [sett_hive_vals]
        jae .hld_ok
        ; the value entry in the image
        mov eax, [sett_hive_keys]
        imul eax, eax, SETT_KEY_ENTRY
        add eax, [sett_hive_hdr]
        mov edx, ecx
        imul edx, edx, SETT_VAL_ENTRY
        add eax, edx
        add eax, sett_image
        mov [.hld_entry], eax
        ; its key, by path
        mov edx, [eax]
        imul edx, edx, SETT_KEY_ENTRY
        add edx, [sett_hive_hdr]
        add edx, sett_image
        lea esi, [edx + 4]
        call sett_key_find
        jc .hld_abort
        mov [.hld_key], eax
        ; its bytes
        mov edx, [sett_hive_keys]
        imul edx, edx, SETT_KEY_ENTRY
        add edx, [sett_hive_hdr]
        mov ebx, [sett_hive_vals]
        imul ebx, ebx, SETT_VAL_ENTRY
        add edx, ebx
        mov ebx, [.hld_entry]
        add edx, [ebx + 8]
        add edx, sett_image
        mov [.hld_data], edx
        ; set it: EAX=key, ESI=name, EDX=type, ECX=len, EBX=data
        mov eax, [.hld_key]
        lea esi, [ebx + 16]
        mov edx, [ebx + 4]
        mov ecx, [ebx + 12]
        mov ebx, [.hld_data]
        call sett_value_set
        jc .hld_abort
        inc dword [.hld_i]
        jmp .hld_val_loop

.hld_ok:
        cmp dword [.hld_began], 0
        je .hld_out
        call sett_txn_end_internal
        jmp .hld_out
.hld_abort:
        cmp dword [.hld_began], 0
        je .hld_fail
        call sett_txn_abort_internal
.hld_fail:
        popad
        stc
        ret
.hld_restore_fail:
        call file_restore_cwd
        jmp .hld_fail
.hld_out:
        popad
        clc
        ret
.hld_file:  dd 0
.hld_fd:    dd 0
.hld_i:     dd 0
.hld_began: dd 0
.hld_entry: dd 0
.hld_key:   dd 0
.hld_data:  dd 0

;=======================================================================
; Text export and import: path|type|name|value
;=======================================================================

; Print one value in the export format. EBX = value entry, ESI = key path.
sett_export_value:
        pushad
        mov [.sev_ent], ebx
        mov [.sev_path], esi
        mov esi, [.sev_path]
        call vga_print
        mov al, '|'
        call vga_putchar
        mov ebx, [.sev_ent]
        mov eax, [ebx + 4]
        cmp eax, SETT_T_U32
        je .sev_u32
        cmp eax, SETT_T_STRING
        je .sev_str
        cmp eax, SETT_T_SECRET
        je .sev_secret
        mov esi, sett_word_bin
        call vga_print
        jmp .sev_name
.sev_u32:
        mov esi, sett_word_u32
        call vga_print
        jmp .sev_name
.sev_str:
        mov esi, sett_word_str
        call vga_print
        jmp .sev_name
.sev_secret:
        mov esi, sett_word_secret
        call vga_print
.sev_name:
        mov al, '|'
        call vga_putchar
        mov ebx, [.sev_ent]
        lea esi, [ebx + 16]
        call vga_print
        mov al, '|'
        call vga_putchar
        mov ebx, [.sev_ent]
        mov eax, [ebx + 4]
        cmp eax, SETT_T_U32
        je .sev_du32
        cmp eax, SETT_T_STRING
        je .sev_dstr
        cmp eax, SETT_T_SECRET
        je .sev_dsecret
        mov esi, sett_arena
        add esi, [ebx + 8]
        mov ecx, [ebx + 12]
        call sett_print_hex
        jmp .sev_done
.sev_du32:
        mov esi, sett_arena
        add esi, [ebx + 8]
        mov eax, [esi]
        call vga_print_dec
        jmp .sev_done
.sev_dstr:
        mov esi, sett_arena
        add esi, [ebx + 8]
        call vga_print
        jmp .sev_done
.sev_dsecret:
        mov esi, msg_sett_secretword
        call vga_print
.sev_done:
        call vga_newline
        popad
        ret
.sev_ent:  dd 0
.sev_path: dd 0

; EDI = optional path filter (canonical) -> CF set when there are no keys
sett_export:
        pushad
        mov dword [.sex_i], 0
        mov dword [.sex_any], 0
.sex_loop:
        mov ecx, [.sex_i]
        cmp ecx, [sett_val_count]
        jae .sex_done
        mov eax, ecx
        call sett_val_base
        mov [.sex_ent], ebx
        mov eax, [ebx]
        call sett_key_base
        mov [.sex_key], ebx
        lea esi, [ebx + 4]
        lea edi, [sett_path_tok]
        call sett_prefix
        jnc .sex_next
        lea esi, [ebx + 4]
        mov ebx, [.sex_ent]
        call sett_export_value
        mov dword [.sex_any], 1
.sex_next:
        inc dword [.sex_i]
        jmp .sex_loop
.sex_done:
        cmp dword [.sex_any], 0
        jne .sex_ok
        stc
        jmp .sex_out
.sex_ok:
        clc
.sex_out:
        popad
        ret
.sex_i:   dd 0
.sex_any: dd 0
.sex_ent: dd 0
.sex_key: dd 0

; Parse "path|type|name|value" out of the line at ESI, in place.
;   Out: CF set when the line is not usable; otherwise the four fields are
;        NUL-terminated in the caller's buffer and sett_lineptr points at the
;        value. sett_expline type word is left in sett_hexword.
sett_import_line:
        pushad
        mov [.sil_p], esi
        ; path
        mov edi, sett_path_tok
        mov ecx, SETT_KEY_PATH - 1
.sil_path:
        mov al, [esi]
        test al, al
        jz .sil_bad
        cmp al, '|'
        je .sil_path_done
        mov [edi], al
        inc edi
        inc esi
        dec ecx
        jnz .sil_path
        jmp .sil_bad
.sil_path_done:
        mov byte [edi], 0
        inc esi
        ; type
        mov edi, sett_hexword
        mov ecx, 15
.sil_type:
        mov al, [esi]
        test al, al
        jz .sil_bad
        cmp al, '|'
        je .sil_type_done
        mov [edi], al
        inc edi
        inc esi
        dec ecx
        jnz .sil_type
        jmp .sil_bad
.sil_type_done:
        mov byte [edi], 0
        inc esi
        ; name
        mov edi, sett_name_tok
        mov ecx, SETT_VAL_NAME - 1
.sil_name:
        mov al, [esi]
        test al, al
        jz .sil_bad
        cmp al, '|'
        je .sil_name_done
        mov [edi], al
        inc edi
        inc esi
        dec ecx
        jnz .sil_name
        jmp .sil_bad
.sil_name_done:
        mov byte [edi], 0
        inc esi
        mov [sett_lineptr], esi
        popad
        clc
        ret
.sil_bad:
        popad
        stc
        ret
.sil_p: dd 0

; ESI = file name -> CF set when the file could not be read or a line was bad
sett_import:
        pushad
        mov [.sim_file], esi
        call file_save_cwd
        call sett_pin_root
        mov esi, [.sim_file]
        mov ecx, FD_FLAG_READ
        call fd_open
        cmp eax, -1
        je .sim_restore_fail
        mov [.sim_fd], eax
        mov ebx, eax
        mov ecx, sett_image
        mov edx, SETT_IMAGE_SIZE - 1
        call fd_read
        mov [.sim_len], eax
        mov ebx, [.sim_fd]
        call fd_close
        call file_restore_cwd
        mov esi, sett_image
        mov eax, [.sim_len]
        mov byte [esi + eax], 0
        mov [.sim_p], esi
        mov dword [.sim_lines], 0
.sim_line:
        mov esi, [.sim_p]
        cmp byte [esi], 0
        je .sim_done
        ; find the end of the line
        mov edi, esi
.sim_find_eol:
        mov al, [edi]
        test al, al
        jz .sim_eol
        cmp al, 0x0A
        je .sim_eol
        inc edi
        jmp .sim_find_eol
.sim_eol:
        mov [.sim_next], edi
        cmp byte [edi], 0
        je .sim_no_advance
        mov byte [edi], 0
        inc dword [.sim_next]
.sim_no_advance:
        ; strip a CR before the newline
        cmp edi, esi
        jbe .sim_parse
        cmp byte [edi - 1], 0x0D
        jne .sim_parse
        mov byte [edi - 1], 0
.sim_parse:
        cmp byte [esi], 0
        je .sim_advance
        mov esi, [.sim_p]
        call sett_import_line
        jc .sim_bad
        ; the type
        mov esi, sett_hexword
        mov edi, sett_word_u32
        call sett_ci_equal
        jc .sim_u32
        mov esi, sett_hexword
        mov edi, sett_word_str
        call sett_ci_equal
        jc .sim_str
        mov esi, sett_hexword
        mov edi, sett_word_bin
        call sett_ci_equal
        jc .sim_bin
        jmp .sim_bad
.sim_u32:
        mov esi, [sett_lineptr]
        call parse_decimal
        mov [sett_u32buf], eax
        mov dword [sett_tmp_type], SETT_T_U32
        mov dword [sett_tmp_len], 4
        mov dword [sett_tmp_data], sett_u32buf
        jmp .sim_store
.sim_str:
        mov esi, [sett_lineptr]
        xor ecx, ecx
.sim_strlen:
        cmp byte [esi + ecx], 0
        je .sim_strdone
        inc ecx
        jmp .sim_strlen
.sim_strdone:
        inc ecx
        mov dword [sett_tmp_type], SETT_T_STRING
        mov [sett_tmp_len], ecx
        mov [sett_tmp_data], esi
        jmp .sim_store
.sim_bin:
        mov esi, [sett_lineptr]
        mov edi, sett_val_tok
        mov edx, 159
        call sett_hex
        jc .sim_bad
        test ecx, ecx
        jz .sim_bad
        mov dword [sett_tmp_type], SETT_T_BINARY
        mov [sett_tmp_len], ecx
        mov dword [sett_tmp_data], sett_val_tok
.sim_store:
        lea esi, [sett_path_tok]
        call sett_ensure_path
        jc .sim_bad
        mov edx, [sett_tmp_type]
        mov ecx, [sett_tmp_len]
        mov ebx, [sett_tmp_data]
        lea esi, [sett_name_tok]
        call sett_value_set
        jc .sim_bad
        inc dword [.sim_lines]
.sim_advance:
        mov eax, [.sim_next]
        mov [.sim_p], eax
        jmp .sim_line
.sim_done:
        popad
        clc
        ret
.sim_restore_fail:
        call file_restore_cwd
.sim_bad:
        popad
        stc
        ret
.sim_file:  dd 0
.sim_fd:    dd 0
.sim_len:   dd 0
.sim_p:     dd 0
.sim_next:  dd 0
.sim_lines: dd 0

;=======================================================================
; Event and watch printing
;=======================================================================

sett_op_names:
        dd sett_op_key, sett_op_setword, sett_op_setword, sett_op_delword
        dd sett_op_delkeyword, sett_op_stateword
sett_op_key:      db "key", 0
sett_op_setword:  db "set", 0
sett_op_delword:  db "del", 0
sett_op_delkeyword: db "delkey", 0
sett_op_stateword: db "state", 0

; Print and consume the event ring.
sett_events_show:
        pushad
        cmp dword [sett_ev_count], 0
        jne .ses_some
        mov esi, msg_sett_noevents
        call vga_print
        jmp .ses_done
.ses_some:
        mov eax, [sett_ev_head]
        sub eax, [sett_ev_count]
        jnc .ses_start
        add eax, SETT_EV_MAX
.ses_start:
        mov [.ses_i], eax
        mov dword [.ses_n], 0
.ses_loop:
        mov eax, [.ses_n]
        cmp eax, [sett_ev_count]
        jae .ses_reset
        mov eax, [.ses_i]
        imul eax, SETT_EV_ENTRY
        add eax, sett_ev_ring
        mov [.ses_slot], eax
        mov al, ' '
        call vga_putchar
        call vga_putchar
        mov eax, [.ses_slot]
        mov eax, [eax]
        cmp eax, SETT_EV_MAX
        jbe .ses_op_ok
        xor eax, eax
.ses_op_ok:
        dec eax
        mov ebx, [sett_op_names + eax*4]
        mov esi, ebx
        call vga_print
        mov al, ' '
        call vga_putchar
        mov eax, [.ses_slot]
        lea esi, [eax + SETT_EV_PATH]
        call vga_print
        mov eax, [.ses_slot]
        cmp byte [eax + SETT_EV_NAME], 0
        je .ses_no_name
        mov al, ' '
        call vga_putchar
        mov eax, [.ses_slot]
        lea esi, [eax + SETT_EV_NAME]
        call vga_print
.ses_no_name:
        mov eax, [.ses_slot]
        mov eax, [eax + 4]
        cmp eax, 1
        jbe .ses_no_count
        mov esi, msg_sett_times
        call vga_print
        mov eax, [.ses_slot]
        mov eax, [eax + 4]
        call vga_print_dec
.ses_no_count:
        call vga_newline
        inc dword [.ses_n]
        mov eax, [.ses_i]
        inc eax
        cmp eax, SETT_EV_MAX
        jb .ses_head_ok
        xor eax, eax
.ses_head_ok:
        mov [.ses_i], eax
        jmp .ses_loop
.ses_reset:
        mov dword [sett_ev_count], 0
        mov dword [sett_ev_head], 0
.ses_done:
        popad
        ret
.ses_i:    dd 0
.ses_n:    dd 0
.ses_slot: dd 0

;=======================================================================
; sett_ext_command - the verbs sett.inc does not have
;   Called with the whole `sett ...` line still in line_buffer.
;=======================================================================
sett_ext_command:
        pushad
        ; sett_args leaves the verb in sett_tok and ESI on its first argument;
        ; the compares below use ESI, so the argument pointer is kept here.
        call sett_args
        mov [sett_argp], esi

        mov esi, sett_tok
        mov edi, sx_word_begin
        call sett_ci_equal
        jc .begin
        mov esi, sett_tok
        mov edi, sx_word_commit
        call sett_ci_equal
        jc .commit
        mov esi, sett_tok
        mov edi, sx_word_abort
        call sett_ci_equal
        jc .abort
        mov esi, sett_tok
        mov edi, sx_word_rollback
        call sett_ci_equal
        jc .rollback
        mov esi, sett_tok
        mov edi, sx_word_watch
        call sett_ci_equal
        jc .watch
        mov esi, sett_tok
        mov edi, sx_word_events
        call sett_ci_equal
        jc .events
        mov esi, sett_tok
        mov edi, sx_word_acl
        call sett_ci_equal
        jc .acl
        mov esi, sett_tok
        mov edi, sx_word_hive
        call sett_ci_equal
        jc .hive
        mov esi, sett_tok
        mov edi, sx_word_export
        call sett_ci_equal
        jc .export
        mov esi, sett_tok
        mov edi, sx_word_import
        call sett_ci_equal
        jc .import
        mov esi, sett_tok
        mov edi, sx_word_gen
        call sett_ci_equal
        jc .gen
        jmp .usage

;--- begin -------------------------------------------------------------
.begin:
        call sett_txn_begin_internal
        mov esi, msg_sett_txnopen
        call vga_print
        jmp .done

;--- commit ------------------------------------------------------------
.commit:
        cmp dword [sett_txn_depth], 0
        je .no_txn
        ; The write happens before the snapshot is dropped: a failed commit
        ; leaves the transaction open and abortable.
        call sett_save
        jc .io
        call sett_txn_end_internal
        mov esi, msg_sett_committed
        call vga_print
        mov eax, [sett_generation]
        call vga_print_dec
        call vga_newline
        jmp .done

;--- abort -------------------------------------------------------------
.abort:
        cmp dword [sett_txn_depth], 0
        je .no_txn
        call sett_txn_abort_internal
        mov esi, msg_sett_aborted
        call vga_print
        jmp .done

;--- rollback ----------------------------------------------------------
.rollback:
        mov eax, [sett_load_name]
        mov [.prev_name], eax
        mov dword [sett_load_name], sett_backup_name
        call sett_load
        pushfd
        mov eax, [.prev_name]
        mov [sett_load_name], eax
        popfd
        jc .nobak
        mov esi, msg_sett_rolledback
        call vga_print
        jmp .done
.nobak:
        mov esi, msg_sett_nobak
        call vga_print
        jmp .done

;--- watch -------------------------------------------------------------
.watch:
        mov esi, [sett_argp]
        mov edi, sett_path_tok
        mov edx, SETT_KEY_PATH - 1
        call sett_word
        jc .usage
        lea esi, [sett_path_tok]
        call sett_path_norm
        jc .usage
        cmp dword [sett_watch_count], SETT_WATCH_MAX
        jae .watch_full
        mov eax, [sett_watch_count]
        imul eax, SETT_KEY_PATH
        add eax, sett_watches
        mov edi, eax
        lea esi, [sett_pathbuf]
        call str_copy
        inc dword [sett_watch_count]
        ; A watcher created after a change still learns the current state: the
        ; registration emits it.
        mov al, SETT_OP_STATE
        lea esi, [sett_pathbuf]
        mov edi, sett_empty_name
        call sett_notify
        mov esi, msg_sett_watching
        call vga_print
        lea esi, [sett_pathbuf]
        call vga_print
        call vga_newline
        jmp .done
.watch_full:
        mov esi, msg_sett_watchfull
        call vga_print
        jmp .done

;--- events ------------------------------------------------------------
.events:
        call sett_events_show
        jmp .done

;--- acl ---------------------------------------------------------------
.acl:
        mov esi, [sett_argp]
        mov edi, sett_path_tok
        mov edx, SETT_KEY_PATH - 1
        call sett_word
        jc .usage
        ; Where the path word ended: the normalise and copy below use ESI/EDI.
        mov [.acl_next], esi
        lea esi, [sett_path_tok]
        call sett_path_norm
        jc .usage
        lea esi, [sett_pathbuf]
        lea edi, [sett_path_tok]
        call str_copy
        lea esi, [sett_path_tok]
        call sett_key_find
        jc .nokey
        mov [.acl_key], eax
        ; an optional owner and mode, read from where the path word ended
        mov esi, [.acl_next]
        mov edi, sett_val_tok
        mov edx, 15
        call sett_word
        jc .acl_show
        ; The token buffer is not the line: keep the line cursor for the next
        ; word and parse the token from its own buffer.
        mov [.acl_next2], esi
        lea esi, [sett_val_tok]
        call parse_decimal
        mov [.acl_owner], eax
        mov esi, [.acl_next2]
        mov edi, sett_hexword
        mov edx, 8
        call sett_word
        jc .usage
        lea esi, [sett_hexword]
        mov edi, sett_val_tok
        mov edx, 1
        call sett_hex
        jc .badval
        cmp ecx, 1
        jne .badval
        movzx eax, byte [sett_val_tok]
        and eax, 0x0F
        shl eax, 24
        or eax, [.acl_owner]
        mov edx, eax                     ; the (mode << 24) | owner word
        mov eax, [.acl_key]
        call sett_key_base
        mov [ebx + SETT_KEY_SPARE], edx
        mov esi, msg_sett_aclset
        call vga_print
        jmp .done
.acl_show:
        mov eax, [.acl_key]
        call sett_acl_word
        mov [.acl_word], eax
        lea esi, [sett_path_tok]
        call vga_print
        mov esi, msg_sett_owner
        call vga_print
        mov eax, [.acl_word]
        and eax, 0x00FFFFFF
        call vga_print_dec
        mov esi, msg_sett_mode
        call vga_print
        mov eax, [.acl_word]
        shr eax, 24
        push eax
        shr al, 4
        call sett_put_hex
        pop eax
        call sett_put_hex
        call vga_newline
        jmp .done

;--- hive --------------------------------------------------------------
.hive:
        mov esi, [sett_argp]
        mov edi, sett_hive_path          ; reused as the sub-verb token
        mov edx, 15
        call sett_word
        jc .usage
        mov [.argp], esi
        mov esi, sett_hive_path
        mov edi, sett_word_save
        call sett_ci_equal
        jc .hive_save
        mov esi, sett_hive_path
        mov edi, sett_word_load
        call sett_ci_equal
        jc .hive_load
        jmp .usage
.hive_save:
        mov esi, [.argp]
        lea edi, [sett_path_tok]
        mov edx, SETT_KEY_PATH - 1
        call sett_word
        jc .usage
        lea edi, [sett_hive_file]
        mov edx, 95
        call sett_word
        jc .usage
        lea esi, [sett_path_tok]
        lea edi, [sett_hive_file]
        call sett_hive_save
        jc .hivefail
        mov esi, msg_sett_hivesaved
        call vga_print
        jmp .done
.hive_load:
        mov esi, [.argp]
        lea edi, [sett_hive_file]
        mov edx, 95
        call sett_word
        jc .usage
        lea esi, [sett_hive_file]
        call sett_hive_load
        jc .hivefail
        mov esi, msg_sett_hiveloaded
        call vga_print
        jmp .done
.hivefail:
        mov esi, msg_sett_hivefail
        call vga_print
        jmp .done

;--- export ------------------------------------------------------------
.export:
        mov byte [sett_path_tok], '\'
        mov byte [sett_path_tok + 1], 0
        mov esi, [sett_argp]
        mov edi, sett_val_tok
        mov edx, SETT_KEY_PATH - 1
        call sett_word
        jc .export_go
        lea esi, [sett_val_tok]
        call sett_path_norm
        jc .usage
        lea esi, [sett_pathbuf]
        lea edi, [sett_path_tok]
        call str_copy
.export_go:
        lea edi, [sett_path_tok]
        call sett_export
        jc .noval
        jmp .done

;--- import ------------------------------------------------------------
.import:
        mov esi, [sett_argp]
        mov edi, sett_hive_file
        mov edx, 95
        call sett_word
        jc .usage
        lea esi, [sett_hive_file]
        call sett_import
        jc .importfail
        mov esi, msg_sett_imported
        call vga_print
        jmp .done
.importfail:
        mov esi, msg_sett_importfail
        call vga_print
        jmp .done

;--- gen ---------------------------------------------------------------
.gen:
        mov esi, msg_sett_gen
        call vga_print
        mov eax, [sett_generation]
        call vga_print_dec
        call vga_newline
        jmp .done

;--- messages ----------------------------------------------------------
.nokey:
        mov esi, msg_sett_nokey
        call vga_print
        jmp .done
.noval:
        mov esi, msg_sett_noval
        call vga_print
        jmp .done
.no_txn:
        mov esi, msg_sett_notxn
        call vga_print
        jmp .done
.io:
        mov esi, msg_sett_iowrite
        call vga_print
        jmp .done
.badval:
        mov esi, msg_sx_aclbad
        call vga_print
        jmp .done
.usage:
        mov esi, msg_sett_usage
        call vga_print
.done:
        popad
        ret
.prev_name: dd 0
.acl_key:   dd 0
.acl_owner: dd 0
.acl_word:  dd 0
.acl_next:  dd 0
.acl_next2: dd 0
.argp:      dd 0

sx_word_begin:    db "begin", 0
sx_word_commit:   db "commit", 0
sx_word_abort:    db "abort", 0
sx_word_rollback: db "rollback", 0
sx_word_watch:    db "watch", 0
sx_word_events:   db "events", 0
sx_word_acl:      db "acl", 0
sx_word_hive:     db "hive", 0
sx_word_export:   db "export", 0
sx_word_import:   db "import", 0
sx_word_gen:      db "gen", 0

msg_sett_txnopen:    db "sett: transaction open", 0x0A, 0
msg_sett_committed:  db "sett: committed, generation ", 0
msg_sett_aborted:    db "sett: changes discarded", 0x0A, 0
msg_sett_rolledback: db "sett: rolled back to the last known good generation", 0x0A, 0
msg_sett_nobak:      db "sett: no last-known-good generation to roll back to", 0x0A, 0
msg_sett_watching:   db "sett: watching ", 0
msg_sett_watchfull:  db "sett: no free watch slots", 0x0A, 0
msg_sett_aclset:     db "sett: access set", 0x0A, 0
msg_sett_owner:      db " owner ", 0
msg_sett_mode:       db " mode 0x", 0
msg_sett_hivesaved:  db "sett: hive written", 0x0A, 0
msg_sett_hiveloaded: db "sett: hive loaded", 0x0A, 0
msg_sett_hivefail:   db "sett: the hive file was refused", 0x0A, 0
msg_sett_imported:   db "sett: settings imported", 0x0A, 0
msg_sett_importfail: db "sett: the text file was refused", 0x0A, 0
msg_sett_gen:        db "sett: generation ", 0
msg_sett_noevents:   db "sett: no events", 0x0A, 0
msg_sett_times:      db " x", 0
msg_sett_notxn:      db "sett: no transaction is open", 0x0A, 0
msg_sx_aclbad:       db "sett: acl: the mode must be two hex digits", 0x0A, 0
msg_sx_secadd:       db "sett: secret: the credential store refused it", 0x0A, 0
msg_sx_secset:       db "sett: secret: the tree refused the entry", 0x0A, 0

;-----------------------------------------------------------------------
; sett_child_name - EAX = Nth direct child of a key
;   In : EAX = ordinal, ESI = parent path, EDI = destination, EDX = room
;   Out: destination holds the component, CF set when there is no Nth child
;
; The Sett has no child list, so every consumer that needs one walks the key
; table and counts. This is that walk, once.
;-----------------------------------------------------------------------
sett_child_name:
        pushad
        mov [.schn_want], eax
        mov [.schn_dest], edi
        mov [.schn_room], edx
        mov [.schn_prefix], esi
        mov dword [.schn_seen], -1
        mov edi, [.schn_dest]
        mov byte [edi], 0

        mov esi, [.schn_prefix]
        call sett_ci_len
        mov [.schn_plen], eax

        xor ecx, ecx
.schn_loop:
        cmp ecx, [sett_key_count]
        jae .schn_done
        mov eax, ecx
        call sett_key_base
        lea esi, [ebx + 4]
        mov edi, [.schn_prefix]
        call sett_prefix
        jnc .schn_next
        mov eax, [.schn_plen]
        lea esi, [ebx + 4 + eax]
        cmp byte [esi], '\'
        jne .schn_next
        inc esi
        xor edx, edx
.schn_scan:
        cmp byte [esi + edx], 0
        je .schn_name_end
        cmp byte [esi + edx], '\'
        je .schn_next                     ; a grandchild, not a direct child
        inc edx
        cmp edx, [.schn_room]
        ja .schn_next
        jmp .schn_scan
.schn_name_end:
        inc dword [.schn_seen]
        mov eax, [.schn_seen]
        cmp eax, [.schn_want]
        jne .schn_next
        mov edi, [.schn_dest]
        mov ecx, edx
        cld
        rep movsb
        mov byte [edi], 0
        popad
        clc
        ret
.schn_next:
        inc ecx
        jmp .schn_loop
.schn_done:
        popad
        stc
        ret
.schn_want:   dd 0
.schn_dest:   dd 0
.schn_room:   dd 0
.schn_prefix: dd 0
.schn_plen:   dd 0
.schn_seen:   dd 0
