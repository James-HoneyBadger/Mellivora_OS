;=======================================================================
; comp.inc - the chunk container for compressed files
;
; A compressed file's data area is two regions, back to back:
;
;   [ chunk table ][ chunk payloads ]
;
; The table holds one 16-byte entry per 16 KiB chunk of plaintext:
;
;   +0  u32  the absolute data block the payload starts at
;   +4  u32  payload bytes stored there
;   +8  u32  CRC-32C of the chunk's *plaintext*
;   +12 u32  flags: bit 0 set means the payload is LZ-encoded, clear means the
;            chunk is stored as itself
;
; Nothing else is needed to find chunk N: its entry is at byte N*16 of the
; table, so the table block is N/256 and the offset inside it is derivable from
; the chunk number alone. That is what makes a read of one section touch one
; table block and one payload instead of the whole file.
;
; Why the payload is contiguous and the table is not an extent list: delete,
; quota accounting and repair all work from the entry's `start` and `blocks`,
; and a compressed file keeps those meaning "one run of this many blocks". The
; container is inside that run, so nothing outside this file has to know.
;
; A chunk that does not compress is stored raw (`flags` bit 0 clear) with its
; CRC still recorded. The file therefore cannot grow: the encoder only accepts
; an encoding smaller than its input, and the table costs 16 bytes per 16 KiB.
;
; The CRC is on the plaintext, not the payload, because the failure that
; matters is a read that returns wrong bytes - a payload bit flip, a wrong
; chunk, a stale block - and all three show up there.
;=======================================================================

HBFS4_DE_COMPRESSED  equ (1 << 1)     ; directory entry flags: data is chunks
COMP_CHUNK           equ 16384        ; plaintext bytes per chunk
COMP_ENTRY_SIZE      equ 16
COMP_ENTRY_PER_BLK   equ (HBFS_BLOCK_SIZE / COMP_ENTRY_SIZE)
COMP_E_BLOCK         equ 0
COMP_E_LEN           equ 4
COMP_E_CRC           equ 8
COMP_E_FLAGS         equ 12
COMP_F_CODEC         equ 1
COMP_MAX_FILE        equ PROGRAM_MAX_SIZE   ; the reader's own window limit
COMP_NONE            equ 0xFFFFFFFF

;-----------------------------------------------------------------------
; comp_geometry - how a plaintext size maps onto the table
;   In : EAX = plaintext size
;   Out: EAX = chunks, EDX = table blocks
;-----------------------------------------------------------------------
comp_geometry:
        push ebx
        mov  ebx, eax
        add  eax, COMP_CHUNK - 1
        shr  eax, 14                     ; chunks
        mov  edx, eax
        shl  edx, 4                      ; table bytes
        add  edx, HBFS_BLOCK_SIZE - 1
        shr  edx, 12                     ; table blocks
        pop  ebx
        ret

;-----------------------------------------------------------------------
; comp_read_chunk - decode one chunk of a compressed file
;   In : EAX = start block, ECX = file size, EDX = chunk index
;   Out: CF = 0, comp_chunk_buf holds min(COMP_CHUNK, size - chunk*COMP_CHUNK)
;        bytes of plaintext; CF = 1 when the table, the payload or the
;        checksum does not hold up
;   One chunk is cached, because every caller reads sequentially and a 16 KiB
;   decode per chunk would otherwise be paid once per 4 KiB block.
;-----------------------------------------------------------------------
comp_read_chunk:
        pushad
        cmp  edx, [comp_cache_chunk]
        jne  .crc_miss
        cmp  eax, [comp_cache_start]
        jne  .crc_miss
        popad
        clc
        ret
.crc_miss:
        mov  [comp_start], eax
        mov  [comp_size], ecx
        mov  [comp_want], edx

        ; Plaintext length of this chunk: every chunk but the last is full.
        mov  ecx, edx
        shl  ecx, 14
        mov  eax, [comp_size]
        sub  eax, ecx
        jbe  .crc_fail                   ; chunk starts past the end
        cmp  eax, COMP_CHUNK
        jbe  .crc_ptlen
        mov  eax, COMP_CHUNK
