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
        mov  eax, [hbfs_super_buf + HBFS_SB_JRNL_EPOCH]
        mov  [hbfs_journal_epoch], eax
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
        ; The bitmap on disk was just rewritten from scratch
        mov  dword [hbfs_bitmap_valid],   0
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
;=======================================================================
; CRC32C (Castagnoli) — used to detect torn journal records.
;
; A crash can land between writing a record's pre-image sector and its header,
; or half-way through either. Without a checksum the header still looks valid
; (magic + epoch match) and recovery would restore a garbage pre-image over a
; live sector. The table is generated at assembly time, so the kernel carries
; no runtime initialisation cost.
;=======================================================================
crc32c_table:
%assign _crc_i 0
%rep 256
  %assign _crc_v _crc_i
  %rep 8
    %if _crc_v & 1
      %assign _crc_v ((_crc_v >> 1) ^ 0x82F63B78)
    %else
      %assign _crc_v (_crc_v >> 1)
    %endif
  %endrep
  dd _crc_v
  %assign _crc_i _crc_i + 1
%endrep

;-----------------------------------------------------------------------
; crc32c — checksum a buffer
;   In : EAX = buffer, ECX = length in bytes
;   Out: EAX = CRC32C
;   Preserves everything except EAX.
;-----------------------------------------------------------------------
crc32c:
        push ebx
        push edx
        push esi
        push ecx
        mov  esi, eax
        mov  eax, 0xFFFFFFFF
.crc_loop:
        test ecx, ecx
        jz   .crc_done
        movzx ebx, byte [esi]
        xor  ebx, eax
        and  ebx, 0xFF                 ; index = (crc ^ byte) & 0xFF
        shr  eax, 8
        xor  eax, [crc32c_table + ebx * 4]
        inc  esi
        dec  ecx
        jmp  .crc_loop
.crc_done:
        not  eax
        pop  ecx
        pop  esi
        pop  edx
        pop  ebx
        ret

;-----------------------------------------------------------------------
; hbfs_journal_begin — start (or nest into) a metadata transaction
;   Every begin must be paired with exactly one commit or abort.
;   Nested calls only bump a depth counter: the outermost transaction owns
;   the journal, so a helper that both allocates blocks (bitmap + superblock)
;   and writes a directory entry still produces one atomic unit.
; All registers preserved.
;-----------------------------------------------------------------------
hbfs_journal_begin:
        ; Skip if not a journalled filesystem (v2 or v3)
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V2
        jb   .hjb_done
        inc  dword [hbfs_journal_depth]
        cmp  dword [hbfs_journal_depth], 1
        jne  .hjb_done              ; nested — the outer transaction owns it

        ; Reset pointer and set state in the in-memory superblock
        mov  eax, [hbfs_journal_epoch]
        inc  eax
        mov  [hbfs_journal_epoch], eax
        mov  [hbfs_super_buf + HBFS_SB_JRNL_EPOCH], eax
        mov  dword [hbfs_journal_ptr],     0
        mov  dword [hbfs_journal_dirty],   1
        mov  dword [hbfs_journal_failed],  0
        mov  dword [hbfs_journal_n_logged], 0
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
;   Out: CF = 0  logged (or journalling is not active for this write)
;        CF = 1  journal full or I/O error; the transaction is marked failed
;   Logs 2 journal sectors: header + original data.
;   All registers preserved.
;-----------------------------------------------------------------------
hbfs_journal_log_sector:
        ; Skip if not a journalled filesystem (v2 or v3)
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V2
        jb   .hjls_ok
        cmp  dword [hbfs_journal_dirty], 1
        jne  .hjls_ok
        cmp  dword [hbfs_journal_failed], 0
        jne  .hjls_fail

        pushad

        ; Remember the target, then check whether this transaction already
        ; logged it.
        mov  [.hjls_lba], eax
        mov  ecx, [hbfs_journal_n_logged]
        xor  edx, edx
.hjls_dup:
        cmp  edx, ecx
        jge  .hjls_dup_done
        cmp  [hbfs_journal_logged + edx * 4], eax
        je   .hjls_already
        inc  edx
        jmp  .hjls_dup
.hjls_already:
        ; Already logged: the existing record holds the pre-transaction content,
        ; which is exactly what undo needs.
        popad
        clc
        ret
