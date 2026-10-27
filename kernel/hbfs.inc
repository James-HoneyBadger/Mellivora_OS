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

;-----------------------------------------------------------------------
; HBFS v4 — verified, read-only in this build.
;
; v4 keeps every v3 field where it is and adds its own after them in the same
; sector, so a v3 reader still finds the geometry it expects. It adds a second
; magic, a feature word, 64-bit counter high halves and a CRC-32C over the
; superblock and every metadata run. The record layouts below must match
; populate.py and tools/fsck_hbfs.py.
;-----------------------------------------------------------------------
HBFS4_MAGIC            equ 0x34424653   ; 'HBF4' on disk
HBFS4_VERSION          equ 4
HBFS4_OFF_FEATURES     equ 60           ; dword
HBFS4_OFF_GENERATION   equ 64           ; dword
HBFS4_OFF_BLOCKS_HI    equ 68           ; dword: high half of total blocks
HBFS4_OFF_DATA_HI      equ 72           ; dword: high half of data start
HBFS4_OFF_CHECKSUM     equ 76           ; dword: CRC-32C of this sector
HBFS4_FEATURE_EXTENTS  equ (1 << 0)
HBFS4_FEATURE_JOURNAL  equ (1 << 1)
HBFS4_FEATURE_SNAPSHOT equ (1 << 2)     ; not honoured here; refuses the mount
HBFS4_DE_EXTENTS       equ (1 << 0)     ; directory entry flags: data is a run list
HBFS4_RUN_CHECKSUM_BYTES equ 4          ; trailer at the end of each metadata run

; An extent entry's `start` names a metadata block of {block, count} runs.
HBFS4_EXTENT_MAGIC     equ 0x34584248   ; 'H','B','X','4' on disk
HBFS4_EXTENT_HDR       equ 8            ; magic, run count
HBFS4_EXTENT_RECORD    equ 8            ; run start block, run block count
HBFS4_EXTENT_MAX       equ ((HBFS_BLOCK_SIZE - HBFS4_EXTENT_HDR - \
                             HBFS4_RUN_CHECKSUM_BYTES) / HBFS4_EXTENT_RECORD)

; A freshly created subdirectory is HBFS_SUBDIR_BLOCKS zero blocks, so its run
; checksum is always the CRC-32C of 65 536 zero bytes. Stamping the last block
; with this value is cheaper than zeroing and checksumming 64 KB per mkdir, and
; tests/test_v4_mount.py fails the fsck if the constant is ever wrong.
HBFS4_EMPTY_DIR_CRC     equ 0x72C0C4A4

hbfs_init:
        pushad

        ; Nothing is mounted until a volume passes every check below. Every
        ; mutation path refuses to write while this is clear, and a volume
        ; this build cannot address is never formatted to make it fit: the
        ; previous behaviour destroyed a v4 or damaged volume on first boot.
        mov dword [hbfs_mounted], 0
        mov dword [hbfs_read_only], 0
        mov dword [hbfs_total_blocks], 0

        cmp byte [ata_present], 0
        je .no_disk

        ; Try to read superblock
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov edi, hbfs_super_buf
        call ata_read_sectors
        jc .read_error

        ; Check magic. v4 uses a second magic in the same sector and is
        ; mounted read-only; everything older is the v3 path below.
        cmp dword [hbfs_super_buf], HBFS4_MAGIC
        je  .mount_v4
        cmp dword [hbfs_super_buf], HBFS_MAGIC
        jne .no_volume

        ; Only the versions this kernel implements may be mounted: v1 (no
        ; journal), v2 and v3. A newer or nonsensical version has rules this
        ; code does not know, so it is refused rather than mounted.
        mov eax, [hbfs_super_buf + 4]
        test eax, eax
        jz .bad_version
        cmp eax, HBFS_VERSION_V3
        ja .bad_version

        ; The block size and the three starts must be the ones this build
        ; addresses. Mounting a volume with a different layout would read and
        ; write the wrong LBAs.
        cmp dword [hbfs_super_buf + 28], HBFS_BLOCK_SIZE
        jne .bad_layout
        mov eax, [hbfs_super_buf + 16]
        cmp eax, HBFS_ROOT_DIR_START
        jne .bad_layout
        mov eax, [hbfs_super_buf + 20]
        cmp eax, HBFS_BITMAP_START
        jne .bad_layout
        mov eax, [hbfs_super_buf + 24]
        cmp eax, HBFS_DATA_START
        jne .bad_layout

        ; The bitmap can only address HBFS_BITMAP_SIZE * 8 blocks, and the
        ; volume must be larger than its reserved journal area.
        mov eax, [hbfs_super_buf + 8]
        cmp eax, HBFS_JOURNAL_BLOCKS
        jbe .bad_layout
        cmp eax, HBFS_BITMAP_SIZE * 8
        ja .bad_layout
        mov [hbfs_total_blocks], eax

        ; A free count larger than the volume itself is corrupt.
        mov ecx, [hbfs_super_buf + 12]
        cmp ecx, eax
        ja .bad_layout

        ; A feature bit this kernel does not implement changes how the volume
        ; must be read and written; mounting it read-write would corrupt it.
        mov eax, [hbfs_super_buf + HBFS_SB_FEATURES]
        test eax, ~(HBFS_FEAT_PERMS | HBFS_FEAT_XATTR)
        jnz .bad_features

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
        ; Load root directory. The mount is marked first so that the loader —
        ; which is itself guarded — is allowed to read; a failed load clears it
        ; again and the volume stays refused.
        mov dword [hbfs_mounted], 1
        call hbfs_load_root_dir
        jc .root_error

        popad
        ret

