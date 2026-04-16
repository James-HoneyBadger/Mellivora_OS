; ==========================================================================
; defrag - HBFS Filesystem Defragmenter for Mellivora OS
; Usage: defrag [-n]    -n = dry-run (analyze only, no changes)
;
; Strategy: HBFS allocates contiguous blocks for each file. Fragmentation
; occurs when files are deleted and new files fill gaps. This tool
; reads each file, deletes it, then rewrites it so HBFS re-allocates
; contiguous blocks from the first available region, packing the disk.
;
; Only plain files in the root directory are processed. System files,
; directories, and symlinks are skipped.
; ==========================================================================

%include "syscalls.inc"

MAX_FILE_SIZE   equ 61440           ; 60KB max per file
MAX_FILES       equ 200             ; max directory entries to scan

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        ; Check for -n (dry-run)
        mov byte [dry_run], 0
        cmp byte [arg_buf], '-'
        jne .no_flag
        cmp byte [arg_buf + 1], 'n'
        jne .no_flag
        mov byte [dry_run], 1
.no_flag:

        ; Print banner
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, banner_str
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        cmp byte [dry_run], 1
        jne .no_dry_msg
        mov eax, SYS_PRINT
        mov ebx, msg_dryrun
        int 0x80
.no_dry_msg:

        ; Phase 1: Scan directory and count files
        mov eax, SYS_PRINT
        mov ebx, msg_scan
        int 0x80

        xor ecx, ecx           ; entry index
        mov dword [file_count], 0
        mov dword [total_bytes], 0
        mov dword [files_processed], 0

.scan_loop:
        cmp ecx, MAX_FILES
        jge .scan_done
        push rcx

        mov eax, SYS_READDIR
        mov ebx, name_buf
        int 0x80

        pop rcx
        cmp eax, -1
        je .scan_done
        cmp eax, 0
        je .scan_next

        ; Only count regular files (text=0 or exec=1)
        cmp eax, FTYPE_TEXT
        je .scan_count
        cmp eax, FTYPE_EXEC
        je .scan_count
        jmp .scan_next

.scan_count:
        inc dword [file_count]
        add [total_bytes], ecx  ; ECX = file size from SYS_READDIR

.scan_next:
        inc ecx
        jmp .scan_loop

.scan_done:
        ; Print scan results
        mov eax, SYS_PRINT
        mov ebx, msg_found
        int 0x80
        mov eax, [file_count]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_files
        int 0x80
        mov eax, [total_bytes]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_bytes_total
        int 0x80

        ; If dry-run, stop here
        cmp byte [dry_run], 1
        je .finish

        ; Check if there are files to process
        cmp dword [file_count], 0
        je .finish

        ; Phase 2: Defragment — re-read each file, delete, rewrite
        mov eax, SYS_PRINT
        mov ebx, msg_defrag
        int 0x80

        xor ecx, ecx           ; entry index
.defrag_loop:
        cmp ecx, MAX_FILES
        jge .defrag_done
        push rcx

        mov eax, SYS_READDIR
        mov ebx, name_buf
        int 0x80

        pop rcx
        cmp eax, -1
        je .defrag_done
        cmp eax, 0
        je .defrag_next

        ; Only process regular files
        cmp eax, FTYPE_TEXT
        je .defrag_file
        cmp eax, FTYPE_EXEC
        je .defrag_file
        jmp .defrag_next

.defrag_file:
        mov [cur_type], eax
        ; ECX from READDIR = file size (save it)
        push rcx               ; save dir index on stack
        ; (Note: ecx from READDIR is size, but we popped dir index earlier)
        ; We need the dir index, so pop and save properly
        ; Actually ecx was already restored from push/pop around READDIR

        ; Read the file content
        mov eax, SYS_FREAD
        mov ebx, name_buf
        mov ecx, file_buf
        mov edx, MAX_FILE_SIZE
        int 0x80
        cmp eax, 0
        jle .defrag_skip       ; skip empty/failed

        mov [cur_size], eax

        ; Print filename
        mov eax, SYS_PRINT
        mov ebx, msg_defrag_file
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, name_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_dots
        int 0x80

        ; Delete the file
        mov eax, SYS_DELETE
        mov ebx, name_buf
        int 0x80
        cmp eax, -1
        je .defrag_err

        ; Rewrite the file (HBFS allocates fresh contiguous blocks)
        mov eax, SYS_FWRITE
        mov ebx, name_buf
        mov ecx, file_buf
        mov edx, [cur_size]
        mov esi, [cur_type]
        int 0x80
        cmp eax, -1
        je .defrag_err

        inc dword [files_processed]

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80

.defrag_skip:
        pop rcx
        jmp .defrag_next

.defrag_err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        pop rcx

.defrag_next:
        inc ecx
        jmp .defrag_loop

.defrag_done:
        ; Print summary
        mov eax, SYS_PRINT
        mov ebx, msg_complete
        int 0x80
        mov eax, [files_processed]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_of
        int 0x80
        mov eax, [file_count]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_relocated
        int 0x80

.finish:
        ; Show free space
        mov eax, SYS_MEMINFO
        int 0x80
        ; EAX = free pages — use df for disk info instead
        mov eax, SYS_PRINT
        mov ebx, msg_done
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ---- PRINT DECIMAL ----
print_decimal:
        PUSHALL
        mov ecx, 10
        xor ebp, ebp
.pd_push:
        xor edx, edx
        div ecx
        push rdx
        inc ebp
        test eax, eax
        jnz .pd_push
.pd_print:
        pop rdx
        add edx, '0'
        mov eax, SYS_PUTCHAR
        mov ebx, edx
        int 0x80
        dec ebp
        jnz .pd_print
        POPALL
        ret

; ---- DATA ----
banner_str:     db "Mellivora HBFS Defragmenter v1.0", 0x0D, 0x0A, 0
msg_dryrun:     db "[DRY RUN - no changes will be made]", 0x0D, 0x0A, 0
msg_scan:       db "Scanning directory...", 0x0D, 0x0A, 0
msg_found:      db "Found ", 0
msg_files:      db " files (", 0
msg_bytes_total: db " bytes total)", 0x0D, 0x0A, 0
msg_defrag:     db "Defragmenting...", 0x0D, 0x0A, 0
msg_defrag_file: db "  ", 0
msg_dots:       db "... ", 0
msg_ok:         db "OK", 0x0D, 0x0A, 0
msg_err:        db "ERR", 0x0D, 0x0A, 0
msg_complete:   db 0x0D, 0x0A, "Complete: ", 0
msg_of:         db " of ", 0
msg_relocated:  db " files relocated", 0x0D, 0x0A, 0
msg_done:       db "Defragmentation finished.", 0x0D, 0x0A, 0

; ---- BSS ----
arg_buf:        times 64 db 0
name_buf:       times 260 db 0
dry_run:        db 0
file_count:     dd 0
total_bytes:    dd 0
files_processed: dd 0
cur_size:       dd 0
cur_type:       dd 0
file_buf:       times MAX_FILE_SIZE db 0
