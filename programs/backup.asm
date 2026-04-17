; ==========================================================================
; backup - Directory backup utility for Mellivora OS
;
; Usage: backup <destdir>           Backup CWD files to <destdir>/
;        backup -v <destdir>        Verbose mode (list each file)
;
; Copies all files from current directory into destdir.
; Creates destdir if it doesn't exist.
; Skips subdirectories (flat copy).
; ==========================================================================
%include "syscalls.inc"

MAX_FILE    equ 32768
MAX_ENTRIES equ 300

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz show_usage

        mov esi, arg_buf
        mov byte [verbose], 0

        ; Check for -v flag
        cmp word [esi], '-v'
        jne .no_verbose
        cmp byte [esi + 2], ' '
        jne .no_verbose
        mov byte [verbose], 1
        add esi, 3
        call skip_sp
.no_verbose:
        ; Copy dest directory name
        mov edi, dest_dir
        call copy_token
        cmp byte [dest_dir], 0
        je show_usage

        ; Try to create dest directory (may already exist — that's OK)
        mov eax, SYS_MKDIR
        mov ebx, dest_dir
        int 0x80

        ; Save current directory
        mov eax, SYS_GETCWD
        mov ebx, saved_cwd
        int 0x80

        ; Iterate directory entries
        mov dword [file_count], 0
        mov dword [byte_count], 0
        mov dword [dir_index], 0

.scan_loop:
        mov eax, SYS_READDIR
        mov ebx, name_buf
        mov ecx, [dir_index]
        int 0x80

        cmp eax, -1
        je .scan_done

        cmp eax, 0              ; free slot
        je .scan_next

        cmp eax, FTYPE_DIR      ; skip directories
        je .scan_next

        ; Save file type
        mov [cur_type], al

        ; Save file size from ECX
        mov [cur_size], ecx

        ; Read the file
        push rax
        mov eax, SYS_FREAD
        mov ebx, name_buf
        mov ecx, file_buf
        int 0x80
        mov [read_bytes], eax
        pop rax

        cmp dword [read_bytes], 0
        je .scan_next

        ; Build destination path: destdir/filename
        mov edi, dest_path
        mov esi, dest_dir
.cp_dir:
        lodsb
        test al, al
        jz .cp_slash
        stosb
        jmp .cp_dir
.cp_slash:
        mov byte [edi], '/'
        inc edi
        mov esi, name_buf
.cp_name:
        lodsb
        stosb                    ; includes null terminator
        test al, al
        jnz .cp_name

        ; Write file to destination
        mov eax, SYS_FWRITE
        mov ebx, dest_path
        mov ecx, file_buf
        mov edx, [read_bytes]
        movzx esi, byte [cur_type]
        int 0x80
        cmp eax, -1
        je .copy_err

        ; Count it
        inc dword [file_count]
        mov eax, [read_bytes]
        add [byte_count], eax

        ; Verbose output
        cmp byte [verbose], 0
        je .scan_next
        push qword [dir_index]
        mov eax, SYS_PRINT
        mov ebx, name_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_arrow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, dest_path
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop qword [dir_index]
        jmp .scan_next

.copy_err:
        push qword [dir_index]
        mov eax, SYS_PRINT
        mov ebx, msg_copy_err
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, name_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop qword [dir_index]

.scan_next:
        inc dword [dir_index]
        cmp dword [dir_index], MAX_ENTRIES
        jl .scan_loop

.scan_done:
        ; Restore CWD
        mov eax, SYS_CHDIR
        mov ebx, saved_cwd
        int 0x80

        ; Print summary
        mov eax, SYS_PRINT
        mov ebx, msg_backed
        int 0x80
        mov eax, [file_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_files_to
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, dest_dir
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_paren
        int 0x80
        mov eax, [byte_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_bytes_nl
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

show_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -------------------------------------------------------------------
; Helpers
; -------------------------------------------------------------------
skip_sp:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_sp
.done:  ret

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

; -------------------------------------------------------------------
; Strings
; -------------------------------------------------------------------
msg_usage:      db "Usage: backup [-v] <destdir>", 0x0A
                db "  Copies all files from CWD to destdir.", 0x0A, 0
msg_arrow:      db " -> ", 0
msg_backed:     db "Backed up ", 0
msg_files_to:   db " file(s) to ", 0
msg_paren:      db " (", 0
msg_bytes_nl:   db " bytes)", 0x0A, 0
msg_copy_err:   db "backup: failed to copy: ", 0

; -------------------------------------------------------------------
; BSS
; -------------------------------------------------------------------
verbose:        db 0
cur_type:       db 0
cur_size:       dd 0
read_bytes:     dd 0
file_count:     dd 0
byte_count:     dd 0
dir_index:      dd 0
arg_buf:        times 256 db 0
name_buf:       times 256 db 0
dest_dir:       times 256 db 0
dest_path:      times 512 db 0
saved_cwd:      times 256 db 0
file_buf:       times MAX_FILE db 0