;-----------------------------------------------------------------------
; HBFS v4 mount — verified and read-only.
;
; Every field is checked before the volume is trusted, and the superblock,
; bitmap and each directory run are verified against their CRC-32C. Writes
; stay refused (hbfs_require_writable) until a v4 writer exists: a write
; would have to restamp runs this build does not stamp.
;-----------------------------------------------------------------------
.mount_v4:
        cmp dword [hbfs_super_buf + 4], HBFS4_VERSION
        jne .bad_version

        ; Only features this reader implements may be present. A snapshot
        ; volume (or anything newer) is read by rules this code does not know.
        mov eax, [hbfs_super_buf + HBFS4_OFF_FEATURES]
        test eax, ~(HBFS4_FEATURE_EXTENTS | HBFS4_FEATURE_JOURNAL)
        jnz .bad_features

        ; The layout is v3's, and this build addresses blocks in 32 bits.
        cmp dword [hbfs_super_buf + 28], HBFS_BLOCK_SIZE
        jne .bad_layout
        mov eax, [hbfs_super_buf + 16]
        cmp eax, HBFS_ROOT_DIR_START
        jne .bad_layout
        mov eax, [hbfs_super_buf + 20]
        cmp eax, HBFS_BITMAP_START
        jne .bad_layout
        mov eax, [hbfs_super_buf + 24]
        cmp eax, HBFS_DATA_START
        jne .bad_layout
        cmp dword [hbfs_super_buf + HBFS4_OFF_BLOCKS_HI], 0
        jne .bad_layout
        cmp dword [hbfs_super_buf + HBFS4_OFF_DATA_HI], 0
        jne .bad_layout

        mov eax, [hbfs_super_buf + 8]
        cmp eax, HBFS_JOURNAL_BLOCKS
        jbe .bad_layout
        cmp eax, HBFS_BITMAP_SIZE * 8
        ja .bad_layout
        mov [hbfs_total_blocks], eax

        mov ecx, [hbfs_super_buf + 12]
        cmp ecx, eax
        ja .bad_layout

        ; The superblock's CRC is what says the geometry above is real.
        call hbfs_verify_superblock
        jc  .bad_checksum

        ; Journal state. A dirty log is replayed with the same undo pass the
        ; v3 path uses: its records carry their own CRC, and the pre-images
        ; they restore are checksum-valid for the run they belong to.
        mov  eax, [hbfs_super_buf + HBFS_SB_JRNL_PTR]
        mov  [hbfs_journal_ptr], eax
        mov  eax, [hbfs_super_buf + HBFS_SB_JRNL_EPOCH]
        mov  [hbfs_journal_epoch], eax
        cmp  dword [hbfs_super_buf + HBFS_SB_JRNL_STATE], 0
        je   .v4_journal_clean
        mov  eax, [hbfs_super_buf + HBFS4_OFF_FEATURES]
        test eax, HBFS4_FEATURE_JOURNAL
        jz   .v4_dirty              ; cannot replay a log it does not claim
        call hbfs_journal_recover
.v4_journal_clean:
        mov  dword [hbfs_journal_dirty], 0

        ; Read and verify the bitmap run before anything could trust it.
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors
        jc  .read_error
        mov eax, hbfs_bitmap_buf
        mov ecx, HBFS_BITMAP_SIZE
        call hbfs_verify_run
        jc  .bad_checksum
        mov dword [hbfs_bitmap_valid], 1

        ; A v4 volume is writable when it declares the journal feature: every
        ; metadata run is restamped as it is written, so the checksums stay
        ; true. Without that feature it stays read-only, as before.
        mov  dword [hbfs_read_only], 1
        mov  eax, [hbfs_super_buf + HBFS4_OFF_FEATURES]
        test eax, HBFS4_FEATURE_JOURNAL
        jz   .v4_ro
        mov  dword [hbfs_read_only], 0
.v4_ro:
        mov  dword [hbfs_mounted], 1
        call hbfs_load_root_dir
        jc  .v4_root_bad

        cmp  dword [hbfs_read_only], 0
        jne  .v4_report_ro
        mov  esi, msg_hbfs_v4_rw
        jmp  .v4_report
.v4_report_ro:
        mov  esi, msg_hbfs_v4_ro
.v4_report:
        call vga_print
        popad
        ret

.v4_dirty:
        mov esi, msg_hbfs_v4_journal
        jmp .refuse
.bad_checksum:
        mov esi, msg_hbfs_bad_checksum
        jmp .refuse
.v4_root_bad:
        mov esi, msg_hbfs_root_err
        jmp .refuse

.read_error:
        mov esi, msg_hbfs_read_err
        jmp .refuse
.no_volume:
        mov esi, msg_hbfs_no_format
        jmp .refuse
.bad_version:
        mov esi, msg_hbfs_bad_version
        jmp .refuse
.bad_layout:
        mov esi, msg_hbfs_bad_layout
        jmp .refuse
.bad_features:
        mov esi, msg_hbfs_bad_features
        jmp .refuse
.root_error:
        ; hbfs_load_root_dir has already said the read failed.
        mov dword [hbfs_mounted], 0
        mov esi, msg_hbfs_root_err
.refuse:
        ; Nothing may allocate against, or write to, a volume that was refused.
        mov dword [hbfs_mounted], 0
        mov dword [hbfs_read_only], 0
        mov dword [hbfs_total_blocks], 0
        call vga_print
        popad
        ret

.no_disk:
        mov esi, msg_hbfs_nodisk
        call vga_print
        popad
        ret

;-----------------------------------------------------------------------
; hbfs_require_mounted - refuse a write when no volume was mounted.
; Returns: CF clear when a volume is mounted, CF set (nothing written) when not.
;-----------------------------------------------------------------------
hbfs_require_mounted:
        cmp dword [hbfs_mounted], 0
        je  .hrm_refuse
        clc
        ret
.hrm_refuse:
        stc
        ret

;-----------------------------------------------------------------------
; hbfs_require_writable - refuse a mutation unless the mounted volume may be
; written. A v4 volume mounts read-only in this build, so every writer goes
; through here: the volume is intact, but nothing may stamp it yet.
; Returns: CF clear when writable, CF set (nothing written) when not.
;-----------------------------------------------------------------------
hbfs_require_writable:
        cmp dword [hbfs_mounted], 0
        je  .hrw_refuse
        cmp dword [hbfs_read_only], 0
        jne .hrw_refuse
        clc
        ret
.hrw_refuse:
        stc
        ret

