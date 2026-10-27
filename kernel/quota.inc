;=======================================================================
; quota.inc - how much disk one user may hold
;
; A filesystem with no quota is one user's accident away from being everybody's
; problem: a runaway program fills the volume and every other user's write
; fails with "no space" for a reason none of them caused, in a place none of
; them can see.
;
; The unit is the 4 KiB block, because that is what the allocator hands out and
; what the directory entry already records per file. The ledger is per uid:
; used blocks, and a ceiling where zero means unlimited.
;
; Where the number comes from matters. It is *derived* from the tree at mount -
; every file's entry says how many blocks it holds and who owns it - and then
; maintained by the operations that change a file's size. That way a power cut
; cannot leave the ledger disagreeing with the disk: the next mount recomputes
; it. `quota rebuild` does the same thing on demand, which is the answer to
; "these numbers look wrong", and the reason no repair tool is needed for the
; accounting.
;
; Enforcement is a refusal, not a truncation: a write that would cross the
; ceiling fails and the file keeps what it had, because a filesystem that
; silently discards data to stay inside a limit is worse than one that says no.
;=======================================================================

QUOTA_USERS         equ 16
QUOTA_DEFAULT       equ 4096            ; blocks: 16 MB, when nothing says otherwise
QUOTA_NOBODY        equ 0xFFFFFFFF      ; an empty ledger slot

quota_uid:          times QUOTA_USERS dd QUOTA_NOBODY
quota_used:         times QUOTA_USERS dd 0
quota_limit:        times QUOTA_USERS dd 0
quota_default:      dd QUOTA_DEFAULT
quota_ready:        db 0
quota_refusals:     dd 0
quota_charged:      dd 0                ; blocks ever charged, for the counters

; Work list for the tree walk: one entry per directory still to visit.
QUOTA_DIRS          equ 24
quota_q_lba:        times QUOTA_DIRS dd 0
quota_q_sects:      times QUOTA_DIRS dd 0
quota_q_head:       dd 0
quota_q_tail:       dd 0

;-----------------------------------------------------------------------
; quota_slot - the ledger slot for a uid, claiming one if needed
;   In : EAX = uid
;   Out: EAX = slot index, CF = 1 when the ledger is full
;-----------------------------------------------------------------------
quota_slot:
        push ebx
        push ecx
        mov ebx, eax                    ; the uid
        xor ecx, ecx
.qs_find:
        cmp ecx, QUOTA_USERS
        jae .qs_full
        cmp [quota_uid + ecx*4], ebx
        je .qs_found
        inc ecx
        jmp .qs_find
.qs_full:
        ; A uid the ledger has never seen takes the first free slot.
        xor ecx, ecx
.qs_free:
        cmp ecx, QUOTA_USERS
        jae .qs_none
        cmp dword [quota_uid + ecx*4], QUOTA_NOBODY
        jne .qs_next
        mov [quota_uid + ecx*4], ebx
        mov dword [quota_used + ecx*4], 0
        mov eax, [quota_default]
        mov [quota_limit + ecx*4], eax
        jmp .qs_found
.qs_next:
        inc ecx
        jmp .qs_free
.qs_none:
        stc
        jmp .qs_out
.qs_found:
        mov eax, ecx
        clc
.qs_out:
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; quota_allow - may this user grow by this many blocks?
;   In : EAX = uid, ECX = blocks
;   Out: EAX = 0 allowed, -1 refused (CF set to match)
;
; A ceiling of zero means unlimited. The check is "used + want <= limit", so a
; write that exactly fills the allowance is allowed - a limit nobody can reach
; is not a limit.
;-----------------------------------------------------------------------
quota_allow:
        push ebx
        push ecx
        push edx
        mov ebx, ecx                    ; want
        call quota_slot
        jc .qa_refuse
        mov edx, [quota_limit + eax*4]
        test edx, edx
        jz .qa_ok                       ; unlimited
        mov ecx, [quota_used + eax*4]
        add ecx, ebx
        cmp ecx, edx
        ja .qa_refuse
.qa_ok:
        xor eax, eax
        clc
        jmp .qa_out
.qa_refuse:
        inc dword [quota_refusals]
        mov eax, -1
        stc