.crc_ptlen:
        mov  [comp_ptlen], eax

        ; Table entry: byte offset chunk*16, so block chunk/256, offset
        ; (chunk*16) % 4096. The table starts at the entry's start block.
        mov  eax, [comp_want]
        shl  eax, 4
        mov  ebx, eax
        shr  eax, 12
        add  eax, [comp_start]           ; table block number
        shl  eax, 3                      ; block -> sector
        add  eax, HBFS_DATA_START
        mov  ecx, HBFS_SECTORS_PER_BLK
        mov  edi, comp_tbl_buf
        call blk_cache_read
        jc   .crc_fail
        and  ebx, 0xFFF
        mov  esi, comp_tbl_buf
        add  esi, ebx
        mov  eax, [esi + COMP_E_BLOCK]
        mov  [comp_eblock], eax
        mov  eax, [esi + COMP_E_LEN]
        mov  [comp_elen], eax
        mov  eax, [esi + COMP_E_CRC]
        mov  [comp_ecrc], eax
        mov  eax, [esi + COMP_E_FLAGS]
        mov  [comp_eflags], eax

        test dword [comp_eflags], COMP_F_CODEC
        jz   .crc_raw

        ; Encoded: the payload is `elen` bytes in whole blocks, and decoding it
        ; must produce exactly the chunk's plaintext length.
        mov  ecx, [comp_elen]
        test ecx, ecx
        jz   .crc_fail
        add  ecx, HBFS_BLOCK_SIZE - 1
        shr  ecx, 12
        jz   .crc_fail
        shl  ecx, 3
        mov  eax, [comp_eblock]
        shl  eax, 3
        add  eax, HBFS_DATA_START
        mov  edi, comp_payload_buf
        call blk_cache_read
        jc   .crc_fail
        mov  esi, comp_payload_buf
        mov  ecx, [comp_elen]
        mov  edi, comp_chunk_buf
        mov  edx, [comp_ptlen]
        call lz_decompress
        jc   .crc_fail
        jmp  .crc_verify

.crc_raw:
        mov  ecx, [comp_ptlen]
        add  ecx, HBFS_BLOCK_SIZE - 1
        shr  ecx, 12
        jz   .crc_fail
        shl  ecx, 3
        mov  eax, [comp_eblock]
        shl  eax, 3
        add  eax, HBFS_DATA_START
        mov  edi, comp_chunk_buf
        call blk_cache_read
        jc   .crc_fail

.crc_verify:
        mov  eax, comp_chunk_buf
        mov  ecx, [comp_ptlen]
        call crc32c
        cmp  eax, [comp_ecrc]
        jne  .crc_fail
        mov  eax, [comp_start]
        mov  [comp_cache_start], eax
        mov  eax, [comp_want]
        mov  [comp_cache_chunk], eax
        popad
        clc
        ret
.crc_fail:
        mov  dword [comp_cache_chunk], COMP_NONE
        popad
        stc
        ret

;-----------------------------------------------------------------------
; comp_read_all - decode a whole compressed file
;   In : EAX = start block, ECX = file size, EDI = destination,
;        EDX = destination capacity in bytes
;   Out: CF = 1 when the file is larger than the buffer or a chunk is bad
;-----------------------------------------------------------------------
comp_read_all:
        pushad
        cmp  ecx, edx
        ja   .cra_fail
        mov  [comp_ro_start], eax
        mov  [comp_ro_size], ecx
        mov  [comp_dst], edi
        mov  [comp_rem], ecx
        mov  dword [comp_cache_chunk], COMP_NONE
        xor  ebx, ebx
.cra_loop:
        cmp  dword [comp_rem], 0
        je   .cra_ok
        mov  eax, [comp_ro_start]
        mov  ecx, [comp_ro_size]
        mov  edx, ebx
        call comp_read_chunk
        jc   .cra_fail
        mov  ecx, COMP_CHUNK
        cmp  ecx, [comp_rem]
        jbe  .cra_len
        mov  ecx, [comp_rem]