; Format: create fresh filesystem
hbfs_format:
        pushad

        ; A formatted volume is v4. This used to write version 2 with no
        ; feature word: the volume a user created in the OS was older than the
        ; one the builder ships, carried no checksums, and had to be upgraded
        ; before permissions or xattrs worked.
        mov edi, hbfs_super_buf
        mov ecx, 512 / 4
        xor eax, eax
        rep stosd

        mov edi, hbfs_super_buf
        mov dword [edi + 0], HBFS4_MAGIC
        mov dword [edi + 4], HBFS4_VERSION
        mov dword [edi + 8], HBFS_TOTAL_BLOCKS
        ; The journal area is marked allocated below, so it is not free.
        mov dword [edi + 12], HBFS_TOTAL_BLOCKS - HBFS_JOURNAL_BLOCKS
        mov dword [edi + 16], HBFS_ROOT_DIR_START
        mov dword [edi + 20], HBFS_BITMAP_START
        mov dword [edi + 24], HBFS_DATA_START
        mov dword [edi + 28], HBFS_BLOCK_SIZE
        mov dword [edi + HBFS_SB_JRNL_STATE], 0
        mov dword [edi + HBFS_SB_JRNL_PTR],   0
        mov dword [edi + HBFS_SB_JRNL_EPOCH], 0
        mov dword [edi + HBFS4_OFF_FEATURES], HBFS4_FEATURE_EXTENTS | HBFS4_FEATURE_JOURNAL
        mov dword [edi + HBFS4_OFF_GENERATION], 0
        mov dword [edi + HBFS4_OFF_BLOCKS_HI], 0
        mov dword [edi + HBFS4_OFF_DATA_HI],   0
        call hbfs_write_superblock          ; stamp the CRC, then write
        jc .fmt_fail

        ; Bitmap: everything free, the journal area reserved, then the run's
        ; CRC into its trailer.
        mov edi, hbfs_bitmap_buf
        mov ecx, HBFS_BITMAP_SIZE / 4
        xor eax, eax
        rep stosd
        mov ecx, HBFS_JOURNAL_BLOCKS
        xor edx, edx
.fmt_mark_journal:
        bts dword [hbfs_bitmap_buf], edx
        inc edx
        loop .fmt_mark_journal
        mov eax, hbfs_bitmap_buf
        mov ecx, HBFS_BITMAP_SIZE
        call hbfs_stamp_run
        mov dword [.fmt_lba], HBFS_BITMAP_START
        mov dword [.fmt_ptr], hbfs_bitmap_buf
        mov dword [.fmt_left], HBFS_BITMAP_BLOCKS
        call .fmt_write_run

        ; Journal area: zero it, so a stale record cannot be replayed.
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        xor eax, eax
        rep stosd
        mov dword [.fmt_lba], HBFS_JOURNAL_LBA
        mov dword [.fmt_ptr], hbfs_block_buf
        mov dword [.fmt_left], HBFS_JOURNAL_BLOCKS
        call .fmt_write_run
        mov dword [hbfs_journal_ptr], 0
        mov dword [hbfs_journal_dirty], 0
        mov dword [hbfs_journal_epoch], 0
        mov dword [hbfs_bitmap_valid], 0

        ; Root directory: 128 KB of zeros, checksummed as one run.
        mov edi, hbfs_dir_buf
        mov ecx, HBFS_ROOT_DIR_SIZE / 4
        xor eax, eax
        rep stosd
        mov eax, hbfs_dir_buf
        mov ecx, HBFS_ROOT_DIR_SIZE
        call hbfs_stamp_run
        mov dword [.fmt_lba], HBFS_ROOT_DIR_START
        mov dword [.fmt_ptr], hbfs_dir_buf
        mov dword [.fmt_left], HBFS_ROOT_DIR_BLOCKS
        call .fmt_write_run
        jmp  .fmt_written
.fmt_dir_done:

        ; Write one block per iteration from .fmt_ptr at .fmt_lba, .fmt_left
        ; times. The counters live in memory because the disk routine does not
        ; promise to preserve every register.
.fmt_write_run:
        cmp  dword [.fmt_left], 0
        je   .fmt_run_done
        mov  eax, [.fmt_lba]
        mov  ecx, HBFS_SECTORS_PER_BLK
        mov  esi, [.fmt_ptr]
        call ata_write_sectors
        jc   .fmt_fail
        add  dword [.fmt_lba], HBFS_SECTORS_PER_BLK
        add  dword [.fmt_ptr], HBFS_BLOCK_SIZE
        dec  dword [.fmt_left]
        jmp  .fmt_write_run
.fmt_run_done:
        ret

.fmt_lba:  dd 0
.fmt_ptr:  dd 0
.fmt_left: dd 0
.fmt_written:


        ; Read the superblock back before declaring the volume mounted: a
        ; format whose writes never reached the disk (a missing or failing
        ; drive) must not be remembered as a mounted filesystem.
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  edi, hbfs_block_buf
        call ata_read_sectors
        jc   .fmt_fail
        cmp  dword [hbfs_block_buf], HBFS4_MAGIC
        jne  .fmt_fail

        ; A fresh volume has an empty root: any working directory, cached
        ; directory and cached bitmap from the old volume point at blocks that
        ; no longer exist.
        mov  dword [current_dir_lba],   HBFS_ROOT_DIR_START
        mov  dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        mov  dword [dir_depth],         0
        mov  byte  [current_dir_name],  '/'
        mov  byte  [current_dir_name + 1], 0
        mov  dword [dir_cache_lba],     0xFFFFFFFF
        mov  dword [hbfs_extent_lba],   0
        mov  dword [hbfs_total_blocks], HBFS_TOTAL_BLOCKS
        mov  dword [hbfs_read_only],    0
        mov  dword [hbfs_mounted],      1

        mov esi, msg_hbfs_formatted
        call vga_print

        popad
        ret

.fmt_fail:
        mov  dword [hbfs_mounted],      0
        mov  dword [hbfs_read_only],    0
        mov  dword [hbfs_total_blocks], 0
        mov  esi, msg_hbfs_format_err
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
; hbfs_verify_run — verify a v4 metadata run's trailing CRC-32C.
;   In : EAX = buffer, ECX = run length in bytes
;   Out: CF clear when the stored trailer matches this run's contents
;
; The trailer is read as zero while the checksum is computed, which is how
; populate.py and tools/fsck_hbfs.py stamp it. crc32c preserves EBX and EDX,
; so the trailer pointer and stored value survive the call.
;-----------------------------------------------------------------------
hbfs_verify_run:
        mov  ebx, eax
        add  ebx, ecx
        sub  ebx, HBFS4_RUN_CHECKSUM_BYTES
        mov  edx, [ebx]
        mov  dword [ebx], 0
        call crc32c
        mov  [ebx], edx
        cmp  eax, edx
        jne  .hvr_bad
        clc
        ret
