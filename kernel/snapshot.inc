;=======================================================================
; HBFS snapshots - copy-on-write block preservation
;
; A snapshot is a set of blocks whose content must stay as of the moment it was
; taken. There is one, it covers the whole volume, and it exists to answer the
; question conformance item 6 asks: can this filesystem take a snapshot mid-
; write, read the files it holds, and return every block it saved when it is
; dropped?
;
; What is kept
;   Every block written while a snapshot exists is first copied, old content
;   intact, to a free block. The pair {source LBA, saved LBA} is appended to the
;   snapshot's map. A block is copied once, not once per write: the first copy
;   is the snapshot's view for as long as the snapshot lives.
;
; Where it lives
;   A contiguous run of HBFS_SNAP_BLOCKS data blocks, found by
;   HBFS_SB_SNAP in the superblock. Block 0 of the run is a header (magic,
;   version, entry count, capacity, and the superblock as of the snapshot);
;   the rest is the map, 8 bytes per entry.
;
; The write hook
;   hbfs_snap_preserve is called from the two places every write in this kernel
;   passes through - hbfs_meta_write for metadata and blk_cache_write for file
;   data - before the write lands. Preserving a block allocates and writes,
;   which are themselves writes, so a guard covers the recursion. The
;   consequence is intended: the bitmap and the superblock are preserved like
;   anything else the first time they change, so the snapshot has its own view
;   of what was allocated and where the tree was.
;
; What it costs
;   One free block per preserved block, for as long as the snapshot exists. A
;   full map refuses the write rather than growing the map behind the caller's
;   back, and says so.
;
; The record's own blocks are never preserved: the map cannot describe itself.
;=======================================================================

HBFS_SNAP_MAGIC       equ 0x50414E53   ; 'SNAP' in the header
HBFS_SNAP_VERSION     equ 1
HBFS_SNAP_BLOCKS      equ 9            ; one header block, eight map blocks
HBFS_SNAP_MAP_BLOCKS  equ HBFS_SNAP_BLOCKS - 1
HBFS_SNAP_ENTRY       equ 8            ; {source LBA, saved LBA}
HBFS_SNAP_CAPACITY    equ (HBFS_SNAP_MAP_BLOCKS * HBFS_BLOCK_SIZE) / HBFS_SNAP_ENTRY
; Header fields, in the first sector of the record.
HBFS_SNAP_HDR_MAGIC    equ 0
HBFS_SNAP_HDR_VERSION  equ 4
HBFS_SNAP_HDR_ENTRIES  equ 8
HBFS_SNAP_HDR_CAPACITY equ 12
HBFS_SNAP_HDR_EPOCH    equ 16
HBFS_SNAP_HDR_SB       equ 32          ; the superblock as of the snapshot

; Snapshot state. `active` is what the hooks test on the fast path.
hbfs_snap_active:  dd 0
hbfs_snap_block:   dd 0                ; first data block of the record, 0 = none
hbfs_snap_lba:     dd 0                ; its first LBA
hbfs_snap_count:   dd 0                ; map entries in use
hbfs_snap_guard:   dd 0                ; non-zero while preserving: no recursion
hbfs_snap_buf:     times HBFS_BLOCK_SIZE db 0
hbfs_snap_hdr:     times 512 db 0
hbfs_snap_map:     times HBFS_SNAP_CAPACITY * HBFS_SNAP_ENTRY db 0