.cra_len:
        sub  [comp_rem], ecx
        mov  esi, comp_chunk_buf
        mov  edi, [comp_dst]
        rep  movsb
        mov  [comp_dst], edi
        inc  ebx
        jmp  .cra_loop
.cra_ok:
        popad
        clc
        ret
.cra_fail:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; comp_read_block - one 4 KiB logical block of a compressed file
;   In : EAX = start block, ECX = file size, EBX = logical block,
;        EDI = destination (4 KiB)
;   Out: CF = 1 when the chunk does not hold up
;   The tail of the last block past the file's size keeps whatever the chunk
;   held there; every caller clamps its copy by the remaining size.
;-----------------------------------------------------------------------
comp_read_block:
        pushad
        mov  [comp_ro_start], eax
        mov  [comp_size], ecx
        mov  eax, ebx
        shr  eax, 2                      ; chunk index
        mov  edx, eax
        mov  eax, ebx
        and  eax, 3
        shl  eax, 12                     ; offset inside the chunk
        mov  [comp_rb_off], eax
        mov  [comp_dst], edi
        mov  eax, [comp_ro_start]
        mov  ecx, [comp_size]
        call comp_read_chunk
        jc   .crb_fail
        mov  esi, comp_chunk_buf
        add  esi, [comp_rb_off]
        mov  edi, [comp_dst]
        mov  ecx, HBFS_BLOCK_SIZE
        rep  movsb
        popad
        clc
        ret
.crb_fail:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; comp_pack - store a file's plaintext as chunks, in a run of its own
;   In : ESI = plaintext, ECX = size, EDI = directory entry in hbfs_dir_buf
;   Out: CF = 1 refused or failed (the entry is unchanged); CF = 0 with the
;        entry naming the new run, the old run freed and the quota settled
;
; The new run is allocated before anything is written and the old one is freed
; only after the entry that named it has been committed. A crash in between
; leaves blocks the tree does not reference, which `repair` reports as a leak -
; the file itself is never a mixture of the two layouts, which is the failure
; that would lose its data.
;-----------------------------------------------------------------------
comp_pack:
        pushad
        mov  [comp_src], esi
        mov  [comp_size], ecx
        mov  [comp_entry], edi
        test ecx, ecx
        jz   .cp_refuse
        cmp  ecx, COMP_MAX_FILE
        ja   .cp_refuse
        test word [edi + 254], HBFS4_DE_COMPRESSED | HBFS4_DE_EXTENTS
        jnz  .cp_refuse
        ; Data and executables: the loader decodes through the same seam a
        ; read does, so a compressed program runs. Directories, links, batch
        ; scripts and streams are not chunked, which keeps one command from
        ; having to explain five layouts.
        movzx eax, byte [edi + HBFS_DE_TYPE]
        cmp  eax, FTYPE_FILE
        je   .cp_type_ok
        cmp  eax, FTYPE_EXEC
        jne  .cp_refuse
.cp_type_ok:
        mov  eax, [edi + 260]
        mov  [comp_old_start], eax
        mov  eax, [edi + 264]
        mov  [comp_old_blocks], eax

        mov  eax, [comp_size]
        call comp_geometry
        mov  [comp_chunks], eax
        mov  [comp_tblblocks], edx
        ; Worst case payload: every chunk stored raw, plus one block per chunk
        ; for the last block of each. Compression only ever uses less.
        mov  ecx, [comp_size]
        add  ecx, HBFS_BLOCK_SIZE - 1
        shr  ecx, 12
        add  ecx, [comp_chunks]
        add  ecx, [comp_tblblocks]
        mov  [comp_alloc_blocks], ecx
        call hbfs_alloc_blocks
        jc   .cp_refuse
        mov  [comp_base], eax

        mov  edi, comp_tbl_buf
        mov  ecx, HBFS_BLOCK_SIZE / 4
        xor  eax, eax
        rep  stosd
        mov  dword [comp_payload_blk], 0
        mov  dword [comp_chunkidx], 0