.hjls_dup_done:

        ; Check there's room for 2 more sectors plus the commit marker
        mov  edx, [hbfs_journal_ptr]
        add  edx, 2
        cmp  edx, HBFS_JOURNAL_SECTS - 1
        jae  .hjls_full             ; journal full — caller must abort

        ; --- Read the live sector into the scratch pre-image slot ---
        ; The pre-image must be captured before the new content is written, and
        ; its CRC goes into the record header, so it is read first.
        ; ata_read_sectors wants its destination in EDI. The old code left EDI
        ; holding the journal LBA, so it read the target sector into absolute
        ; address ~0x1180 (low memory) and then journalled whatever was there.
        mov  eax, [.hjls_lba]
        mov  ecx, 1
        lea  edi, [hbfs_jrnl_buf + 512]
        call ata_read_sectors
        jc   .hjls_iofail

        ; --- Store the pre-image in the journal (data sector first) ---
        ; Writing the payload first means a crash between the two writes leaves
        ; a record whose header is not yet valid, so recovery ignores it.
        mov  eax, [hbfs_journal_ptr]
        add  eax, HBFS_JOURNAL_LBA + 1
        mov  ecx, 1
        lea  esi, [hbfs_jrnl_buf + 512]
        call ata_write_sectors
        jc   .hjls_iofail

        ; --- Build the record header, including the pre-image checksum ---
        lea  eax, [hbfs_jrnl_buf + 512]
        mov  ecx, 512
        call crc32c
        mov  esi, hbfs_jrnl_buf
        mov  [esi + 16], eax            ; CRC32C of the pre-image sector
        mov  dword [esi + 0], HBFS_JRNL_MAGIC
        mov  dword [esi + 4], HBFS_JRNL_T_PRE
        mov  eax, [.hjls_lba]
        mov  [esi + 8], eax             ; target LBA
        mov  eax, [hbfs_journal_epoch]
        mov  [esi + 12], eax            ; transaction epoch

        ; Zero the rest of the header sector
        push edi
        lea  edi, [esi + 20]
        mov  ecx, (512 - 20) / 4
        xor  eax, eax
        rep  stosd
        pop  edi

        ; --- Write the header; the record becomes valid at this point ---
        mov  eax, [hbfs_journal_ptr]
        add  eax, HBFS_JOURNAL_LBA
        mov  ecx, 1
        mov  esi, hbfs_jrnl_buf
        call ata_write_sectors
        jc   .hjls_iofail

        ; Advance journal pointer by 2, and remember the target
        mov  ecx, [hbfs_journal_n_logged]
        cmp  ecx, 16
        jge  .hjls_no_list
        mov  eax, [.hjls_lba]
        mov  [hbfs_journal_logged + ecx * 4], eax
        inc  dword [hbfs_journal_n_logged]
.hjls_no_list:
        add  dword [hbfs_journal_ptr], 2
        ; Update in-memory superblock ptr field (flushed at commit)
        mov  edx, [hbfs_journal_ptr]
        mov  [hbfs_super_buf + HBFS_SB_JRNL_PTR], edx

        popad
        clc
        ret

.hjls_full:
        ; Journal full: refuse to write the sector. Marking the transaction
        ; failed makes hbfs_meta_write abort (replay pre-images) instead of
        ; leaving a half-logged mutation behind.
        mov  dword [hbfs_journal_failed], 1
        popad
        stc
        ret
.hjls_iofail:
        mov  dword [hbfs_journal_failed], 1
        popad
        stc
        ret
.hjls_ok:
        clc
        ret
.hjls_fail:
        stc
        ret

.hjls_lba: dd 0

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

        ; Only the outermost commit closes the transaction
        dec  dword [hbfs_journal_depth]
        cmp  dword [hbfs_journal_depth], 0
        jg   .hjc_ret
        mov  dword [hbfs_journal_depth], 0

        ; A transaction that overflowed the journal is rolled back, not
        ; committed: the mutations it could not log are still on disk.
        cmp  dword [hbfs_journal_failed], 0
        jne  .hjc_abort

        pushad

        ; Write commit marker at current journal pointer
        mov  edx, [hbfs_journal_ptr]
        cmp  edx, HBFS_JOURNAL_SECTS
        jge  .hjc_flush             ; no room — skip marker (still mark clean)

        lea  edi, [HBFS_JOURNAL_LBA + edx]
        mov  esi, hbfs_jrnl_buf
        mov  dword [esi + 0], HBFS_JRNL_MAGIC
        mov  dword [esi + 4], HBFS_JRNL_T_COMMIT
        mov  dword [esi + 8], 0
        mov  eax, [hbfs_journal_epoch]
        mov  [esi + 12], eax            ; epoch of the transaction being closed
        mov  eax, edi
        mov  ecx, 1
        call ata_write_sectors

.hjc_flush:
        ; Reset journal state and flush superblock
        mov  dword [hbfs_journal_dirty], 0
        mov  dword [hbfs_journal_ptr],   0
        mov  dword [hbfs_journal_n_logged], 0
        mov  dword [hbfs_super_buf + HBFS_SB_JRNL_STATE], 0
        mov  dword [hbfs_super_buf + HBFS_SB_JRNL_PTR],   0
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call ata_write_sectors

        popad
.hjc_ret:
.hjc_done:
        ret

.hjc_abort:
        call hbfs_journal_abort
        ret

