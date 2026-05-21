;=======================================================================
; HBFS - HONEY BADGER FILE SYSTEM
;
; Superblock (LBA HBFS_SUPERBLOCK_LBA, 1 sector):
;   [0-3]   Magic: 'HBFS'
;   [4-7]   Version: 1 / 2 (v9.0) / 3 (v10.0)
;   [8-11]  Total blocks
;   [12-15] Free blocks
;   [16-19] Root directory block
;   [20-23] Bitmap start block
;   [24-27] Data start block
;   [28-31] Block size (4096)
;   --- v2+ ---
;   [40]    Journal state (dword)
;   [44]    Journal write pointer (dword)
;   --- v3+ ---
;   [48]    Feature flags (dword): bit0=permissions, bit1=xattr
;   [52]    Root UID / GID (word each)
;
; Directory Entry v3 (288 bytes — same size as v1/v2):
;   [0-252]   Filename (null-terminated, 252 chars max)
;   [253]     Type (0=free, 1=file, 2=dir, 3=exec, 4=batch, 5=link)
;   [254-255] Flags
;   [256-259] File size in bytes
;   [260-263] Start block (in data area)
;   [264-267] Block count
;   [268-271] Created timestamp (packed RTC)
;   [272-275] Modified timestamp (packed RTC)
;   --- v3: previously reserved [276-287] ---
;   [276-277] Permission mode (Unix rwxrwxrwx + setuid/setgid/sticky)
;   [278-279] Owner UID (16-bit)
;   [280-281] Owner GID (16-bit)
;   [282-283] xattr entry count (0 = no xattrs)
;   [284-287] xattr block LBA  (0 = no xattrs)
;
; xattr block layout (one 4 KB block per file):
;   32 entries × 128 bytes = 4096 bytes
;   Each entry: key[64] + value[64]
;
; Block allocation bitmap:
;   128 sectors (65536 bytes) = 524288 blocks max
;   At 4KB/block = 2 GB addressable
;
;=======================================================================

; HBFS v3 feature flag bits (stored at superblock offset 48)
HBFS_FEAT_PERMS      equ (1 << 0)    ; permission bits are valid
HBFS_FEAT_XATTR      equ (1 << 1)    ; xattr blocks are present

; Superblock offset for v3 fields
HBFS_SB_FEATURES     equ 48          ; dword: feature flags
HBFS_SB_ROOT_UID     equ 52          ; word: root UID
HBFS_SB_ROOT_GID     equ 54          ; word: root GID

hbfs_init:
        pushad

        cmp byte [ata_present], 0
        je .no_disk

        ; Try to read superblock
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov edi, hbfs_super_buf
        call ata_read_sectors
        jc .format_new

        ; Check magic
        cmp dword [hbfs_super_buf], HBFS_MAGIC
        jne .format_new

        ; Filesystem exists
        mov esi, msg_hbfs_found
        call vga_print

        ; v9.0+: Check for journal recovery (v2 and v3 both use journal)
        mov  eax, [hbfs_super_buf + 4]
        cmp  eax, HBFS_VERSION_V2
        jb   .hbfs_v1_mount         ; v1: no journal
        ; Init journal runtime state from superblock
        mov  eax, [hbfs_super_buf + HBFS_SB_JRNL_PTR]
        mov  [hbfs_journal_ptr], eax
        cmp  dword [hbfs_super_buf + HBFS_SB_JRNL_STATE], 1
        jne  .hbfs_journal_clean    ; state=0: clean, skip recovery
        ; Dirty journal: replay UNDO log (restore pre-images)
        call hbfs_journal_recover
.hbfs_journal_clean:
        mov  dword [hbfs_journal_dirty], 0
.hbfs_v1_mount:
        ; Load root directory
        call hbfs_load_root_dir

        popad
        ret

.format_new:
        mov esi, msg_hbfs_format
        call vga_print
        call hbfs_format
        popad
        ret

.no_disk:
        mov esi, msg_hbfs_nodisk
        call vga_print
        popad
        ret

; Format: create fresh filesystem
hbfs_format:
        pushad

        ; Build superblock in buffer
        mov edi, hbfs_super_buf
        mov ecx, 512 / 4
        xor eax, eax
        rep stosd               ; Zero buffer

        mov edi, hbfs_super_buf
        mov dword [edi + 0], HBFS_MAGIC
        mov dword [edi + 4], HBFS_VERSION_V2        ; v9.0: version 2
        mov dword [edi + 8], HBFS_TOTAL_BLOCKS  ; Total blocks
        mov dword [edi + 12], HBFS_TOTAL_BLOCKS ; Free blocks
        mov dword [edi + 16], HBFS_ROOT_DIR_START; Root dir LBA
        mov dword [edi + 20], HBFS_BITMAP_START  ; Bitmap start LBA
        mov dword [edi + 24], HBFS_DATA_START    ; Data start LBA
        mov dword [edi + 28], HBFS_BLOCK_SIZE
        ; v9.0 journal fields
        mov dword [edi + HBFS_SB_JRNL_STATE], 0 ; journal state = clean
        mov dword [edi + HBFS_SB_JRNL_PTR],   0 ; write pointer = 0

        ; Write superblock
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors

        ; Zero out bitmap (HBFS_BITMAP_BLOCKS blocks)
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        xor eax, eax
        rep stosd

        mov eax, HBFS_BITMAP_START
        mov ebx, HBFS_BITMAP_BLOCKS
.fmt_bmp_loop:
        cmp ebx, 0
        je .fmt_bmp_done
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        add eax, HBFS_SECTORS_PER_BLK
        dec ebx
        jmp .fmt_bmp_loop
.fmt_bmp_done:
        ; v9.0: Mark first HBFS_JOURNAL_BLOCKS blocks as used (journal reserved area).
        ; Read the first bitmap sector, set bits 0..HBFS_JOURNAL_BLOCKS-1, write back.
        push eax
        push ecx
        push esi
        push edi
        mov  eax, HBFS_BITMAP_START
        mov  ecx, 1
        mov  edi, hbfs_block_buf
        call ata_read_sectors
        ; Set first HBFS_JOURNAL_BLOCKS bits in bitmap
        mov  ecx, HBFS_JOURNAL_BLOCKS
        xor  edx, edx
.fmt_jrnl_mark:
        bts  dword [hbfs_block_buf], edx
        inc  edx
        loop .fmt_jrnl_mark
        ; Write back updated bitmap sector
        mov  eax, HBFS_BITMAP_START
        mov  ecx, 1
        mov  esi, hbfs_block_buf
        call ata_write_sectors
        ; Zero the journal area itself (HBFS_JOURNAL_SECTS sectors at HBFS_JOURNAL_LBA)
        mov  edi, hbfs_block_buf
        xor  eax, eax
        mov  ecx, HBFS_BLOCK_SIZE / 4
        rep  stosd
        mov  eax, HBFS_JOURNAL_LBA
        mov  ebx, HBFS_JOURNAL_BLOCKS
.fmt_jrnl_zero:
        test ebx, ebx
        jz   .fmt_jrnl_done
        mov  ecx, HBFS_SECTORS_PER_BLK
        mov  esi, hbfs_block_buf
        call ata_write_sectors
        add  eax, HBFS_SECTORS_PER_BLK
        dec  ebx
        jmp  .fmt_jrnl_zero
.fmt_jrnl_done:
        ; Init journal runtime state
        mov  dword [hbfs_journal_ptr],   0
        mov  dword [hbfs_journal_dirty],  0
        pop  edi
        pop  esi
        pop  ecx
        pop  eax

        ; Zero out root directory (256 sectors = 32 blocks)
        ; Use a loop to write all blocks
        mov eax, HBFS_ROOT_DIR_START
        mov ebx, HBFS_ROOT_DIR_BLOCKS
.fmt_dir_loop:
        cmp ebx, 0
        je .fmt_dir_done
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        add eax, HBFS_SECTORS_PER_BLK
        dec ebx
        jmp .fmt_dir_loop
.fmt_dir_done:

        mov esi, msg_hbfs_formatted
        call vga_print

        popad
        ret

