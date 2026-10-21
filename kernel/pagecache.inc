;=======================================================================
; pagecache.inc - one frame per file block, shared by every mapping of it
;
; SYS_MMAP_FILE with MF_SHARED maps a file's pages into user space and writes
; what the program changed back. A per-mapping frame cannot do that coherently:
; two mappings of the same block would each keep a private copy, so neither
; would see the other's writes and the last one released would win. The page
; cache therefore owns one frame per file block and every shared mapping of
; that block maps the same frame - a write through one mapping is visible
; through the others because it is literally the same memory.
;
; The key is the HBFS block number, counted from HBFS_DATA_START. A file's first
; data sector is block-aligned within the file, but the data area itself does not
; start on a block boundary of the disk, so the raw sector cannot be shifted
; down into a block number.
;
; Reference counting: pc_refs counts page-table entries that map the frame, not
; mappings. A demand fault that installs a PTE takes one reference, and fork
; takes one for every inherited PTE, because a shared mapping is deliberately
; not copy-on-write. An entry whose count reaches zero stays cached so the next
; mapping of the block is a hit; it is evicted (and its frame returned to the
; PMM) only when another block needs the room.
;
; Dirty state needs no bookkeeping here: a write sets the dirty bit of the PTE
; that performed it, and that mapping's own release pushes the block back
; before dropping its reference. So every modified frame has at least one dirty
; PTE that will flush it, and an entry is clean by the time its count reaches
; zero.
;
; pc_invalidate runs on every disk write, next to blk_cache_flush: a block that
; was freed and handed to another file must not keep serving the old bytes to
; the next mapping of it. Unlike the block cache, it can only drop entries
; nobody maps.
;=======================================================================

PC_ENTRIES equ 128                  ; 128 x 4 KB = 512 KB of file pages

;-----------------------------------------------------------------------
; pc_init - mark every entry empty
;-----------------------------------------------------------------------
pc_init:
        push eax
        push ecx
        push edi
        mov edi, pc_tags
        xor eax, eax
        mov ecx, PC_ENTRIES
        cld
        rep stosd
        mov [pc_hand], eax              ; EAX is still 0
        pop edi
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; pc_lookup - entry holding a block
;   In : EAX = block number        Out: EAX = entry index, or -1
;-----------------------------------------------------------------------
pc_lookup:
        push ecx
        push esi
        inc eax                         ; tags store block + 1; 0 means empty
        mov esi, pc_tags
        mov ecx, PC_ENTRIES
.pl_scan:
        cmp [esi], eax
        je .pl_hit
        add esi, 4
        dec ecx
        jnz .pl_scan
        mov eax, -1
        pop esi
        pop ecx
        ret
.pl_hit:
        mov eax, PC_ENTRIES
        sub eax, ecx
        pop esi
        pop ecx
        ret

;-----------------------------------------------------------------------
; pc_entry_of_frame - entry backing a frame
;   In : EAX = frame address       Out: EAX = entry index, or -1
;-----------------------------------------------------------------------
pc_entry_of_frame:
        push ecx
        push esi
        mov esi, pc_frames
        mov ecx, PC_ENTRIES
.pe_scan:
        cmp [esi], eax
        je .pe_hit
        add esi, 4
        dec ecx
        jnz .pe_scan
        mov eax, -1
        pop esi
        pop ecx
        ret
.pe_hit:
        mov eax, PC_ENTRIES
        sub eax, ecx
        pop esi
        pop ecx
        ret

;-----------------------------------------------------------------------
; pc_slot - index to load a block into, evicting a clean entry
;   Out: EAX = entry index, or -1 when every entry is still mapped
;
; Pass 1 takes a slot that was never used. Pass 2 evicts the first unmapped
; entry found from the round-robin hand. An entry with references is never
; evicted: some address space still maps that frame.
;-----------------------------------------------------------------------
pc_slot:
        push ecx
        push edx
        push esi
        mov esi, pc_tags
        xor ecx, ecx
.ps_free:
        cmp dword [esi], 0
        je .ps_take
        add esi, 4
        inc ecx
        cmp ecx, PC_ENTRIES
        jb .ps_free

        mov ecx, PC_ENTRIES
        mov eax, [pc_hand]
.ps_try:
        and eax, PC_ENTRIES - 1
        cmp byte [pc_refs + eax], 0
        je .ps_evict
        inc eax
        dec ecx
        jnz .ps_try
        mov eax, -1
        pop esi
        pop edx
        pop ecx
        ret
.ps_evict:
        mov [pc_hand], eax
        mov edx, [pc_frames + eax*4]
        push eax
        mov dword [pc_tags + eax*4], 0
        mov dword [pc_frames + eax*4], 0
        mov eax, edx
        call pmm_free_page              ; clean: its last mapping flushed it
        pop eax
        pop esi
        pop edx
        pop ecx
        ret
.ps_take:
        mov eax, ecx
        mov [pc_hand], ecx              ; next eviction starts here
        pop esi
        pop edx
        pop ecx
        ret

