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
;
; Directory Entry (288 bytes):
;   [0-252]   Filename (null-terminated, 252 chars max)
;   [253]     Type (0=free, 1=file, 2=dir)
;   [254-255] Flags
;   [256-259] File size in bytes
;   [260-263] Start block (in data area)
;   [264-267] Block count
;   [268-271] Created timestamp (ticks)
;   [272-275] Modified timestamp (ticks)
;   [276-287] Reserved
;
; Block allocation bitmap:
;   8 sectors (4096 bytes) = 32768 blocks max
;   At 4KB/block = 128MB addressable
;   For 64GB, we'd need more bitmap but this is a solid start
;
;=======================================================================

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
        mov dword [edi + 4], 1                  ; Version
        mov dword [edi + 8], 32768              ; Total blocks
        mov dword [edi + 12], 32768             ; Free blocks
        mov dword [edi + 16], HBFS_ROOT_DIR_START; Root dir LBA
        mov dword [edi + 20], HBFS_BITMAP_START  ; Bitmap start LBA
        mov dword [edi + 24], HBFS_DATA_START    ; Data start LBA
        mov dword [edi + 28], HBFS_BLOCK_SIZE

        ; Write superblock
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors

        ; Zero out bitmap (8 sectors)
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        xor eax, eax
        rep stosd

        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors

        ; Zero out root directory (128 sectors = 16 blocks)
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

; Load current directory into memory
; Returns: CF set on read error
hbfs_load_root_dir:
        pushad
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
        mov ecx, HBFS_SECTORS_PER_BLK
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
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors

        ; Scan for N contiguous free bits
        xor edx, edx           ; Current block number
        mov ecx, 32768          ; Total blocks in bitmap

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
        mov ecx, HBFS_SECTORS_PER_BLK
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
        mov ecx, HBFS_SECTORS_PER_BLK
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
        mov ecx, HBFS_SECTORS_PER_BLK
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
        mov ecx, [tick_count]
        mov [edi + 268], ecx            ; Created timestamp
        mov [edi + 272], ecx            ; Modified timestamp

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
