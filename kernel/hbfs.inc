;=======================================================================
; HBFS - HONEY BADGER FILE SYSTEM
;
; Superblock (LBA HBFS_SUPERBLOCK_LBA, 1 sector):
;   [0-3]   Magic: 'HBFS'
;   [4-7]   Version: 1
;   [8-11]  Total blocks
;   [12-15] Free blocks
;   [16-19] Root directory block
;   [20-23] Bitmap start block
;   [24-27] Data start block
;   [28-31] Block size (4096)
;   [32]    Journal dirty flag (0=clean, 1=dirty)
;   [33-35] Reserved
;   [36-63] Reserved for journal
;
; Directory Entry (288 bytes):
;   [0-252]   Filename (null-terminated, 252 chars max)
;   [253]     Type (0=free, 1=file, 2=dir)
;   [254-255] Flags
;   [256-259] File size in bytes
;   [260-263] Start block (in data area)
;   [264-267] Block count
;   [268-271] Created timestamp (packed RTC)
;   [272-275] Modified timestamp (packed RTC)
;   [276-287] Reserved
;
; Block allocation bitmap:
;   128 sectors (65536 bytes) = 524288 blocks max
;   At 4KB/block = 2 GB addressable
;
;=======================================================================

hbfs_init:
        PUSHALL

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

        ; Check journal dirty flag and replay if needed
        call hbfs_journal_check

        ; Load root directory
        call hbfs_load_root_dir

        POPALL
        ret

.format_new:
        mov esi, msg_hbfs_format
        call vga_print
        call hbfs_format
        POPALL
        ret

.no_disk:
        mov esi, msg_hbfs_nodisk
        call vga_print
        POPALL
        ret

; Format: create fresh filesystem
hbfs_format:
        PUSHALL

        ; Build superblock in buffer
        mov edi, hbfs_super_buf
        mov ecx, 512 / 4
        xor eax, eax
        rep stosd               ; Zero buffer

        mov edi, hbfs_super_buf
        mov dword [edi + 0], HBFS_MAGIC
        mov dword [edi + 4], 1                  ; Version
        mov dword [edi + 8], HBFS_TOTAL_BLOCKS  ; Total blocks
        mov dword [edi + 12], HBFS_TOTAL_BLOCKS ; Free blocks
        mov dword [edi + 16], HBFS_ROOT_DIR_START; Root dir LBA
        mov dword [edi + 20], HBFS_BITMAP_START  ; Bitmap start LBA
        mov dword [edi + 24], HBFS_DATA_START    ; Data start LBA
        mov dword [edi + 28], HBFS_BLOCK_SIZE

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

        POPALL
        ret

; Load current directory into memory (with cache)
; Returns: CF set on read error
hbfs_load_root_dir:
        PUSHALL
        ; Check if the directory buffer already has this directory cached
        mov eax, [current_dir_lba]
        cmp eax, [dir_cache_lba]
        jne .hlrd_miss
        mov eax, [current_dir_sects]
        cmp eax, [dir_cache_sects]
        jne .hlrd_miss
        ; Cache hit — buffer is already valid
        POPALL
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
        POPALL
        clc
        ret
.hlrd_err:
        mov esi, msg_hbfs_dir_read_err
        call vga_print
        POPALL
        stc
        ret

; Load bitmap from disk into hbfs_bitmap_buf
; Returns: CF set on read error
hbfs_load_bitmap:
        PUSHALL
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors
        jc .hlb_err
        POPALL
        clc
        ret
.hlb_err:
        mov esi, msg_hbfs_bitmap_err
        call vga_print
        POPALL
        stc
        ret

; Save current directory to disk
; Returns: CF set on write error
hbfs_save_root_dir:
        PUSHALL
        mov eax, [current_dir_lba]
        mov ecx, [current_dir_sects]
        mov esi, hbfs_dir_buf
        call ata_write_sectors
        jc .hsrd_err
        POPALL
        clc
        ret
.hsrd_err:
        mov esi, msg_hbfs_dir_write_err
        call vga_print
        POPALL
        stc
        ret