.cp_chunk:
        mov  ebx, [comp_chunkidx]
        cmp  ebx, [comp_chunks]
        jae  .cp_flush_last

        mov  eax, ebx
        shl  eax, 14
        mov  ecx, [comp_size]
        sub  ecx, eax
        cmp  ecx, COMP_CHUNK
        jbe  .cp_ptlen
        mov  ecx, COMP_CHUNK
.cp_ptlen:
        mov  [comp_ptlen], ecx

        mov  esi, [comp_src]
        mov  eax, ebx
        shl  eax, 14
        add  esi, eax
        mov  edi, comp_payload_buf
        mov  edx, COMP_CHUNK
        call lz_compress
        jc   .cp_raw
        test eax, eax
        jz   .cp_raw                     ; did not shrink: store it as itself
        mov  [comp_clen], eax
        mov  dword [comp_cflags], COMP_F_CODEC
        jmp  .cp_have_payload

.cp_raw:
        mov  ecx, [comp_ptlen]
        mov  [comp_clen], ecx
        mov  dword [comp_cflags], 0
        mov  esi, [comp_src]
        mov  eax, [comp_chunkidx]
        shl  eax, 14
        add  esi, eax
        mov  edi, comp_payload_buf
        rep  movsb

.cp_have_payload:
        mov  eax, [comp_src]
        mov  ecx, [comp_chunkidx]
        shl  ecx, 14
        add  eax, ecx
        mov  ecx, [comp_ptlen]
        call crc32c
        mov  [comp_crc], eax

        mov  eax, [comp_clen]
        add  eax, HBFS_BLOCK_SIZE - 1
        shr  eax, 12
        mov  [comp_pblocks], eax
        mov  ecx, [comp_payload_blk]
        add  ecx, [comp_tblblocks]
        add  ecx, [comp_base]
        mov  [comp_eblock], ecx
        mov  eax, ecx
        shl  eax, 3
        add  eax, HBFS_DATA_START
        mov  ecx, [comp_pblocks]
        shl  ecx, 3
        mov  esi, comp_payload_buf
        call blk_cache_write
        jc   .cp_fail

        ; One table entry: block, payload length, plaintext checksum, flags.
        mov  eax, [comp_chunkidx]
        shl  eax, 4
        and  eax, 0xFFF
        mov  edi, comp_tbl_buf
        add  edi, eax
        mov  eax, [comp_eblock]
        mov  [edi + COMP_E_BLOCK], eax
        mov  eax, [comp_clen]
        mov  [edi + COMP_E_LEN], eax
        mov  eax, [comp_crc]
        mov  [edi + COMP_E_CRC], eax
        mov  eax, [comp_cflags]
        mov  [edi + COMP_E_FLAGS], eax

        mov  eax, [comp_pblocks]
        add  [comp_payload_blk], eax
        inc  dword [comp_chunkidx]
        mov  eax, [comp_chunkidx]
        test eax, COMP_ENTRY_PER_BLK - 1
        jnz  .cp_chunk
        call comp_flush_table
        jc   .cp_fail
        jmp  .cp_chunk

.cp_flush_last:
        mov  eax, [comp_chunkidx]
        test eax, COMP_ENTRY_PER_BLK - 1
        jz   .cp_finish
        call comp_flush_table
        jc   .cp_fail
.cp_finish:
        mov  eax, [comp_payload_blk]
        add  eax, [comp_tblblocks]
        mov  [comp_used], eax
        ; Give back whatever the worst case reserved and the data did not need.
        mov  ecx, [comp_alloc_blocks]
        sub  ecx, eax
        jz   .cp_no_tail
        mov  eax, [comp_base]
        add  eax, [comp_used]
        call hbfs_free_blocks