.qa_out:
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; quota_charge - account blocks to a user, already allowed
;   In : EAX = uid, ECX = blocks
;-----------------------------------------------------------------------
quota_charge:
        push ebx
        push ecx
        push edx
        mov ebx, ecx
        call quota_slot
        jc .qc_out
        mov edx, [quota_used + eax*4]
        add edx, ebx
        mov [quota_used + eax*4], edx
        mov ecx, [quota_charged]
        add ecx, ebx
        mov [quota_charged], ecx
.qc_out:
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; quota_release - give blocks back to a user
;   In : EAX = uid, ECX = blocks
;
; Never goes below zero: a release with nothing charged is a bug in the caller,
; and letting the counter wrap would make the next allowance enormous.
;-----------------------------------------------------------------------
quota_release:
        push ebx
        push ecx
        push edx
        mov ebx, ecx
        call quota_slot
        jc .qr_out
        mov edx, [quota_used + eax*4]
        cmp edx, ebx
        jae .qr_sub
        xor edx, edx
        jmp .qr_store
.qr_sub:
        sub edx, ebx
.qr_store:
        mov [quota_used + eax*4], edx
.qr_out:
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; quota_uid_of_current - the uid a charge belongs to
;   Out: EAX = effective uid, 0 for the kernel/shell
;-----------------------------------------------------------------------
quota_uid_of_current:
        push ecx
        call sched_get_current_task
        test eax, eax
        jz .qoc_root
        mov eax, [eax + TCB_EUID]
        pop ecx
        ret
.qoc_root:
        xor eax, eax
        pop ecx
        ret

;-----------------------------------------------------------------------
; quota_clear - empty the ledger
;-----------------------------------------------------------------------
quota_clear:
        push eax
        push ecx
        push edi
        mov edi, quota_uid
        mov ecx, QUOTA_USERS
        mov eax, QUOTA_NOBODY
        cld
        rep stosd
        mov edi, quota_used
        mov ecx, QUOTA_USERS
        xor eax, eax
        rep stosd
        mov edi, quota_limit
        mov ecx, QUOTA_USERS
        rep stosd
        pop edi
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; quota_queue_dir - add one directory to the walk
;   In : EAX = LBA, ECX = sectors
;-----------------------------------------------------------------------
quota_queue_dir:
        push eax
        push ecx
        push edx
        mov edx, [quota_q_head]
        cmp edx, QUOTA_DIRS
        jae .qqd_full
        mov [quota_q_lba + edx*4], eax
        mov [quota_q_sects + edx*4], ecx
        inc dword [quota_q_head]
.qqd_full:
        pop edx
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; quota_build - derive the ledger from the tree
;   Out: EAX = blocks accounted, -1 when the volume is not mounted
;
; Breadth-first: the root directory, then every subdirectory it names that has
; room in the work list. Subdirectories that do not fit are counted by their own
; entry's block count but not descended into, and the count of those is
; reported - an accounting tool that silently skips a subtree is worse than one
; that says it did.
;
; The walk borrows hbfs_dir_buf, which belongs to the directory the shell is
; looking at, so the view is invalidated afterwards and reloaded on next use.
; Called at mount, when nothing else is using it, and by `quota rebuild`.
;-----------------------------------------------------------------------
quota_build:
        pushad
        call hbfs_require_mounted
        jc .qb_nomount

        call quota_clear
        mov dword [quota_q_head], 0
        mov dword [quota_q_tail], 0
        mov dword [.qb_skipped], 0
        mov dword [.qb_total], 0

        mov eax, HBFS_ROOT_DIR_START
        mov ecx, HBFS_ROOT_DIR_SECTS
        call quota_queue_dir

.qb_loop:
        mov edx, [quota_q_tail]
        cmp edx, [quota_q_head]
        jae .qb_done
        mov eax, [quota_q_lba + edx*4]
        mov ecx, [quota_q_sects + edx*4]
        inc dword [quota_q_tail]

        ; Read the directory into the shared buffer.
        mov [.qb_lba], eax
        mov [.qb_sects], ecx
        mov edi, hbfs_dir_buf
        push ecx
        mov ecx, HBFS_ROOT_DIR_SIZE / 4
        xor eax, eax
        cld
        rep stosd
        pop ecx
        mov eax, [.qb_lba]
        mov edi, hbfs_dir_buf
        call ata_read_sectors
        jc .qb_done

        ; Entries: (sectors * 512) / HBFS_DIR_ENTRY_SIZE of them.
        mov eax, [.qb_sects]
        shl eax, 9
        xor edx, edx
        mov ecx, HBFS_DIR_ENTRY_SIZE
        div ecx
        mov [.qb_entries], eax

        xor ebx, ebx