; Get max directory entries for current directory
; Returns: EAX = number of entries that fit in current directory
hbfs_get_max_entries:
        mov eax, [current_dir_sects]
        shl eax, 9              ; sectors * 512 = bytes
        xor edx, edx
        push rcx
        mov ecx, HBFS_DIR_ENTRY_SIZE
        div ecx                 ; bytes / 288 = max entries
        pop rcx
        ret

; Find file in current directory
; ESI = filename to find
; Returns: EDI = pointer to dir entry (in hbfs_dir_buf), CF set if not found
hbfs_find_file:
        PUSHALL
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax

.search:
        cmp byte [edi + 253], FTYPE_FREE
        je .next

        ; Compare filename
        push rsi
        push rdi
        push rcx
.cmp_loop:
        mov al, [esi]
        mov ah, [edi]
        cmp al, ah
        jne .no_match
        test al, al
        jz .match
        inc esi
        inc edi
        jmp .cmp_loop

.no_match:
        pop rcx
        pop rdi
        pop rsi
.next:
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .search

        POPALL
        stc
        ret

.match:
        pop rcx
        pop rdi                 ; EDI = dir entry start
        pop rsi
        mov [rsp + FRAME_RDI], rdi ; Update RDI in PUSHALL frame
        POPALL
        clc
        ret

; Allocate N contiguous data blocks
; ECX = number of blocks needed
; Returns: EAX = first block number (relative to data start), CF set if full
hbfs_alloc_blocks:
        PUSHALL
        mov [.blocks_needed], ecx

        ; Read bitmap
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors

        ; Scan for N contiguous free bits
        xor edx, edx           ; Current block number
        mov ecx, HBFS_TOTAL_BLOCKS ; Total blocks in bitmap

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
        push rdx                ; save start candidate
        mov esi, 1              ; count of free found so far
.check_run:
        cmp esi, [.blocks_needed]
        jge .found_run
        push rdx
        add edx, esi
        cmp edx, ecx
        pop rdx
        jge .run_fail           ; past end of bitmap

        ; Check block (start + esi)
        mov eax, edx
        add eax, esi
        push rax
        shr eax, 3
        mov ebx, edx
        add ebx, esi
        and ebx, 7
        bt dword [hbfs_bitmap_buf + eax], ebx
        pop rax
        jc .run_fail
        inc esi
        jmp .check_run

.run_fail:
        pop rdx                 ; restore start candidate
        inc edx
        jmp .scan_start

.found_run:
        pop rdx                 ; EDX = first free block

        ; Mark all N blocks as used in bitmap
        mov esi, 0
.mark_loop:
        cmp esi, [.blocks_needed]
        jge .mark_done
        mov eax, edx
        add eax, esi
        push rax
        shr eax, 3
        mov ebx, edx
        add ebx, esi
        and ebx, 7
        bts dword [hbfs_bitmap_buf + eax], ebx
        pop rax
        inc esi
        jmp .mark_loop
.mark_done:

        ; Begin journal transaction (marks superblock dirty)
        push rdx
        call hbfs_journal_begin
        pop rdx

        ; Write bitmap back
        push rdx
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov esi, hbfs_bitmap_buf
        call ata_write_sectors
        pop rdx

        ; Update superblock free_blocks counter in buffer
        push rdx
        mov eax, [.blocks_needed]
        sub [hbfs_super_buf + 12], eax   ; free_blocks -= allocated
        ; Commit journal (clears dirty flag + writes superblock)
        call hbfs_journal_commit
        pop rdx

        mov [rsp + FRAME_RAX], rdx    ; Return first block number via RAX
        POPALL
        clc
        ret

.occupied:
        inc edx
        jmp .scan_start

.no_space:
        POPALL
        stc
        ret

.blocks_needed: dd 0

; Allocate a single data block (convenience wrapper)
; Returns: EAX = block number, CF set if full
hbfs_alloc_block:
        push rcx
        mov ecx, 1
        call hbfs_alloc_blocks
        pop rcx
        ret