;-----------------------------------------------------------------------
; hbfs_snap_load - take the snapshot record the superblock points at
;   Out: CF = 1 when the superblock claims a snapshot this code cannot use
;
; Called once, from the mount path. A volume that says it has a snapshot and
; then hands over a record that is not one is not a volume to write to: the
; caller turns this into a refused mount rather than an empty snapshot that
; silently preserves nothing.
;-----------------------------------------------------------------------
hbfs_snap_load:
        pushad
        mov dword [hbfs_snap_active], 0
        mov dword [hbfs_snap_block],  0
        mov dword [hbfs_snap_lba],    0
        mov dword [hbfs_snap_count],  0

        mov eax, [hbfs_super_buf + HBFS_SB_SNAP]
        test eax, eax
        jz .hsl_none
        cmp eax, [hbfs_total_blocks]
        jae .hsl_bad
        mov [hbfs_snap_block], eax
        mov ecx, eax
        shl eax, 3
        add eax, HBFS_DATA_START
        mov [hbfs_snap_lba], eax
        ; The run has to fit inside the data area.
        add ecx, HBFS_SNAP_BLOCKS
        cmp ecx, [hbfs_total_blocks]
        ja .hsl_bad

        ; The header, then the map.
        mov eax, [hbfs_snap_lba]
        mov ecx, 1
        mov edi, hbfs_snap_hdr
        call ata_read_sectors
        jc .hsl_bad
        cmp dword [hbfs_snap_hdr + HBFS_SNAP_HDR_MAGIC], HBFS_SNAP_MAGIC
        jne .hsl_bad
        cmp dword [hbfs_snap_hdr + HBFS_SNAP_HDR_VERSION], HBFS_SNAP_VERSION
        jne .hsl_bad
        mov eax, [hbfs_snap_hdr + HBFS_SNAP_HDR_CAPACITY]
        cmp eax, HBFS_SNAP_CAPACITY
        jne .hsl_bad
        mov eax, [hbfs_snap_hdr + HBFS_SNAP_HDR_ENTRIES]
        cmp eax, HBFS_SNAP_CAPACITY
        ja .hsl_bad
        mov [hbfs_snap_count], eax

        mov eax, [hbfs_snap_lba]
        add eax, HBFS_SECTORS_PER_BLK
        mov ecx, HBFS_SNAP_MAP_BLOCKS * HBFS_SECTORS_PER_BLK
        mov edi, hbfs_snap_map
        call ata_read_sectors
        jc .hsl_bad

        mov dword [hbfs_snap_active], 1
        popad
        clc
        ret
.hsl_none:
        popad
        clc
        ret
.hsl_bad:
        mov dword [hbfs_snap_active], 0
        popad
        stc
        ret

;-----------------------------------------------------------------------
; hbfs_snap_find - is this block already preserved?
;   In : EBX = source LBA
;   Out: EAX = saved LBA, CF = 1 when the block is not in the map
;-----------------------------------------------------------------------
hbfs_snap_find:
        push ebx
        push ecx
        push esi
        xor ecx, ecx
        mov esi, hbfs_snap_map
.hsf_loop:
        cmp ecx, [hbfs_snap_count]
        jae .hsf_none
        cmp [esi], ebx
        je .hsf_found
        add esi, HBFS_SNAP_ENTRY
        inc ecx
        jmp .hsf_loop
.hsf_found:
        mov eax, [esi + 4]
        pop esi
        pop ecx
        pop ebx
        clc
        ret
.hsf_none:
        pop esi
        pop ecx
        pop ebx
        stc
        ret