.cp_no_tail:
        mov  edi, [comp_entry]
        or   word [edi + 254], HBFS4_DE_COMPRESSED
        mov  eax, [comp_base]
        mov  [edi + 260], eax
        mov  eax, [comp_used]
        mov  [edi + 264], eax
        call hbfs_save_root_dir
        jc   .cp_fail_keep
        ; The entry no longer names the old blocks, so they can go.
        mov  ecx, [comp_old_blocks]
        test ecx, ecx
        jz   .cp_quota
        mov  eax, [comp_old_start]
        call hbfs_free_blocks
.cp_quota:
        call comp_settle_quota
        popad
        clc
        ret
.cp_fail_keep:
        ; The new run was written but the entry never named it: free it, so the
        ; failure leaves the file exactly as it was.
        mov  ecx, [comp_used]
        mov  eax, [comp_base]
        call hbfs_free_blocks
        popad
        stc
        ret
.cp_fail:
        mov  ecx, [comp_alloc_blocks]
        mov  eax, [comp_base]
        call hbfs_free_blocks
        popad
        stc
        ret
.cp_refuse:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; comp_flush_table - write the table block the current chunk index just ended
;   Out: CF = 1 on a write error; the buffer is cleared for the next block
;-----------------------------------------------------------------------
comp_flush_table:
        pushad
        mov  eax, [comp_chunkidx]
        dec  eax
        shr  eax, 8                      ; COMP_ENTRY_PER_BLK = 256
        add  eax, [comp_base]
        shl  eax, 3
        add  eax, HBFS_DATA_START
        mov  ecx, HBFS_SECTORS_PER_BLK
        mov  esi, comp_tbl_buf
        call blk_cache_write
        pushfd
        mov  edi, comp_tbl_buf
        mov  ecx, HBFS_BLOCK_SIZE / 4
        xor  eax, eax
        rep  stosd
        popfd
        popad
        ret

;-----------------------------------------------------------------------
; comp_settle_quota - charge or release the difference the new run made
;   In : comp_entry, comp_used, comp_old_blocks
;   The file's blocks are its owner's, so the difference goes to the owner and
;   not to whoever ran the command.
;-----------------------------------------------------------------------
comp_settle_quota:
        pushad
        mov  edi, [comp_entry]
        movzx eax, word [edi + HBFS_DE_UID]
        mov  ecx, [comp_used]
        cmp  ecx, [comp_old_blocks]
        jbe  .csq_release
        sub  ecx, [comp_old_blocks]
        call quota_charge
        popad
        ret
.csq_release:
        mov  ecx, [comp_old_blocks]
        sub  ecx, [comp_used]
        jz   .csq_done
        call quota_release
.csq_done:
        popad
        ret

;-----------------------------------------------------------------------
; comp_expand - rewrite a compressed file as one plain run
;   In : ESI = plaintext (already decoded), ECX = size,
;        EDI = directory entry in hbfs_dir_buf
;   Out: CF = 1 on failure, leaving the entry as it was
;-----------------------------------------------------------------------
comp_expand:
        pushad
        mov  [comp_src], esi
        mov  [comp_size], ecx
        mov  [comp_entry], edi
        test word [edi + 254], HBFS4_DE_COMPRESSED
        jz   .ce_refuse                  ; nothing to expand
        mov  eax, [edi + 260]
        mov  [comp_old_start], eax
        mov  eax, [edi + 264]
        mov  [comp_old_blocks], eax
        cmp  dword [comp_size], 0
        je   .ce_empty
        mov  ecx, [comp_size]
        add  ecx, HBFS_BLOCK_SIZE - 1
        shr  ecx, 12
        mov  [comp_used], ecx
        call hbfs_alloc_blocks
        jc   .ce_refuse
        mov  [comp_base], eax
        shl  eax, 3
        add  eax, HBFS_DATA_START
        mov  ecx, [comp_used]
        shl  ecx, 3
        mov  esi, [comp_src]
        call blk_cache_write
        jc   .ce_fail
        jmp  .ce_commit
.ce_empty:
        mov  dword [comp_base], 0
        mov  dword [comp_used], 0