;-----------------------------------------------------------------------
; hbfs_journal_abort — roll the open transaction back
;   Rewrites every pre-image logged so far, then clears the journal state.
;   Safe to call when no transaction is open.
; All registers preserved.
;-----------------------------------------------------------------------
hbfs_journal_abort:
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V2
        jb   .hja_done
        pushad
        mov  dword [hbfs_journal_depth], 0

        ; Seal the log with a commit marker at the write pointer. Replay stops
        ; at the first commit marker, so stale records left by an older
        ; transaction (which sit beyond the pointer) can never be replayed.
        mov  edx, [hbfs_journal_ptr]
        cmp  edx, HBFS_JOURNAL_SECTS
        jae  .hja_replay
        lea  edi, [HBFS_JOURNAL_LBA + edx]
        mov  esi, hbfs_jrnl_buf
        mov  dword [esi + 0], HBFS_JRNL_MAGIC
        mov  dword [esi + 4], HBFS_JRNL_T_COMMIT
        mov  dword [esi + 8], 0
        mov  eax, [hbfs_journal_epoch]
        mov  [esi + 12], eax
        mov  eax, edi
        mov  ecx, 1
        call ata_write_sectors

.hja_replay:
        call hbfs_journal_recover
        mov  dword [hbfs_journal_dirty],  0
        mov  dword [hbfs_journal_failed], 0
        mov  dword [hbfs_journal_ptr],    0
        mov  dword [hbfs_journal_n_logged], 0
        ; The in-memory directory no longer matches the disk: force a reload.
        mov  dword [dir_cache_lba], -1
        popad
.hja_done:
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
        lea  edi, [hbfs_jrnl_buf]           ; 1 KB private scratch
        call ata_read_sectors
        jc   .hjr_done                     ; read error — stop recovery

        ; Check magic
        cmp  dword [hbfs_jrnl_buf], HBFS_JRNL_MAGIC
        jne  .hjr_done              ; no valid entry — done

        ; Only records from the transaction that was interrupted may be
        ; replayed. Older transactions leave their records in the circular log
        ; with valid magic, and replaying those would roll the filesystem back
        ; to an older state.
        mov  eax, [hbfs_jrnl_buf + 12]
        cmp  eax, [hbfs_journal_epoch]
        jne  .hjr_done

        ; Check type
        mov  edx, [hbfs_jrnl_buf + 4]
        cmp  edx, HBFS_JRNL_T_COMMIT
        je   .hjr_done              ; found commit — transaction was OK, stop undo

        cmp  edx, HBFS_JRNL_T_PRE
        jne  .hjr_next              ; unknown type — skip

        ; Pre-image entry: read the data sector (index+1) and write it back to
        ; the target LBA. The target must be kept in memory: ata_read_sectors
        ; takes its *destination* in EDI, so holding the target LBA in EDI
        ; across the read made the write go to a low memory address and pushed
        ; an unfilled buffer over the real sector (a replayed journal record
        ; for the bitmap turned the whole bitmap into a journal header).
        mov  eax, [hbfs_jrnl_buf + 8]  ; target LBA
        mov  [.hjr_target], eax
        inc  ebx
        lea  eax, [HBFS_JOURNAL_LBA + ebx]
        mov  ecx, 1
        lea  edi, [hbfs_jrnl_buf + 512]      ; read destination
        call ata_read_sectors
        jc   .hjr_next              ; read error on data — skip

        ; A record is only trustworthy if its payload matches the checksum in
        ; its header. A torn write (header landed, payload did not) would
        ; otherwise restore garbage over a live sector. A zero checksum means
        ; the record predates the checksum (v2/v3 images), so it is accepted.
        mov  eax, [hbfs_jrnl_buf + 16]
        test eax, eax
        jz   .hjr_crc_ok
        push eax
        lea  eax, [hbfs_jrnl_buf + 512]
        mov  ecx, 512
        call crc32c
        pop  edx
        cmp  eax, edx
        jne  .hjr_done              ; torn record — stop here, restore nothing
.hjr_crc_ok:

        ; Restore original content to target LBA
        mov  eax, [.hjr_target]
        mov  ecx, 1
        mov  esi, hbfs_jrnl_buf + 512
        call ata_write_sectors

.hjr_next:
        inc  ebx
        jmp  .hjr_scan

.hjr_done:
        ; Reset journal state
        mov  dword [hbfs_super_buf + HBFS_SB_JRNL_STATE], 0
        mov  dword [hbfs_super_buf + HBFS_SB_JRNL_PTR],   0
        mov  dword [hbfs_journal_dirty],   0
        mov  dword [hbfs_journal_depth],   0
        mov  dword [hbfs_journal_failed],  0
        mov  dword [hbfs_journal_ptr],     0
        ; Anything replayed came from disk, so the cached directory is stale.
        mov  dword [dir_cache_lba], -1
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call ata_write_sectors

        popad
        ret
.hjr_target: dd 0
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
        ; The shadow must match what is on disk before anyone mutates the
        ; buffer: hbfs_save_root_dir diffs the two to decide what to write.
        push esi
        push edi
        push ecx
        mov esi, hbfs_dir_buf
        mov edi, hbfs_dir_shadow
        mov ecx, HBFS_ROOT_DIR_SIZE / 4
        rep movsd
        pop ecx
        pop edi
        pop esi
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
; The bitmap is kept resident: every allocation used to read the whole 64 KB
; bitmap from disk first, which dominated the cost of creating a file.
; Returns: CF set on read error
hbfs_load_bitmap:
        cmp dword [hbfs_bitmap_valid], 1
        je  .hlb_cached

        pushad
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors
        jc .hlb_err
        mov dword [hbfs_bitmap_valid], 1
        popad
        clc
        ret
