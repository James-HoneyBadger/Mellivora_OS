; ==========================================================================
; tar - Simple tape archive utility for Mellivora OS
;
; Usage: tar c <archive> <file1> [file2 ...]  Create archive from files
;        tar x <archive>                      Extract archive to CWD
;        tar t <archive>                      List archive contents
;
; Archive format (MTAR):
;   For each member:
;     [2 bytes] filename length (LE)
;     [N bytes] filename (no null)
;     [1 byte]  file type (1=text, 3=exec, 4=batch, 5=link)
;     [4 bytes] file size (LE)
;     [N bytes] file data
;   End of archive:
;     [2 bytes] 0x0000 (zero-length filename = EOF marker)
; ==========================================================================
%include "syscalls.inc"

MAX_FILE    equ 32768
MAX_ARCHIVE equ 131072          ; 128 KB max archive
MAX_ARGS    equ 32              ; Max files to archive

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz show_usage

        ; Parse mode character
        mov esi, arg_buf
        mov al, [esi]
        cmp al, 'c'
        je mode_create
        cmp al, 't'
        je mode_list
        cmp al, 'x'
        je mode_extract
        jmp show_usage

; ===================================================================
; CREATE mode: tar c <archive> <file1> [file2 ...]
; ===================================================================
mode_create:
        ; Skip 'c' and space to get archive name
        add esi, 1
        call skip_spaces
        cmp byte [esi], 0
        je show_usage

        ; Copy archive name
        mov edi, archive_name
        call copy_token

        ; Now parse file list
        mov dword [file_count], 0
        mov edi, file_list      ; array of 64-byte names

.parse_files:
        call skip_spaces
        cmp byte [esi], 0
        je .create_go
        cmp dword [file_count], MAX_ARGS
        jge .create_go

        push rdi
        call copy_token
        pop rdi
        add edi, 64
        inc dword [file_count]
        jmp .parse_files

.create_go:
        cmp dword [file_count], 0
        je show_usage

        ; Build archive in archive_buf
        mov edi, archive_buf
        mov ecx, [file_count]
        mov esi, file_list

.create_loop:
        test ecx, ecx
        jz .create_eof
        push rcx
        push rsi

        ; Get file size and type via READDIR scan
        ; First, just try reading the file
        push rdi
        mov eax, SYS_FREAD
        mov ebx, esi            ; filename
        mov ecx, file_buf
        int 0x80
        pop rdi
        test eax, eax
        jz .create_skip         ; file not found, skip

        mov [cur_file_size], eax

        ; Detect file type via SYS_STAT (limited: only gives size)
        ; Default to FTYPE_TEXT, but check .asm/.bat -> FTYPE_BATCH
        ; and check if file is executable by looking at header
        push rdi
        mov eax, [file_buf]     ; first 4 bytes
        cmp ax, 0x00EB          ; short JMP (common flat binary start)
        jne .not_exec
        mov byte [cur_file_type], FTYPE_EXEC
        jmp .type_done
.not_exec:
        cmp byte [file_buf], 0xE9  ; near JMP (ORG 0x200000 programs)
        jne .not_exec2
        mov byte [cur_file_type], FTYPE_EXEC
        jmp .type_done
.not_exec2:
        ; Check for "jmp start" emitted by NASM with [ORG]: E9 xx xx xx xx
        ; or for ELF magic
        cmp dword [file_buf], 0x464C457F  ; .ELF
        jne .check_batch
        mov byte [cur_file_type], FTYPE_EXEC
        jmp .type_done
.check_batch:
        ; Check if filename ends in .bat
        mov byte [cur_file_type], FTYPE_TEXT
.type_done:
        pop rdi

        ; Write header: [2] name_len [N] name [1] type [4] size
        ; Calculate filename length
        pop rsi
        push rsi
        push rdi
        xor ecx, ecx
        mov ebx, esi
.namelen:
        cmp byte [ebx + ecx], 0
        je .namelen_done
        inc ecx
        cmp ecx, 252
        jge .namelen_done
        jmp .namelen
.namelen_done:
        pop rdi
        ; Store name length (2 bytes LE)
        mov [edi], cx
        add edi, 2
        ; Copy filename
        push rcx
        mov ebx, esi