.hvr_bad:
        stc
        ret

;-----------------------------------------------------------------------
; hbfs_verify_superblock — verify the v4 superblock's CRC-32C at offset 76.
;   In : superblock already read into hbfs_super_buf
;   Out: CF clear when valid
;-----------------------------------------------------------------------
hbfs_verify_superblock:
        mov  edx, [hbfs_super_buf + HBFS4_OFF_CHECKSUM]
        mov  dword [hbfs_super_buf + HBFS4_OFF_CHECKSUM], 0
        mov  eax, hbfs_super_buf
        mov  ecx, 512
        call crc32c
        mov  [hbfs_super_buf + HBFS4_OFF_CHECKSUM], edx
        cmp  eax, edx
        jne  .hvs_bad
        clc
        ret
.hvs_bad:
        stc
        ret

;-----------------------------------------------------------------------
; hbfs_stamp_run - write a metadata run's CRC-32C into its trailer
;   In : EAX = buffer, ECX = run length in bytes
; The trailer is read as zero while the checksum is computed, matching
; populate.py, tools/fsck_hbfs.py and hbfs_verify_run.
;-----------------------------------------------------------------------
hbfs_stamp_run:
        mov  ebx, eax
        add  ebx, ecx
        sub  ebx, HBFS4_RUN_CHECKSUM_BYTES
        mov  dword [ebx], 0
        call crc32c
        mov  [ebx], eax
        ret

;-----------------------------------------------------------------------
; hbfs_stamp_superblock - refresh the v4 superblock CRC at offset 76
;-----------------------------------------------------------------------
hbfs_stamp_superblock:
        mov  dword [hbfs_super_buf + HBFS4_OFF_CHECKSUM], 0
        mov  eax, hbfs_super_buf
        mov  ecx, 512
        call crc32c
        mov  [hbfs_super_buf + HBFS4_OFF_CHECKSUM], eax
        ret

;-----------------------------------------------------------------------
; hbfs_write_superblock - stamp a v4 superblock, then write it
;   Out: CF as ata_write_sectors
; Every superblock write goes through here so a v4 volume never carries a
; stale checksum: the journal state itself is inside the checksummed sector.
;-----------------------------------------------------------------------
hbfs_write_superblock:
        cmp  dword [hbfs_super_buf], HBFS4_MAGIC
        jne  .hws_write
        call hbfs_stamp_superblock
.hws_write:
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        jmp  ata_write_sectors

;-----------------------------------------------------------------------
; hbfs_map_block - translate a file's logical block to a physical block
;   In : EDI = directory entry, EBX = logical block index (0-based)
;   Out: EAX = physical data block, CF set if the block does not exist
;
; An inline entry (the v3 form, and every one-run v4 file) stores `start` as
; its first data block, so the map is an addition. A v4 extent entry stores
; `start` as a metadata block of {start, count} runs, and the logical block is
; found by walking them. The extent block is cached because a sequential read
; would otherwise re-read it for every 4 KB. Block 0 is the journal area and
; can never be an extent block, so it is a safe "nothing cached" value.
;-----------------------------------------------------------------------
hbfs_map_block:
        test word [edi + 254], HBFS4_DE_EXTENTS
        jnz  .hmb_extent
        mov  eax, [edi + 260]
        add  eax, ebx
        clc
        ret
.hmb_extent:
        mov  eax, [edi + 260]                ; extent block number
        jmp  hbfs_map_extent

;-----------------------------------------------------------------------
; hbfs_map_extent - map a logical block through a v4 extent block
;   In : EAX = extent block number, EBX = logical block index
;   Out: EAX = physical data block, CF set if the list is invalid or too
;        short; ECX, EDX, ESI preserved; EBX is the input, not a result
;
; This is the half of the map that does not need a directory entry, so the
; fd layer - which remembers only the block number - can use it too.
;-----------------------------------------------------------------------
hbfs_map_extent:
        push ecx
        push edx
        push esi
        mov  [.hmw_want], ebx

        mov  esi, eax                        ; extent block number
        cmp  esi, [hbfs_extent_lba]
        je   .hmw_cached
        mov  eax, esi
        imul eax, HBFS_SECTORS_PER_BLK
        add  eax, HBFS_DATA_START
        mov  ecx, HBFS_SECTORS_PER_BLK
        mov  edi, hbfs_extent_buf
        call ata_read_sectors
        jc   .hmw_fail
        mov  eax, hbfs_extent_buf
        mov  ecx, HBFS_BLOCK_SIZE
        call hbfs_verify_run                ; it is a metadata run like any other
        jc   .hmw_fail
        mov  [hbfs_extent_lba], esi

.hmw_cached:
        cmp  dword [hbfs_extent_buf], HBFS4_EXTENT_MAGIC
        jne  .hmw_fail
        mov  ecx, [hbfs_extent_buf + 4]     ; number of runs
        test ecx, ecx
        jz   .hmw_fail
        cmp  ecx, HBFS4_EXTENT_MAX
        ja   .hmw_fail
        lea  esi, [hbfs_extent_buf + HBFS4_EXTENT_HDR]
        mov  edx, [.hmw_want]
.hmw_walk:
        mov  eax, [esi + 4]                 ; blocks in this run
        test eax, eax
        jz   .hmw_fail
        cmp  edx, eax
        jb   .hmw_found
        sub  edx, eax
        add  esi, HBFS4_EXTENT_RECORD
        dec  ecx
        jnz  .hmw_walk
        jmp  .hmw_fail                      ; past the last run
.hmw_found:
        mov  eax, [esi]
        add  eax, edx
        pop  esi
        pop  edx
        pop  ecx
        clc
        ret