.hlb_cached:
        clc
        ret
.hlb_err:
        mov esi, msg_hbfs_bitmap_err
        call vga_print
        popad
        stc
        ret

;-----------------------------------------------------------------------
; hbfs_meta_write — write metadata sectors under the journal
;   In : EAX = LBA of first sector, ESI = buffer, ECX = sector count
;   Out: CF = 0 written; CF = 1 failed (the transaction is already rolled back)
;
; Each sector's old content is logged before it is overwritten, so a crash
; leaves either the old or the new state, never a mixture.
; All registers preserved.
;-----------------------------------------------------------------------
hbfs_meta_write:
        pushad
        mov [.hmw_lba], eax
        mov [.hmw_buf], esi
        mov [.hmw_cnt], ecx
.mhw_loop:
        cmp dword [.hmw_cnt], 0
        je  .mhw_ok
        mov eax, [.hmw_lba]
        call hbfs_journal_log_sector
        jc  .mhw_fail
        mov eax, [.hmw_lba]
        mov ecx, 1
        mov esi, [.hmw_buf]
        call ata_write_sectors
        jc  .mhw_fail
        inc dword [.hmw_lba]
        add dword [.hmw_buf], 512
        dec dword [.hmw_cnt]
        jmp .mhw_loop
.mhw_ok:
        popad
        clc
        ret
.mhw_fail:
        call hbfs_journal_abort
        popad
        stc
        ret
.hmw_lba: dd 0
.hmw_buf: dd 0
.hmw_cnt: dd 0

; Save current directory to disk
; Only the sectors whose content differs from the on-disk shadow are written,
; and each one is journaled. Rewriting the whole 128 KB directory for a single
; entry change was slow (a `ls` of a big directory could take seconds) and
; impossible to journal with a 16 KB undo log.
; Returns: CF set on write error
hbfs_save_root_dir:
        pushad
        call hbfs_journal_begin
        mov eax, [current_dir_lba]
        mov [.hsrd_lba], eax
        mov eax, [current_dir_sects]
        mov [.hsrd_cnt], eax
        mov dword [.hsrd_off], 0
.hsrd_loop:
        cmp dword [.hsrd_cnt], 0
        je  .hsrd_done
        ; Compare this 512-byte sector against the shadow
        mov esi, hbfs_dir_buf
        add esi, [.hsrd_off]
        mov edi, hbfs_dir_shadow
        add edi, [.hsrd_off]
        mov ecx, 512 / 4
        repe cmpsd
        je  .hsrd_next                  ; identical — nothing to write
        ; Changed: log the pre-image, write the sector, update the shadow
        mov eax, [.hsrd_lba]
        call hbfs_journal_log_sector
        jc  .hsrd_err
        mov eax, [.hsrd_lba]
        mov ecx, 1
        mov esi, hbfs_dir_buf
        add esi, [.hsrd_off]
        call ata_write_sectors
        jc  .hsrd_err
        mov esi, hbfs_dir_buf
        add esi, [.hsrd_off]
        mov edi, hbfs_dir_shadow
        add edi, [.hsrd_off]
        mov ecx, 512 / 4
        rep movsd
.hsrd_next:
        inc dword [.hsrd_lba]
        add dword [.hsrd_off], 512
        dec dword [.hsrd_cnt]
        jmp .hsrd_loop
.hsrd_done:
        call hbfs_journal_commit
        popad
        clc
        ret
.hsrd_err:
        call hbfs_journal_abort
        mov esi, msg_hbfs_dir_write_err
        call vga_print
        popad
        stc
        ret
.hsrd_lba: dd 0
.hsrd_cnt: dd 0
.hsrd_off: dd 0

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

        ; Read bitmap (served from the resident copy when valid)
        call hbfs_load_bitmap

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
        mov [.alloc_first], edx

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

        ; --- Persist the change as one journaled transaction ---
        call hbfs_journal_begin

        ; Only the bitmap sectors covering [first, first+n) are dirty. The
        ; bitmap is 64 KB and rewriting all of it for one allocation was the
        ; single biggest cost of every metadata operation.
        mov  eax, [.alloc_first]
        shr  eax, 12                    ; first dirty bitmap sector index
        mov  [.bitmap_sect], eax
        mov  ecx, [.alloc_first]
        add  ecx, [.blocks_needed]
        dec  ecx
        shr  ecx, 12                    ; last dirty sector index
        sub  ecx, eax
        inc  ecx                        ; sector count
        mov  eax, [.bitmap_sect]
        add  eax, HBFS_BITMAP_START
        mov  esi, hbfs_bitmap_buf
        mov  edx, [.bitmap_sect]
        shl  edx, 9                     ; byte offset into the bitmap buffer
        add  esi, edx
        call hbfs_meta_write
        jc   .alloc_fail

        ; Update superblock free_blocks counter (in-memory copy is authoritative)
        mov  eax, [.blocks_needed]
        sub [hbfs_super_buf + 12], eax   ; free_blocks -= allocated
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call hbfs_meta_write
        jc   .alloc_fail

        call hbfs_journal_commit

        mov  eax, [.alloc_first]    ; Return first block number via EAX
        mov  [esp + 28], eax
        popad
        clc
        ret