;-----------------------------------------------------------------------
; hbfs_snap_append - add one {source, saved} pair
;   In : EAX = source LBA, EBX = saved LBA
;   Out: CF = 1 when the map is full or the write failed
;
; The entry goes to the map block on disk only, sector by sector: the sector
; holding entry n is rewritten from the resident copy, so appending costs one
; 512-byte journaled write plus the header's count.
;-----------------------------------------------------------------------
hbfs_snap_append:
        pushad
        mov [.hsa_src], eax
        mov [.hsa_dst], ebx
        mov eax, [hbfs_snap_count]
        cmp eax, HBFS_SNAP_CAPACITY
        jae .hsa_full

        ; Put it in the resident map first: the disk write below reads from
        ; there, so the two can never disagree.
        mov ecx, eax
        shl ecx, 3                      ; byte offset of the entry
        mov edi, hbfs_snap_map
        add edi, ecx
        mov eax, [.hsa_src]
        mov [edi], eax
        mov eax, [.hsa_dst]
        mov [edi + 4], eax

        ; Write the 512-byte sector that holds it.
        mov eax, [hbfs_snap_count]
        shl eax, 3
        mov ecx, eax
        and eax, ~511                   ; sector offset within the map
        shr ecx, 9                      ; sector index
        mov [.hsa_off], eax
        mov eax, [hbfs_snap_lba]
        add eax, HBFS_SECTORS_PER_BLK   ; past the header
        add eax, ecx
        mov [.hsa_lba], eax
        mov esi, hbfs_snap_map
        add esi, [.hsa_off]
        mov ecx, 1
        call hbfs_meta_write
        jc .hsa_fail

        ; The header's count, so a snapshot loaded after a power cut knows how
        ; much of the map is real.
        inc dword [hbfs_snap_count]
        mov eax, [hbfs_snap_count]
        mov [hbfs_snap_hdr + HBFS_SNAP_HDR_ENTRIES], eax
        mov eax, [hbfs_snap_lba]
        mov ecx, 1
        mov esi, hbfs_snap_hdr
        call hbfs_meta_write
        jc .hsa_fail_undo
        popad
        clc
        ret
.hsa_fail_undo:
        dec dword [hbfs_snap_count]
        mov eax, [hbfs_snap_count]
        mov [hbfs_snap_hdr + HBFS_SNAP_HDR_ENTRIES], eax
.hsa_fail:
        popad
        stc
        ret
.hsa_full:
        popad
        stc
        ret
.hsa_src: dd 0
.hsa_dst: dd 0
.hsa_off: dd 0
.hsa_lba: dd 0

;-----------------------------------------------------------------------
; hbfs_snap_block_lba - the LBA that starts the block containing one
;   In : EAX = LBA
;   Out: EAX = the first LBA of that block
;
; The data area does not start on an LBA that is a multiple of the sectors in a
; block, so its grid is anchored at HBFS_DATA_START rather than at zero. Rounding
; down to a multiple of eight instead lands on the middle of the neighbouring
; block - which is how a dropped snapshot once freed a block a file was using
; and left its own copy allocated.
;-----------------------------------------------------------------------
hbfs_snap_block_lba:
        cmp eax, HBFS_DATA_START
        jb .hsb_region
        sub eax, HBFS_DATA_START
        and eax, ~(HBFS_SECTORS_PER_BLK - 1)
        add eax, HBFS_DATA_START
        ret
.hsb_region:
        and eax, ~(HBFS_SECTORS_PER_BLK - 1)
        ret

;-----------------------------------------------------------------------
; hbfs_snap_preserve - keep the old content of a block before it changes
;   In : EBX = LBA about to be written
;   Out: CF = 1 when it could not be preserved and the write must be refused
;
; Called from both write paths. Returns at once when no snapshot is live, when
; the write is itself part of preserving (the guard), or when the LBA belongs to
; the snapshot's own record - a map cannot describe itself.
;-----------------------------------------------------------------------
hbfs_snap_preserve:
        cmp dword [hbfs_snap_active], 0
        je .hsp_ok
        cmp dword [hbfs_snap_guard], 0
        jne .hsp_ok
        cmp dword [hbfs_read_only], 0
        jne .hsp_ok
        mov eax, ebx
        sub eax, [hbfs_snap_lba]
        cmp eax, HBFS_SNAP_BLOCKS * HBFS_SECTORS_PER_BLK
        jb .hsp_ok                     ; inside the record: nothing to keep

        mov eax, ebx
        call hbfs_snap_block_lba               ; the block this LBA belongs to
        mov ebx, eax
        call hbfs_snap_find
        jnc .hsp_ok                    ; already kept

        pushad
        mov [.hsp_src], ebx
        ; Read the block as it is now, whole.
        mov eax, ebx
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_snap_buf
        call ata_read_sectors
        jc .hsp_fail

        ; A home for the copy. The allocation writes the bitmap and the free
        ; count, and neither is the snapshot's subject - the guard keeps them
        ; out of the map.
        ; One transaction around the whole preserve: the copy's allocation, the
        ; copy itself and the map entry that names it. A power cut between two
        ; of those would otherwise leave a block allocated that nothing names,
        ; which is one leaked block per interrupted write.
        inc dword [hbfs_snap_guard]
        call hbfs_journal_begin
        call hbfs_alloc_block
        jc .hsp_abort_fail
        mov [.hsp_block], eax
        mov ebx, eax
        shl ebx, 3
        add ebx, HBFS_DATA_START       ; saved LBA
        mov [.hsp_dst], ebx

        mov eax, ebx
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_snap_buf
        call blk_cache_write
        jc .hsp_abort_fail

        mov eax, [.hsp_src]
        mov ebx, [.hsp_dst]
        call hbfs_snap_append
        jc .hsp_abort_fail
        call hbfs_journal_commit
        dec dword [hbfs_snap_guard]
        popad
        clc
        ret
