;=======================================================================
; FILE SEARCH
; Search the current working directory only.
;
; ESI = filename to find (must remain valid, not modified)
; Returns: EDI = pointer to dir entry (in hbfs_dir_buf)
;          CF set if not found
; Note: .gff_moved is always 0 (kept for ABI compat with callers that
;       still check it).
;=======================================================================
hbfs_find_file_global:
        mov dword [.gff_moved], 0
        call hbfs_load_root_dir
        call hbfs_find_file
        ret                     ; CF already set/clear from hbfs_find_file

.gff_idx:   dd 0
.gff_moved: dd 0

;-----------------------------------------------------------------------
; GFF-private CWD save/restore (separate slots to avoid conflict with
; file_save_cwd / path_save_cwd used by other subsystems)
;-----------------------------------------------------------------------
gff_save_cwd:
        pushad
        mov eax, [current_dir_lba]
        mov [gff_cwd_lba], eax
        mov eax, [current_dir_sects]
        mov [gff_cwd_sects], eax
        mov eax, [dir_depth]
        mov [gff_cwd_depth], eax
        mov esi, current_dir_name
        mov edi, gff_cwd_name
        call str_copy
        mov esi, dir_stack
        mov edi, gff_cwd_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

gff_restore_cwd:
        pushad
        mov eax, [gff_cwd_lba]
        mov [current_dir_lba], eax
        mov eax, [gff_cwd_sects]
        mov [current_dir_sects], eax
        mov eax, [gff_cwd_depth]
        mov [dir_depth], eax
        mov esi, gff_cwd_name
        mov edi, current_dir_name
        call str_copy
        mov esi, gff_cwd_stack
        mov edi, dir_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

;---------------------------------------
; set_program_brk_init - record where the heap may start for this image
;   In : EAX = first free byte after the image (image end address)
;
; The value is page-aligned, kept inside the program window and well below the
; user stack, then stored in program_brk_init so sched_create_task can seed the
; task's TCB_BRK with it. Without this the break started at the end of the whole
; window, so every SYS_SBRK failed.
;---------------------------------------
set_program_brk_init:
        pushad
        add eax, 0xFFF
        and eax, ~0xFFF
        cmp eax, PROGRAM_BASE + 0x1000
        jae .sbi_min_ok
        mov eax, PROGRAM_BASE + 0x1000
.sbi_min_ok:
        mov ebx, USER_STACK_BASE        ; heap stops at the stack guard page
        cmp eax, ebx
        jbe .sbi_max_ok
        mov eax, ebx
.sbi_max_ok:
        mov [program_brk_init], eax
        popad
        ret

; Load file data into buffer
; ESI = filename, EDI = destination buffer
; Returns: ECX = file size, CF set if not found
;
; Reads are capped at HBFS_READ_MAX_BLOCKS. Callers pass a destination inside
; the program window (PROGRAM_BASE..PROGRAM_END_ADDR, 1 MB); a larger file is
; refused rather than read past that window into the paging structures and the
; PMM bitmap that sit above it.
HBFS_READ_MAX_BLOCKS equ PROGRAM_MAX_SIZE / HBFS_BLOCK_SIZE   ; 256 blocks = 1 MB
hbfs_read_file:
        pushad
        mov [.save_dest], edi

        ; === Path resolution: check if filename contains '/' ===
        push esi
        mov edi, esi
.scan_slash:
        cmp byte [edi], 0
        je .no_path_slash
        cmp byte [edi], '/'
        je .has_path_slash
        inc edi
        jmp .scan_slash

.no_path_slash:
        pop esi
        jmp .read_local         ; No slash - read from current directory

.has_path_slash:
        pop esi                 ; ESI = full path string
        ; Find the LAST '/' to split into directory + basename
        push esi
        xor ebx, ebx            ; EBX will hold pointer to last '/'
.find_last_slash:
        cmp byte [esi], 0
        je .split_path
        cmp byte [esi], '/'
        jne .fls_skip
        mov ebx, esi
.fls_skip:
        inc esi
        jmp .find_last_slash

.split_path:
        pop esi                 ; ESI = start of full path
        ; Copy directory part (ESI up to EBX) into path_dir_buf
        mov edi, path_dir_buf
.cp_dir:
        cmp esi, ebx
        je .cp_dir_done
        movsb
        jmp .cp_dir
.cp_dir_done:
        ; If dir part is empty, path was "/filename" - use "/"
        cmp edi, path_dir_buf
        jne .dir_ok
        mov byte [edi], '/'
        inc edi
.dir_ok:
        mov byte [edi], 0

        ; Basename starts after the last '/'
        lea esi, [ebx + 1]
        mov edi, path_base_buf
.cp_base:
        lodsb
        stosb
        test al, al
        jnz .cp_base

        ; If basename is empty (trailing slash), fail
        cmp byte [path_base_buf], 0
        je .path_read_fail_no_restore

        ; Save CWD using file-level save (separate from PATH search save)
        call file_save_cwd

        ; cd into the directory
        mov esi, path_dir_buf
        call cmd_cd_internal
        test eax, eax
        jnz .path_read_fail

        ; Now read from the resolved directory
        call hbfs_load_root_dir
        mov esi, path_base_buf
        call hbfs_find_file
        jc .path_read_fail

        ; Found it - read file data
        movzx ebx, byte [edi + 253]
        mov [last_file_type], bl
        mov ebx, [edi + 256]    ; file size
        mov [.save_fsize], ebx
        ; A v4 entry whose flags name runs is read block by block; an inline
        ; entry keeps the one contiguous read it has always done.
        test word [edi + 254], HBFS4_DE_EXTENTS
        jnz .path_read_extent
        mov eax, [edi + 260]    ; start block
        mov ecx, [edi + 264]    ; block count
        cmp ecx, HBFS_READ_MAX_BLOCKS
        ja  .path_read_fail     ; file is larger than the caller's buffer
        shl eax, 3
        add eax, HBFS_DATA_START
        shl ecx, 3
        mov edi, [.save_dest]
        call ata_read_sectors
        jc .path_read_fail
        jmp .path_read_ok

