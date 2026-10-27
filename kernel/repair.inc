;=======================================================================
; HBFS repair - the recovery console's half of the storage contract
;
; tools/fsck_hbfs.py checks a volume offline; this does the same walk against a
; mounted volume, from the shell, so a machine that will not come up usable can
; still be told what is wrong with its disk - and fix what is safe to fix.
;
; Checked, in the classes the offline checker reports:
;   * a block referenced by an entry but not marked allocated - the data is
;     there, the bitmap forgot it, and the next allocation hands the block to
;     someone else;
;   * a block marked allocated that nothing references - a leak;
;   * a block two entries both claim - reported, never "fixed": freeing it
;     truncates one of them and no rule here can say which one deserves it;
;   * an entry with an empty name, a '/' in the name, an unknown type, a block
;     range outside the data area, or an xattr pointer outside it.
;
; The walk borrows the resident bitmap as its own scratch: referenced bits are
; cleared as they are met, so what is still set afterwards is exactly the
; leaks, and a second pass sets the referenced bits again. Nothing is written
; until the end, so a machine that dies mid-check leaves the volume untouched,
; and one that dies during the single bitmap write is fixed by the next run.
;=======================================================================

REPAIR_MAX_DEPTH      equ 8

repair_ref_total:     dd 0      ; blocks the tree claims, duplicates included
repair_missing:       dd 0      ; claimed, but the bitmap said free
repair_bad:           dd 0      ; entries that cannot be trusted
repair_shared:        dd 0      ; blocks claimed twice
repair_entries:       dd 0      ; live entries seen
repair_orphans:       dd 0      ; set in the bitmap, claimed by nothing
repair_alloc_before:  dd 0
repair_expected:      dd 0      ; blocks that should be set after the repair
repair_mode:          dd 0      ; 0 = clear as we go, 1 = set (rebuild)
repair_fix_wanted:    dd 0      ; 1 = the operator asked for a repair

;-----------------------------------------------------------------------
; repair_claim - claim [start, start+count) in the resident bitmap
;   In : EAX = start (data-area block), ECX = count
;   Mode 0: clear each bit, counting claims and the blocks it found free.
;   Mode 1: set each bit. Neither mode writes anything.
;-----------------------------------------------------------------------
repair_claim:
        pushad
        test ecx, ecx
        jle .rc_done
.rc_loop:
        test ecx, ecx
        jle .rc_done
        cmp eax, HBFS_TOTAL_BLOCKS
        jae .rc_out_of_range
        mov edx, eax
        shr edx, 3
        mov esi, eax
        and esi, 7
        cmp dword [repair_mode], 0
        jne .rc_set_bit
        inc dword [repair_ref_total]
        btr dword [hbfs_bitmap_buf + edx], esi
        jc .rc_advance
        inc dword [repair_missing]
        jmp .rc_advance
.rc_set_bit:
        bts dword [hbfs_bitmap_buf + edx], esi
.rc_advance:
        inc eax
        dec ecx
        jmp .rc_loop
.rc_out_of_range:
        inc dword [repair_bad]
.rc_done:
        popad
        ret

;-----------------------------------------------------------------------
; repair_entry - check one entry, claim what it owns, name a child to walk
;   In : EDI = entry pointer (inside hbfs_dir_buf)
;   Out: EAX = start LBA of a subdirectory, or 0
;        ECX = its sector count
;-----------------------------------------------------------------------
repair_entry:
        push ebx
        push edx
        push esi
        push edi
        mov dword [.re_child_lba], 0
        mov dword [.re_child_sects], 0
        cmp dword [repair_mode], 0
        jne .re_counted
        inc dword [repair_entries]
.re_counted:

        ; A path is not a name, and a name is not empty
        cmp byte [edi + HBFS_DE_NAME], 0
        je .re_bad
        xor ecx, ecx
.re_name:
        mov al, [edi + ecx]
        test al, al
        jz .re_name_ok
        cmp al, '/'
        je .re_bad
        inc ecx
        cmp ecx, HBFS_MAX_FILENAME
        jb .re_name
        jmp .re_bad                       ; no terminator within the field