.ce_commit:
        mov  edi, [comp_entry]
        and  word [edi + 254], ~HBFS4_DE_COMPRESSED
        mov  eax, [comp_base]
        mov  [edi + 260], eax
        mov  eax, [comp_used]
        mov  [edi + 264], eax
        call hbfs_save_root_dir
        jc   .ce_fail_keep
        mov  ecx, [comp_old_blocks]
        test ecx, ecx
        jz   .ce_quota
        mov  eax, [comp_old_start]
        call hbfs_free_blocks
.ce_quota:
        call comp_settle_quota
        popad
        clc
        ret
.ce_fail_keep:
        mov  ecx, [comp_used]
        test ecx, ecx
        jz   .ce_refuse
        mov  eax, [comp_base]
        call hbfs_free_blocks
        popad
        stc
        ret
.ce_fail:
        mov  ecx, [comp_used]
        mov  eax, [comp_base]
        call hbfs_free_blocks
        popad
        stc
        ret
.ce_refuse:
        popad
        stc
        ret


;-----------------------------------------------------------------------
; cmd_comp_handle - the shell's compact/expand command
;   In : ESI = the argument string, comp_mode = 0 to compress, 1 to expand
;   Out: none; prints one line
;
; The file's plaintext is read first, into a page-allocator buffer, before
; anything on disk is touched: a compact that cannot read the file must not
; have already allocated a run for it. The entry is then looked up a second
; time, because reading a file moves the current directory and refills the
; directory buffer the first pointer came from.
;-----------------------------------------------------------------------
cmd_comp_handle:
        pushad
        mov  dword [comp_buf], 0
        cmp  byte [esi], 0
        je   .usage
        mov  edi, comp_arg_buf
        mov  ecx, 250
        call copy_word_n
        cmp  byte [comp_arg_buf], 0
        je   .usage

        mov  ebx, comp_arg_buf
        call hbfs_enter_path
        test eax, eax
        jnz  .no_such
        call hbfs_load_root_dir
        mov  esi, path_base_buf
        call hbfs_find_file
        jc   .fail_leave
        movzx eax, byte [edi + HBFS_DE_TYPE]
        cmp  eax, FTYPE_FILE
        je   .type_ok
        cmp  eax, FTYPE_EXEC
        jne  .not_a_file
.type_ok:
        mov  eax, [edi + 256]
        mov  [comp_fsize], eax
        movzx edx, word [edi + 254]
        and  edx, HBFS4_DE_COMPRESSED
        cmp  dword [comp_mode], 0
        jne  .want_expand
        test edx, edx
        jnz  .already
        jmp  .shape_ok
.want_expand:
        test edx, edx
        jz   .not_packed
.shape_ok:
        mov  ebx, HBFS_ACC_WRITE
        call hbfs_access_current
        test eax, eax
        jnz  .denied
        mov  eax, [comp_fsize]
        test eax, eax
        jz   .empty
        cmp  eax, COMP_MAX_FILE
        ja   .too_big
        add  eax, HBFS_BLOCK_SIZE - 1
        shr  eax, 12
        mov  [comp_pages], eax
        mov  ecx, eax
        call pmm_alloc_pages
        test eax, eax
        jz   .no_mem
        mov  [comp_buf], eax

        mov  esi, path_base_buf
        mov  edi, eax
        call hbfs_read_file
        jc   .read_failed
        cmp  ecx, [comp_fsize]
        jne  .read_failed

        ; The read refilled the directory buffer, so find the entry again.
        call hbfs_load_root_dir
        mov  esi, path_base_buf
        call hbfs_find_file
        jc   .read_failed
        mov  esi, [comp_buf]
        mov  ecx, [comp_fsize]
        cmp  dword [comp_mode], 0
        jne  .do_expand
        call comp_pack
        jc   .write_failed
        jmp  .saved
.do_expand:
        call comp_expand
        jc   .write_failed
