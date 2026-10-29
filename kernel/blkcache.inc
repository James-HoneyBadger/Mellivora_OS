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
        ; A run of several blocks is walked one block at a time: the cache and
        ; the volume transform both work in 4 KiB units, and a run that skipped
        ; them would hand back ciphertext.
        ;
        ; The walk's cursor lives in the saved registers, not in statics: the
        ; body calls this function again, and a preempted task that entered the
        ; same path would overwrite a shared cursor and copy blocks from
        ; another file's LBA. Everything above the pushad is ours alone, and
        ; the nested call preserves all of it.
        cmp ecx, BLK_CACHE_SECTORS
        jbe .bcr_single
        test ecx, BLK_CACHE_SECTORS - 1
        jnz .bcr_single                 ; not whole blocks: let the checks speak
        mov ebx, eax                    ; current LBA
        mov edx, ecx                    ; sectors left
        mov ebp, edi                    ; destination
.bcr_run_loop:
        mov eax, ebx
        mov ecx, BLK_CACHE_SECTORS
        mov edi, ebp
        call blk_cache_read
        jc .bcr_run_fail
        add ebx, BLK_CACHE_SECTORS
        add ebp, HBFS_BLOCK_SIZE
        sub edx, BLK_CACHE_SECTORS
        jnz .bcr_run_loop
        popad
        clc
        ret
.bcr_run_fail:
        popad
        stc
        ret

.bcr_single:
        mov [.bcr_lba], eax
        push edi
        mov edi, blk_cache_lock
        call spin_lock
        pop edi
        test eax, eax
        jnz .bcr_locked_out

        ; The lock returned its status in EAX, which was the caller's LBA.
        ; Recovering it from the saved copy is not tidiness: taking the status
        ; for a block number makes every read address block zero, and the bug
        ; looks like corrupt files rather than like a locking mistake.
        mov eax, [.bcr_lba]
        mov [.bcr_dst], edi
        cmp ecx, BLK_CACHE_SECTORS
        jne .bcr_passthru
        ; A 4 KiB block of file data starts on a boundary measured from the
        ; data area, not from LBA zero: HBFS_DATA_START is not a multiple of
        ; eight sectors, so an absolute test leaves every block on the
        ; passthrough path - which is also the path that does not decrypt.
        cmp eax, HBFS_DATA_START
        jb .bcr_passthru                ; metadata is never cached
        sub eax, HBFS_DATA_START
        test eax, BLK_CACHE_SECTORS - 1
        jnz .bcr_passthru               ; not a whole data block

        mov ebx, eax
        shr ebx, 3                      ; data-area block number
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

        ; The cache holds what is on the disk - ciphertext on an encrypted
        ; volume - and the transform happens on the way to the caller, so a
        ; cache hit and a cache miss return the same thing.
        ;
        ; The decode runs after the cache lock is dropped. The cipher has a
        ; lock of its own, ordered below this one so that a write can hold it
        ; and still let the device write flush the cache underneath; taking it
        ; here would invert that order. The three arguments are read before the
        ; unlock because the statics they come from are another caller's to
        ; overwrite the moment the lock is gone.
        mov eax, [.bcr_lba]
        mov ecx, BLK_CACHE_SECTORS
        mov edi, [.bcr_dst]
        push eax
        push ecx
        push edi
        mov edi, blk_cache_lock
        call spin_unlock
        pop edi
        pop ecx
        pop eax
        call hbfs_crypt_decode
        popad
        jc .bcr_dec_fail
        clc
        ret
.bcr_dec_fail:
        ; The block is in the region but the cipher could not open it (the keys
        ; were cleared). Ciphertext must not be handed back as if it were data.
        stc
        ret