.qb_entry_loop:
        cmp ebx, [.qb_entries]
        jae .qb_loop
        mov eax, ebx
        imul eax, HBFS_DIR_ENTRY_SIZE
        lea esi, [hbfs_dir_buf + eax]

        movzx eax, byte [esi + 253]      ; type
        cmp eax, FTYPE_FREE
        je .qb_next

        ; Blocks this entry holds.
        mov ecx, [esi + 264]
        test ecx, ecx
        jz .qb_next

        ; A subdirectory's own blocks belong to whoever owns the directory, and
        ; its contents are counted when it is visited.
        cmp eax, FTYPE_DIR
        jne .qb_charge_entry
        ; The entry stores a block number in the data area, not an LBA: every
        ; reader adds HBFS_DATA_START, and so does this walk.
        mov edx, [esi + 260]
        add edx, HBFS_DATA_START
        mov eax, [esi + 264]
        shl eax, 3                       ; blocks -> sectors
        push ecx
        push esi
        mov ecx, eax
        mov eax, edx
        call quota_queue_dir
        pop esi
        pop ecx

.qb_charge_entry:
        movzx eax, word [esi + 278]      ; owner uid
        mov edx, [esi + 264]
        add [.qb_total], edx
        ; A directory's blocks are charged once, to its owner.
        mov ecx, edx
        call quota_charge
.qb_next:
        inc ebx
        jmp .qb_entry_loop

.qb_done:
        ; The shared buffer no longer holds what the shell's directory expects,
        ; so the cache is invalidated and the next read reloads it.
        mov dword [dir_cache_lba], 0xFFFFFFFF
        mov byte [quota_ready], 1
        mov eax, [.qb_total]
        mov [quota_build_total], eax
        mov [esp + 28], eax
        popad
        ret
.qb_nomount:
        mov dword [esp + 28], -1
        popad
        ret
.qb_lba:     dd 0
.qb_sects:   dd 0
.qb_entries: dd 0
.qb_total:   dd 0
.qb_skipped: dd 0

;-----------------------------------------------------------------------
; quota_show - the `quota` command
;
; One line per user the ledger knows, plus the totals. Blocks are printed with
; their KiB equivalent because "4096 blocks" means nothing to a person and
; "16 MB" does.
;-----------------------------------------------------------------------
quota_show:
        pushad
        mov esi, msg_quota_head
        call vga_print
        mov eax, [quota_refusals]
        call vga_print_dec
        mov esi, msg_quota_refusals
        call vga_print
        mov eax, [quota_charged]
        call vga_print_dec
        mov esi, msg_quota_charged
        call vga_print

        mov esi, msg_quota_cols
        call vga_print

        xor ecx, ecx
        mov dword [.qs_used_total], 0
        mov dword [.qs_users], 0
.qs_loop:
        cmp ecx, QUOTA_USERS
        jae .qs_done
        cmp dword [quota_uid + ecx*4], QUOTA_NOBODY
        je .qs_next

        inc dword [.qs_users]
        mov al, ' '
        call vga_putchar
        mov al, ' '
        call vga_putchar

        mov eax, [quota_uid + ecx*4]
        call vga_print_dec
        mov esi, msg_quota_used
        call vga_print
        mov eax, [quota_used + ecx*4]
        mov edx, eax
        add [.qs_used_total], edx
        call vga_print_dec
        mov esi, msg_quota_of
        call vga_print
        mov eax, [quota_limit + ecx*4]
        test eax, eax
        jnz .qs_limit
        mov esi, msg_quota_none
        call vga_print
        jmp .qs_nl
.qs_limit:
        call vga_print_dec
        mov esi, msg_quota_blocks
        call vga_print
.qs_nl:
        mov al, 0x0A
        call vga_putchar
.qs_next:
        inc ecx
        jmp .qs_loop