.alloc_fail:
        popad
        stc
        ret

.occupied:
        inc edx
        jmp .scan_start

.no_space:
        popad
        stc
        ret

.blocks_needed: dd 0
.alloc_first:   dd 0
.bitmap_sect:   dd 0

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

        ; Read bitmap (served from the resident copy when valid)
        call hbfs_load_bitmap

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

        ; --- Persist the change as one journaled transaction ---
        call hbfs_journal_begin

        ; Only the bitmap sectors covering the freed range are dirty
        mov  eax, [.free_start]
        shr  eax, 12
        mov  [.free_sect], eax
        mov  ecx, [.free_start]
        add  ecx, [.free_count]
        dec  ecx
        shr  ecx, 12
        sub  ecx, eax
        inc  ecx
        mov  eax, [.free_sect]
        add  eax, HBFS_BITMAP_START
        mov  esi, hbfs_bitmap_buf
        mov  edx, [.free_sect]
        shl  edx, 9
        add  esi, edx
        call hbfs_meta_write
        jc   .free_out

        ; Update superblock free_blocks counter
        mov  eax, [.free_count]
        add [hbfs_super_buf + 12], eax   ; free_blocks += freed
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call hbfs_meta_write
        jc   .free_out

        call hbfs_journal_commit

.free_out:
        popad
        ret

.free_start: dd 0
.free_count: dd 0
.free_sect:  dd 0

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
        ; Adding an entry is a write to the directory, so check that first and
        ; before the transaction opens: a refusal must not journal anything.
        call hbfs_cwd_access_current
        test eax, eax
        jnz .hcf_denied
        ; One transaction spans the block allocation (bitmap + superblock) and
        ; the directory entry, so a crash can never leave allocated-but-orphan
        ; blocks or an entry pointing at unallocated space.
        call hbfs_journal_begin
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

.hcf_denied:
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

        call hbfs_journal_commit
        popad
        clc
        ret

.alloc_fail:
        call hbfs_journal_abort
.write_fail:
        ; A failed file-data write must not leave the transaction open: an
        ; uncommitted log would be replayed (undoing the metadata) long after
        ; the caller has moved on.
        call hbfs_journal_abort
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
        call hbfs_journal_begin
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
        call hbfs_journal_commit
        popad
        ret

; Create a subdirectory in the current directory
; ESI = directory name (null-terminated)
; Returns: CF=0 success, CF=1 error
hbfs_mkdir:
        pushad
        ; A subdirectory is an entry too: the parent's write bit governs it.
        call hbfs_cwd_access_current
        test eax, eax
        jnz .hm_denied
        call hbfs_journal_begin
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
        ; A new directory is rwxr-xr-x, owned by root. Without this the entry
        ; kept mode 0, and fsck reported it as a legacy entry (and the
        ; permission checks treat mode 0 as "root only").
        mov word [edi + DIRENT_PERMS], HBFS_MODE_DIR_DEF
        mov word [edi + DIRENT_OWNER], 0
        call hbfs_save_root_dir
        call hbfs_journal_commit

        popad
        clc
        ret

.hm_fail:
        call hbfs_journal_abort
        popad
        stc
        ret
.hm_denied:
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
        ; EDI = directory entry pointer. Only an administrator may reassign
        ; ownership (see sys_chown_v3).
        push edi
        call require_root
        pop edi
        test eax, eax
        jnz .chown_fail
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
        mov eax, ebx
        call validate_user_ptr
        jc .symlink_bad
        mov eax, ecx
        call validate_user_ptr
        jc .symlink_bad
        pushad
        ; Calculate target path length, capped at the longest name HBFS stores:
        ; an unbounded scan of a caller's string can walk out of user space.
        mov esi, ecx
        xor edx, edx
.sym_len:
        lodsb
        inc edx
        test al, al
        jz .sym_measured
        cmp edx, HBFS_MAX_FILENAME
        jb .sym_len
        jmp .symlink_fail              ; no terminator within the cap
.sym_measured:

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
.symlink_bad:
        mov eax, -1
        iretd

;---------------------------------------
; sys_readlink - Read symbolic link target
;   EBX = link name pointer
;   ECX = output buffer pointer
; Returns: EAX = target path length, -1 on failure
;---------------------------------------
sys_readlink:
        mov eax, ebx
        call validate_user_ptr
        jc .readlink_bad
        mov eax, ecx
        call validate_user_ptr
        jc .readlink_bad
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
        ; Copy target path to output buffer. The size comes from the directory
        ; entry, so it cannot be trusted: clamp it to what a link target can be
        ; (the copy source holds one block, and a link is a path), then check
        ; the caller's buffer covers that many bytes.
        cmp edx, HBFS_MAX_FILENAME
        jbe .rl_size_ok
        mov edx, HBFS_MAX_FILENAME