.hsp_abort_fail:
        call hbfs_journal_abort
.dec_guard:
        dec dword [hbfs_snap_guard]
.hsp_fail:
        ; A snapshot that cannot keep a block must not let the write through:
        ; the caller refuses it, so the snapshot stays true and the operator
        ; is told why.
        popad
        stc
        ret
.hsp_ok:
        clc
        ret
.hsp_src:   dd 0
.hsp_dst:   dd 0
.hsp_block: dd 0

;-----------------------------------------------------------------------
; hbfs_snap_create - take a snapshot of the volume as it is now
;   Out: CF = 1 refused (one exists, read-only, no room, I/O error)
;
; The record is built before the volume claims it: the superblock's pointer and
; feature bit are the last thing written, in the same transaction, so a power
; cut leaves either a volume with no snapshot or one whose record is complete.
;-----------------------------------------------------------------------
hbfs_snap_create:
        pushad
        call hbfs_require_writable
        jc .hsc_bad
        cmp dword [hbfs_snap_active], 0
        jne .hsc_exists
        cmp dword [hbfs_super_buf + HBFS_SB_SNAP], 0
        jne .hsc_exists

        call hbfs_journal_begin
        inc dword [hbfs_snap_guard]     ; the record's own blocks are not preserved

        mov ecx, HBFS_SNAP_BLOCKS
        call hbfs_alloc_blocks
        jc .hsc_fail
        mov [.hsc_block], eax
        mov [hbfs_snap_block], eax
        mov ebx, eax
        shl eax, 3
        add eax, HBFS_DATA_START
        mov [.hsc_lba], eax
        mov [hbfs_snap_lba], eax

        ; A zeroed run, so the map reads as "no entry" wherever it is read
        ; before being written.
        mov edi, hbfs_snap_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        xor eax, eax
        cld
        rep stosd
        mov ecx, HBFS_SNAP_BLOCKS
        mov eax, [.hsc_lba]
        mov esi, hbfs_snap_buf
        call blk_cache_write
        jc .hsc_fail

        ; The header: what this record is, and the volume as of now.
        mov edi, hbfs_snap_hdr
        mov ecx, 512 / 4
        xor eax, eax
        rep stosd
        mov dword [hbfs_snap_hdr + HBFS_SNAP_HDR_MAGIC], HBFS_SNAP_MAGIC
        mov dword [hbfs_snap_hdr + HBFS_SNAP_HDR_VERSION], HBFS_SNAP_VERSION
        mov dword [hbfs_snap_hdr + HBFS_SNAP_HDR_ENTRIES], 0
        mov dword [hbfs_snap_hdr + HBFS_SNAP_HDR_CAPACITY], HBFS_SNAP_CAPACITY
        mov eax, [hbfs_journal_epoch]
        mov [hbfs_snap_hdr + HBFS_SNAP_HDR_EPOCH], eax
        ; The superblock as it stands: the geometry, the free count and the
        ; label of the volume the snapshot was taken of.
        mov esi, hbfs_super_buf
        lea edi, [hbfs_snap_hdr + HBFS_SNAP_HDR_SB]
        mov ecx, 512 / 4
        cld
        rep movsd
        mov eax, [.hsc_lba]
        mov ecx, 1
        mov esi, hbfs_snap_hdr
        call hbfs_meta_write
        jc .hsc_fail

        ; Now the volume may say it has one.
        mov eax, [.hsc_block]
        mov [hbfs_super_buf + HBFS_SB_SNAP], eax
        ; A v3 volume has no feature word this code may set; a v4 volume
        ; records the feature the reader checks for.
        cmp dword [hbfs_super_buf], HBFS4_MAGIC
        jne .hsc_no_feature
        or dword [hbfs_super_buf + HBFS4_OFF_FEATURES], HBFS4_FEATURE_SNAPSHOT