;=======================================================================
; HBFS v2 Write-Ahead Journal (v9.0)
;
; Each transaction:
;   1. hbfs_journal_begin   — marks superblock state = 1 (in-progress)
;   2. hbfs_journal_log_sector(EAX=lba) — saves OLD sector content
;      to journal before the caller writes new content
;   3. hbfs_journal_commit  — writes commit marker + clears state
;
; On mount with state=1: hbfs_journal_recover replays pre-images (UNDO).
;
; Journal layout in the data area (HBFS_JOURNAL_LBA):
;   Sector 0 per entry: header [JRNL magic][type][target LBA][padding]
;   Sector 1 per entry: 512 bytes of original sector content
;   Two sectors per logged sector; journal wraps around at HBFS_JOURNAL_SECTS.
;   Commit entry: single header sector with type = HBFS_JRNL_T_COMMIT.
;=======================================================================

;-----------------------------------------------------------------------
; hbfs_journal_begin — start a metadata transaction
; Sets superblock journal state = 1 and resets the write pointer.
; All registers preserved.
;-----------------------------------------------------------------------
hbfs_journal_begin:
        ; Skip if not a journalled filesystem (v2 or v3)
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V2
        jb   .hjb_done
        ; Reset pointer and set state in the in-memory superblock
        mov  dword [hbfs_journal_ptr],   0
        mov  dword [hbfs_journal_dirty],  1
        mov  dword [hbfs_super_buf + HBFS_SB_JRNL_STATE], 1
        ; Flush superblock to disk immediately (so crash leaves state=1)
        push eax
        push ecx
        push esi
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call ata_write_sectors
        pop  esi
        pop  ecx
        pop  eax
.hjb_done:
        ret

;-----------------------------------------------------------------------
; hbfs_journal_log_sector — save one sector's original content to journal
;   In:  EAX = disk LBA to log
;   Logs 2 journal sectors: header + original data.
;   All registers preserved.  Does nothing for v1 or if journal full.
;-----------------------------------------------------------------------
hbfs_journal_log_sector:
        ; Skip if not a journalled filesystem (v2 or v3)
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V2
        jb   .hjls_done
        cmp  dword [hbfs_journal_dirty], 1
        jne  .hjls_done

        pushad

        ; Check there's room for 2 more sectors (need ptr+1 < HBFS_JOURNAL_SECTS-1)
        mov  edx, [hbfs_journal_ptr]
        add  edx, 2
        cmp  edx, HBFS_JOURNAL_SECTS
        jge  .hjls_full             ; journal full — skip logging this sector

        ; Compute LBA of next journal slot
        mov  edx, [hbfs_journal_ptr]
        lea  edi, [HBFS_JOURNAL_LBA + edx]  ; journal write LBA

        ; --- Write journal header sector ---
        ; Use hbfs_block_buf (first 512 bytes) as the header sector
        ; (caller is not currently doing file I/O, so this is safe)
        mov  esi, hbfs_block_buf
        push eax                    ; save target LBA
        mov  dword [esi + 0], HBFS_JRNL_MAGIC
        mov  dword [esi + 4], HBFS_JRNL_T_PRE
        mov  eax, [esp]             ; target LBA
        mov  [esi + 8], eax
        mov  dword [esi + 12], 0    ; reserved
        pop  eax

        ; Zero the rest of the header sector
        push esi
        push edi
        add  esi, 16
        xor  ecx, ecx
        mov  ecx, (512 - 16) / 4
        push eax
        xor  eax, eax
        rep  stosd
        pop  eax
        pop  edi
        pop  esi

        ; Write header to current journal slot
        push eax
        push ecx
        mov  eax, edi
        mov  ecx, 1
        call ata_write_sectors
        pop  ecx
        pop  eax

        ; --- Read and write the original data sector ---
        inc  edi                    ; next journal sector

        ; Read original sector into hbfs_block_buf+512
        push eax
        push ecx
        mov  ecx, 1
        lea  esi, [hbfs_block_buf + 512]
        call ata_read_sectors       ; EAX = target LBA
        ; Write original data to journal
        mov  eax, edi
        mov  ecx, 1
        call ata_write_sectors
        pop  ecx
        pop  eax

        ; Advance journal pointer by 2
        add  dword [hbfs_journal_ptr], 2
        ; Update in-memory superblock ptr field (flushed at commit)
        mov  edx, [hbfs_journal_ptr]
        mov  [hbfs_super_buf + HBFS_SB_JRNL_PTR], edx

.hjls_full:
        popad
.hjls_done:
        ret

;-----------------------------------------------------------------------
; hbfs_journal_commit — close a transaction
; Writes a commit sector and resets the journal state to clean.
; All registers preserved.
;-----------------------------------------------------------------------
hbfs_journal_commit:
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V2
        jb   .hjc_done
        cmp  dword [hbfs_journal_dirty], 1
        jne  .hjc_done

        pushad

        ; Write commit marker at current journal pointer
        mov  edx, [hbfs_journal_ptr]
        cmp  edx, HBFS_JOURNAL_SECTS
        jge  .hjc_flush             ; no room — skip marker (still mark clean)

        lea  edi, [HBFS_JOURNAL_LBA + edx]
        mov  esi, hbfs_block_buf
        mov  dword [esi + 0], HBFS_JRNL_MAGIC
        mov  dword [esi + 4], HBFS_JRNL_T_COMMIT
        mov  dword [esi + 8], 0
        mov  dword [esi + 12], 0
        mov  eax, edi
        mov  ecx, 1
        call ata_write_sectors

.hjc_flush:
        ; Reset journal state and flush superblock
        mov  dword [hbfs_journal_dirty], 0
        mov  dword [hbfs_journal_ptr],   0
        mov  dword [hbfs_super_buf + HBFS_SB_JRNL_STATE], 0
        mov  dword [hbfs_super_buf + HBFS_SB_JRNL_PTR],   0
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call ata_write_sectors

        popad
.hjc_done:
        ret

;-----------------------------------------------------------------------
; hbfs_journal_recover — replay UNDO log after unclean shutdown
; Called from hbfs_init when superblock state == 1 (in-progress).
; Restores each pre-image sector to its original location.
; All registers preserved.
;-----------------------------------------------------------------------
hbfs_journal_recover:
        pushad

        mov  ebx, 0             ; journal sector index