.hmw_fail:
        pop  esi
        pop  edx
        pop  ecx
        stc
        ret
.hmw_want: dd 0

;-----------------------------------------------------------------------
; hbfs_journal_begin — start (or nest into) a metadata transaction
;   Every begin must be paired with exactly one commit or abort.
;   Nested calls only bump a depth counter: the outermost transaction owns
;   the journal, so a helper that both allocates blocks (bitmap + superblock)
;   and writes a directory entry still produces one atomic unit.
; All registers preserved.
;-----------------------------------------------------------------------
hbfs_journal_begin:
        ; A transaction must never open on an unmounted volume: its first act
        ; is to write JRNL_STATE=1 into whatever superblock is in memory.
        call hbfs_require_writable
        jc   .hjb_done
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
        call hbfs_write_superblock
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
        call hbfs_write_superblock

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
        call hbfs_write_superblock

        popad
        ret
.hjr_target: dd 0
; Returns: CF set on read error
hbfs_load_root_dir:
        ; A refused volume must not be read back through the shell either: the
        ; directory would be parsed with rules the mount check just rejected.
        call hbfs_require_mounted
        jc .hlrd_refuse
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
        ; A v4 volume carries a CRC-32C in the last four bytes of every
        ; metadata run. Verify this directory's before anything trusts it —
        ; a torn directory would otherwise be parsed as entries. This is keyed
        ; on the format, not on read-only: a journalled v4 volume is writable
        ; and still has to be checked on every load.
        cmp dword [hbfs_super_buf], HBFS4_MAGIC
        jne .hlrd_verified
        mov eax, hbfs_dir_buf
        mov ecx, [current_dir_sects]
        shl ecx, 9                      ; sectors -> bytes
        call hbfs_verify_run
        jc  .hlrd_crc_bad
.hlrd_verified:
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
.hlrd_crc_bad:
        ; The read already filled the buffer before the checksum said no. Drop
        ; it: a caller that ignores CF must not see entries from a run this
        ; mount refused, or the refusal would be cosmetic.
        pushad
        mov  edi, hbfs_dir_buf
        mov  ecx, HBFS_ROOT_DIR_SIZE / 4
        xor  eax, eax
        rep  stosd
        popad
        mov esi, msg_hbfs_dir_crc_err
        call vga_print
        popad
        stc
        ret
.hlrd_refuse:
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
        call hbfs_require_writable
        jc  .hmw_refuse
        pushad
        mov [.hmw_lba], eax
        mov [.hmw_buf], esi
        mov [.hmw_cnt], ecx
        mov dword [.hmw_extra], 0

        ; A v4 volume carries a CRC-32C at the end of every metadata run. The
        ; caller writes a range of sectors, so restamp the run here and, when
        ; the range does not reach the end of the run, write the trailer's
        ; sector too — otherwise the volume would be left with a stale
        ; checksum that the next mount would (rightly) refuse.
        cmp  dword [hbfs_super_buf], HBFS4_MAGIC
        jne  .mhw_stamped
        cmp  eax, HBFS_SUPERBLOCK_LBA
        jne  .mhw_bitmap?
        call hbfs_stamp_superblock
        jmp  .mhw_stamped
.mhw_bitmap?:
        mov  edx, eax
        sub  edx, HBFS_BITMAP_START
        cmp  edx, HBFS_BITMAP_SECTS
        jae  .mhw_stamped
        mov  eax, hbfs_bitmap_buf
        mov  ecx, HBFS_BITMAP_SIZE
        call hbfs_stamp_run
        mov  edx, [.hmw_lba]
        add  edx, [.hmw_cnt]
        cmp  edx, HBFS_BITMAP_START + HBFS_BITMAP_SECTS
        jae  .mhw_stamped
        mov  dword [.hmw_extra], 1
.mhw_stamped:
        mov  eax, [.hmw_lba]
        mov  ecx, [.hmw_cnt]
        mov  esi, [.hmw_buf]
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
        ; A partial bitmap write still has to publish the run's new trailer.
        cmp  dword [.hmw_extra], 0
        je   .mhw_done
        mov  dword [.hmw_extra], 0
        mov  eax, HBFS_BITMAP_START + HBFS_BITMAP_SECTS - 1
        call hbfs_journal_log_sector
        jc   .mhw_fail
        mov  eax, HBFS_BITMAP_START + HBFS_BITMAP_SECTS - 1
        mov  ecx, 1
        mov  esi, hbfs_bitmap_buf + HBFS_BITMAP_SIZE - 512
        call ata_write_sectors
        jc   .mhw_fail
.mhw_done:
        popad
        clc
        ret
.mhw_fail:
        call hbfs_journal_abort
        popad
        stc
        ret
.hmw_refuse:
        stc
        ret
.hmw_lba: dd 0
.hmw_buf: dd 0
.hmw_cnt: dd 0
.hmw_extra: dd 0

; Save current directory to disk
; Only the sectors whose content differs from the on-disk shadow are written,
; and each one is journaled. Rewriting the whole 128 KB directory for a single
; entry change was slow (a `ls` of a big directory could take seconds) and
; impossible to journal with a 16 KB undo log.
; Returns: CF set on write error
hbfs_save_root_dir:
        call hbfs_require_writable
        jc  .hsrd_refuse
        pushad
        call hbfs_journal_begin
        ; Stamp first: the trailer is part of the run, so the diff below then
        ; writes the sector that carries it along with the changed entries.
        cmp  dword [hbfs_super_buf], HBFS4_MAGIC
        jne  .hsrd_stamped
        mov  eax, hbfs_dir_buf
        mov  ecx, [current_dir_sects]
        shl  ecx, 9
        call hbfs_stamp_run
.hsrd_stamped:
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
.hsrd_refuse:
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
        ; v9.0: start from HBFS_JOURNAL_BLOCKS to skip journal reserved area.
        ; The limit comes from the mounted volume, not this build's default
        ; size: a smaller image has no blocks past its own count to allocate.
        mov edx, HBFS_JOURNAL_BLOCKS   ; Start block (skip journal)
        mov ecx, [hbfs_total_blocks]

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
        call hbfs_require_writable
        jc .hcf_denied
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

        ; Allocate contiguous blocks. If the volume has no run that big, a v4
        ; volume stores the file as a run list instead of failing.
        mov ecx, [.save_blocks]
        push edi
        call hbfs_alloc_blocks
        pop edi
        jnc .hcf_start_ready
        cmp dword [hbfs_super_buf], HBFS4_MAGIC
        jne .alloc_fail
        mov ecx, [.save_blocks]
        call hbfs_extent_alloc
        jc .alloc_fail
        mov word [edi + 254], HBFS4_DE_EXTENTS