.hsc_no_feature:
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call hbfs_meta_write
        jc .hsc_fail

        dec dword [hbfs_snap_guard]
        call hbfs_journal_commit

        mov dword [hbfs_snap_count], 0
        mov dword [hbfs_snap_active], 1
        popad
        clc
        ret
.hsc_fail:
        dec dword [hbfs_snap_guard]
        mov dword [hbfs_super_buf + HBFS_SB_SNAP], 0
        call hbfs_journal_abort
        popad
        stc
        ret
.hsc_exists:
        mov esi, msg_snap_exists
        call vga_print
        popad
        stc
        ret
.hsc_bad:
        stc
        ret
.hsc_block: dd 0
.hsc_lba:   dd 0

;-----------------------------------------------------------------------
; hbfs_snap_drop - discard the snapshot and give back every block it kept
;   Out: CF = 1 when there is no snapshot or a write failed
;
; This is the half of the guarantee that says the free count comes back: each
; saved copy is freed, then the record's own run, then the volume stops
; claiming a snapshot.
;-----------------------------------------------------------------------
hbfs_snap_drop:
        pushad
        call hbfs_require_writable
        jc .hsd_bad
        cmp dword [hbfs_snap_active], 0
        jne .hsd_live
        cmp dword [hbfs_super_buf + HBFS_SB_SNAP], 0
        je .hsd_none
        ; The superblock claims one this boot did not load: read it first, so
        ; its copies are not simply forgotten and leaked.
        call hbfs_snap_load
        jc .hsd_bad
.hsd_live:
        call hbfs_journal_begin

        mov ebx, 0
.hsd_free_loop:
        cmp ebx, [hbfs_snap_count]
        jae .hsd_free_record
        mov eax, ebx
        shl eax, 3
        mov esi, hbfs_snap_map
        add esi, eax
        mov eax, [esi + 4]             ; saved LBA
        cmp eax, HBFS_DATA_START
        jb .hsd_next                   ; not a data block: nothing to give back
        sub eax, HBFS_DATA_START
        test eax, 7
        jnz .hsd_fail                  ; a copy starts on a block boundary
        shr eax, 3                     ; its block
        mov [.hsd_block], eax
        mov ecx, 1
        call hbfs_free_blocks
        jc .hsd_fail
.hsd_next:
        inc ebx
        jmp .hsd_free_loop
.hsd_free_record:
        mov eax, [hbfs_snap_block]
        test eax, eax
        jz .hsd_clear
        mov ecx, HBFS_SNAP_BLOCKS
        call hbfs_free_blocks
        jc .hsd_fail
.hsd_clear:
        mov dword [hbfs_super_buf + HBFS_SB_SNAP], 0
        cmp dword [hbfs_super_buf], HBFS4_MAGIC
        jne .hsd_no_feature
        and dword [hbfs_super_buf + HBFS4_OFF_FEATURES], ~HBFS4_FEATURE_SNAPSHOT
.hsd_no_feature:
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call hbfs_meta_write
        jc .hsd_fail

        call hbfs_journal_commit
        mov dword [hbfs_snap_active], 0
        mov dword [hbfs_snap_count],  0
        mov dword [hbfs_snap_block],  0
        mov dword [hbfs_snap_lba],    0
        popad
        clc
        ret
