;=======================================================================
; crypt.inc - HBFS volume encryption
;
; docs/spec/STORAGE.md section 8: AES-256-XTS over 4 KiB units, a volume key
; wrapped by a key in the store, and a volume whose key is unavailable is
; refused - never mounted empty, never formatted to make it fit.
;
; On-disk: three fields in the v3 superblock's spare space, plus a feature bit
; so a reader that does not implement this refuses the volume instead of
; handing back ciphertext as if it were a file.
;
;   offset 96   crypt word   0 = plain, 1 = AES-256-XTS
;   offset 100  key check    8 bytes, AES(secret, "HBFS-CRYPT-CHECK")
;   offset 108  key name     16 bytes, the store entry the volume wants
;
; What is encrypted: every whole 4 KiB block at or above the end of the
; journal. The journal stays plaintext on purpose - a replay has to work
; before the key is available, and its pre-images are ciphertext like the
; blocks they came from. The superblock, bitmap and root directory are below
; the data area and stay readable, which is what lets the mount say "this
; volume is encrypted" instead of "this is not a volume".
;=======================================================================

HBFS_FEAT_CRYPT       equ (1 << 2)   ; volume data is AES-256-XTS
HBFS_SB_CRYPT         equ 96         ; dword: HBFS_CRYPT_NONE/AES_XTS
HBFS_SB_CRYPT_CHECK   equ 100        ; 8 bytes
HBFS_SB_CRYPT_NAME    equ 108        ; 16 bytes
HBFS_CRYPT_NONE       equ 0
HBFS_CRYPT_AES_XTS    equ 1
HBFS_CRYPT_NAME_LEN   equ 16

align 4
hbfs_crypt_on:     dd 0              ; 1 once the keys are expanded
hbfs_format_crypt: dd 0              ; set by `crypt format` for the format call
hbfs_crypt_first_lba: dd 0           ; first transformed LBA (journal end)
hbfs_locked:       dd 0              ; 1 = volume found but its key is missing
hbfs_crypt_check_tmp: times 8 db 0
hbfs_crypt_name_tmp:  times HBFS_CRYPT_NAME_LEN db 0

; The expanded keys and the AES scratch are one shared set, so anything that
; runs the cipher is serialised here. The rank sits below the block cache: a
; write holds this lock and the device write underneath it drops the cache,
; which takes the cache lock; and a read takes the cache lock first, so it must
; not take this one until it has let go of that.
align 16
blk_crypt_lock:
        dd 0                            ; LK_LOCKED
        dd RANK_BLOCKCRYPT
        dd -1
        dd blk_crypt_lock_name
blk_crypt_lock_name: db "volume cipher", 0

;-----------------------------------------------------------------------
; hbfs_crypt_load - decide whether this volume is encrypted, and get its key
;   In : nothing (hbfs_super_buf holds the superblock)
;   Out: CF clear when the volume may be mounted; CF set when it must be
;        refused, with hbfs_locked set if the reason is a missing key
;
; Called from the mount once the geometry has been validated, before any data
; block is read, so a missing key can never be papered over by a stale cache.
;-----------------------------------------------------------------------
hbfs_crypt_load:
        pushad
        mov dword [hbfs_crypt_on], 0
        mov dword [hbfs_locked], 0

        ; The first transformed LBA: the data area minus the journal.
        mov eax, HBFS_DATA_START
        add eax, HBFS_JOURNAL_SECTS
        mov [hbfs_crypt_first_lba], eax

        cmp dword [hbfs_super_buf + HBFS_SB_CRYPT], HBFS_CRYPT_NONE
        je .hcl_plain
        cmp dword [hbfs_super_buf + HBFS_SB_CRYPT], HBFS_CRYPT_AES_XTS
        jne .hcl_unknown

        ; Copy the name out: the superblock buffer is reused by other paths.
        lea esi, [hbfs_super_buf + HBFS_SB_CRYPT_NAME]
        lea edi, [hbfs_crypt_name_tmp]
        mov ecx, HBFS_CRYPT_NAME_LEN / 4
        cld
        rep movsd

        lea esi, [hbfs_crypt_name_tmp]
        call key_find
        jc .hcl_nokey

        ; The check value proves the stored secret is this volume's, not just
        ; something with the right name.
        call key_secret_of
        mov edi, hbfs_crypt_check_tmp
        call aes_key_check

        lea esi, [hbfs_crypt_check_tmp]
        lea edi, [hbfs_super_buf + HBFS_SB_CRYPT_CHECK]
        mov ecx, 8
        call hbfs_crypt_memcmp8
        jne .hcl_wrongkey

        ; Right key: expand it and let the block layer transform.
        lea esi, [hbfs_crypt_name_tmp]
        call key_find
        call key_secret_of
        call aes_setup_secret
        jc .hcl_nokey
        mov dword [hbfs_crypt_on], 1
        mov esi, msg_hbfs_crypt_open
        call vga_print
        popad
        clc
        ret

