;=======================================================================
; blkcache.inc - unified block cache in front of the ATA data path
;
; Every HBFS data read (program load, `cat`, fd_read, demand-paged file
; mappings) fetches whole 4 KB blocks from the same place, so one small cache
; in front of the device serves all of them:
;
;   blk_cache_read   - read sectors, served from the cache when possible
;   blk_cache_flush  - drop every cached block
;
; The cache is keyed by absolute 4 KB block number and is fully associative
; with a round-robin hand for replacement. Only whole, block-aligned reads are
; cached; anything else passes straight through to ata_read_sectors, so the
; metadata and journal paths keep their existing behaviour.
;
; Coherence is deliberately blunt: every successful disk write drops the whole
; cache (ata_write_sectors calls blk_cache_flush). HBFS has one writer and no
; write-back, so a stale block can never survive a write, and the alternative
; - per-block invalidation - would need every writer to know which blocks it
; touched.
;=======================================================================

BLK_CACHE_ENTRIES  equ 32                       ; 32 x 4 KB = 128 KB
BLK_CACHE_SECTORS  equ HBFS_SECTORS_PER_BLK     ; one block per entry

; One lock for tags, data, the hand and the device behind them. The scan, the
; fill and the flush are one critical section each: two processors filling the
; same entry would leave one of them reading a block that is being overwritten,
; and the device itself has one command port, so two reads in flight is not a
; cache problem to be solved - it is a reason to serialise the path.
;
; The lock is held across the ATA read on a miss, which is a disk access. That
; is deliberate: the alternative is a per-entry state machine, and the cache
; exists to make the common path one memory copy. The rank is what keeps it
; safe - the block cache may take the frame allocator's lock or the VFS's, and
; never the other way round.
align 16
blk_cache_lock:
        dd 0                            ; LK_LOCKED
        dd RANK_BLOCKCACHE
        dd -1
        dd blk_cache_lock_name
blk_cache_lock_name: db "block cache", 0
blk_cache_waits:    dd 0

;-----------------------------------------------------------------------
; blk_cache_read - read sectors, using the cache for whole blocks
;   In : EAX = LBA, ECX = sector count, EDI = destination
;   Out: CF=0 on success, CF=1 on I/O error
;
; A drop-in replacement for ata_read_sectors at every whole-block call site.
;-----------------------------------------------------------------------
blk_cache_read:
        pushad
        mov [.bcr_lba], eax
        push edi
        mov edi, blk_cache_lock
        call spin_lock
        pop edi
        test eax, eax
        jnz .bcr_locked_out

        mov [.bcr_dst], edi
        cmp ecx, BLK_CACHE_SECTORS
        jne .bcr_passthru
        test eax, BLK_CACHE_SECTORS - 1
        jnz .bcr_passthru               ; not block aligned

        mov ebx, eax
        shr ebx, 3                      ; block number
        inc ebx                         ; tags store block + 1; 0 means empty

        ; --- Lookup ---
        mov esi, blk_cache_tags
        mov edi, blk_cache_data
        mov ecx, BLK_CACHE_ENTRIES
.bcr_scan:
        cmp [esi], ebx
        je .bcr_hit
        add esi, 4
        add edi, HBFS_BLOCK_SIZE
        dec ecx
        jnz .bcr_scan

        ; --- Miss: load the block into the entry the hand points at ---
        mov eax, [blk_cache_hand]
        and eax, BLK_CACHE_ENTRIES - 1
        inc dword [blk_cache_hand]
        mov [.bcr_slot], eax

        mov edi, eax
        shl edi, 12
        add edi, blk_cache_data
        mov eax, [.bcr_lba]
        mov ecx, BLK_CACHE_SECTORS
        call ata_read_sectors
        jc .bcr_fail

        mov esi, [.bcr_slot]
        mov [blk_cache_tags + esi*4], ebx

.bcr_hit:
        ; EDI = cached block, copy it to the caller's buffer
        mov esi, edi
        mov edi, [.bcr_dst]
        mov ecx, HBFS_BLOCK_SIZE / 4
        cld
        rep movsd
        mov edi, blk_cache_lock
        call spin_unlock
        popad
        clc
        ret

.bcr_passthru:
        mov eax, [esp + 28]             ; EAX slot of the pushad frame
        mov ecx, [esp + 24]             ; ECX slot
        mov edi, [esp + 0]              ; EDI slot
        call ata_read_sectors
        jc .bcr_fail
        mov edi, blk_cache_lock
        call spin_unlock
        popad
        clc
        ret

.bcr_fail:
        mov edi, blk_cache_lock
        call spin_unlock
        popad
        stc
        ret

.bcr_locked_out:
        ; The rank gate refused the lock (only possible with the gate disarmed,
        ; in its own self-test): report an I/O error rather than corrupting the
        ; cache.
        inc dword [blk_cache_waits]
        popad
        stc
        ret
.bcr_lba:  dd 0
.bcr_dst:  dd 0
.bcr_slot: dd 0

;-----------------------------------------------------------------------
; blk_cache_flush - drop every cached block
;   Called from ata_write_sectors: a write may change any block.
;-----------------------------------------------------------------------
blk_cache_flush:
        push eax
        push ecx
        push edi
        mov edi, blk_cache_lock
        call spin_lock
        test eax, eax
        jnz .bcf_out
        mov edi, blk_cache_tags
        xor eax, eax
        mov ecx, BLK_CACHE_ENTRIES
        cld
        rep stosd
        mov edi, blk_cache_lock
        call spin_unlock
.bcf_out:
        pop edi
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; blk_cache_init - mark every entry empty and rewind the hand
;-----------------------------------------------------------------------
blk_cache_init:
        mov dword [blk_cache_hand], 0
        jmp blk_cache_flush

;-----------------------------------------------------------------------
; The cache's own index is only touched under the lock, so the "static local"
; slots in blk_cache_read are safe again: two processors can no longer be
; inside it at once. Before the lock they were a corruption waiting for a
; second processor - the exact bug the stress harness was written to find.
;-----------------------------------------------------------------------