; Free N contiguous data blocks
; EAX = first block number, ECX = block count
hbfs_free_blocks:
        PUSHALL
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

        ; Begin journal transaction (marks superblock dirty)
        call hbfs_journal_begin

        ; Write back
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov esi, hbfs_bitmap_buf
        call ata_write_sectors

        ; Update superblock free_blocks counter in buffer
        mov eax, [.free_count]
        add [hbfs_super_buf + 12], eax   ; free_blocks += freed
        ; Commit journal (clears dirty flag + writes superblock)
        call hbfs_journal_commit

        POPALL
        ret

.free_start: dd 0
.free_count: dd 0

; Free a single data block (convenience wrapper)
; EAX = block number
hbfs_free_block:
        push rcx
        mov ecx, 1
        call hbfs_free_blocks
        pop rcx
        ret

; Create a file
; ESI = filename, ECX = size, EDI = data pointer, EDX = file type (FTYPE_*)
; Returns: CF set on error
hbfs_create_file:
        PUSHALL
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
        POPALL
        stc
        ret

.found_slot:
        ; Copy filename into directory entry (max 252 chars + null)
        push rdi                ; Save entry start
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
        pop rdi                 ; Restore entry start

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
        push rdi
        call hbfs_alloc_blocks
        pop rdi
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
        mov ecx, [.save_blocks]
        shl eax, 3              ; first_block * 8 sectors
        add eax, HBFS_DATA_START ; + data area start = LBA
        shl ecx, 3              ; blocks * 8 = total sectors
        mov esi, [.save_data]
        call ata_write_sectors
        jc .write_fail

        POPALL
        clc
        ret

.alloc_fail:
.write_fail:
        POPALL
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
        PUSHALL
        ; Free ALL data blocks (contiguous)
        mov eax, [edi + 260]    ; Start block
        mov ecx, [edi + 264]    ; Block count
        cmp ecx, 0
        jle .skip_free
        call hbfs_free_blocks
.skip_free:

        ; Zero the directory entry
        mov ecx, HBFS_DIR_ENTRY_SIZE / 4
        push rdi
        xor eax, eax
        rep stosd
        pop rdi

        call hbfs_save_root_dir
        POPALL
        ret

; Create a subdirectory in the current directory
; ESI = directory name (null-terminated)
; Returns: CF=0 success, CF=1 error
hbfs_mkdir:
        PUSHALL
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
        push rax
        push rbx
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        xor eax, eax
        rep stosd
        pop rbx
        pop rax
        push rax
        push rbx
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        pop rbx
        pop rax
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
        push rdi
        mov esi, [.hm_name]
.hm_cpn:
        lodsb
        stosb
        test al, al
        jnz .hm_cpn
        pop rdi

        mov eax, [.hm_block]
        mov byte [edi + DIRENT_TYPE], FTYPE_DIR
        mov [edi + DIRENT_START_BLOCK], eax
        mov dword [edi + DIRENT_SIZE], 0
        mov dword [edi + DIRENT_BLOCK_COUNT], HBFS_SUBDIR_BLOCKS
        mov eax, [tick_count]
        mov [edi + DIRENT_CREATED], eax
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir

        POPALL
        clc
        ret

.hm_fail:
        POPALL
        stc
        ret

.hm_name:  dd 0
.hm_block: dd 0

;=======================================================================
; HBFS JOURNAL - Write-Ahead Log for crash recovery
;
; Uses superblock byte [32] as a dirty flag. Before any metadata write
; (bitmap + free_blocks update), the dirty flag is set and flushed.
; After the writes complete, the flag is cleared. On boot, if the flag
; is set, recovery recounts free blocks from the bitmap to restore
; consistency.
;=======================================================================

; hbfs_journal_begin - Mark filesystem as dirty before metadata writes
; Reads current superblock, sets dirty flag, writes back.
hbfs_journal_begin:
        PUSHALL
        ; Read current superblock
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov edi, hbfs_super_buf
        call ata_read_sectors
        ; Set dirty flag
        mov byte [hbfs_super_buf + 32], 1
        ; Write back
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors
        POPALL
        ret