.hcl_plain:
        popad
        clc
        ret

.hcl_nokey:
        mov dword [hbfs_locked], 1
        mov esi, msg_hbfs_crypt_locked
        call vga_print
        popad
        stc
        ret

.hcl_wrongkey:
        mov dword [hbfs_locked], 1
        mov esi, msg_hbfs_crypt_wrong
        call vga_print
        popad
        stc
        ret

.hcl_unknown:
        mov esi, msg_hbfs_crypt_unknown
        call vga_print
        popad
        stc
        ret

; ZF set when the 8 bytes at ESI equal the 8 at EDI
hbfs_crypt_memcmp8:
        push ecx
        push esi
        push edi
        mov ecx, 8
.cmc_loop:
        mov al, [esi]
        cmp al, [edi]
        jne .cmc_diff
        inc esi
        inc edi
        dec ecx
        jnz .cmc_loop
        xor eax, eax
        pop edi
        pop esi
        pop ecx
        ret
.cmc_diff:
        mov eax, 1
        pop edi
        pop esi
        pop ecx
        ret

;-----------------------------------------------------------------------
; hbfs_crypt_in_region - is this whole-block request transformed?
;   In : EAX = LBA, ECX = sector count
;   Out: CF set when the request must be transformed
;
; Only whole blocks, block aligned, at or past the journal. Alignment is
; measured from the first transformed LBA, not from LBA zero: HBFS's data area
; starts at LBA 8866, so no data block is a multiple of eight in absolute
; terms, and testing the absolute LBA would never match a single block.
;
; The unit number is the data-area block index, so it is (LBA - first) / 8.
;-----------------------------------------------------------------------
hbfs_crypt_in_region:
        push eax
        push ecx
        cmp dword [hbfs_crypt_on], 0
        je .hcir_no
        test ecx, ecx
        jz .hcir_no                     ; nothing to transform, nothing to do
        ; A whole number of blocks, starting on a block boundary.
        test ecx, HBFS_SECTORS_PER_BLK - 1
        jnz .hcir_no
        cmp eax, [hbfs_crypt_first_lba]
        jb .hcir_no
        sub eax, [hbfs_crypt_first_lba]
        test eax, HBFS_SECTORS_PER_BLK - 1
        jnz .hcir_no
        pop ecx
        pop eax
        stc
        ret
.hcir_no:
        pop ecx
        pop eax
        clc
        ret

;-----------------------------------------------------------------------
; hbfs_crypt_partial - would this request read or write part of a unit?
;   In : EAX = LBA, ECX = sector count
;   Out: CF set when it touches encrypted space without covering whole blocks
;
; A partial access to a 4 KiB unit cannot be transformed without a
; read-modify-write over the whole unit, which the journal's one-sector writes
; must not become. It is refused instead, so a caller that has to be fixed
; fails loudly rather than returning ciphertext.
;-----------------------------------------------------------------------
hbfs_crypt_partial:
        push eax
        push ecx
        push edx
        cmp dword [hbfs_crypt_on], 0
        je .hcp_no
        ; A request may sit entirely below the region (the journal) or entirely
        ; inside it; one that straddles the start would write plaintext into
        ; the first encrypted unit.
        cmp eax, [hbfs_crypt_first_lba]
        jae .hcp_in
        lea edx, [eax + ecx]
        cmp edx, [hbfs_crypt_first_lba]
        ja .hcp_yes
        jmp .hcp_no
.hcp_in:
        test ecx, HBFS_SECTORS_PER_BLK - 1
        jnz .hcp_yes
        sub eax, [hbfs_crypt_first_lba]
        test eax, HBFS_SECTORS_PER_BLK - 1
        jnz .hcp_yes
.hcp_no:
        pop edx
        pop ecx
        pop eax
        clc
        ret
.hcp_yes:
        pop edx
        pop ecx
        pop eax
        stc
        ret