.rl_size_ok:
        mov eax, edi
        mov ecx, edx
        call user_range_ok
        jc .readlink_fail
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
.readlink_bad:
        mov eax, -1
        iretd

;---------------------------------------
; sys_rename - Rename a file or directory (same directory)
;   EBX = old filename pointer
;   ECX = new filename pointer
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_rename:
        mov eax, ebx
        call validate_user_ptr
        jc .sr_bad_ptr
        mov eax, ecx
        call validate_user_ptr
        jc .sr_bad_ptr
        pushad
        mov [.sr_old], ebx
        mov [.sr_new], ecx
        call hbfs_load_root_dir
        ; Find old entry
        mov esi, ebx
        call hbfs_find_file
        jc .sr_fail             ; source not found
        ; Renaming rewrites the entry's name, so it is a write to the file and
        ; to the directory that holds it.
        mov ebx, HBFS_ACC_WRITE
        call hbfs_access_current
        test eax, eax
        jnz .sr_fail
        call hbfs_cwd_access_current
        test eax, eax
        jnz .sr_fail
        ; Reload: the directory check borrows the buffer for its parent scan
        call hbfs_load_root_dir
        mov esi, [.sr_old]
        call hbfs_find_file
        jc .sr_fail
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
.sr_bad_ptr:
        mov eax, -1
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
        mov eax, ebx
        call validate_user_ptr
        jc .srd_bad_ptr
        pushad
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .srd_fail            ; not found
        ; Must be a directory
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne .srd_fail
        ; Removing it rewrites its entry, so hold the caller to the write bit
        mov ebx, HBFS_ACC_WRITE
        call hbfs_access_current
        test eax, eax
        jnz .srd_fail
        ; Save entry info
        mov [.srd_edi], edi
        mov ebp, [edi + DIRENT_START_BLOCK]
        mov ebx, [edi + DIRENT_BLOCK_COUNT]
        test ebx, ebx
        jle .srd_fail

        ; Check every block of the directory, not just the first 4 KB.
        ; Subdirectories are 16 blocks / 224 entries.
.srd_scan_block:
        mov eax, ebp
        imul eax, HBFS_SECTORS_PER_BLK
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        jc .srd_fail

        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / HBFS_DIR_ENTRY_SIZE
.srd_scan:
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        jne .srd_notempty
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .srd_scan

        inc ebp
        dec ebx
        jnz .srd_scan_block

        ; Directory is empty — free all of its blocks.
        mov eax, [.srd_edi]
        mov eax, [eax + DIRENT_START_BLOCK]
        mov ecx, [.srd_edi]
        mov ecx, [ecx + DIRENT_BLOCK_COUNT]
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
.srd_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.srd_bad_ptr:
        mov eax, -1
        iretd
.srd_edi: dd 0

;---------------------------------------
; sys_truncate - Truncate a file to a given size (shrink only)
;   EBX = filename pointer
;   ECX = new size in bytes (must be <= current size)
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_truncate:
        mov eax, ebx
        call validate_user_ptr
        jc .st_bad_ptr
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
        ; Discarding content is a write to the file
        mov ebx, HBFS_ACC_WRITE
        call hbfs_access_current
        test eax, eax
        jnz .st_fail
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
.st_bad_ptr:
        mov eax, -1
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
        mov eax, ebx
        call validate_user_ptr
        jc .sgmt_bad_ptr
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
.sgmt_bad_ptr:
        mov eax, 0
        mov ecx, 0
        iretd

;---------------------------------------
; sys_setmtime - Set last-modified timestamp of a file
;   EBX = filename pointer
;   ECX = new timestamp (packed 32-bit, 0 = use current RTC time)
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_setmtime:
        mov eax, ebx
        call validate_user_ptr
        jc .ssmt_bad_ptr
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
.ssmt_bad_ptr:
        mov eax, -1
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
; hbfs_access_current — access check against the running task's credentials
;   In : EDI = directory entry, EBX = HBFS_ACC_* bits
;   Out: EAX = 0 allowed, -1 denied. EDI, EBX, ECX and EDX are preserved.
;
; The shell and other kernel contexts have no TCB and are treated as root,
; which is what the prompt has always been.
;-----------------------------------------------------------------------
hbfs_access_current:
        push edi
        push ebx
        push ecx
        push edx
        call sched_get_current_task
        xor ecx, ecx
        xor edx, edx
        test eax, eax
        jz .hac_check
        mov ecx, [eax + TCB_EUID]
        mov edx, [eax + TCB_EGID]
.hac_check:
        call hbfs_check_access
        pop edx
        pop ecx
        pop ebx
        pop edi
        ret

;-----------------------------------------------------------------------
; require_root — is the running task privileged?
;   Out: EAX = 0 when the caller is root (or the kernel/shell context), -1
;        otherwise. Preserves every other register.
;-----------------------------------------------------------------------
require_root:
        push edi
        push ecx
        push edx
        call sched_get_current_task
        test eax, eax
        jz .rr_ok                       ; shell / kernel context is root
        cmp dword [eax + TCB_EUID], 0
        jne .rr_denied
.rr_ok:
        xor eax, eax
        jmp .rr_done