; hbfs_journal_commit - Clear dirty flag after successful metadata writes
; Caller must have already updated free_blocks in hbfs_super_buf.
hbfs_journal_commit:
        PUSHALL
        ; Clear dirty flag
        mov byte [hbfs_super_buf + 32], 0
        ; Write superblock with updated free_blocks and clean flag
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors
        POPALL
        ret

; hbfs_journal_check - On boot, check dirty flag and recover if needed
; Called after superblock is loaded into hbfs_super_buf.
; If dirty, recounts free blocks from bitmap and fixes superblock.
hbfs_journal_check:
        PUSHALL
        cmp byte [hbfs_super_buf + 32], 0
        je .jc_clean

        ; Recovery needed
        mov esi, msg_hbfs_journal_recover
        call vga_print

        ; Read bitmap
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors

        ; Count used blocks by iterating bitmap bytes (Kernighan popcount)
        xor edx, edx           ; used count
        xor ecx, ecx           ; byte index
.jc_count:
        cmp ecx, HBFS_BITMAP_SIZE
        jge .jc_count_done
        movzx eax, byte [hbfs_bitmap_buf + ecx]
        ; Popcount this byte
.jc_pop:
        test al, al
        jz .jc_pop_done
        lea ebx, [eax - 1]
        and eax, ebx
        inc edx
        jmp .jc_pop
.jc_pop_done:
        inc ecx
        jmp .jc_count
.jc_count_done:
        ; EDX = total used blocks, free = total - used
        mov eax, HBFS_TOTAL_BLOCKS
        sub eax, edx
        mov [hbfs_super_buf + 12], eax

        ; Clear dirty flag
        mov byte [hbfs_super_buf + 32], 0

        ; Write fixed superblock
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors

        mov esi, msg_hbfs_journal_fixed
        call vga_print
.jc_clean:
        POPALL
        ret

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
        PUSHALL
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
        mov qword [rsp + FRAME_RAX], 0
        POPALL
        iretq
.chmod_fail:
        mov qword [rsp + FRAME_RAX], -1
        POPALL
        iretq

;---------------------------------------
; sys_chown - Change file owner
;   EBX = filename pointer
;   ECX = new owner UID
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_chown:
        PUSHALL
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
        mov qword [rsp + FRAME_RAX], 0
        POPALL
        iretq
.chown_fail:
        mov qword [rsp + FRAME_RAX], -1
        POPALL
        iretq

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
        PUSHALL
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
        ; Retrieve original ECX (target path pointer) from PUSHALL frame
        ; New 64-bit PUSHALL layout: RSP+96=RCX, RSP+112=RAX (see FRAME_* constants)
        mov rdi, [rsp + FRAME_RCX]     ; original RCX = target path pointer
        mov edx, FTYPE_LINK
        call hbfs_create_file
        jc .symlink_fail
        mov qword [rsp + FRAME_RAX], 0
        POPALL
        iretq
.symlink_fail:
        mov qword [rsp + FRAME_RAX], -1
        POPALL
        iretq

;---------------------------------------
; sys_readlink - Read symbolic link target
;   EBX = link name pointer
;   ECX = output buffer pointer
; Returns: EAX = target path length, -1 on failure
;---------------------------------------
sys_readlink:
        PUSHALL
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .readlink_fail
        ; Check it's actually a symlink
        cmp byte [edi + DIRENT_TYPE], FTYPE_LINK
        jne .readlink_fail
        ; Read the link data (target path) from disk
        mov eax, [edi + DIRENT_START_BLOCK]
        push rcx                ; save output buffer
        push qword [edi + DIRENT_SIZE]  ; save size
        ; Convert block to LBA
        imul eax, HBFS_SECTORS_PER_BLK
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        pop rdx                 ; EDX = size
        pop rdi                 ; EDI = output buffer
        jc .readlink_fail
        ; Copy target path to output buffer
        mov esi, hbfs_block_buf
        mov ecx, edx
        rep movsb
        mov [rsp + FRAME_RAX], rdx     ; Return length in RAX
        POPALL
        iretq
.readlink_fail:
        mov qword [rsp + FRAME_RAX], -1
        POPALL
        iretq