;-----------------------------------------------------------------------
; hbfs_crypt_encode / hbfs_crypt_decode
;   In : EAX = LBA of the first sector, ECX = sector count, EDI = buffer
;   Out: CF clear on success, CF set when the request needed transforming and
;        could not be (the keys are gone).
;
; The AES state and scratch are shared, so the cipher's lock is taken here;
; callers must not hold the block cache lock, which ranks above it.
;-----------------------------------------------------------------------
hbfs_crypt_encode:
        pushad
        mov [.hce_lba], eax
        mov [.hce_cnt], ecx
        mov [.hce_buf], edi
        call hbfs_crypt_in_region
        jnc .hce_done
        mov edi, blk_crypt_lock
        call spin_lock
        test eax, eax
        jnz .hce_fail
        mov ecx, [.hce_cnt]
        shr ecx, 3                      ; blocks
        mov [.hce_blocks], ecx
        mov ebx, [.hce_lba]
        sub ebx, [hbfs_crypt_first_lba]
        shr ebx, 3                      ; data-area block index = XTS unit
        mov esi, [.hce_buf]
.hce_loop:
        mov eax, ebx
        mov edi, esi
        xor edx, edx                    ; encrypt
        call xts_crypt_unit
        jc .hce_unlock_fail
        add ebx, 1
        add esi, HBFS_BLOCK_SIZE
        dec dword [.hce_blocks]
        jnz .hce_loop
        mov edi, blk_crypt_lock
        call spin_unlock
.hce_done:
        popad
        clc
        ret
.hce_unlock_fail:
        mov edi, blk_crypt_lock
        call spin_unlock
.hce_fail:
        popad
        stc
        ret
.hce_lba:    dd 0
.hce_cnt:    dd 0
.hce_buf:    dd 0
.hce_blocks: dd 0

hbfs_crypt_decode:
        pushad
        mov [.hcd_lba], eax
        mov [.hcd_cnt], ecx
        mov [.hcd_buf], edi
        call hbfs_crypt_in_region
        jnc .hcd_done
        mov edi, blk_crypt_lock
        call spin_lock
        test eax, eax
        jnz .hcd_fail
        mov ecx, [.hcd_cnt]
        shr ecx, 3
        mov [.hcd_blocks], ecx
        mov ebx, [.hcd_lba]
        sub ebx, [hbfs_crypt_first_lba]
        shr ebx, 3
        mov esi, [.hcd_buf]
.hcd_loop:
        mov eax, ebx
        mov edi, esi
        mov edx, 1                      ; decrypt
        call xts_crypt_unit
        jc .hcd_unlock_fail
        add ebx, 1
        add esi, HBFS_BLOCK_SIZE
        dec dword [.hcd_blocks]
        jnz .hcd_loop
        mov edi, blk_crypt_lock
        call spin_unlock
.hcd_done:
        popad
        clc
        ret
.hcd_unlock_fail:
        mov edi, blk_crypt_lock
        call spin_unlock
.hcd_fail:
        popad
        stc
        ret
.hcd_lba:    dd 0
.hcd_cnt:    dd 0
.hcd_buf:    dd 0
.hcd_blocks: dd 0

;-----------------------------------------------------------------------
; hbfs_crypt_mark - stamp a freshly formatted volume as encrypted
;   In : ESI = store key name
;   Out: CF set when the key is not in the store or the write fails
;
; The volume key never touches the disk: what is stored is the check value, so
; the volume can say "I need this key" without carrying a wrapped copy of it
; that a future reader might be tempted to unwrap with a weaker key.
;-----------------------------------------------------------------------
hbfs_crypt_mark:
        pushad
        mov [.hcm_name], esi
        call key_find
        jc .hcm_nokey
        call key_secret_of
        mov edi, hbfs_crypt_check_tmp
        call aes_key_check

        ; Name into the superblock's field, refusing an over-long one.
        mov esi, [.hcm_name]
        lea edi, [hbfs_super_buf + HBFS_SB_CRYPT_NAME]
        mov ecx, HBFS_CRYPT_NAME_LEN - 1
.hcm_copyname:
        mov al, [esi]
        test al, al
        jz .hcm_nameend
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jnz .hcm_copyname
        cmp byte [esi], 0
        jne .hcm_badname