.rr_denied:
        mov eax, -1
.rr_done:
        pop edx
        pop ecx
        pop edi
        ret

;-----------------------------------------------------------------------
; hbfs_cwd_access_current — may the caller add entries to the cwd?
;   Out: EAX = 0 allowed, -1 denied. Every other register is preserved.
;
; Creating a file writes its name into the directory, so the directory's own
; write bit governs it - the file's mode does not exist yet. The cwd is tracked
; as an LBA, so the bits that govern it are the ones in the entry that names it
; in its parent, and the directory stack records that parent. The root
; directory has no such entry, so it uses the stand-in below: root-owned and
; 0755, which is what a directory is created with and what an ordinary user
; may not write to.
;
; The directory buffer is borrowed for the parent scan, so the cache is
; invalidated afterwards and the next load re-reads the cwd. Callers reach here
; before they load the directory, when the buffer holds nothing unsaved.
;-----------------------------------------------------------------------
hbfs_cwd_access_current:
        pushad
        cmp dword [current_dir_lba], 0xFFFFFFFF
        je  .hcc_allowed                ; a virtual mount: not HBFS's to judge
        cmp dword [dir_depth], 0
        je  .hcc_root

        mov eax, [dir_depth]
        dec eax
        imul eax, DIR_STACK_ENTRY_SIZE
        mov ebx, [dir_stack + eax]          ; parent LBA
        mov ecx, [dir_stack + eax + 4]      ; parent sector count
        test ebx, ebx
        jz .hcc_deny
        mov [.hcc_sects], ecx

        mov dword [dir_cache_lba], 0xFFFFFFFF
        mov edi, hbfs_dir_buf
        mov eax, ebx
        call ata_read_sectors
        jc .hcc_deny                        ; cannot read it: refuse

        mov eax, [.hcc_sects]
        shl eax, 9
        xor edx, edx
        mov ecx, HBFS_DIR_ENTRY_SIZE
        div ecx
        mov ecx, eax                        ; entries in the parent
        mov edx, hbfs_dir_buf
.hcc_scan:
        test ecx, ecx
        jz .hcc_deny                        ; not found: refuse
        cmp byte [edx + DIRENT_TYPE], FTYPE_FREE
        je .hcc_next
        mov eax, [edx + DIRENT_START_BLOCK]
        shl eax, 3
        add eax, HBFS_DATA_START            ; first sector of this entry
        cmp eax, [current_dir_lba]
        je .hcc_found
.hcc_next:
        add edx, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .hcc_scan
.hcc_found:
        mov edi, edx
        jmp .hcc_check
.hcc_root:
        mov edi, hbfs_root_dir_mode_entry
.hcc_check:
        mov ebx, HBFS_ACC_WRITE
        call hbfs_access_current
        test eax, eax
        jz .hcc_allowed
.hcc_deny:
        mov dword [esp + 28], -1
        popad
        ret
.hcc_allowed:
        mov dword [esp + 28], 0
        popad
        ret
.hcc_sects: dd 0

; Stand-in dirent for "/", in the field positions hbfs_check_access reads.
align 4
hbfs_root_dir_mode_entry:
        times DIRENT_PERMS db 0
        dw HBFS_MODE_DIR_DEF            ; 276 mode: rwxr-xr-x
        dw 0                            ; 278 owner: root
        dw 0                            ; 280 group: root
        times 4 db 0

;-----------------------------------------------------------------------
; require_owner_or_root — may the running task change this file's mode?
;   In : EDI = directory entry
;   Out: EAX = 0 allowed, -1 denied. Preserves every other register.
;-----------------------------------------------------------------------
require_owner_or_root:
        push edi
        push ecx
        push edx
        movzx edx, word [edi + HBFS_DE_UID]
        call sched_get_current_task
        test eax, eax
        jz .ror_ok                      ; shell / kernel context is root
        mov ecx, [eax + TCB_EUID]
        test ecx, ecx
        jz .ror_ok                      ; root
        cmp ecx, edx
        jne .ror_denied
.ror_ok:
        xor eax, eax
        jmp .ror_done
.ror_denied:
        mov eax, -1
.ror_done:
        pop edx
        pop ecx
        pop edi
        ret


;-----------------------------------------------------------------------
; sys_chmod — SYS_CHMOD implementation (v3: actual on-disk write)
;   In (from syscall frame): EBX = filename_ptr, ECX = mode_word
;   Out: EAX = 0 success, -1 fail
;-----------------------------------------------------------------------
sys_chmod_v3:
        pushad
        call hbfs_journal_begin
        ; Validate user pointer
        mov eax, ebx
        call validate_user_ptr
        jc .schmod_fail

        ; Load the directory before searching it. hbfs_find_entry reads
        ; hbfs_dir_buf as it stands, and nothing had loaded the directory for
        ; this path: chmod searched whatever an earlier command left behind, so
        ; it reported "file not found" for files that were right there.
        call hbfs_load_root_dir
        jc .schmod_fail

        ; Find the file in HBFS directory
        call hbfs_find_entry     ; EBX=name -> EDI=entry ptr or 0; EAX=entry#
        test eax, eax
        js .schmod_fail

        ; The mode is the owner's to change; anyone else is refused.
        push ecx
        call require_owner_or_root
        pop ecx
        test eax, eax
        jnz .schmod_fail

        ; Update mode field in the loaded directory entry buffer
        mov [edi + HBFS_DE_MODE], cx

        ; Mark version as v3 if not already (so we persist the permission data)
        cmp dword [hbfs_super_buf + 4], HBFS_VERSION_V3
        je .schmod_flush
        mov dword [hbfs_super_buf + 4], HBFS_VERSION_V3
        ; Enable permission feature flag
        or  dword [hbfs_super_buf + HBFS_SB_FEATURES], HBFS_FEAT_PERMS
        ; Write updated superblock (journaled: it carries the feature flag)
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call hbfs_meta_write
        jc   .schmod_fail