.saved:
        call path_leave
        mov  eax, [comp_buf]
        mov  ecx, [comp_pages]
        call pmm_free_pages
        mov  esi, msg_saved
        call vga_print
        popad
        ret

.read_failed:
        mov  esi, msg_comp_read
        jmp  .tidy_leave
.write_failed:
        mov  esi, msg_comp_write
        jmp  .tidy_leave
.not_a_file:
        mov  esi, msg_comp_type
        jmp  .tidy_leave
.already:
        mov  esi, msg_comp_already
        jmp  .tidy_leave
.not_packed:
        mov  esi, msg_comp_plain
        jmp  .tidy_leave
.denied:
        mov  esi, msg_comp_denied
        jmp  .tidy_leave
.empty:
        mov  esi, msg_comp_empty
        jmp  .tidy_leave
.too_big:
        mov  esi, msg_comp_big
        jmp  .tidy_leave
.no_mem:
        mov  esi, msg_comp_mem
        jmp  .tidy_leave
.tidy_leave:
        mov  [comp_msg], esi
        mov  eax, [comp_buf]
        test eax, eax
        jz   .tidy_emit
        mov  ecx, [comp_pages]
        call pmm_free_pages
        mov  dword [comp_buf], 0
.tidy_emit:
        mov  esi, [comp_msg]
        call vga_print
.fail_leave:
        call path_leave
        popad
        ret
.no_such:
        mov  esi, msg_comp_nofile
        call vga_print
        popad
        ret
.usage:
        mov  esi, msg_comp_usage
        call vga_print
        popad
        ret

msg_comp_usage:   db "usage: compact FILE | expand FILE", 0x0A, 0
msg_comp_nofile:  db "No such file", 0x0A, 0
msg_comp_read:    db "compact: could not read the file whole", 0x0A, 0
msg_comp_write:   db "compact: the volume refused the write", 0x0A, 0
msg_comp_type:    db "compact: only files and executables are stored as chunks", 0x0A, 0
msg_comp_already: db "compact: that file is already stored compressed", 0x0A, 0
msg_comp_plain:   db "expand: that file is not stored compressed", 0x0A, 0
msg_comp_denied:  db "Permission denied", 0x0A, 0
msg_comp_empty:   db "compact: an empty file has nothing to store", 0x0A, 0
msg_comp_big:     db "compact: the file is larger than the reader's window", 0x0A, 0
msg_comp_mem:     db "compact: out of memory for the file", 0x0A, 0

comp_mode:      dd 0
comp_buf:       dd 0
comp_pages:     dd 0
comp_fsize:     dd 0
comp_msg:       dd 0
comp_arg_buf:   resb 256

;-----------------------------------------------------------------------
; State. One file's read state is cached, which is safe because every caller
; on the storage path holds the volume lock; the writer's state is only used
; by one command at a time.
;-----------------------------------------------------------------------
comp_start:         dd 0
comp_size:          dd 0
comp_want:          dd 0
comp_ptlen:         dd 0
comp_eblock:        dd 0
comp_elen:          dd 0
comp_ecrc:          dd 0
comp_eflags:        dd 0
comp_cache_start:   dd 0
comp_cache_chunk:   dd COMP_NONE
comp_ro_start:      dd 0
comp_ro_size:       dd 0
comp_rb_off:        dd 0
comp_dst:           dd 0
comp_rem:           dd 0
comp_src:           dd 0
comp_entry:         dd 0
comp_old_start:     dd 0
comp_old_blocks:    dd 0
comp_base:          dd 0
comp_used:          dd 0
comp_alloc_blocks:  dd 0
comp_chunks:        dd 0
comp_tblblocks:     dd 0
comp_payload_blk:   dd 0
comp_chunkidx:      dd 0
comp_clen:          dd 0
comp_cflags:        dd 0
comp_crc:           dd 0
comp_pblocks:       dd 0

comp_chunk_buf:     resb COMP_CHUNK
comp_payload_buf:   resb COMP_CHUNK
comp_tbl_buf:       resb HBFS_BLOCK_SIZE