.re_name_ok:

        movzx eax, byte [edi + HBFS_DE_TYPE]
        cmp eax, FTYPE_TEXT
        jb .re_bad
        cmp eax, FTYPE_LINK
        ja .re_bad
        mov [.re_type], eax

        ; The entry's data blocks
        mov eax, [edi + HBFS_DE_BLOCK]
        mov ecx, [edi + HBFS_DE_BLKCNT]
        test ecx, ecx
        jle .re_no_blocks
        mov edx, eax
        add edx, ecx
        cmp edx, HBFS_TOTAL_BLOCKS
        ja .re_bad
        call repair_claim
.re_no_blocks:

        ; The xattr block is a block too
        cmp dword [edi + HBFS_DE_XATTR_LBA], 0
        je .re_no_xattr
        cmp word [edi + HBFS_DE_XATTR_COUNT], HBFS_XATTR_PER_BLOCK
        ja .re_bad
        mov eax, [edi + HBFS_DE_XATTR_LBA]
        sub eax, HBFS_DATA_START
        jb .re_bad
        mov ecx, HBFS_SECTORS_PER_BLK
        xor edx, edx
        div ecx                            ; EAX = data block, EDX = sector offset
        test edx, edx
        jnz .re_bad                        ; a run starts on a block boundary
        mov ecx, 1
        call repair_claim
.re_no_xattr:

        ; A subdirectory is walked, not merely claimed
        cmp dword [.re_type], FTYPE_DIR
        jne .re_out
        mov ecx, [edi + HBFS_DE_BLKCNT]
        test ecx, ecx
        jz .re_out
        mov eax, [edi + HBFS_DE_BLOCK]
        shl ecx, 3                         ; blocks -> sectors
        imul eax, eax, HBFS_SECTORS_PER_BLK
        add eax, HBFS_DATA_START           ; block -> LBA
        mov [.re_child_lba], eax
        mov [.re_child_sects], ecx
        jmp .re_out
.re_bad:
        inc dword [repair_bad]
.re_out:
        pop edi
        pop esi
        pop edx
        pop ebx
        mov eax, [.re_child_lba]
        mov ecx, [.re_child_sects]
        ret

.re_type:        dd 0
.re_child_lba:   dd 0
.re_child_sects: dd 0

;-----------------------------------------------------------------------
; repair_walk - walk one directory and everything under it
;   In : EBX = directory start LBA, ECX = sectors, EDX = depth
;-----------------------------------------------------------------------
repair_walk:
        pushad
        cmp edx, REPAIR_MAX_DEPTH
        ja .rw_too_deep
        mov [.rw_lba], ebx
        mov [.rw_sects], ecx
        mov [.rw_depth], edx
        push dword [current_dir_lba]
        push dword [current_dir_sects]
        mov [current_dir_lba], ebx
        mov [current_dir_sects], ecx
        mov dword [dir_cache_lba], 0xFFFFFFFE
        call hbfs_load_root_dir
        jc .rw_unreadable
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov [.rw_max], eax
        mov dword [.rw_index], 0

.rw_loop:
        mov eax, [.rw_index]
        cmp eax, [.rw_max]
        jge .rw_done
        imul edi, eax, HBFS_DIR_ENTRY_SIZE
        add edi, hbfs_dir_buf
        cmp byte [edi + HBFS_DE_TYPE], FTYPE_FREE
        je .rw_next
        call repair_entry
        test eax, eax
        jz .rw_next
        ; This walk's own state lives in statics, and the child is about to use
        ; the same ones: keep a copy across the call or the loop resumes with
        ; the child's index and stops at the first subdirectory.
        push dword [.rw_max]
        push dword [.rw_index]
        push dword [.rw_lba]
        push dword [.rw_sects]
        mov ebx, eax
        mov edx, [.rw_depth]
        inc edx
        call repair_walk                   ; clobbers hbfs_dir_buf
        pop dword [.rw_sects]
        pop dword [.rw_lba]
        pop dword [.rw_index]
        pop dword [.rw_max]
        mov eax, [.rw_lba]
        mov [current_dir_lba], eax
        mov eax, [.rw_sects]
        mov [current_dir_sects], eax
        mov dword [dir_cache_lba], 0xFFFFFFFE
        call hbfs_load_root_dir            ; the parent again, for the next index
        jc .rw_unreadable