;-----------------------------------------------------------------------
; pc_get - frame holding a file block, loaded on first use
;   In : EAX = block number
;   Out: EAX = frame, with one reference taken; 0 when the block could not be
;        cached (every entry mapped, allocation failed, or the read failed)
;-----------------------------------------------------------------------
pc_get:
        pushad
        mov [.pg_block], eax
        call pc_lookup
        cmp eax, -1
        jne .pg_have

        mov eax, [.pg_block]
        call pc_slot
        cmp eax, -1
        je .pg_fail
        mov [.pg_slot], eax

        call pmm_alloc_page
        test eax, eax
        jz .pg_fail
        mov [.pg_frame], eax

        mov edi, eax
        mov eax, [.pg_block]
        shl eax, 3                      ; 8 sectors per block
        add eax, HBFS_DATA_START        ; HBFS block numbers start at the data area
        mov ecx, HBFS_SECTORS_PER_BLK
        call blk_cache_read
        jc .pg_read_fail

        mov eax, [.pg_slot]
        mov ecx, [.pg_block]
        inc ecx
        mov [pc_tags + eax*4], ecx
        mov ecx, [.pg_frame]
        mov [pc_frames + eax*4], ecx
.pg_have:
        cmp byte [pc_refs + eax], 0xFE
        jae .pg_referenced
        inc byte [pc_refs + eax]
.pg_referenced:
        mov eax, [pc_frames + eax*4]
        mov [esp + 28], eax
        popad
        ret
.pg_read_fail:
        mov eax, [.pg_frame]
        call pmm_free_page              ; the slot stays empty
.pg_fail:
        mov dword [esp + 28], 0
        popad
        ret
.pg_block: dd 0
.pg_slot:  dd 0
.pg_frame: dd 0

;-----------------------------------------------------------------------
; pc_ref / pc_unref - take or drop one reference to a cached frame
;   In : EAX = frame
;   Out: pc_ref   - EAX = 1 when the frame is cached, 0 when it is not
;        pc_unref - EAX = 1 when the frame belongs to the cache (the caller
;                   must not free it), 0 when it does not (free it)
;-----------------------------------------------------------------------
pc_ref:
        push ecx
        call pc_entry_of_frame
        cmp eax, -1
        je .pr_none
        cmp byte [pc_refs + eax], 0xFE
        jae .pr_held
        inc byte [pc_refs + eax]
.pr_held:
        mov eax, 1
        pop ecx
        ret
.pr_none:
        xor eax, eax
        pop ecx
        ret

pc_unref:
        push ecx
        call pc_entry_of_frame
        cmp eax, -1
        je .pu_none
        cmp byte [pc_refs + eax], 0
        je .pu_held
        dec byte [pc_refs + eax]
.pu_held:
        mov eax, 1
        pop ecx
        ret
.pu_none:
        xor eax, eax
        pop ecx
        ret

;-----------------------------------------------------------------------
; pc_ref_range - count one reference per present page of a range
;   In : EAX = page directory, EBX = start (page aligned), ECX = page count
;
; fork() copies the parent's page tables, so every inherited page that maps a
; cached frame is one more reference - and, because a shared mapping is not
; copy-on-write, one more writer of the same frame.
;-----------------------------------------------------------------------
pc_ref_range:
        pushad
        mov [.prr_pd], eax
        mov [.prr_va], ebx
        mov [.prr_left], ecx
.prr_loop:
        cmp dword [.prr_left], 0
        je .prr_done
        mov eax, [.prr_pd]
        mov ebx, [.prr_va]
        call vm_pte_phys
        test eax, eax
        jz .prr_next
        call pc_ref
.prr_next:
        add dword [.prr_va], 0x1000
        dec dword [.prr_left]
        jmp .prr_loop
.prr_done:
        popad
        ret
.prr_pd:   dd 0
.prr_va:   dd 0
.prr_left: dd 0

;-----------------------------------------------------------------------
; pc_invalidate - drop cached blocks a disk write has just changed
;   In : EAX = first sector, ECX = sector count
;
; The block-cache equivalent of this is `blk_cache_flush`; the page cache
; cannot do that, because its frames may still be mapped. What it can do is
; forget the blocks nobody maps: a block that was freed and reused by another
; file must not keep serving the old bytes to the next mapping of it. An entry
; that is still mapped is left alone - whoever wrote it meant to, and that
; mapping flushes it when it goes.
;-----------------------------------------------------------------------
pc_invalidate:
        pushad
        cmp eax, HBFS_DATA_START
        jb .pi_done                     ; metadata and journal: never cached
        sub eax, HBFS_DATA_START
        shr eax, 3                      ; first block touched
        mov [.pi_next_block], eax
        add ecx, HBFS_SECTORS_PER_BLK - 1
        shr ecx, 3
        add ecx, eax                    ; first block past the range
        mov [.pi_end], ecx
.pi_loop:
        mov eax, [.pi_next_block]
        cmp eax, [.pi_end]
        jae .pi_done
        call pc_lookup
        cmp eax, -1
        je .pi_advance
        cmp byte [pc_refs + eax], 0
        jne .pi_advance                 ; still mapped: not ours to drop
        mov edx, [pc_frames + eax*4]
        mov dword [pc_tags + eax*4], 0
        mov dword [pc_frames + eax*4], 0
        mov eax, edx
        call pmm_free_page
.pi_advance:
        inc dword [.pi_next_block]
        jmp .pi_loop
.pi_done:
        popad
        ret
.pi_next_block: dd 0
.pi_end:        dd 0