.hjr_scan:
        ; Check if we've scanned all journal sectors
        cmp  ebx, HBFS_JOURNAL_SECTS
        jge  .hjr_done

        ; Read journal header sector at this index
        lea  eax, [HBFS_JOURNAL_LBA + ebx]
        mov  ecx, 1
        lea  edi, [hbfs_block_buf + 1024]  ; temp at +1024 (doesn't stomp first 1024)
        call ata_read_sectors
        jc   .hjr_done                     ; read error — stop recovery

        ; Check magic
        cmp  dword [hbfs_block_buf + 1024], HBFS_JRNL_MAGIC
        jne  .hjr_done              ; no valid entry — done

        ; Check type
        mov  edx, [hbfs_block_buf + 1024 + 4]
        cmp  edx, HBFS_JRNL_T_COMMIT
        je   .hjr_done              ; found commit — transaction was OK, stop undo

        cmp  edx, HBFS_JRNL_T_PRE
        jne  .hjr_next              ; unknown type — skip

        ; Pre-image entry: read the data sector (index+1) and write to target LBA
        mov  edi, [hbfs_block_buf + 1024 + 8]  ; target LBA
        inc  ebx
        lea  eax, [HBFS_JOURNAL_LBA + ebx]
        mov  ecx, 1
        lea  esi, [hbfs_block_buf + 512]
        call ata_read_sectors
        jc   .hjr_next              ; read error on data — skip

        ; Restore original content to target LBA
        mov  eax, edi
        mov  ecx, 1
        mov  esi, hbfs_block_buf + 512
        call ata_write_sectors

.hjr_next:
        inc  ebx
        jmp  .hjr_scan

.hjr_done:
        ; Reset journal state
        mov  dword [hbfs_super_buf + HBFS_SB_JRNL_STATE], 0
        mov  dword [hbfs_super_buf + HBFS_SB_JRNL_PTR],   0
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call ata_write_sectors

        popad
        ret
; Returns: CF set on read error
hbfs_load_root_dir:
        pushad
        ; Check if the directory buffer already has this directory cached
        mov eax, [current_dir_lba]
        cmp eax, [dir_cache_lba]
        jne .hlrd_miss
        mov eax, [current_dir_sects]
        cmp eax, [dir_cache_sects]
        jne .hlrd_miss
        ; Cache hit — buffer is already valid
        popad
        clc
        ret
.hlrd_miss:
        ; Cache miss — read from disk
        ; Zero entire dir buffer first to clear stale data
        mov edi, hbfs_dir_buf
        mov ecx, HBFS_ROOT_DIR_SIZE / 4
        xor eax, eax
        rep stosd
        ; Now load actual directory data
        mov eax, [current_dir_lba]
        mov ecx, [current_dir_sects]
        mov edi, hbfs_dir_buf
        call ata_read_sectors
        jc .hlrd_err
        ; Update cache tag
        mov eax, [current_dir_lba]
        mov [dir_cache_lba], eax
        mov eax, [current_dir_sects]
        mov [dir_cache_sects], eax
        popad
        clc
        ret
.hlrd_err:
        mov esi, msg_hbfs_dir_read_err
        call vga_print
        popad
        stc
        ret

; Load bitmap from disk into hbfs_bitmap_buf
; Returns: CF set on read error
hbfs_load_bitmap:
        pushad
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors
        jc .hlb_err
        popad
        clc
        ret
.hlb_err:
        mov esi, msg_hbfs_bitmap_err
        call vga_print
        popad
        stc
        ret

; Save current directory to disk
; Returns: CF set on write error
hbfs_save_root_dir:
        pushad
        mov eax, [current_dir_lba]
        mov ecx, [current_dir_sects]
        mov esi, hbfs_dir_buf
        call ata_write_sectors
        jc .hsrd_err
        popad
        clc
        ret
.hsrd_err:
        mov esi, msg_hbfs_dir_write_err
        call vga_print
        popad
        stc
        ret

; Get max directory entries for current directory
; Returns: EAX = number of entries that fit in current directory
hbfs_get_max_entries:
        mov eax, [current_dir_sects]
        shl eax, 9              ; sectors * 512 = bytes
        xor edx, edx
        push ecx
        mov ecx, HBFS_DIR_ENTRY_SIZE
        div ecx                 ; bytes / 288 = max entries
        pop ecx
        ret

; Find file in current directory
; ESI = filename to find
; Returns: EDI = pointer to dir entry (in hbfs_dir_buf), CF set if not found
hbfs_find_file:
        pushad
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax

.search:
        cmp byte [edi + 253], FTYPE_FREE
        je .next

        ; Compare filename
        push esi
        push edi
        push ecx
        ; Phase 1.5: cap filename comparison to HBFS max (252) so a missing
        ; null terminator in the search key can't walk past the dir entry.
        mov ecx, 252
.cmp_loop:
        mov al, [esi]
        mov ah, [edi]
        cmp al, ah
        jne .no_match
        test al, al
        jz .match
        inc esi
        inc edi
        dec ecx
        jnz .cmp_loop
        ; Ran past the cap without seeing a terminator — treat as no-match
        jmp .no_match

.no_match:
        pop ecx
        pop edi
        pop esi
.next:
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .search

        popad
        stc
        ret

.match:
        pop ecx
        pop edi                 ; EDI = dir entry start
        pop esi
        mov [esp], edi          ; Update EDI in pushad frame (EDI is at offset 0)
        popad
        clc
        ret

; Allocate N contiguous data blocks
; ECX = number of blocks needed
; Returns: EAX = first block number (relative to data start), CF set if full
hbfs_alloc_blocks:
        pushad
        mov [.blocks_needed], ecx

        ; Read bitmap
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors

        ; Scan for N contiguous free bits
        ; v9.0: start from HBFS_JOURNAL_BLOCKS to skip journal reserved area
        mov edx, HBFS_JOURNAL_BLOCKS   ; Start block (skip journal)
        mov ecx, HBFS_TOTAL_BLOCKS

.scan_start:
        cmp edx, ecx
        jge .no_space
        ; Check if block EDX is free
        mov eax, edx
        shr eax, 3              ; byte index
        mov ebx, edx
        and ebx, 7              ; bit index
        bt dword [hbfs_bitmap_buf + eax], ebx
        jc .occupied

        ; This block is free — check if N contiguous blocks from here
        push edx                ; save start candidate
        mov esi, 1              ; count of free found so far
.check_run:
        cmp esi, [.blocks_needed]
        jge .found_run
        push edx
        add edx, esi
        cmp edx, ecx
        pop edx
        jge .run_fail           ; past end of bitmap

        ; Check block (start + esi)
        mov eax, edx
        add eax, esi
        push eax
        shr eax, 3
        mov ebx, edx
        add ebx, esi
        and ebx, 7
        bt dword [hbfs_bitmap_buf + eax], ebx
        pop eax
        jc .run_fail
        inc esi
        jmp .check_run

.run_fail:
        pop edx                 ; restore start candidate
        inc edx
        jmp .scan_start

.found_run:
        pop edx                 ; EDX = first free block

        ; Mark all N blocks as used in bitmap
        mov esi, 0
.mark_loop:
        cmp esi, [.blocks_needed]
        jge .mark_done
        mov eax, edx
        add eax, esi
        push eax
        shr eax, 3
        mov ebx, edx
        add ebx, esi
        and ebx, 7
        bts dword [hbfs_bitmap_buf + eax], ebx
        pop eax
        inc esi
        jmp .mark_loop
.mark_done:

        ; Write bitmap back
        push edx
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov esi, hbfs_bitmap_buf
        call ata_write_sectors
        pop edx

        ; Update superblock free_blocks counter
        push edx
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov edi, hbfs_super_buf
        call ata_read_sectors
        mov eax, [.blocks_needed]
        sub [hbfs_super_buf + 12], eax   ; free_blocks -= allocated
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors
        pop edx

        mov [esp + 28], edx    ; Return first block number via EAX
        popad
        clc
        ret

.occupied:
        inc edx
        jmp .scan_start

.no_space:
        popad
        stc
        ret

.blocks_needed: dd 0

; Allocate a single data block (convenience wrapper)
; Returns: EAX = block number, CF set if full
hbfs_alloc_block:
        push ecx
        mov ecx, 1
        call hbfs_alloc_blocks
        pop ecx
        ret

; Free N contiguous data blocks
; EAX = first block number, ECX = block count
hbfs_free_blocks:
        pushad
        mov [.free_start], eax
        mov [.free_count], ecx

        ; Read bitmap
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors

        ; Clear bits for all blocks
        mov esi, 0
.free_loop:
        cmp esi, [.free_count]
        jge .free_done
        mov eax, [.free_start]
        add eax, esi
        mov ecx, eax
        shr ecx, 3              ; byte index
        and eax, 7              ; bit index
        mov edi, hbfs_bitmap_buf
        add edi, ecx
        mov cl, al
        mov ch, 1
        shl ch, cl
        not ch
        and [edi], ch
        inc esi
        jmp .free_loop
.free_done:

        ; Write back
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov esi, hbfs_bitmap_buf
        call ata_write_sectors

        ; Update superblock free_blocks counter
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov edi, hbfs_super_buf
        call ata_read_sectors
        mov eax, [.free_count]
        add [hbfs_super_buf + 12], eax   ; free_blocks += freed
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors

        popad
        ret

.free_start: dd 0
.free_count: dd 0

; Free a single data block (convenience wrapper)
; EAX = block number
hbfs_free_block:
        push ecx
        mov ecx, 1
        call hbfs_free_blocks
        pop ecx
        ret

; Create a file
; ESI = filename, ECX = size, EDI = data pointer, EDX = file type (FTYPE_*)
; Returns: CF set on error
hbfs_create_file:
        pushad
        ; Save parameters to local variables
        mov [.save_size], ecx
        mov [.save_data], edi
        mov [.save_name], esi
        mov [.save_type], edx

        ; Load directory and check if file already exists
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .not_exists
        call hbfs_delete_file_entry
.not_exists:

        ; Reload directory (may have changed after delete)
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax

.find_free:
        cmp byte [edi + 253], FTYPE_FREE
        je .found_slot
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .find_free

        ; Directory full
        popad
        stc
        ret

.found_slot:
        ; Copy filename into directory entry (max 252 chars + null)
        push edi                ; Save entry start
        mov esi, [.save_name]
        mov ecx, HBFS_MAX_FILENAME ; 252 chars max
.copy_name:
        lodsb
        stosb
        test al, al
        jz .name_done
        dec ecx
        jnz .copy_name
        xor al, al              ; Force null terminator if name too long
        stosb
.name_done:
        pop edi                 ; Restore entry start

        ; Set type from caller's EDX
        mov al, [.save_type]
        mov byte [edi + 253], al
        mov word [edi + 254], 0         ; Flags = 0

        ; Calculate blocks needed
        mov ecx, [.save_size]
        add ecx, HBFS_BLOCK_SIZE - 1    ; round up
        shr ecx, 12                     ; / 4096 = blocks needed
        cmp ecx, 0
        jg .has_data
        mov ecx, 1                      ; minimum 1 block
.has_data:
        mov [.save_blocks], ecx

        ; Allocate contiguous blocks
        push edi
        call hbfs_alloc_blocks
        pop edi
        jc .alloc_fail

        ; EAX = first allocated block number
        mov [edi + 260], eax            ; Start block
        mov ecx, [.save_size]
        mov [edi + 256], ecx            ; File size
        mov ecx, [.save_blocks]
        mov [edi + 264], ecx            ; Block count
        call rtc_get_timestamp
        mov [edi + 268], eax            ; Created timestamp (RTC packed)
        mov [edi + 272], eax            ; Modified timestamp (RTC packed)

        ; v3.0: Set default permissions and owner
        mov word [edi + DIRENT_PERMS], 0x1FF   ; rwxrwxrwx (0777)
        mov word [edi + DIRENT_OWNER], 0       ; root (UID 0)

        ; Save directory
        call hbfs_save_root_dir

        ; Write file data to disk
        ; LBA = HBFS_DATA_START + block_number * HBFS_SECTORS_PER_BLK
        mov eax, [edi + 260]    ; Reload first block from dir entry
        mov ecx, [.save_blocks]
        shl eax, 3              ; first_block * 8 sectors
        add eax, HBFS_DATA_START ; + data area start = LBA
        shl ecx, 3              ; blocks * 8 = total sectors
        mov esi, [.save_data]
        call ata_write_sectors
        jc .write_fail

        popad
        clc
        ret

.alloc_fail:
.write_fail:
        popad
        stc
        ret

; Local storage for hbfs_create_file
.save_size:   dd 0
.save_data:   dd 0
.save_name:   dd 0
.save_blocks: dd 0
.save_type:   dd 0

; Delete file entry (EDI already points to entry in hbfs_dir_buf)
hbfs_delete_file_entry:
        pushad
        ; Free ALL data blocks (contiguous)
        mov eax, [edi + 260]    ; Start block
        mov ecx, [edi + 264]    ; Block count
        cmp ecx, 0
        jle .skip_free
        call hbfs_free_blocks
.skip_free:

        ; Zero the directory entry
        mov ecx, HBFS_DIR_ENTRY_SIZE / 4
        push edi
        xor eax, eax
        rep stosd
        pop edi

        call hbfs_save_root_dir
        popad
        ret

; Create a subdirectory in the current directory
; ESI = directory name (null-terminated)
; Returns: CF=0 success, CF=1 error
hbfs_mkdir:
        pushad
        mov [.hm_name], esi

        call hbfs_load_root_dir
        call hbfs_find_file
        jnc .hm_fail             ; already exists

        mov ecx, HBFS_SUBDIR_BLOCKS
        call hbfs_alloc_blocks
        jc .hm_fail
        ; EAX = first allocated block
        mov [.hm_block], eax

        ; Zero all allocated blocks on disk
        mov ebx, HBFS_SUBDIR_BLOCKS
.hm_zero:
        cmp ebx, 0
        je .hm_zero_done
        push eax
        push ebx
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        xor eax, eax
        rep stosd
        pop ebx
        pop eax
        push eax
        push ebx
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        pop ebx
        pop eax
        inc eax
        dec ebx
        jmp .hm_zero
.hm_zero_done:
        ; Find free directory slot
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
.hm_find:
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .hm_slot
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .hm_find
        jmp .hm_fail

.hm_slot:
        push edi
        mov esi, [.hm_name]
.hm_cpn:
        lodsb
        stosb
        test al, al
        jnz .hm_cpn
        pop edi

        mov eax, [.hm_block]
        mov byte [edi + DIRENT_TYPE], FTYPE_DIR
        mov [edi + DIRENT_START_BLOCK], eax
        mov dword [edi + DIRENT_SIZE], 0
        mov dword [edi + DIRENT_BLOCK_COUNT], HBFS_SUBDIR_BLOCKS
        mov eax, [tick_count]
        mov [edi + DIRENT_CREATED], eax
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir

        popad
        clc
        ret

.hm_fail:
        popad
        stc
        ret

.hm_name:  dd 0
.hm_block: dd 0

;=======================================================================
; v3.0 FILESYSTEM EXTENSIONS
;=======================================================================

;---------------------------------------
; sys_chmod - Change file permissions
;   EBX = filename pointer
;   ECX = new permission bits (9 bits: rwxrwxrwx)
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_chmod:
        pushad
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .chmod_fail
        ; EDI = directory entry pointer
        and ecx, 0x1FF         ; Mask to 9 permission bits
        mov [edi + DIRENT_PERMS], cx
        ; Update modified timestamp
        call rtc_get_timestamp
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir
        mov dword [esp + 28], 0
        popad
        iretd
.chmod_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_chown - Change file owner
;   EBX = filename pointer
;   ECX = new owner UID
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_chown:
        pushad
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .chown_fail
        ; EDI = directory entry pointer
        mov [edi + DIRENT_OWNER], cx
        ; Update modified timestamp
        call rtc_get_timestamp
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir
        mov dword [esp + 28], 0
        popad
        iretd
.chown_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_symlink - Create a symbolic link
;   EBX = link name pointer
;   ECX = target path pointer
; Returns: EAX = 0 on success, -1 on failure
;
; Creates a file of type FTYPE_LINK whose
; data content is the target path string.
;---------------------------------------
sys_symlink:
        pushad
        ; Calculate target path length
        mov esi, ecx
        xor edx, edx
.sym_len:
        lodsb
        inc edx
        test al, al
        jnz .sym_len
        ; EDX = length including null terminator

        ; Create file with FTYPE_LINK
        mov esi, ebx            ; link name
        mov ecx, edx            ; size = target length
        ; Retrieve original ECX (target path pointer) from pushad frame
        ; pushad layout: [esp+0]=EDI [esp+4]=ESI [esp+8]=EBP [esp+12]=ESP
        ;                [esp+16]=EBX [esp+20]=EDX [esp+24]=ECX [esp+28]=EAX
        mov edi, [esp + 24]     ; original ECX = target path pointer
        mov edx, FTYPE_LINK
        call hbfs_create_file
        jc .symlink_fail
        mov dword [esp + 28], 0
        popad
        iretd
.symlink_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_readlink - Read symbolic link target
;   EBX = link name pointer
;   ECX = output buffer pointer
; Returns: EAX = target path length, -1 on failure
;---------------------------------------
sys_readlink:
        pushad
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .readlink_fail
        ; Check it's actually a symlink
        cmp byte [edi + DIRENT_TYPE], FTYPE_LINK
        jne .readlink_fail
        ; Read the link data (target path) from disk
        mov eax, [edi + DIRENT_START_BLOCK]
        push ecx                ; save output buffer
        push dword [edi + DIRENT_SIZE]  ; save size
        ; Convert block to LBA
        imul eax, HBFS_SECTORS_PER_BLK
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        pop edx                 ; EDX = size
        pop edi                 ; EDI = output buffer
        jc .readlink_fail
        ; Copy target path to output buffer
        mov esi, hbfs_block_buf
        mov ecx, edx
        rep movsb
        mov [esp + 28], edx     ; Return length in EAX
        popad
        iretd
.readlink_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_rename - Rename a file or directory (same directory)
;   EBX = old filename pointer
;   ECX = new filename pointer
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_rename:
        pushad
        mov [.sr_old], ebx
        mov [.sr_new], ecx
        call hbfs_load_root_dir
        ; Find old entry
        mov esi, ebx
        call hbfs_find_file
        jc .sr_fail             ; source not found
        ; Save entry pointer
        mov [.sr_edi], edi
        ; Check new name does not already exist
        mov esi, [.sr_new]
        call hbfs_find_file
        jnc .sr_fail            ; dest already exists
        ; Overwrite the name field in the old entry with new name
        mov edi, [.sr_edi]
        mov esi, [.sr_new]
        push edi
        mov ecx, HBFS_MAX_FILENAME
.sr_cp:
        lodsb
        stosb
        test al, al
        jz .sr_cp_done
        dec ecx
        jnz .sr_cp
        xor al, al
        stosb
.sr_cp_done:
        pop edi
        ; Update modified timestamp
        call rtc_get_timestamp
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir
        mov dword [esp + 28], 0
        popad
        iretd
.sr_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.sr_old: dd 0
.sr_new: dd 0
.sr_edi: dd 0

;---------------------------------------
; sys_rmdir - Remove an empty directory
;   EBX = directory name pointer
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_rmdir:
        pushad
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .srd_fail            ; not found
        ; Must be a directory
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne .srd_fail
        ; Save entry info
        mov [.srd_edi], edi
        push dword [edi + DIRENT_START_BLOCK]
        push dword [edi + DIRENT_BLOCK_COUNT]
        ; Check directory is empty: read its block and scan for non-free entries
        mov eax, [edi + DIRENT_START_BLOCK]
        imul eax, HBFS_SECTORS_PER_BLK
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        jc .srd_fail_pop
        ; Scan the first block for any used entries
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / HBFS_DIR_ENTRY_SIZE
.srd_scan:
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        jne .srd_notempty
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .srd_scan
        ; Directory is empty — free its blocks
        pop ecx                 ; block count
        pop eax                 ; start block
        call hbfs_free_blocks
        ; Reload parent directory and zero the entry
        call hbfs_load_root_dir
        mov edi, [.srd_edi]
        mov ecx, HBFS_DIR_ENTRY_SIZE / 4
        xor eax, eax
        rep stosd
        call hbfs_save_root_dir
        mov dword [esp + 28], 0
        popad
        iretd
.srd_notempty:
.srd_fail_pop:
        pop ecx
        pop eax
.srd_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.srd_edi: dd 0

;---------------------------------------
; sys_truncate - Truncate a file to a given size (shrink only)
;   EBX = filename pointer
;   ECX = new size in bytes (must be <= current size)
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_truncate:
        pushad
        mov [.st_new_size], ecx
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .st_fail
        ; Only truncate regular files
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .st_fail
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        je .st_fail
        ; New size must be <= current size
        mov eax, [.st_new_size]
        cmp eax, [edi + DIRENT_SIZE]
        ja .st_fail
        ; Update size in directory entry
        mov [edi + DIRENT_SIZE], eax
        ; Calculate new block count needed
        mov ecx, eax
        add ecx, HBFS_BLOCK_SIZE - 1
        shr ecx, 12             ; ceil(new_size / 4096)
        cmp ecx, 0
        jg .st_has_blocks
        mov ecx, 1
.st_has_blocks:
        ; Free excess blocks if block count shrank
        mov eax, [edi + DIRENT_BLOCK_COUNT]
        cmp eax, ecx
        jle .st_no_free
        ; Free the tail blocks: start = (start_block + new_count), count = old-new
        push ecx                ; save new block count
        sub eax, ecx            ; EAX = blocks to free
        push eax
        mov eax, [edi + DIRENT_START_BLOCK]
        add eax, ecx            ; first block to free
        pop ecx                 ; blocks to free
        call hbfs_free_blocks
        pop ecx                 ; new block count
        mov [edi + DIRENT_BLOCK_COUNT], ecx
.st_no_free:
        call rtc_get_timestamp
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir
        mov dword [esp + 28], 0
        popad
        iretd
.st_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.st_new_size: dd 0

;---------------------------------------
; sys_getmtime - Get last-modified timestamp of a file
;   EBX = filename pointer
; Returns: EAX = packed 32-bit timestamp (same format as rtc_get_timestamp)
;          EAX = 0 if file not found
; Also returns ECX = created timestamp
;---------------------------------------
sys_getmtime:
        pushad
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .sgmt_fail
        mov eax, [edi + DIRENT_MODIFIED]
        mov ecx, [edi + DIRENT_CREATED]
        mov [esp + 28], eax     ; return modified in EAX
        mov [esp + 24], ecx     ; return created in ECX
        popad
        iretd
.sgmt_fail:
        mov dword [esp + 28], 0
        mov dword [esp + 24], 0
        popad
        iretd

;---------------------------------------
; sys_setmtime - Set last-modified timestamp of a file
;   EBX = filename pointer
;   ECX = new timestamp (packed 32-bit, 0 = use current RTC time)
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_setmtime:
        pushad
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .ssmt_fail
        ; If ECX == 0, use current RTC time
        test ecx, ecx
        jnz .ssmt_use_arg
        call rtc_get_timestamp
        mov ecx, eax
.ssmt_use_arg:
        mov [edi + DIRENT_MODIFIED], ecx
        call hbfs_save_root_dir
        mov dword [esp + 28], 0
        popad
        iretd
.ssmt_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;=======================================================================
; sys_defrag — SYS_DEFRAG (104) — HBFS Filesystem Maintenance
;
; Phase 1: Count free blocks and fragmented free runs.
; Phase 2: Compact file blocks toward block HBFS_JOURNAL_BLOCKS
;          (forward-scan each dirent; if its start block can move to a
;           lower free contiguous region, copy sector-by-sector, update
;           the bitmap and directory entry).
;
; Returns: EAX = fragmented free runs detected (before defrag), -1 on error.
;=======================================================================
sys_defrag:
        pushad

        call hbfs_load_bitmap
        jc   .sdf_fail

        call hbfs_load_root_dir

        ; ---- Phase 1: scan bitmap for free-block / fragmentation stats ----
        xor  ecx, ecx           ; free block count
        xor  edx, edx           ; fragmented run count
        xor  ebx, ebx           ; block index
        xor  esi, esi           ; prev-was-free flag
.sdf_scan:
        cmp  ebx, HBFS_TOTAL_BLOCKS
        jl   .sdf_scan_body
        jmp  .sdf_scan_done
.sdf_scan_body:
        mov  eax, ebx
        shr  eax, 3
        mov  edi, ebx
        and  edi, 7
        bt   dword [hbfs_bitmap_buf + eax], edi
        jc   .sdf_blk_used
        inc  ecx
        test esi, esi
        jnz  .sdf_blk_next      ; continuing a free run
        inc  edx                ; new run boundary
        mov  esi, 1
        jmp  .sdf_blk_next
.sdf_blk_used:
        xor  esi, esi
.sdf_blk_next:
        inc  ebx
        jmp  .sdf_scan
.sdf_scan_done:
        cmp  edx, 1
        jle  .sdf_no_adj
        dec  edx
.sdf_no_adj:
        mov  [.sdf_frag_cnt], edx
        mov  [hbfs_super_buf + 12], ecx ; update free count in superblock

        ; ---- Phase 2: block relocation ----
        call hbfs_get_max_entries
        mov  [.sdf_n_ent], eax
        xor  ebx, ebx           ; dirent index

.sdf_loop:
        cmp  ebx, [.sdf_n_ent]
        jl   .sdf_loop_body
        jmp  .sdf_loop_done

.sdf_loop_body:
        ; EDI = ptr to current dirent
        imul eax, ebx, HBFS_DIR_ENTRY_SIZE
        lea  edi, [hbfs_dir_buf + eax]

        ; Skip FREE entries
        cmp  byte [edi + DIRENT_TYPE], FTYPE_FREE
        jne  .sdf_ck_dir
        jmp  .sdf_next
.sdf_ck_dir:
        cmp  byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne  .sdf_ck_blks
        jmp  .sdf_next
.sdf_ck_blks:
        mov  eax, [edi + 260]   ; start block  (HBFS_DE_BLOCK)
        mov  ecx, [edi + 264]   ; block count
        test ecx, ecx
        jnz  .sdf_have_cnt
        ; Derive block count from file size
        mov  ecx, [edi + 256]   ; file size (HBFS_DE_SIZE)
        add  ecx, HBFS_BLOCK_SIZE - 1
        shr  ecx, 12            ; ceil(size / 4096)
        test ecx, ecx
        jnz  .sdf_have_cnt
        jmp  .sdf_next          ; 0-size file
.sdf_have_cnt:
        ; Skip if already at or before the journal reserved area
        cmp  eax, HBFS_JOURNAL_BLOCKS
        ja   .sdf_find_start
        jmp  .sdf_next
.sdf_find_start:
        mov  [.sdf_cur_blk], eax
        mov  [.sdf_blk_cnt], ecx

        ; Scan for lowest free run of ECX blocks at index < [.sdf_cur_blk]
        mov  edx, HBFS_JOURNAL_BLOCKS   ; search cursor

.sdf_find:
        ; Stop if we've reached the current file position
        mov  eax, [.sdf_cur_blk]
        cmp  edx, eax
        jl   .sdf_find_body
        jmp  .sdf_next          ; no improvement found
.sdf_find_body:
        ; Is block EDX free?
        mov  eax, edx
        shr  eax, 3
        mov  esi, edx
        and  esi, 7
        bt   dword [hbfs_bitmap_buf + eax], esi
        jnc  .sdf_try_run       ; free: try to build a run
        inc  edx
        jmp  .sdf_find

.sdf_try_run:
        ; EDX is free. Check if the next (blk_cnt-1) blocks are also free.
        push edx                ; save run-start candidate
        mov  esi, 1             ; offset into run (block 0 = EDX is already known free)
.sdf_run_chk:
        cmp  esi, [.sdf_blk_cnt]
        jl   .sdf_run_body
        jmp  .sdf_run_ok        ; all blocks free — go move
.sdf_run_body:
        push edx
        add  edx, esi           ; EDX = candidate + offset
        mov  eax, edx
        shr  eax, 3
        mov  edi, edx
        and  edi, 7
        bt   dword [hbfs_bitmap_buf + eax], edi
        pop  edx
        jnc  .sdf_run_cont      ; still free
        ; Block at offset ESI is occupied — advance past it and retry
        pop  edx                ; restore run start
        add  edx, esi
        inc  edx
        jmp  .sdf_find
.sdf_run_cont:
        inc  esi
        jmp  .sdf_run_chk

.sdf_run_ok:
        pop  edx                ; EDX = destination start block
        cmp  edx, [.sdf_cur_blk]
        jl   .sdf_do_move
        jmp  .sdf_next          ; somehow not lower; skip

.sdf_do_move:
        mov  [.sdf_dst_blk], edx

        ; Compute source and destination start LBAs
        mov  eax, [.sdf_cur_blk]
        imul eax, eax, HBFS_SECTORS_PER_BLK
        add  eax, HBFS_DATA_START
        mov  [.sdf_src_lba], eax

        mov  eax, [.sdf_dst_blk]
        imul eax, eax, HBFS_SECTORS_PER_BLK
        add  eax, HBFS_DATA_START
        mov  [.sdf_dst_lba], eax

        mov  eax, [.sdf_blk_cnt]
        imul eax, eax, HBFS_SECTORS_PER_BLK
        mov  [.sdf_sects], eax  ; total sectors to copy

.sdf_copy:
        cmp  dword [.sdf_sects], 0
        je   .sdf_copy_done
        ; Read one sector from source
        mov  eax, [.sdf_src_lba]
        mov  ecx, 1
        mov  edi, hbfs_block_buf
        call ata_read_sectors
        jnc  .sdf_cp_rd_ok
        jmp  .sdf_next          ; read error: abort this file
.sdf_cp_rd_ok:
        ; Write to destination
        mov  eax, [.sdf_dst_lba]
        mov  ecx, 1
        mov  esi, hbfs_block_buf
        call ata_write_sectors
        jnc  .sdf_cp_wr_ok
        jmp  .sdf_next          ; write error: abort this file
.sdf_cp_wr_ok:
        inc  dword [.sdf_src_lba]
        inc  dword [.sdf_dst_lba]
        dec  dword [.sdf_sects]
        jmp  .sdf_copy

.sdf_copy_done:
        ; Mark destination blocks as used in bitmap
        mov  ecx, [.sdf_blk_cnt]
        mov  edx, [.sdf_dst_blk]
.sdf_bm_set:
        test ecx, ecx
        jz   .sdf_bm_set_done
        mov  eax, edx
        shr  eax, 3
        mov  esi, edx
        and  esi, 7
        bts  dword [hbfs_bitmap_buf + eax], esi
        inc  edx
        dec  ecx
        jmp  .sdf_bm_set
.sdf_bm_set_done:

        ; Mark source blocks as free in bitmap
        mov  ecx, [.sdf_blk_cnt]
        mov  edx, [.sdf_cur_blk]
.sdf_bm_clr:
        test ecx, ecx
        jz   .sdf_bm_clr_done
        mov  eax, edx
        shr  eax, 3
        mov  esi, edx
        and  esi, 7
        btr  dword [hbfs_bitmap_buf + eax], esi
        inc  edx
        dec  ecx
        jmp  .sdf_bm_clr
.sdf_bm_clr_done:

        ; Update dirent start block (recompute EDI from EBX)
        imul eax, ebx, HBFS_DIR_ENTRY_SIZE
        lea  edi, [hbfs_dir_buf + eax]
        mov  eax, [.sdf_dst_blk]
        mov  [edi + 260], eax   ; HBFS_DE_BLOCK

.sdf_next:
        inc  ebx
        jmp  .sdf_loop

.sdf_loop_done:
        ; Flush bitmap
        mov  eax, HBFS_BITMAP_START
        mov  ecx, HBFS_BITMAP_SECTS
        mov  esi, hbfs_bitmap_buf
        call ata_write_sectors
        jc   .sdf_fail_commit

        ; Save updated root directory
        call hbfs_save_root_dir

        ; Journal + flush superblock
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V2
        jb   .sdf_flush
        call hbfs_journal_begin
        mov  eax, HBFS_SUPERBLOCK_LBA
        call hbfs_journal_log_sector
.sdf_flush:
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call ata_write_sectors
        jc   .sdf_fail_commit
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V2
        jb   .sdf_done
        call hbfs_journal_commit

.sdf_done:
        mov  eax, [.sdf_frag_cnt]
        mov  [esp + 28], eax
        popad
        iretd

.sdf_fail_commit:
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V2
        jb   .sdf_fail
        call hbfs_journal_commit
.sdf_fail:
        mov  dword [esp + 28], -1
        popad
        iretd

; Local storage for sys_defrag
.sdf_frag_cnt:  dd 0
.sdf_n_ent:     dd 0
.sdf_cur_blk:   dd 0
.sdf_blk_cnt:   dd 0
.sdf_dst_blk:   dd 0
.sdf_src_lba:   dd 0
.sdf_dst_lba:   dd 0
.sdf_sects:     dd 0

;=======================================================================
; HBFS v3 — Permission, UID/GID, and xattr support
;
; These routines are called by v10.0 syscall handlers.  They are safe
; to compile against v1/v2 images — permission fields simply read as 0
; (reserved bytes from older images) which maps to root ownership with
; mode 0o000; the kernel treats that as "accessible by root only."
;
; Call convention: all functions use pushad/popad and return in EAX.
;=======================================================================

;-----------------------------------------------------------------------
; hbfs_get_mode  — read permission mode from a directory entry
;   In : EDI = pointer to 288-byte directory entry in RAM
;   Out: EAX = mode word (12-bit permissions), ZF set if mode is 0
;-----------------------------------------------------------------------
hbfs_get_mode:
        movzx eax, word [edi + HBFS_DE_MODE]
        test eax, eax
        ret

;-----------------------------------------------------------------------
; hbfs_set_mode  — write permission mode into a directory entry
;   In : EDI = pointer to directory entry (in RAM; caller must flush)
;        EBX = new mode word
;   Does NOT write to disk — caller must call hbfs_flush_dir_entry.
;-----------------------------------------------------------------------
hbfs_set_mode:
        mov [edi + HBFS_DE_MODE], bx
        ret

;-----------------------------------------------------------------------
; hbfs_check_access — verify that the current process may access a file
;   In : EDI = pointer to directory entry (in RAM)
;        EBX = access bits requested (HBFS_ACC_READ/WRITE/EXEC)
;        ECX = current effective UID
;        EDX = current effective GID
;   Out: EAX = 0 allowed, -1 denied
;
; Permission check mirrors standard POSIX logic:
;   1. If UID == 0 (root), always grant.
;   2. If file UID == calling UID, use owner bits.
;   3. If file GID == calling GID, use group bits.
;   4. Otherwise use world bits.
;-----------------------------------------------------------------------
HBFS_ACC_READ   equ (1 << 2)
HBFS_ACC_WRITE  equ (1 << 1)
HBFS_ACC_EXEC   equ (1 << 0)

hbfs_check_access:
        ; Root always passes
        test ecx, ecx
        jz .hca_ok

        movzx eax, word [edi + HBFS_DE_MODE]
        movzx esi, word [edi + HBFS_DE_UID]
        movzx edi, word [edi + HBFS_DE_GID]   ; NOTE: overwrites EDI!

        ; If mode is all-zero (legacy entry), deny writes but allow reads
        test eax, eax
        jnz .hca_check_uid
        ; Legacy (no permission bits): deny if requesting write/exec
        test ebx, (HBFS_ACC_WRITE | HBFS_ACC_EXEC)
        jnz .hca_deny
        jmp .hca_ok

.hca_check_uid:
        cmp ecx, esi            ; calling UID == file UID?
        jne .hca_check_gid
        ; Use owner bits [8:6]
        mov esi, eax
        shr esi, 6
        jmp .hca_test

.hca_check_gid:
        cmp edx, edi            ; calling GID == file GID?
        jne .hca_world
        ; Use group bits [5:3]
        mov esi, eax
        shr esi, 3
        jmp .hca_test

.hca_world:
        ; Use world bits [2:0]
        mov esi, eax

.hca_test:
        and esi, 0x07           ; low 3 bits of relevant triplet
        and esi, ebx            ; mask with requested access
        cmp esi, ebx
        jne .hca_deny
.hca_ok:
        xor eax, eax
        ret
.hca_deny:
        mov eax, -1
        ret

;-----------------------------------------------------------------------
; sys_chmod — SYS_CHMOD implementation (v3: actual on-disk write)
;   In (from syscall frame): EBX = filename_ptr, ECX = mode_word
;   Out: EAX = 0 success, -1 fail
;-----------------------------------------------------------------------
sys_chmod_v3:
        pushad
        ; Validate user pointer
        mov eax, ebx
        call validate_user_ptr
        jc .schmod_fail

        ; Find the file in HBFS directory
        call hbfs_find_entry     ; EBX=name -> EDI=entry ptr or 0; EAX=entry#
        test eax, eax
        js .schmod_fail

        ; Update mode field in the loaded directory entry buffer
        mov [edi + HBFS_DE_MODE], cx

        ; Mark version as v3 if not already (so we persist the permission data)
        cmp dword [hbfs_super_buf + 4], HBFS_VERSION_V3
        je .schmod_flush
        mov dword [hbfs_super_buf + 4], HBFS_VERSION_V3
        ; Enable permission feature flag
        or  dword [hbfs_super_buf + HBFS_SB_FEATURES], HBFS_FEAT_PERMS
        ; Write updated superblock
        push eax
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call ata_write_sectors
        pop  eax

.schmod_flush:
        ; Write the modified directory entry back to disk
        call hbfs_flush_dir_entry   ; EDI=entry, EAX=entry# -> writes sector
        jc .schmod_fail

        mov dword [esp + 28], 0    ; EAX in saved pushad frame
        popad
        iretd

.schmod_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_chown_v3 — SYS_CHOWN implementation (v3: actual on-disk write)
;   In: EBX = filename_ptr, ECX = new_uid, EDX = new_gid
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_chown_v3:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc .schown_fail

        call hbfs_find_entry
        test eax, eax
        js .schown_fail

        ; Update UID and GID in loaded entry buffer
        mov [edi + HBFS_DE_UID], cx
        mov [edi + HBFS_DE_GID], dx

        ; Upgrade to v3 if needed
        cmp dword [hbfs_super_buf + 4], HBFS_VERSION_V3
        je .schown_flush
        mov dword [hbfs_super_buf + 4], HBFS_VERSION_V3
        or  dword [hbfs_super_buf + HBFS_SB_FEATURES], HBFS_FEAT_PERMS
        push eax
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call ata_write_sectors
        pop  eax

.schown_flush:
        call hbfs_flush_dir_entry
        jc .schown_fail
        mov dword [esp + 28], 0
        popad
        iretd
.schown_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; hbfs_xattr_get — read an extended attribute value
;   In : EBX = entry ptr (in RAM, directory entry)
;        ECX = key_ptr (null-terminated, up to 63 chars)
;        EDX = value_buf (caller buffer)
;        ESI = value_buf_max (max bytes to write)
;   Out: EAX = value length, or -1 if not found
;-----------------------------------------------------------------------
hbfs_xattr_get:
        pushad
        movzx eax, word [ebx + HBFS_DE_XATTR_COUNT]
        test eax, eax
        jz .hxg_notfound

        mov edi, [ebx + HBFS_DE_XATTR_LBA]
        test edi, edi
        jz .hxg_notfound

        ; Load xattr block from disk (edi = absolute LBA already)
        push eax
        mov eax, edi
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        pop eax
        jc .hxg_notfound

        ; Scan entries for matching key
        mov edi, hbfs_block_buf
        xor ecx, ecx
.hxg_loop:
        cmp ecx, [ebx + HBFS_DE_XATTR_COUNT + 0]   ; word compare
        jge .hxg_notfound
        ; Compare key
        push esi
        push edi
        push ecx
        mov esi, ecx            ; use ECX for str compare
        mov esi, ebx            ; restore: key in caller ECX param
        ; Note: EBX, ECX, EDX are from the caller (not preserved by pushad)
        ; This function uses non-standard internal reg usage; fix-up:
        ; The caller's ECX = key_ptr, EDX = val_buf, ESI = max
        ; After pushad, saved regs are on stack; original EBX/ECX/EDX/ESI still accessible
        ; For simplicity, compare via helper that takes ESI=a EDI=b
        pop ecx
        pop edi
        pop esi
        push edi
        mov esi, [esp + 12]     ; original ECX (key_ptr) from pushad frame offset
        ; Simpler approach: inline compare
        push ecx
        mov ecx, HBFS_XATTR_KEY_LEN - 1
        push esi
        push edi
        ; edi = current key in xattr block
.hxg_cmp:
        lodsb
        cmp al, [edi]
        jne .hxg_cmp_ne
        inc edi
        test al, al
        jz .hxg_cmp_match
        loop .hxg_cmp
.hxg_cmp_match:
        pop edi
        pop esi
        pop ecx
        ; Match found — copy value to EDX buf up to ESI max bytes
        add edi, HBFS_XATTR_KEY_LEN   ; edi -> value field
        push edi
        xor eax, eax
.hxg_copy:
        cmp eax, esi
        jge .hxg_copy_done
        mov bl, [edi + eax]
        mov [edx + eax], bl
        test bl, bl
        jz .hxg_copy_done
        inc eax
        jmp .hxg_copy
.hxg_copy_done:
        pop edi
        mov [esp + 28], eax    ; return length in EAX slot of pushad
        popad
        ret

.hxg_cmp_ne:
        pop edi
        pop esi
        pop ecx
        pop edi                ; restore from outer push
        add edi, HBFS_XATTR_ENTRY_SIZE   ; advance to next entry
        inc ecx
        jmp .hxg_loop

.hxg_notfound:
        mov dword [esp + 28], -1
        popad
        ret

;-----------------------------------------------------------------------
; hbfs_xattr_set — write an extended attribute (creates if not exists)
;   In : EBX = entry ptr, ECX = key_ptr, EDX = value_ptr
;   Out: EAX = 0 success, -1 fail
;-----------------------------------------------------------------------
hbfs_xattr_set:
        pushad
        mov [.hxs_key_ptr], ecx        ; save before registers change
        mov [.hxs_val_ptr], edx

        movzx eax, word [ebx + HBFS_DE_XATTR_COUNT]

        ; If no xattr block yet, allocate one
        cmp dword [ebx + HBFS_DE_XATTR_LBA], 0
        jne .hxs_have_block

        push eax
        call hbfs_alloc_block   ; -> EAX = new block number or -1
        test eax, eax
        js .hxs_fail_pop

        ; Zero the new block
        push eax
        mov edi, hbfs_block_buf
        xor eax, eax
        mov ecx, HBFS_BLOCK_SIZE / 4
        rep stosd
        pop eax

        ; Store the LBA (convert block# to LBA)
        mov edi, eax
        imul edi, HBFS_SECTORS_PER_BLK
        add edi, HBFS_DATA_START + HBFS_JOURNAL_SECTS
        mov [ebx + HBFS_DE_XATTR_LBA], edi

        ; Enable xattr feature flag in superblock
        or  dword [hbfs_super_buf + HBFS_SB_FEATURES], HBFS_FEAT_XATTR
        pop eax

.hxs_have_block:
        ; Load existing xattr block
        mov edi, [ebx + HBFS_DE_XATTR_LBA]
        push eax
        mov eax, edi
        mov ecx, HBFS_SECTORS_PER_BLK
        push edi
        mov edi, hbfs_block_buf
        call ata_read_sectors
        pop edi
        pop eax
        jc .hxs_fail

        ; Search for existing key or first free slot
        push edi                        ; save xattr LBA for disk write later
        mov edi, hbfs_block_buf
        xor ecx, ecx                    ; slot index
.hxs_scan:
        cmp ecx, HBFS_XATTR_PER_BLOCK
        jge .hxs_new_slot
        ; Check if this slot is free (key[0] == 0)
        cmp byte [edi], 0
        je .hxs_write_here
        ; Compare stored key with requested key
        push ecx
        push esi
        push edi
        mov esi, [.hxs_key_ptr]
        mov ecx, HBFS_XATTR_KEY_LEN
        cld
        repe cmpsb
        pop edi
        pop esi
        pop ecx
        je .hxs_key_match
        ; Keys differ: advance to next slot
        add edi, HBFS_XATTR_ENTRY_SIZE
        inc ecx
        jmp .hxs_scan

.hxs_new_slot:
        ; No free slot found — xattr block is full
        pop edi
        jmp .hxs_fail

.hxs_key_match:
        ; Update the value field of an existing xattr entry in-place
        add edi, HBFS_XATTR_KEY_LEN
        mov esi, [.hxs_val_ptr]
        mov ecx, HBFS_XATTR_VAL_LEN - 1
        cld
.hxs_uk:
        lodsb
        stosb
        test al, al
        jz .hxs_uk_done
        loop .hxs_uk
        mov byte [edi], 0
.hxs_uk_done:
        ; No xattr count change (key already existed)
        pop edi                         ; xattr LBA
        mov eax, edi
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        jc .hxs_fail
        mov dword [esp + 28], 0
        popad
        ret

.hxs_write_here:
        ; edi -> free slot; write key then value
        push edi                        ; save slot base ptr
        mov esi, [.hxs_key_ptr]
        mov ecx, HBFS_XATTR_KEY_LEN - 1
        cld
.hxs_wk:
        lodsb
        stosb
        test al, al
        jz .hxs_wk_done
        loop .hxs_wk
        mov byte [edi], 0
.hxs_wk_done:
        pop edi                         ; restore slot base
        add edi, HBFS_XATTR_KEY_LEN
        mov esi, [.hxs_val_ptr]
        mov ecx, HBFS_XATTR_VAL_LEN - 1
.hxs_wv:
        lodsb
        stosb
        test al, al
        jz .hxs_wv_done
        loop .hxs_wv
        mov byte [edi], 0
.hxs_wv_done:
        ; Update xattr count in entry
        inc word [ebx + HBFS_DE_XATTR_COUNT]

        ; Write xattr block to disk
        pop edi                         ; xattr LBA
        mov eax, edi
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        jc .hxs_fail

        mov dword [esp + 28], 0
        popad
        ret

.hxs_fail_pop:
        pop eax
.hxs_fail:
        mov dword [esp + 28], -1
        popad
        ret

.hxs_key_ptr:   dd 0
.hxs_val_ptr:   dd 0

;-----------------------------------------------------------------------
; hbfs_find_entry — search loaded directory for a filename
;   In : EBX = filename pointer
;   Out: EAX = entry index (0-based), or -1 if not found
;        EDI = pointer to 288-byte entry in hbfs_dir_buf
;-----------------------------------------------------------------------
hbfs_find_entry:
        push esi
        push ecx
        xor ecx, ecx
        mov edi, hbfs_dir_buf
.hfe_loop:
        cmp ecx, HBFS_MAX_FILES
        jge .hfe_notfound
        cmp byte [edi + HBFS_DE_TYPE], FTYPE_FREE
        je .hfe_next
        push edi
        mov esi, ebx
        add edi, HBFS_DE_NAME
        call str_compare        ; ESI=query, EDI=stored name -> EAX=0 if equal
        pop edi
        test eax, eax
        jz .hfe_found
.hfe_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        inc ecx
        jmp .hfe_loop
.hfe_found:
        mov eax, ecx
        pop ecx
        pop esi
        ret
.hfe_notfound:
        mov eax, -1
        pop ecx
        pop esi
        ret

;-----------------------------------------------------------------------
; hbfs_flush_dir_entry — write a single directory entry back to disk
;   In : EDI = pointer to entry in hbfs_dir_buf
;        EAX = entry index (0-based)
;   Out: CF set on error
;
; Calculates which sector of the root directory contains this entry
; and does a read-modify-write to preserve other entries in the sector.
;-----------------------------------------------------------------------
hbfs_flush_dir_entry:
        pushad
        ; Compute: LBA = HBFS_ROOT_DIR_START + (entry# * HBFS_DIR_ENTRY_SIZE) / 512
        mov ebx, eax
        imul ebx, HBFS_DIR_ENTRY_SIZE       ; byte offset
        mov ecx, 512
        xor edx, edx
        div ecx                              ; EAX = sector offset, EDX = byte-in-sector
        add eax, HBFS_ROOT_DIR_START         ; absolute LBA

        ; Read the 512-byte sector
        push eax
        mov edi, hbfs_sector_rw_buf
        mov ecx, 1
        call ata_read_sectors
        pop eax
        jc .hfde_fail

        ; Patch the entry within the sector
        ; entry_offset_in_sector = (orig_entry# * HBFS_DIR_ENTRY_SIZE) mod 512
        push eax
        mov eax, [esp + 8]           ; original EAX (entry index) from pushad frame
        imul eax, HBFS_DIR_ENTRY_SIZE
        xor edx, edx
        mov ecx, 512
        div ecx                      ; EDX = offset within sector
        pop eax

        mov esi, [esp + 4]           ; original EDI (entry ptr) from pushad frame
        lea edi, [hbfs_sector_rw_buf + edx]
        mov ecx, HBFS_DIR_ENTRY_SIZE
        rep movsb

        ; Write sector back
        mov ecx, 1
        mov esi, hbfs_sector_rw_buf
        call ata_write_sectors
        jc .hfde_fail
        popad
        clc
        ret
.hfde_fail:
        popad
        stc
        ret