.hcm_nameend:
        mov byte [edi], 0

        mov dword [hbfs_super_buf + HBFS_SB_CRYPT], HBFS_CRYPT_AES_XTS
        lea esi, [hbfs_crypt_check_tmp]
        lea edi, [hbfs_super_buf + HBFS_SB_CRYPT_CHECK]
        mov ecx, 2
        cld
        rep movsd
        or dword [hbfs_super_buf + HBFS_SB_FEATURES], HBFS_FEAT_CRYPT

        call hbfs_write_superblock
        jc .hcm_fail

        ; The keys are live from here: the volume that was just created is
        ; written through the same transform it will be read through.
        mov esi, [.hcm_name]
        call key_find
        call key_secret_of
        call aes_setup_secret
        jc .hcm_fail
        mov dword [hbfs_crypt_on], 1
        popad
        clc
        ret
.hcm_nokey:
        mov esi, msg_crypt_no_key
        call vga_print
        jmp .hcm_fail
.hcm_badname:
        mov esi, msg_crypt_long_name
        call vga_print
.hcm_fail:
        popad
        stc
        ret
.hcm_name: dd 0

msg_hbfs_crypt_open:    db 0x0A, "HBFS: volume unlocked (AES-256-XTS)", 0x0A, 0
msg_hbfs_crypt_locked:  db 0x0A, "HBFS: volume is encrypted; 'key add <name> <hex>' then 'crypt unlock'", 0x0A, 0
msg_hbfs_crypt_wrong:   db 0x0A, "HBFS: the stored key is not this volume's", 0x0A, 0
msg_hbfs_crypt_unknown: db 0x0A, "HBFS: unknown volume encryption, refusing the mount", 0x0A, 0
msg_crypt_no_key:       db "crypt: no such key in the store", 0x0A, 0
msg_crypt_long_name:    db "crypt: key name is longer than 15 characters", 0x0A, 0

;-----------------------------------------------------------------------
; crypt_next_token - take the next word from the command line
;   In : ESI = source, EDX = destination size in characters
;        (the source is advanced past the word)
;   Out: EDI = destination, ESI advanced, CF set when there is no word
;-----------------------------------------------------------------------
crypt_next_token:
        push eax
        push ecx
        push edi
        call skip_spaces
        cmp byte [esi], 0
        je .cnt_none
        mov ecx, edx
        test ecx, ecx
        jz .cnt_none
.cnt_copy:
        mov al, [esi]
        test al, al
        jz .cnt_end
        cmp al, ' '
        je .cnt_end
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jnz .cnt_copy
.cnt_end:
        mov byte [edi], 0
        pop edi
        pop ecx
        pop eax
        clc
        ret
.cnt_none:
        pop edi
        pop ecx
        pop eax
        stc
        ret

;-----------------------------------------------------------------------
; crypt_args - point ESI at the first argument of a `crypt` command
;   Out: ESI = first argument, CF set when the line has none
;
; A verb compare leaves ESI on tok_buf, not on the line, so a handler that
; wants arguments has to re-derive them. This steps over the command word and
; the verb, leaving ESI on whatever follows.
;-----------------------------------------------------------------------
crypt_args:
        push edi
        push edx
        mov esi, line_buffer
        mov edi, cmd_crypt_str
        call str_starts_with
        mov edi, tok_buf
        mov edx, 32
        call crypt_next_token           ; the verb, matched by the caller
        pop edx
        pop edi
        ret

;-----------------------------------------------------------------------
; key_command - `key add <name> <hex>`, `key list`, `key del <name>`
;-----------------------------------------------------------------------
key_command:
        pushad
        cmp byte [esi], 0
        je .kc_usage
        mov esi, line_buffer
        mov edi, cmd_key_str
        call str_starts_with           ; ESI past the command word

        mov edi, tok_buf
        mov edx, 32
        call crypt_next_token
        jc .kc_usage

        ; add?
        mov esi, tok_buf
        mov edi, word_add
        call crypt_token_is
        jc .kc_add
        mov esi, tok_buf
        mov edi, word_list
        call crypt_token_is
        jc .kc_list
        mov esi, tok_buf
        mov edi, word_del
        call crypt_token_is
        jc .kc_del
        jmp .kc_usage

.kc_add:
        ; The remainder of the line is the name, then the hex.
        mov esi, line_buffer
        mov edi, cmd_key_str
        call str_starts_with
        mov edi, tok_buf               ; skip "add"
        mov edx, 32
        call crypt_next_token
        mov edi, name_buf2
        mov edx, 32
        call crypt_next_token
        jc .kc_usage
        mov edi, hex_buf
        mov edx, 72                    ; 64 digits, and room to see an extra
        call crypt_next_token
        jc .kc_usage
        mov esi, name_buf2
        mov edi, hex_buf
        call key_add
        jc .kc_add_fail
        mov esi, msg_key_added
        call vga_print
        jmp .kc_done