.hcf_start_ready:
        mov [edi + 260], eax            ; Start block / extent block
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

        ; Write file data to disk. An extent file's data lives in the runs
        ; just allocated, so it is written run by run.
        cmp word [edi + 254], HBFS4_DE_EXTENTS
        jne .hcf_write_inline
        mov esi, [.save_data]
        call hbfs_extent_write
        jc .write_fail
        jmp .hcf_write_done
.hcf_write_inline:
        ; LBA = HBFS_DATA_START + block_number * HBFS_SECTORS_PER_BLK
        mov eax, [edi + 260]    ; Reload first block from dir entry
        mov ecx, [.save_blocks]
        shl eax, 3              ; first_block * 8 sectors
        add eax, HBFS_DATA_START ; + data area start = LBA
        shl ecx, 3              ; blocks * 8 = total sectors
        mov esi, [.save_data]
        call ata_write_sectors
        jc .write_fail
.hcf_write_done:

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

;-----------------------------------------------------------------------
; hbfs_extent_alloc - allocate blocks as one or more runs, for a v4 file
;   In : ECX = data blocks needed
;   Out: EAX = extent block number, CF set if the volume cannot hold it
; Fills hbfs_run_buf/hbfs_run_count with the runs it allocated, and writes
; the extent block (journaled, checksummed). The caller opens the transaction,
; so an abort undoes every allocation made here.
;-----------------------------------------------------------------------
hbfs_extent_alloc:
        pushad
        mov  [.hea_remain], ecx
        mov  dword [hbfs_run_count], 0
        cmp  ecx, HBFS4_EXTENT_MAX
        ja   .hea_fail

.hea_more:
        cmp  dword [.hea_remain], 0
        je   .hea_runs_done
        mov  eax, [.hea_remain]
        mov  [.hea_try], eax
.hea_try_more:
        mov  ecx, [.hea_try]
        call hbfs_alloc_blocks
        jnc  .hea_got
        dec  dword [.hea_try]
        jnz  .hea_try_more
        jmp  .hea_fail                  ; not one free block left
.hea_got:
        mov  ecx, [.hea_try]
        mov  edx, [hbfs_run_count]
        imul edx, HBFS4_EXTENT_RECORD
        add  edx, hbfs_run_buf
        mov  [edx], eax                 ; run start
        mov  [edx + 4], ecx             ; run length
        inc  dword [hbfs_run_count]
        sub  [.hea_remain], ecx
        jmp  .hea_more

.hea_runs_done:
        mov  ecx, 1
        call hbfs_alloc_blocks          ; the extent block itself
        jc   .hea_fail
        mov  [.hea_block], eax

        ; Build the block: magic, count, records, checksum.
        mov  edi, hbfs_extent_buf
        mov  ecx, HBFS_BLOCK_SIZE / 4
        xor  eax, eax
        rep  stosd
        mov  dword [hbfs_extent_buf], HBFS4_EXTENT_MAGIC
        mov  eax, [hbfs_run_count]
        mov  [hbfs_extent_buf + 4], eax
        mov  esi, hbfs_run_buf
        mov  edi, hbfs_extent_buf + HBFS4_EXTENT_HDR
        mov  ecx, eax
        shl  ecx, 1                     ; records are two dwords each
        rep  movsd
        mov  eax, hbfs_extent_buf
        mov  ecx, HBFS_BLOCK_SIZE
        call hbfs_stamp_run

        mov  eax, [.hea_block]
        shl  eax, 3
        add  eax, HBFS_DATA_START
        mov  ecx, HBFS_SECTORS_PER_BLK
        mov  esi, hbfs_extent_buf
        call hbfs_meta_write            ; journaled, so an abort undoes it too
        jc   .hea_fail
        mov  eax, [.hea_block]
        mov  [hbfs_extent_lba], eax
        mov  [esp + 28], eax            ; return value through pushad
        popad
        clc
        ret
.hea_fail:
        popad
        stc
        ret
.hea_remain: dd 0
.hea_try:    dd 0
.hea_block:  dd 0

;-----------------------------------------------------------------------
; hbfs_extent_write - write a file's data across the runs just allocated
;   In : ESI = data source; hbfs_run_buf/hbfs_run_count hold the runs
;   Out: CF set on an I/O error
;-----------------------------------------------------------------------
hbfs_extent_write:
        pushad
        mov  [.hew_src], esi
        mov  dword [.hew_idx], 0
.hew_run:
        mov  eax, [.hew_idx]
        cmp  eax, [hbfs_run_count]
        jae  .hew_ok
        imul eax, HBFS4_EXTENT_RECORD
        add  eax, hbfs_run_buf
        mov  edx, [eax]                 ; run start
        mov  ecx, [eax + 4]             ; run length in blocks
        mov  eax, edx
        shl  eax, 3
        add  eax, HBFS_DATA_START
        shl  ecx, 3                     ; sectors
        mov  esi, [.hew_src]
        call ata_write_sectors
        jc   .hew_fail
        mov  eax, [.hew_idx]
        imul eax, HBFS4_EXTENT_RECORD
        add  eax, hbfs_run_buf
        mov  ecx, [eax + 4]
        shl  ecx, 12                    ; bytes in this run
        add  [.hew_src], ecx
        inc  dword [.hew_idx]
        jmp  .hew_run
.hew_ok:
        popad
        clc
        ret
.hew_fail:
        popad
        stc
        ret
.hew_src: dd 0
.hew_idx: dd 0