.rw_next:
        inc dword [.rw_index]
        jmp .rw_loop

.rw_too_deep:
        inc dword [repair_bad]
        jmp .rw_done
.rw_unreadable:
        inc dword [repair_bad]
.rw_done:
        pop dword [current_dir_sects]
        pop dword [current_dir_lba]
        mov dword [dir_cache_lba], 0xFFFFFFFE
        popad
        ret

.rw_lba:    dd 0
.rw_sects:  dd 0
.rw_depth:  dd 0
.rw_max:    dd 0
.rw_index:  dd 0

;-----------------------------------------------------------------------
; repair_popcount - how many blocks the resident bitmap says are taken
;   Out: EAX
;-----------------------------------------------------------------------
repair_popcount:
        push esi
        push ecx
        push edx
        mov esi, hbfs_bitmap_buf
        mov ecx, HBFS_BITMAP_SIZE
        xor edx, edx
.rp_byte:
        mov al, [esi]
        inc esi
        test al, al
        jz .rp_next
.rp_bit:
        test al, 1
        jz .rp_skip
        inc edx
.rp_skip:
        shr al, 1
        jnz .rp_bit
.rp_next:
        dec ecx
        jnz .rp_byte
        mov eax, edx
        pop edx
        pop ecx
        pop esi
        ret

;-----------------------------------------------------------------------
; repair_pass - one whole-tree pass in the current repair_mode
;-----------------------------------------------------------------------
repair_pass:
        ; The journal lives in the first blocks of the data area and is
        ; allocated in the bitmap even though no entry references it. Leaving
        ; it out of the reference would free it and then hand it to a file.
        xor eax, eax
        mov ecx, HBFS_JOURNAL_BLOCKS
        call repair_claim
        mov ebx, HBFS_ROOT_DIR_START
        mov ecx, HBFS_ROOT_DIR_SECTS
        xor edx, edx
        call repair_walk
        ret

;-----------------------------------------------------------------------
; repair_scan - read the volume, walk it twice, leave the counts filled in
;   Out: EAX = 0 ok, -1 refused or unreadable
;-----------------------------------------------------------------------
repair_scan:
        call hbfs_require_writable
        jc .rs_refused
        call hbfs_load_bitmap
        jc .rs_unreadable
        call repair_popcount
        mov [repair_alloc_before], eax

        mov dword [repair_mode], 0
        call repair_pass
        call repair_popcount                ; what is left set: the leaks
        mov [repair_orphans], eax

        ; Blocks claimed more than once. Distinct blocks that are referenced
        ; and were marked allocated = everything the bitmap held, less the
        ; leaks; the blocks the bitmap had freed are referenced but are not in
        ; that count, so they come out of the claims as well before the rest is
        ; called a duplicate.
        mov ecx, [repair_alloc_before]
        sub ecx, eax                        ; allocated and referenced
        add ecx, [repair_missing]           ; plus the ones it had marked free
        mov edx, [repair_ref_total]
        sub edx, ecx
        jns .rs_shared_ok
        xor edx, edx
.rs_shared_ok:
        mov [repair_shared], edx

        ; The bits still set are the leaks, and they have to go: zero the
        ; bitmap and let the rebuild put back exactly what the tree claims.
        mov edi, hbfs_bitmap_buf
        mov ecx, HBFS_BITMAP_SIZE / 4
        xor eax, eax
        cld
        rep stosd

        mov dword [repair_mode], 1
        call repair_pass
        call repair_popcount
        mov [repair_expected], eax
        mov dword [hbfs_bitmap_valid], 0    ; RAM no longer matches the disk
        xor eax, eax
        ret
.rs_refused:
        mov eax, -1
        ret
.rs_unreadable:
        mov eax, -1
        ret