.kc_add_fail:
        mov esi, msg_key_add_fail
        call vga_print
        jmp .kc_done

.kc_del:
        mov esi, line_buffer
        mov edi, cmd_key_str
        call str_starts_with
        mov edi, tok_buf
        mov edx, 32
        call crypt_next_token
        mov edi, name_buf2
        mov edx, 32
        call crypt_next_token
        jc .kc_usage
        mov esi, name_buf2
        call key_del
        jc .kc_no_such
        mov esi, msg_key_deleted
        call vga_print
        jmp .kc_done

.kc_list:
        call key_list
        jmp .kc_done

.kc_no_such:
        mov esi, msg_crypt_no_key
        call vga_print
        jmp .kc_done
.kc_usage:
        mov esi, msg_key_usage
        call vga_print
.kc_done:
        popad
        ret

; ZF-style token compare: CF set when the null-terminated strings match
crypt_token_is:
        push esi
        push edi
.cti_loop:
        mov al, [esi]
        cmp al, [edi]
        jne .cti_no
        test al, al
        jz .cti_yes
        inc esi
        inc edi
        jmp .cti_loop
.cti_yes:
        pop edi
        pop esi
        stc
        ret
.cti_no:
        pop edi
        pop esi
        clc
        ret

;-----------------------------------------------------------------------
; key_list - names and lengths, never the secret
;-----------------------------------------------------------------------
key_list:
        pushad
        mov esi, msg_key_head
        call vga_print
        xor ecx, ecx
.kl_loop:
        cmp ecx, KEY_SLOTS
        jae .kl_done
        cmp byte [key_used + ecx], 0
        je .kl_next
        push ecx
        mov eax, ecx
        call key_slot_of
        mov esi, ebx
        call vga_print
        mov esi, msg_key_len
        call vga_print
        mov eax, KEY_SECRET_BYTES
        call vga_print_dec
        mov esi, msg_key_bytes
        call vga_print
        pop ecx
.kl_next:
        inc ecx
        jmp .kl_loop
.kl_done:
        cmp dword [key_count], 0
        jne .kl_out
        mov esi, msg_key_empty
        call vga_print
.kl_out:
        popad
        ret

;-----------------------------------------------------------------------
; crypt_command - `crypt status`, `crypt format <name> confirm`,
;                 `crypt unlock <name>`
;-----------------------------------------------------------------------
crypt_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_crypt_str
        call str_starts_with
        mov edi, tok_buf
        mov edx, 32
        call crypt_next_token
        jc .cc_usage

        mov esi, tok_buf
        mov edi, word_status
        call crypt_token_is
        jc .cc_status
        mov esi, tok_buf
        mov edi, word_format
        call crypt_token_is
        jc .cc_format
        mov esi, tok_buf
        mov edi, word_unlock
        call crypt_token_is
        jc .cc_unlock
        jmp .cc_usage

.cc_status:
        mov esi, msg_crypt_status
        call vga_print
        cmp dword [hbfs_crypt_on], 0
        jne .cc_status_open
        ; A volume whose key is missing is encrypted, and saying "not
        ; encrypted" would send the operator looking for the wrong fault: the
        ; volume has its key stamped, the store just has not been given it.
        cmp dword [hbfs_locked], 0
        jne .cc_status_locked
        mov esi, msg_crypt_no
        call vga_print
        jmp .cc_done
.cc_status_open:
        mov esi, msg_crypt_yes
        call vga_print
        ; The superblock is where the name lives: `crypt format` writes it
        ; there, and the mount copies it out of there, so it is right in both
        ; cases and does not depend on a load having happened this boot.
        lea esi, [hbfs_super_buf + HBFS_SB_CRYPT_NAME]
        call vga_print
        mov esi, msg_crypt_name_end
        call vga_print
        jmp .cc_done
.cc_status_locked:
        mov esi, msg_crypt_yes
        call vga_print
        lea esi, [hbfs_super_buf + HBFS_SB_CRYPT_NAME]
        call vga_print
        mov esi, msg_crypt_locked_tail
        call vga_print
        jmp .cc_done

