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

; Maximum symlink resolution depth (prevents infinite loops)
SYMLINK_MAX_DEPTH equ 8

;-----------------------------------------------------------------------
; GFF-private CWD save/restore (separate slots to avoid conflict with
; file_save_cwd / path_save_cwd used by other subsystems)
;-----------------------------------------------------------------------
gff_save_cwd:
        PUSHALL
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
        POPALL
        ret

gff_restore_cwd:
        PUSHALL
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
        POPALL
        ret

; Load file data into buffer
; ESI = filename, EDI = destination buffer
; Returns: ECX = file size, CF set if not found
hbfs_read_file:
        PUSHALL
        mov [.save_dest], edi

        ; === Path resolution: check if filename contains '/' ===
        mov dword [symlink_depth], 0
.resolve_entry:
        push rsi
        mov edi, esi
.scan_slash:
        cmp byte [edi], 0
        je .no_path_slash
        cmp byte [edi], '/'
        je .has_path_slash
        inc edi
        jmp .scan_slash

.no_path_slash:
        pop rsi
        jmp .read_local         ; No slash - read from current directory

.has_path_slash:
        pop rsi                 ; ESI = full path string
        ; Find the LAST '/' to split into directory + basename
        push rsi
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
        pop rsi                 ; ESI = start of full path
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

        ; Check for symlink and resolve transparently
        cmp byte [edi + 253], FTYPE_LINK
        jne .path_read_data

        ; Read symlink target
        mov eax, [edi + DIRENT_START_BLOCK]
        push qword [edi + DIRENT_SIZE]
        imul eax, HBFS_SECTORS_PER_BLK
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        pop rdx
        jc .path_read_fail
        cmp edx, HBFS_BLOCK_SIZE - 1
        jl .pl_term_ok
        mov edx, HBFS_BLOCK_SIZE - 1
.pl_term_ok:
        mov byte [hbfs_block_buf + edx], 0

        inc dword [symlink_depth]
        cmp dword [symlink_depth], SYMLINK_MAX_DEPTH
        jg .path_symlink_loop

        ; Restore CWD before re-resolving
        call file_restore_cwd
        ; Copy target and restart full resolution
        mov esi, hbfs_block_buf
        mov edi, symlink_path_buf
.pl_copy:
        lodsb
        stosb
        test al, al
        jnz .pl_copy
        mov esi, symlink_path_buf
        jmp .resolve_entry

.path_symlink_loop:
        mov dword [symlink_depth], 0
        jmp .path_read_fail

.path_read_data:
        ; Found it - read file data
        movzx ebx, byte [edi + 253]
        mov [last_file_type], bl
        mov ebx, [edi + 256]    ; file size
        mov [.save_fsize], ebx
        mov eax, [edi + 260]    ; start block
        mov ecx, [edi + 264]    ; block count
        shl eax, 3
        add eax, HBFS_DATA_START
        shl ecx, 3
        mov edi, [.save_dest]
        call ata_read_sectors
        jc .path_read_fail

        ; Restore CWD
        call file_restore_cwd

        ; Return file size in ECX
        mov eax, [.save_fsize]
        mov [rsp + FRAME_RCX], rax     ; RCX position in PUSHALL frame
        POPALL
        clc
        ret

.path_read_fail:
        call file_restore_cwd
.path_read_fail_no_restore:
        POPALL
        stc
        ret

        ; === Local file read (no path - original behavior) ===
.read_local:
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .not_found

        ; Check for symlink and resolve transparently
        cmp byte [edi + 253], FTYPE_LINK
        jne .read_local_data

        ; Read symlink target path and re-resolve via full path read
        mov eax, [edi + DIRENT_START_BLOCK]
        push qword [edi + DIRENT_SIZE]
        imul eax, HBFS_SECTORS_PER_BLK
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        pop rdx                 ; EDX = link data size
        jc .not_found

        ; Null-terminate the target path
        cmp edx, HBFS_BLOCK_SIZE - 1
        jl .rl_term_ok
        mov edx, HBFS_BLOCK_SIZE - 1
.rl_term_ok:
        mov byte [hbfs_block_buf + edx], 0

        ; Check recursion depth
        inc dword [symlink_depth]
        cmp dword [symlink_depth], SYMLINK_MAX_DEPTH
        jg .symlink_loop

        ; Copy target into symlink_path_buf and re-resolve
        mov esi, hbfs_block_buf
        mov edi, symlink_path_buf
.rl_copy_target:
        lodsb
        stosb
        test al, al
        jnz .rl_copy_target

        ; Re-enter full path resolution with the symlink target
        mov esi, symlink_path_buf
        jmp .resolve_entry

.symlink_loop:
        mov dword [symlink_depth], 0
        jmp .not_found

.read_local_data:

        ; EDI now points to the directory entry
        ; Save the file type for callers to inspect
        movzx ebx, byte [edi + 253]
        mov [last_file_type], bl
        ; Save the file size before we use EDI for reading
        mov ebx, [edi + 256]    ; File size
        mov [.save_fsize], ebx

        ; Get block number and block count, then read all data
        mov eax, [edi + 260]    ; Start block
        mov ecx, [edi + 264]    ; Block count
        shl eax, 3              ; * 8 = sector offset within data area
        add eax, HBFS_DATA_START
        shl ecx, 3              ; block_count * 8 = total sectors to read
        mov edi, [.save_dest]
        call ata_read_sectors
        jc .read_error

        ; Return file size via ECX in PUSHALL frame
        mov eax, [.save_fsize]
        mov [rsp + FRAME_RCX], rax     ; RCX position in PUSHALL frame

        POPALL
        clc
        ret

.not_found:
        ; File not found in current directory — return error.
        ; Callers that need to search other directories (like cmd_exec_program)
        ; implement their own PATH-based search.
        POPALL
        stc
        ret

.read_error:
        POPALL
        stc
        ret

.save_dest:  dd 0
.save_fsize: dd 0