;-----------------------------------------------------------------------
; repair_apply - write the repaired bitmap and the free count, then prove the
; bitmap that landed is the one that was intended
;   Out: EAX = 0 written and verified, -1 failed
;-----------------------------------------------------------------------
repair_apply:
        pushad
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov esi, hbfs_bitmap_buf
        call hbfs_meta_write
        jc .ra_fail

        ; Read it back. A torn 64 KB write is silent otherwise, and this is
        ; the one write a repair cannot check by trying again later.
        mov dword [hbfs_bitmap_valid], 0
        call hbfs_load_bitmap
        jc .ra_fail
        call repair_popcount
        cmp eax, [repair_expected]
        jne .ra_fail

        ; Free count = what the bitmap says is free, and no entry can argue
        mov ecx, HBFS_TOTAL_BLOCKS
        sub ecx, eax
        mov [hbfs_super_buf + 12], ecx
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call hbfs_meta_write
        jc .ra_fail
        mov dword [esp + 28], 0
        popad
        ret
.ra_fail:
        mov dword [hbfs_bitmap_valid], 0
        mov dword [esp + 28], -1
        popad
        ret

;-----------------------------------------------------------------------
; repair_discard_journal - give up on a log that cannot be replayed
;   A pre-image that was torn when the machine died cannot be applied, so the
;   transaction it belongs to can never be undone. Zeroing the log says so out
;   loud: the volume mounts clean, and whatever that transaction had half
;   written is the next check's problem rather than a replay's guess.
;   Out: EAX = 0 discarded, -1 failed
;-----------------------------------------------------------------------
repair_discard_journal:
        pushad
        ; Nothing to give up on: say so rather than rewriting a clean log.
        mov eax, [hbfs_super_buf + HBFS_SB_JRNL_STATE]
        test eax, eax
        jnz .rdj_dirty
        mov eax, [hbfs_journal_dirty]
        test eax, eax
        jnz .rdj_dirty
        mov dword [esp + 28], 1         ; nothing to discard
        popad
        ret
.rdj_dirty:
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        xor eax, eax
        cld
        rep stosd
        mov dword [.rdj_left], HBFS_JOURNAL_BLOCKS
        mov dword [.rdj_lba], HBFS_JOURNAL_LBA
.rdj_write:
        mov eax, [.rdj_lba]
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call hbfs_meta_write
        jc .rdj_fail
        add dword [.rdj_lba], HBFS_SECTORS_PER_BLK
        dec dword [.rdj_left]
        jnz .rdj_write

        mov dword [hbfs_super_buf + HBFS_SB_JRNL_STATE], 0
        mov dword [hbfs_super_buf + HBFS_SB_JRNL_PTR], 0
        mov dword [hbfs_journal_dirty], 0
        mov dword [hbfs_journal_depth], 0
        mov dword [hbfs_journal_failed], 0
        mov dword [hbfs_journal_ptr], 0
        call hbfs_write_superblock
        mov dword [esp + 28], 0         ; discarded
        popad
        ret
.rdj_fail:
        mov dword [esp + 28], -1
        popad
        ret
.rdj_left: dd 0
.rdj_lba:  dd 0

;-----------------------------------------------------------------------
; repair_report - print the counts, then the verdict
;   In : DL = 1 when the bitmap was written
;-----------------------------------------------------------------------
repair_report:
        pushad
        mov byte [.rr_fixed], dl
        mov esi, msg_repair_hdr
        call vga_print
        mov esi, msg_repair_entries
        call vga_print
        mov eax, [repair_entries]
        call vga_print_dec
        mov esi, msg_repair_blocks
        call vga_print
        mov eax, [repair_ref_total]
        call vga_print_dec

        mov esi, msg_repair_leaks
        call vga_print
        mov eax, [repair_orphans]
        call vga_print_dec
        mov esi, msg_repair_shared
        call vga_print
        mov eax, [repair_shared]
        call vga_print_dec
        mov esi, msg_repair_bad
        call vga_print
        mov eax, [repair_bad]
        call vga_print_dec

        mov esi, msg_repair_lost
        call vga_print
        mov eax, [repair_missing]
        call vga_print_dec

        ; Veto: a volume with a damaged entry or a shared block is not called
        ; clean even when its bitmap now adds up.
        mov al, 0x0A
        call vga_putchar
        mov eax, [repair_bad]
        add eax, [repair_shared]
        jnz .rr_trouble

        mov eax, [repair_missing]
        add eax, [repair_orphans]
        jnz .rr_issues

        mov esi, msg_repair_clean
        call vga_print
        jmp .rr_done