.hsd_fail:
        call hbfs_journal_abort
        popad
        stc
        ret
.hsd_none:
        mov esi, msg_snap_none
        call vga_print
        popad
        stc
        ret
.hsd_bad:
        stc
        ret
.hsd_block: dd 0

;-----------------------------------------------------------------------
; cmd_snapshot - `snapshot [create|info|drop]`
;-----------------------------------------------------------------------
snap_word_create: db "create", 0
snap_word_info:   db "info", 0
snap_word_drop:   db "drop", 0
snap_word_delete: db "delete", 0

cmd_snapshot:
        pushad
        call skip_spaces
        cmp byte [esi], 0
        je .cs_info
        ; str_starts_with sets CF on a match and consumes the word, which is
        ; why the order matters: each test sees what the last one left.
        mov edi, snap_word_create
        call str_starts_with
        jc .cs_create
        mov edi, snap_word_info
        call str_starts_with
        jc .cs_info
        mov edi, snap_word_drop
        call str_starts_with
        jc .cs_drop
        mov edi, snap_word_delete
        call str_starts_with
        jc .cs_drop
        mov esi, msg_snap_usage
        call vga_print
        popad
        ret
.cs_create:
        call require_root
        test eax, eax
        jnz .cs_denied
        call hbfs_snap_create
        jc .cs_create_failed
        mov esi, msg_snap_created
        call vga_print
        call snap_report
        popad
        ret
.cs_create_failed:
        ; hbfs_snap_create says "one is already taken" itself; only an
        ; unexplained failure gets the generic line.
        cmp dword [hbfs_super_buf + HBFS_SB_SNAP], 0
        jne .cs_out
        mov esi, msg_snap_create_failed
        call vga_print
        popad
        ret
.cs_drop:
        call require_root
        test eax, eax
        jnz .cs_denied
        mov eax, [hbfs_snap_count]
        mov [snap_held], eax
        call hbfs_snap_drop
        jc .cs_out
        mov esi, msg_snap_dropped
        call vga_print
        mov eax, [snap_held]
        call vga_print_dec
        mov esi, msg_snap_dropped_copies
        call vga_print
        popad
        ret
.cs_info:
        call snap_report
        popad
        ret
.cs_denied:
        mov esi, msg_snap_denied
        call vga_print
.cs_out:
        popad
        ret

;-----------------------------------------------------------------------
; snap_report - say what the snapshot is holding
;-----------------------------------------------------------------------
snap_report:
        cmp dword [hbfs_snap_active], 0
        je .sr_none
        mov esi, msg_snap_is
        call vga_print
        mov eax, [hbfs_snap_count]
        call vga_print_dec
        mov esi, msg_snap_of
        call vga_print
        mov eax, HBFS_SNAP_CAPACITY
        call vga_print_dec
        mov esi, msg_snap_entries
        call vga_print
        ret
.sr_none:
        mov esi, msg_snap_none
        call vga_print
        ret

snap_held: dd 0

msg_snap_created:  db "snapshot: taken, ", 0
msg_snap_dropped:  db "snapshot: dropped, ", 0
msg_snap_dropped_copies: db " block(s) returned to the free list", 0x0A, 0
msg_snap_is:       db "Snapshot: ", 0
msg_snap_of:       db " of ", 0
msg_snap_entries:  db " map entries used", 0x0A, 0
msg_snap_none:     db "snapshot: none taken", 0x0A, 0
msg_snap_exists:   db "snapshot: one is already taken", 0x0A, 0
msg_snap_denied:   db "snapshot: root only", 0x0A, 0
msg_snap_create_failed: db "snapshot: could not be taken (no room or write failed)", 0x0A, 0
msg_snap_usage:    db "snapshot [create|info|drop]", 0x0A, 0
msg_snap_full:     db 0x0A, "HBFS: snapshot map full, write refused", 0x0A, 0