.copy_name:
        test ecx, ecx
        jz .name_copied
        mov al, [ebx]
        mov [edi], al
        inc ebx
        inc edi
        dec ecx
        jmp .copy_name
.name_copied:
        pop rcx
        ; Store type (1 byte)
        mov al, [cur_file_type]
        mov [edi], al
        inc edi
        ; Store file size (4 bytes LE)
        mov eax, [cur_file_size]
        mov [edi], eax
        add edi, 4
        ; Copy file data
        mov ecx, [cur_file_size]
        mov esi, file_buf
.copy_data:
        test ecx, ecx
        jz .file_done
        mov al, [esi]
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jmp .copy_data

.file_done:
        pop rsi
        add esi, 64
        pop rcx
        dec ecx
        jmp .create_loop

.create_skip:
        ; Print warning
        pop rsi
        push rsi
        mov eax, SYS_PRINT
        mov ebx, msg_skip
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rsi
        add esi, 64
        pop rcx
        dec ecx
        jmp .create_loop

.create_eof:
        ; Write EOF marker (2 zero bytes)
        mov word [edi], 0
        add edi, 2

        ; Calculate total archive size
        mov eax, edi
        sub eax, archive_buf
        mov [archive_size], eax

        ; Write archive file
        mov eax, SYS_FWRITE
        mov ebx, archive_name
        mov ecx, archive_buf
        mov edx, [archive_size]
        mov esi, FTYPE_TEXT
        int 0x80
        cmp eax, -1
        je .write_err

        ; Print summary
        mov eax, SYS_PRINT
        mov ebx, msg_created
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, archive_name
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_paren
        int 0x80
        mov eax, [archive_size]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_bytes_nl
        int 0x80
        jmp exit_ok

.write_err:
        mov eax, SYS_PRINT
        mov ebx, msg_write_err
        int 0x80
        jmp exit_err

; ===================================================================
; LIST mode: tar t <archive>
; ===================================================================
mode_list:
        add esi, 1
        call skip_spaces
        cmp byte [esi], 0
        je show_usage
        mov edi, archive_name
        call copy_token

        ; Read archive
        call load_archive
        test eax, eax
        jz .list_err

        ; Parse and list entries
        mov esi, archive_buf
        xor ebp, ebp            ; file count

.list_loop:
        movzx ecx, word [esi]   ; filename length
        test ecx, ecx
        jz .list_done
        add esi, 2

        ; Copy name to temp
        push rsi
        push rcx
        mov edi, name_buf
        rep movsb
        mov byte [edi], 0
        pop rcx
        pop rsi
        add esi, ecx

        ; Read type
        movzx eax, byte [esi]
        inc esi
        push rax

        ; Read size
        mov edx, [esi]
        add esi, 4

        ; Print: type  size  filename
        pop rax
        push rsi
        push rdx

        ; Type letter
        cmp eax, FTYPE_EXEC
        jne .lt_not_exec
        mov ebx, 'x'
        jmp .lt_type
.lt_not_exec:
        cmp eax, FTYPE_DIR
        jne .lt_not_dir
        mov ebx, 'd'
        jmp .lt_type
.lt_not_dir:
        cmp eax, FTYPE_BATCH
        jne .lt_not_bat
        mov ebx, 'b'
        jmp .lt_type
.lt_not_bat:
        mov ebx, '-'
.lt_type:
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_tab
        int 0x80

        ; Size
        pop rax                  ; size
        push rax
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_tab
        int 0x80

        ; Filename
        mov eax, SYS_PRINT
        mov ebx, name_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        pop rdx                  ; size
        pop rsi                  ; archive pos
        add esi, edx             ; skip file data
        inc ebp
        jmp .list_loop

.list_done:
        mov eax, SYS_PRINT
        mov ebx, msg_total_pre
        int 0x80
        mov eax, ebp
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_files_nl
        int 0x80
        jmp exit_ok

.list_err:
        mov eax, SYS_PRINT
        mov ebx, msg_read_err
        int 0x80
        jmp exit_err