.path_read_extent:
        mov [.save_entry], edi
        mov ecx, [edi + 264]    ; data blocks (not the extent block)
        cmp ecx, HBFS_READ_MAX_BLOCKS
        ja  .path_read_fail
        mov [.save_blocks], ecx
        mov dword [.save_index], 0
        mov eax, [.save_fsize]
        mov [.save_remain], eax ; .save_fsize is the value the caller gets back
        mov edi, [.save_dest]
        mov [.save_out], edi
.path_ext_block:
        cmp dword [.save_blocks], 0
        je  .path_read_ok
        mov edi, [.save_entry]
        mov ebx, [.save_index]
        call hbfs_map_block
        jc  .path_read_fail
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        jc  .path_read_fail
        mov ecx, HBFS_BLOCK_SIZE
        cmp ecx, [.save_remain]
        jbe .path_ext_len
        mov ecx, [.save_remain]
.path_ext_len:
        jecxz .path_ext_next
        sub [.save_remain], ecx
        mov esi, hbfs_block_buf
        mov edi, [.save_out]
        rep movsb
        mov [.save_out], edi
.path_ext_next:
        inc dword [.save_index]
        dec dword [.save_blocks]
        jmp .path_ext_block

.path_read_ok:
        ; Restore CWD
        call file_restore_cwd

        ; Return file size in ECX
        mov eax, [.save_fsize]
        mov [esp + 24], eax     ; ECX position in pushad frame
        popad
        clc
        ret

.path_read_fail:
        call file_restore_cwd
.path_read_fail_no_restore:
        popad
        stc
        ret

        ; === Local file read (no path - original behavior) ===
.read_local:
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .not_found

        ; EDI now points to the directory entry
        ; Save the file type for callers to inspect
        movzx ebx, byte [edi + 253]
        mov [last_file_type], bl
        ; Save the file size before we use EDI for reading
        mov ebx, [edi + 256]    ; File size
        mov [.save_fsize], ebx
        mov [.save_fsize_orig], ebx

        ; Get block number and block count, then read all data.
        ;
        ; The file occupies whole blocks, but the caller sized its buffer from
        ; the FILE size, so reading every block straight into the destination
        ; overruns it by up to one block (the padding of the last block). Each
        ; block therefore lands in the bounce buffer and only the bytes the
        ; file actually contains are copied out.
        mov eax, [edi + 260]    ; Start block (inline) / extent block
        mov [.save_start], eax
        mov [.save_entry], edi
        mov dword [.save_index], 0
        mov dword [.save_extent], 0
        test word [edi + 254], HBFS4_DE_EXTENTS
        jz  .read_inline_blocks
        mov dword [.save_extent], 1
.read_inline_blocks:
        mov ecx, [edi + 264]    ; Block count
        cmp ecx, HBFS_READ_MAX_BLOCKS
        ja  .read_error         ; file is larger than the caller's buffer
        mov [.save_blocks], ecx
        mov edi, [.save_dest]
        mov [.save_out], edi

.read_block:
        cmp dword [.save_blocks], 0
        je .read_done
        cmp dword [.save_extent], 0
        jne .read_block_extent
        mov eax, [.save_start]
        jmp .read_have_block
.read_block_extent:
        mov edi, [.save_entry]
        mov ebx, [.save_index]
        call hbfs_map_block
        jc .read_error
.read_have_block:
        shl eax, 3              ; * 8 = sector offset within data area
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call blk_cache_read
        jc .read_error

        ; Copy min(remaining file bytes, one block)
        mov ecx, HBFS_BLOCK_SIZE
        cmp ecx, [.save_fsize]
        jbe .have_copy_len
        mov ecx, [.save_fsize]
.have_copy_len:
        jecxz .next_block
        sub [.save_fsize], ecx
        mov esi, hbfs_block_buf
        mov edi, [.save_out]
        rep movsb               ; EDI advances past the copied bytes
        mov [.save_out], edi
.next_block:
        inc dword [.save_start]
        inc dword [.save_index]
        dec dword [.save_blocks]
        jmp .read_block

.read_done:
        ; Return file size via ECX in pushad frame
        mov eax, [.save_fsize_orig]
        mov [esp + 24], eax     ; ECX position in pushad frame

        popad
        clc
        ret

.not_found:
        ; File not found in current directory — return error.
        ; Callers that need to search other directories (like cmd_exec_program)
        ; implement their own PATH-based search.
        popad
        stc
        ret

.read_error:
        popad
        stc
        ret

.save_dest:   dd 0
.save_fsize:  dd 0
.save_fsize_orig: dd 0
.save_start:  dd 0
.save_entry:  dd 0
.save_index:  dd 0
.save_extent: dd 0
.save_out:    dd 0
.save_remain: dd 0
.save_blocks: dd 0