.bcr_passthru:
        ; A partial access into the encrypted region has no correct answer that
        ; is cheap, so it is an error rather than ciphertext in a file.
        mov eax, [esp + 28]
        mov ecx, [esp + 24]
        call hbfs_crypt_partial
        jc .bcr_fail

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
; blk_cache_write - write sectors, encrypting whole blocks on the way out
;   In : EAX = LBA, ECX = sector count, ESI = source
;   Out: CF=0 on success, CF=1 on I/O error or a partial write into the
;        encrypted region.
;
; The paired half of blk_cache_read. Everything that writes file data goes
; through here so that the transform cannot be forgotten at one call site: a
; block that bypasses it is unreadable afterwards, and the only way to keep
; that true is to have one door.
;
; It takes the cipher's lock, not the cache's. The device write underneath
; drops the block cache - that is what keeps the two coherent - and dropping it
; takes the cache lock; taking that lock here as well would be this processor
; waiting for itself, which the order gate refuses by stopping the machine.
;-----------------------------------------------------------------------
blk_cache_write:
        pushad
        mov [.bcw_lba], eax
        mov [.bcw_cnt], ecx
        mov [.bcw_src], esi

        mov eax, [.bcw_lba]
        mov ecx, [.bcw_cnt]
        call hbfs_crypt_partial
        jc .bcw_partial

        mov eax, [.bcw_lba]
        mov ecx, [.bcw_cnt]
        call hbfs_crypt_in_region
        jnc .bcw_plain

        mov edi, blk_crypt_lock
        call spin_lock
        test eax, eax
        jnz .bcw_locked_out

        ; Encrypt block by block through one scratch buffer, so the caller's
        ; data is not modified and the device still sees whole blocks.
        mov eax, [.bcw_cnt]
        shr eax, 3
        mov [.bcw_blocks], eax
        mov eax, [.bcw_lba]
        mov [.bcw_cur_lba], eax
        mov eax, [.bcw_lba]
        sub eax, [hbfs_crypt_first_lba]
        shr eax, 3                      ; the first block's XTS unit
        mov [.bcw_cur_blk], eax
        mov eax, [.bcw_src]
        mov [.bcw_cur_src], eax
.bcw_loop:
        mov esi, [.bcw_cur_src]
        mov edi, blk_crypt_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        cld
        rep movsd

        mov eax, [.bcw_cur_blk]         ; this block's XTS unit
        mov edi, blk_crypt_buf
        xor edx, edx                    ; encrypt
        call xts_crypt_unit
        jc .bcw_fail                    ; no keys: a plain block would be wrong

        mov eax, [.bcw_cur_lba]
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, blk_crypt_buf
        call ata_write_sectors
        jc .bcw_fail

        add dword [.bcw_cur_lba], HBFS_SECTORS_PER_BLK
        add dword [.bcw_cur_src], HBFS_BLOCK_SIZE
        inc dword [.bcw_cur_blk]
        dec dword [.bcw_blocks]
        jnz .bcw_loop

        mov edi, blk_crypt_lock
        call spin_unlock
        popad
        clc
        ret

.bcw_plain:
        mov eax, [.bcw_lba]
        mov ecx, [.bcw_cnt]
        mov esi, [.bcw_src]
        call ata_write_sectors
        jc .bcw_plain_fail
        popad
        clc
        ret
.bcw_plain_fail:
        popad
        stc
        ret
.bcw_partial:
        mov esi, msg_crypt_partial
        call vga_print
        popad
        stc
        ret
.bcw_fail:
        ; Reached only from the encrypted loop, which holds the cipher's lock.
        mov edi, blk_crypt_lock
        call spin_unlock
        popad
        stc
        ret
.bcw_locked_out:
        popad
        stc
        ret
.bcw_lba:      dd 0
.bcw_cnt:      dd 0
.bcw_src:      dd 0
.bcw_blocks:   dd 0
.bcw_cur_lba:  dd 0
.bcw_cur_src:  dd 0
.bcw_cur_blk:  dd 0

msg_crypt_partial: db 0x0A, "HBFS: refusing a partial access to encrypted space", 0x0A, 0

align 16
blk_crypt_buf: times HBFS_BLOCK_SIZE db 0

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