;-----------------------------------------------------------------------
; hbfs_truncate_extent - shrink an extent file's run list
;   In : EDI = directory entry, ECX = new size in bytes (<= current size)
;   Out: CF set on a read/verify/write error; the entry is not modified
;
; Runs that are dropped entirely are freed; a run that is kept in part has its
; tail freed. The list is rebuilt in place (the write cursor never overtakes
; the read cursor), restamped and written through the journal.
;-----------------------------------------------------------------------
hbfs_truncate_extent:
        pushad
        mov  [.hte_entry], edi

        mov  eax, ecx
        add  eax, HBFS_BLOCK_SIZE - 1
        shr  eax, 12
        cmp  eax, 0
        jg   .hte_keep_ok
        mov  eax, 1
.hte_keep_ok:
        mov  [.hte_remain], eax         ; blocks still to keep

        mov  eax, [edi + 260]
        mov  [.hte_block], eax
        imul eax, HBFS_SECTORS_PER_BLK
        add  eax, HBFS_DATA_START
        mov  ecx, HBFS_SECTORS_PER_BLK
        mov  edi, hbfs_extent_buf
        call ata_read_sectors
        jc   .hte_fail
        mov  eax, hbfs_extent_buf
        mov  ecx, HBFS_BLOCK_SIZE
        call hbfs_verify_run
        jc   .hte_fail
        mov  eax, [.hte_block]
        mov  [hbfs_extent_lba], eax

        cmp  dword [hbfs_extent_buf], HBFS4_EXTENT_MAGIC
        jne  .hte_fail
        mov  ecx, [hbfs_extent_buf + 4]
        test ecx, ecx
        jz   .hte_fail
        cmp  ecx, HBFS4_EXTENT_MAX
        ja   .hte_fail
        mov  [.hte_count], ecx
        mov  dword [.hte_off], HBFS4_EXTENT_HDR
        mov  dword [.hte_new], 0

.hte_walk:
        cmp  dword [.hte_count], 0
        je   .hte_done
        mov  esi, hbfs_extent_buf
        add  esi, [.hte_off]
        mov  eax, [esi]                 ; run start
        mov  ecx, [esi + 4]             ; run length
        test ecx, ecx
        jz   .hte_fail
        cmp  [.hte_remain], ecx
        jb   .hte_partial
        ; Keep the whole run.
        mov  edx, [.hte_new]
        imul edx, HBFS4_EXTENT_RECORD
        add  edx, hbfs_extent_buf + HBFS4_EXTENT_HDR
        mov  [edx], eax
        mov  [edx + 4], ecx
        inc  dword [.hte_new]
        sub  [.hte_remain], ecx
        add  dword [.hte_off], HBFS4_EXTENT_RECORD
        dec  dword [.hte_count]
        jmp  .hte_walk

.hte_partial:
        mov  ecx, [.hte_remain]
        jecxz .hte_free_rest
        ; Free the tail of this run: start+keep .. start+len-1
        mov  eax, [esi]
        mov  edx, [esi + 4]
        sub  edx, ecx
        add  eax, ecx
        mov  ecx, edx
        call hbfs_free_blocks
        ; Keep the head of this run.
        mov  eax, [esi]
        mov  ecx, [.hte_remain]
        mov  edx, [.hte_new]
        imul edx, HBFS4_EXTENT_RECORD
        add  edx, hbfs_extent_buf + HBFS4_EXTENT_HDR
        mov  [edx], eax
        mov  [edx + 4], ecx
        inc  dword [.hte_new]
        mov  dword [.hte_remain], 0

.hte_free_rest:
        add  dword [.hte_off], HBFS4_EXTENT_RECORD
        dec  dword [.hte_count]
.hte_free_loop:
        cmp  dword [.hte_count], 0
        je   .hte_done
        mov  esi, hbfs_extent_buf
        add  esi, [.hte_off]
        mov  eax, [esi]
        mov  ecx, [esi + 4]
        test ecx, ecx
        jz   .hte_fail
        call hbfs_free_blocks
        add  dword [.hte_off], HBFS4_EXTENT_RECORD
        dec  dword [.hte_count]
        jmp  .hte_free_loop

.hte_done:
        mov  eax, [.hte_new]
        test eax, eax
        jz   .hte_fail
        mov  [hbfs_extent_buf + 4], eax
        mov  eax, hbfs_extent_buf
        mov  ecx, HBFS_BLOCK_SIZE
        call hbfs_stamp_run
        mov  eax, [.hte_block]
        shl  eax, 3
        add  eax, HBFS_DATA_START
        mov  ecx, HBFS_SECTORS_PER_BLK
        mov  esi, hbfs_extent_buf
        call hbfs_meta_write
        jc   .hte_fail
        mov  eax, [.hte_block]
        mov  [hbfs_extent_lba], eax
        popad
        clc
        ret
.hte_fail:
        popad
        stc
        ret
.hte_entry:  dd 0
.hte_block:  dd 0
.hte_count:  dd 0
.hte_off:    dd 0
.hte_new:    dd 0
.hte_remain: dd 0

;-----------------------------------------------------------------------
; hbfs_free_extent - free every run of an extent file, then its extent block
;   In : EDI = directory entry with HBFS4_DE_EXTENTS set
;   Out: CF set if the run list cannot be read or verified (nothing freed)
;
; Freeing `start` as one contiguous range would free the extent block and
; whatever sits after the first run, so the list is walked instead. The
; extent block itself is metadata and is freed after the runs it names.
;-----------------------------------------------------------------------
hbfs_free_extent:
        pushad
        mov  [.hfe_entry], edi

        mov  eax, [edi + 260]
        mov  [.hfe_block], eax
        imul eax, HBFS_SECTORS_PER_BLK
        add  eax, HBFS_DATA_START
        mov  ecx, HBFS_SECTORS_PER_BLK
        mov  edi, hbfs_extent_buf
        call ata_read_sectors
        jc   .hfe_fail
        mov  eax, hbfs_extent_buf
        mov  ecx, HBFS_BLOCK_SIZE
        call hbfs_verify_run
        jc   .hfe_fail
        mov  eax, [.hfe_block]
        mov  [hbfs_extent_lba], eax

        cmp  dword [hbfs_extent_buf], HBFS4_EXTENT_MAGIC
        jne  .hfe_fail
        mov  ecx, [hbfs_extent_buf + 4]
        test ecx, ecx
        jz   .hfe_fail
        cmp  ecx, HBFS4_EXTENT_MAX
        ja   .hfe_fail
        mov  [.hfe_count], ecx
        mov  dword [.hfe_off], HBFS4_EXTENT_HDR