.schmod_flush:
        ; Write the modified directory entry back to disk
        call hbfs_flush_dir_entry   ; EDI=entry, EAX=entry# -> writes sector
        jc .schmod_fail

        call hbfs_journal_commit
        mov dword [esp + 28], 0    ; EAX in saved pushad frame
        popad
        iretd

.schmod_fail:
        call hbfs_journal_abort
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
        call hbfs_journal_begin
        mov eax, ebx
        call validate_user_ptr
        jc .schown_fail

        call hbfs_load_root_dir
        jc .schown_fail

        call hbfs_find_entry
        test eax, eax
        js .schown_fail

        ; Changing an owner is an administrative act: without this, any
        ; process could chown a root-owned file to itself and then write it.
        call require_root
        test eax, eax
        jnz .schown_fail

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
        call hbfs_meta_write
        pop  eax

.schown_flush:
        call hbfs_flush_dir_entry
        jc .schown_fail
        call hbfs_journal_commit
        mov dword [esp + 28], 0
        popad
        iretd
.schown_fail:
        call hbfs_journal_abort
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
        mov [.hxg_entry], ebx
        mov [.hxg_key], ecx
        mov [.hxg_val], edx
        mov [.hxg_max], esi

        movzx eax, word [ebx + HBFS_DE_XATTR_COUNT]
        test eax, eax
        jz .hxg_notfound
        mov eax, [ebx + HBFS_DE_XATTR_LBA]
        test eax, eax
        jz .hxg_notfound

        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        jc .hxg_notfound

        mov edi, hbfs_block_buf
        mov ecx, [.hxg_entry]
        movzx ecx, word [ecx + HBFS_DE_XATTR_COUNT]
        xor edx, edx                    ; slot index
.hxg_loop:
        cmp edx, ecx
        jge .hxg_notfound
        cmp byte [edi], 0
        je .hxg_notfound                ; free slot: key is absent
        push ecx
        push edx
        push edi
        mov esi, [.hxg_key]
        call str_compare                ; ESI=requested, EDI=stored
        pop edi
        pop edx
        pop ecx
        jz .hxg_found
        add edi, HBFS_XATTR_ENTRY_SIZE
        inc edx
        jmp .hxg_loop

.hxg_found:
        add edi, HBFS_XATTR_KEY_LEN     ; EDI -> value field
        mov esi, edi                    ; ESI = value source
        mov edi, [.hxg_val]             ; EDI = caller buffer
        mov ecx, [.hxg_max]
        xor eax, eax
        test ecx, ecx
        jz .hxg_copy_done
.hxg_copy:
        cmp eax, ecx
        jge .hxg_copy_done
        mov dl, [esi + eax]
        mov [edi + eax], dl
        test dl, dl
        jz .hxg_copy_done
        inc eax
        jmp .hxg_copy
.hxg_copy_done:
        mov [esp + 28], eax             ; return length
        popad
        ret

.hxg_notfound:
        mov dword [esp + 28], -1
        popad
        ret

.hxg_entry: dd 0
.hxg_key:   dd 0
.hxg_val:   dd 0
.hxg_max:   dd 0

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

        ; Store the LBA (convert block# to LBA). hbfs_alloc_block returns a
        ; data-block number; its LBA is DATA_START + block*8. Adding the
        ; journal offset here pointed four blocks past the allocated block.
        mov edi, eax
        imul edi, HBFS_SECTORS_PER_BLK
        add edi, HBFS_DATA_START
        mov [ebx + HBFS_DE_XATTR_LBA], edi

        ; Persist the zeroed block before the read-back below, otherwise the
        ; free-slot scan sees whatever stale sectors were on disk.
        push edi
        mov eax, edi
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        pop edi
        jc .hxs_fail_pop

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
        push edi
        mov esi, [.hxs_key_ptr]
        call str_compare
        pop edi
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
        ; Callers mutate the entry in hbfs_dir_buf and then ask for it to be
        ; persisted. hbfs_save_root_dir writes only the sectors that differ from
        ; the on-disk shadow — for a single entry that is exactly one 512-byte
        ; sector — and journals it. (This function used to do its own
        ; read-modify-write, which ignored current_dir_lba entirely.)
        jmp hbfs_save_root_dir