.cc_format:
        ; The verb compare restores ESI, so it still points at tok_buf when the
        ; handler starts. The arguments are read from the line again, the way
        ; key_command does it, or the first "argument" would be the verb.
        call crypt_args
        jc .cc_usage
        mov edi, name_buf2
        mov edx, 32
        call crypt_next_token
        jc .cc_usage
        mov edi, tok_buf
        mov edx, 32
        call crypt_next_token
        jc .cc_usage
        mov esi, tok_buf
        mov edi, word_confirm
        call crypt_token_is
        jnc .cc_need_confirm

        ; The key has to exist before the volume is stamped with its check
        ; value, or the volume would be unopenable the moment it was created.
        mov esi, name_buf2
        call key_find
        jc .cc_no_such_key

        mov dword [hbfs_format_crypt], 1
        call hbfs_format
        mov dword [hbfs_format_crypt], 0

        ; hbfs_format reports failure by leaving the volume unmounted rather
        ; than in CF, and stamping the fields of a volume that was never
        ; written would claim encryption the disk does not have.
        cmp dword [hbfs_mounted], 0
        je .cc_format_failed

        ; hbfs_crypt_mark stamps the fields and lights the transform. A failed
        ; mark leaves a plain volume, which is what the message says.
        mov esi, name_buf2
        call hbfs_crypt_mark
        jc .cc_mark_fail
        mov esi, msg_crypt_formatted
        call vga_print
        jmp .cc_done
.cc_format_failed:
        mov esi, msg_crypt_format_failed
        call vga_print
        jmp .cc_done
.cc_need_confirm:
        mov esi, msg_crypt_confirm
        call vga_print
        jmp .cc_done
.cc_mark_fail:
        mov esi, msg_crypt_plain_now
        call vga_print
        jmp .cc_done

.cc_unlock:
        call crypt_args
        jc .cc_usage
        mov edi, name_buf2
        mov edx, 32
        call crypt_next_token
        jc .cc_usage
        mov esi, name_buf2
        call key_find
        jc .cc_no_such_key
        ; The mount is the only thing that decides: it re-reads the superblock
        ; and checks the key against the check value.
        call hbfs_init
        cmp dword [hbfs_crypt_on], 0
        je .cc_unlock_failed
        mov esi, msg_crypt_unlocked
        call vga_print
        jmp .cc_done
.cc_unlock_failed:
        mov esi, msg_crypt_not_open
        call vga_print
        jmp .cc_done
.cc_no_such_key:
        mov esi, msg_crypt_no_key
        call vga_print
        jmp .cc_done
.cc_usage:
        mov esi, msg_crypt_usage
        call vga_print
.cc_done:
        popad
        ret

word_add:      db "add", 0
word_list:     db "list", 0
word_del:      db "del", 0
word_status:   db "status", 0
word_format:   db "format", 0
word_unlock:   db "unlock", 0
word_confirm:  db "confirm", 0

msg_key_head:        db 0x0A, "Keys in the store:", 0x0A, 0
msg_key_empty:       db "  (none)", 0x0A, 0
msg_key_len:         db "  ", 0
msg_key_bytes:       db " bytes", 0x0A, 0
msg_key_added:       db "key: stored", 0x0A, 0
msg_key_add_fail:    db "key: refused (duplicate name, bad name or not 64 hex digits)", 0x0A, 0
msg_key_deleted:     db "key: deleted", 0x0A, 0
msg_key_usage:       db "key add <name> <64 hex> | key list | key del <name>", 0x0A, 0
msg_crypt_status:    db "crypt: volume is ", 0
msg_crypt_yes:       db "encrypted (AES-256-XTS), key '", 0
msg_crypt_no:        db "not encrypted", 0x0A, 0
msg_crypt_name_end:  db "'", 0x0A, 0
msg_crypt_locked_tail: db "' - locked: the key is not in the store", 0x0A, 0
msg_crypt_format_failed: db "crypt: the format failed, so the volume was not marked encrypted", 0x0A, 0
msg_crypt_usage:     db "crypt status | crypt format <key> confirm | crypt unlock <key>", 0x0A, 0
msg_crypt_confirm:   db "crypt: this destroys the volume; say 'confirm' to go ahead", 0x0A, 0
msg_crypt_formatted: db "crypt: volume formatted with AES-256-XTS", 0x0A, 0
msg_crypt_plain_now: db "crypt: the volume is plain; the key was not stamped", 0x0A, 0
msg_crypt_unlocked:  db "crypt: volume unlocked", 0x0A, 0
msg_crypt_not_open:  db "crypt: the volume did not open with that key", 0x0A, 0

cmd_key_str:    db "key", 0
cmd_crypt_str:  db "crypt", 0

align 4
tok_buf:    times 32 db 0
name_buf2:  times 32 db 0
hex_buf:    times 80 db 0