.hfe_run:
        cmp  dword [.hfe_count], 0
        je   .hfe_runs_done
        mov  esi, hbfs_extent_buf
        add  esi, [.hfe_off]
        mov  eax, [esi]                 ; run start block
        mov  ecx, [esi + 4]             ; run length
        test ecx, ecx
        jz   .hfe_fail
        call hbfs_free_blocks
        add  dword [.hfe_off], HBFS4_EXTENT_RECORD
        dec  dword [.hfe_count]
        jmp  .hfe_run

.hfe_runs_done:
        mov  eax, [.hfe_block]
        mov  ecx, 1
        call hbfs_free_blocks
        mov  dword [hbfs_extent_lba], 0
        popad
        clc
        ret
.hfe_fail:
        popad
        stc
        ret
.hfe_entry: dd 0
.hfe_block: dd 0
.hfe_count: dd 0
.hfe_off:   dd 0

; Delete file entry (EDI already points to entry in hbfs_dir_buf)
hbfs_delete_file_entry:
        pushad
        call hbfs_require_writable
        jc .hdfe_refuse
        call hbfs_journal_begin
        ; An extent entry names runs, not one range: walk its list. A v3 (or
        ; one-run) entry frees `start` .. `start+count-1` as before.
        test word [edi + 254], HBFS4_DE_EXTENTS
        jnz .hdfe_extent
        mov eax, [edi + 260]    ; Start block
        mov ecx, [edi + 264]    ; Block count
        cmp ecx, 0
        jle .skip_free
        call hbfs_free_blocks
        jmp .skip_free
.hdfe_extent:
        call hbfs_free_extent
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

.hdfe_refuse:
        popad
        stc
        ret

; Create a subdirectory in the current directory
; ESI = directory name (null-terminated)
; Returns: CF=0 success, CF=1 error
hbfs_mkdir:
        pushad
        call hbfs_require_writable
        jc .hm_denied
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
        ; v4: the subdirectory is one checksummed run. The last block carries
        ; the CRC of the whole (still zero) 64 KB.
        cmp  dword [hbfs_super_buf], HBFS4_MAGIC
        jne  .hm_zero_write
        cmp  ebx, 1
        jne  .hm_zero_write
        mov  dword [hbfs_block_buf + HBFS_BLOCK_SIZE - HBFS4_RUN_CHECKSUM_BYTES], HBFS4_EMPTY_DIR_CRC
.hm_zero_write:
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
        call hbfs_require_writable
        jc .srd_fail
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
        call hbfs_require_writable
        jc .st_fail
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
        ; Calculate new block count needed
        mov ecx, eax
        add ecx, HBFS_BLOCK_SIZE - 1
        shr ecx, 12             ; ceil(new_size / 4096)
        cmp ecx, 0
        jg .st_has_blocks
        mov ecx, 1
.st_has_blocks:
        test word [edi + 254], HBFS4_DE_EXTENTS
        jnz .st_extent
        ; Update size in directory entry
        mov eax, [.st_new_size]
        mov [edi + DIRENT_SIZE], eax
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
.st_extent:
        ; Shrink the run list; the helper restamps and writes the extent block.
        mov  ecx, [.st_new_size]
        call hbfs_truncate_extent
        jc   .st_fail
        mov  eax, [.st_new_size]
        mov  [edi + DIRENT_SIZE], eax
        mov  ecx, eax
        add  ecx, HBFS_BLOCK_SIZE - 1
        shr  ecx, 12
        cmp  ecx, 0
        jg   .st_ext_blocks
        mov  ecx, 1
.st_ext_blocks:
        mov  [edi + DIRENT_BLOCK_COUNT], ecx
        jmp  .st_no_free

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
        call hbfs_require_writable
        jc .sdf_fail

        call hbfs_load_bitmap
        jc   .sdf_fail

        call hbfs_load_root_dir

        ; ---- Phase 1: scan bitmap for free-block / fragmentation stats ----
        xor  ecx, ecx           ; free block count
        xor  edx, edx           ; fragmented run count
        xor  ebx, ebx           ; block index
        xor  esi, esi           ; prev-was-free flag
.sdf_scan:
        cmp  ebx, [hbfs_total_blocks]
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
        call hbfs_require_writable
        jc .hxs_fail
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
; hbfs_sync — put everything that is only in memory onto the disk
;   Out: EAX = 0 when the volume is consistent on disk, -1 otherwise
;
; Called before anything that stops the machine: a power button, a shutdown, a
; reset. The order matters - the bitmap and the superblock describe allocations
; the directory already references, so the directory goes first, and the
; journal commits last so a machine that dies mid-sync still replays to a
; consistent volume.
;
; A drive with a write cache can still be holding sectors that were accepted,
; so the cache is flushed whether or not anything needed writing.
;-----------------------------------------------------------------------
hbfs_sync:
        pushad
        mov dword [.hs_status], 0
        cmp dword [hbfs_mounted], 0
        je .hs_flush                    ; nothing mounted: just the drive cache
        cmp dword [hbfs_read_only], 0
        jne .hs_flush

        call hbfs_save_root_dir
        jc .hs_failed

        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov esi, hbfs_bitmap_buf
        call hbfs_meta_write
        jc .hs_failed

        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call hbfs_meta_write
        jc .hs_failed

        call hbfs_journal_commit        ; its own failures surface on the next mount

.hs_flush:
        mov dx, ATA_CMD
        mov al, 0xE7                    ; FLUSH CACHE
        out dx, al
        mov ecx, 0xFFFFF
.hs_wait:
        in al, dx
        test al, 0x80                   ; BSY?
        jz .hs_done
        dec ecx
        jnz .hs_wait
        jmp .hs_failed
.hs_done:
        jmp .hs_out
.hs_failed:
        mov dword [.hs_status], -1
.hs_out:
        mov eax, [.hs_status]
        mov [esp + 28], eax
        popad
        ret
.hs_status: dd 0

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