.rr_issues:
        cmp byte [.rr_fixed], 0
        je .rr_check_only
        mov esi, msg_repair_fixed
        call vga_print
        jmp .rr_done
.rr_check_only:
        mov esi, msg_repair_issues
        call vga_print
        jmp .rr_done
.rr_trouble:
        mov esi, msg_repair_trouble
        call vga_print
.rr_done:
        popad
        ret
.rr_fixed: db 0

repair_cmd_fix:     db "fix", 0
repair_cmd_journal: db "journal", 0

msg_repair_hdr:      db "repair ", 0
msg_repair_entries:  db "entries ", 0
msg_repair_blocks:   db "  blocks ", 0
msg_repair_leaks:    db "  leaks ", 0
msg_repair_shared:   db "  shared ", 0
msg_repair_bad:      db "  bad ", 0
msg_repair_lost:     db "  lost ", 0
msg_repair_clean:    db "REPAIR_CLEAN", 0x0A, 0
msg_repair_fixed:    db "REPAIR_FIXED", 0x0A, 0
msg_repair_issues:   db "REPAIR_ISSUES", 0x0A, 0
msg_repair_trouble:  db "REPAIR_TROUBLE", 0x0A, 0
msg_repair_refused:  db "repair: volume is read-only", 0x0A, 0
msg_repair_writebad: db "repair: rewrite failed", 0x0A, 0
msg_repair_jrnl:     db "journal discarded", 0x0A, 0
msg_repair_jrnlbad:  db "repair: journal failed", 0x0A, 0
msg_repair_jrnl_clean: db "journal is already clean", 0x0A, 0

;-----------------------------------------------------------------------
; cmd_repair - the `repair` shell command
;   repair           check and report
;   repair fix       check, rewrite the bitmap and the free count
;   repair journal   discard a log that cannot be replayed
;-----------------------------------------------------------------------
cmd_repair:
        pushad
        call skip_spaces
        mov edi, repair_cmd_journal
        call str_starts_with            ; CF set = match, ESI past the word
        jc .rp_journal
        mov edi, repair_cmd_fix
        call str_starts_with
        jc .rp_fix
        xor edx, edx
        jmp .rp_check

.rp_journal:
        call repair_discard_journal
        cmp eax, 0
        je .rp_journal_done
        js .rp_journal_failed
        mov esi, msg_repair_jrnl_clean
        call vga_print
        popad
        ret
.rp_journal_done:
        mov esi, msg_repair_jrnl
        call vga_print
        popad
        ret
.rp_journal_failed:
        call vga_set_error
        mov esi, msg_repair_jrnlbad
        call vga_print
        call vga_reset_color
        popad
        ret

.rp_check:
        mov dword [repair_fix_wanted], edx
        call repair_scan
        test eax, eax
        jnz .rp_refused
        cmp dword [repair_fix_wanted], 0
        je .rp_report_only
        ; Fixing a volume whose entries are damaged would only make the next
        ; check agree with the damage, so refuse and say what is wrong.
        mov eax, [repair_bad]
        add eax, [repair_shared]
        jnz .rp_report_only
        call repair_apply
        test eax, eax
        jnz .rp_write_failed
        mov edx, 1
        jmp .rp_report_only_edx
.rp_report_only:
        xor edx, edx
.rp_report_only_edx:
        call repair_report
        popad
        ret
.rp_fix:
        mov edx, 1
        jmp .rp_check

.rp_refused:
        call vga_set_error
        mov esi, msg_repair_refused
        call vga_print
        call vga_reset_color
        popad
        ret
.rp_write_failed:
        call vga_set_error
        mov esi, msg_repair_writebad
        call vga_print
        call vga_reset_color
        popad
        ret