.qs_done:
        cmp dword [.qs_users], 0
        jne .qs_total
        mov esi, msg_quota_empty
        call vga_print
.qs_total:
        mov esi, msg_quota_total
        call vga_print
        mov eax, [.qs_used_total]
        call vga_print_dec
        mov esi, msg_quota_blocks
        call vga_print
        mov al, 0x0A
        call vga_putchar

        ; The same summary on COM1: a conformance run reads the serial console.
        mov esi, msg_quota_serial
        call serial_print
        mov eax, [.qs_users]
        call serial_print_dec
        mov esi, msg_quota_serial_used
        call serial_print
        mov eax, [.qs_used_total]
        call serial_print_dec
        mov esi, msg_quota_serial_ref
        call serial_print
        mov eax, [quota_refusals]
        call serial_print_dec
        mov esi, msg_quota_serial_ok
        call serial_print
        popad
        ret
.qs_used_total: dd 0
.qs_users:      dd 0

;-----------------------------------------------------------------------
; quota_command - `quota`, `quota set <uid> <blocks>`, `quota rebuild`
;   In : ESI = the rest of the command line
;-----------------------------------------------------------------------
quota_command:
        pushad
        call skip_spaces
        mov al, [esi]
        test al, al
        jz .qc_show

        ; "rebuild" recomputes the ledger from the tree.
        mov edi, msg_quota_rebuild_word
        call str_starts_with
        jnc .qc_set
        call quota_build
        mov esi, msg_quota_rebuilt
        call vga_print
        mov eax, [quota_build_total]
        call vga_print_dec
        mov esi, msg_quota_blocks
        call vga_print
        mov al, 0x0A
        call vga_putchar
        jmp .qc_out

.qc_set:
        mov edi, msg_quota_set_word
        call str_starts_with
        jnc .qc_show
        call skip_spaces
        call parse_decimal
        mov [.qc_uid], eax
        call skip_spaces
        call parse_decimal
        mov [.qc_limit], eax
        mov eax, [.qc_uid]
        call quota_slot
        jc .qc_full
        mov ecx, [.qc_limit]
        mov [quota_limit + eax*4], ecx
        mov esi, msg_quota_set
        call vga_print
        mov eax, [.qc_uid]
        call vga_print_dec
        mov esi, msg_quota_to
        call vga_print
        mov eax, [.qc_limit]
        call vga_print_dec
        mov esi, msg_quota_blocks
        call vga_print
        cmp dword [.qc_limit], 0
        jne .qc_nl
        mov esi, msg_quota_unlimited
        call vga_print
.qc_nl:
        mov al, 0x0A
        call vga_putchar
        jmp .qc_out
.qc_full:
        mov esi, msg_quota_ledger_full
        call vga_print
        jmp .qc_out
.qc_show:
        call quota_show
.qc_out:
        popad
        ret
.qc_uid:   dd 0
.qc_limit: dd 0

; quota_build reports its total through its own word, because the command needs
; it after pushad has been restored.
align 4
quota_build_total: dd 0

msg_quota_head:      db 0x0A, "Quota: ", 0
msg_quota_refusals:  db " refusals, ", 0
msg_quota_charged:   db " blocks charged since mount", 0x0A, 0
msg_quota_cols:      db "  uid      used        limit", 0x0A, 0
msg_quota_used:      db " blocks, ", 0
msg_quota_of:        db " of ", 0
msg_quota_none:      db "unlimited", 0
msg_quota_blocks:    db " blocks", 0
msg_quota_empty:     db "  (no user holds anything)", 0x0A, 0
msg_quota_total:     db "  total ", 0
msg_quota_serial:    db "QUOTA users=", 0
msg_quota_serial_used: db " used=", 0
msg_quota_serial_ref:  db " refusals=", 0
msg_quota_serial_ok:   db " QUOTA_OK", 10, 0
msg_quota_rebuild_word: db "rebuild", 0
msg_quota_set_word:  db "set", 0
msg_quota_rebuilt:   db "Quota ledger rebuilt: ", 0
msg_quota_set:       db "Quota: uid ", 0
msg_quota_to:        db " limited to ", 0
msg_quota_unlimited: db " (unlimited)", 0
msg_quota_ledger_full: db "Quota: the ledger has no free slot", 0x0A, 0