; ===================================================================
; EXTRACT mode: tar x <archive>
; ===================================================================
mode_extract:
        add esi, 1
        call skip_spaces
        cmp byte [esi], 0
        je show_usage
        mov edi, archive_name
        call copy_token

        ; Read archive
        call load_archive
        test eax, eax
        jz .ext_err

        ; Parse and extract
        mov esi, archive_buf
        xor ebp, ebp

.ext_loop:
        movzx ecx, word [esi]
        test ecx, ecx
        jz .ext_done
        add esi, 2

        ; Copy filename
        push rcx
        mov edi, name_buf
        rep movsb
        mov byte [edi], 0
        pop rcx
        add esi, ecx

        ; Read type
        movzx eax, byte [esi]
        mov [cur_file_type], al
        inc esi

        ; Read size
        mov edx, [esi]
        mov [cur_file_size], edx
        add esi, 4

        ; Write file
        push rsi
        mov eax, SYS_FWRITE
        mov ebx, name_buf
        mov ecx, esi             ; data pointer within archive_buf
        mov edx, [cur_file_size]
        movzx esi, byte [cur_file_type]
        int 0x80
        pop rsi

        cmp eax, -1
        je .ext_file_err

        ; Print extracted filename
        push rsi
        mov eax, SYS_PRINT
        mov ebx, name_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rsi

        add esi, [cur_file_size]
        inc ebp
        jmp .ext_loop

.ext_file_err:
        push rsi
        mov eax, SYS_PRINT
        mov ebx, msg_ext_fail
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, name_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rsi
        add esi, [cur_file_size]
        inc ebp
        jmp .ext_loop

.ext_done:
        mov eax, SYS_PRINT
        mov ebx, msg_extracted
        int 0x80
        mov eax, ebp
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_files_nl
        int 0x80
        jmp exit_ok

.ext_err:
        mov eax, SYS_PRINT
        mov ebx, msg_read_err
        int 0x80
        jmp exit_err

; ===================================================================
; Helpers
; ===================================================================

; load_archive - Read archive file into archive_buf
; Returns: EAX = bytes read (0 on failure)
load_archive:
        mov eax, SYS_FREAD
        mov ebx, archive_name
        mov ecx, archive_buf
        int 0x80
        ret

; skip_spaces - Advance ESI past spaces
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

; copy_token - Copy next space/null-delimited token from ESI to EDI
; Advances ESI, null-terminates EDI
copy_token:
.ct_loop:
        lodsb
        test al, al
        jz .ct_end
        cmp al, ' '
        je .ct_end
        stosb
        jmp .ct_loop
.ct_end:
        mov byte [edi], 0
        ret

show_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
exit_ok:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
exit_err:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; -------------------------------------------------------------------
; Strings
; -------------------------------------------------------------------
msg_usage:      db "Usage: tar c <archive> <files...>  Create archive", 0x0A
                db "       tar x <archive>             Extract files", 0x0A
                db "       tar t <archive>             List contents", 0x0A, 0
msg_created:    db "Created: ", 0
msg_paren:      db " (", 0
msg_bytes_nl:   db " bytes)", 0x0A, 0
msg_skip:       db "tar: skipping missing file: ", 0
msg_write_err:  db "tar: error writing archive", 0x0A, 0
msg_read_err:   db "tar: cannot read archive", 0x0A, 0
msg_ext_fail:   db "tar: error extracting: ", 0
msg_extracted:  db "Extracted ", 0
msg_total_pre:  db "Total: ", 0
msg_files_nl:   db " file(s)", 0x0A, 0
msg_tab:        db "  ", 0

; -------------------------------------------------------------------
; BSS
; -------------------------------------------------------------------
archive_name:   times 256 db 0
name_buf:       times 256 db 0
arg_buf:        times 512 db 0
file_count:     dd 0
archive_size:   dd 0
cur_file_size:  dd 0
cur_file_type:  db 0
file_list:      times (MAX_ARGS * 64) db 0     ; 2 KB for filenames
file_buf:       times MAX_FILE db 0            ; 32 KB scratch for one file
archive_buf:    times MAX_ARCHIVE db 0         ; 128 KB archive buffer
