; tar.asm - Simple tape archive utility for Mellivora OS
; Format: HBTAR archive (custom flat binary archive)
;   Header:  magic[8] + entry_count[4]
;   Entry:   name[256] + size[4] + data[size]
; Usage:
;   tar c archive.tar file1 [file2 ...]   -- create archive
;   tar x archive.tar                     -- extract files
;   tar t archive.tar                     -- list contents
%include "syscalls.inc"

MAGIC_LEN       equ 8
HDR_SIZE        equ 12          ; magic(8) + count(4)
ENTRY_NAME_LEN  equ 256
ENTRY_HDR_SIZE  equ 260         ; name(256) + size(4)
MAX_FILES       equ 8
MAX_FILE_SIZE   equ 65536       ; 64 KB per file
MAX_ARCHIVE     equ (HDR_SIZE + MAX_FILES * (ENTRY_HDR_SIZE + MAX_FILE_SIZE))

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        lea esi, [arg_buf]
        call skip_self          ; skip argv[0]
        call next_arg           ; get command char
        test eax, eax
        jz .usage

        mov cl, [eax]
        cmp cl, 'c'
        je .do_create
        cmp cl, 'x'
        je .do_extract
        cmp cl, 't'
        je .do_list
        ; also allow -c/-x/-t
        cmp cl, '-'
        jne .usage
        mov cl, [eax + 1]
        cmp cl, 'c'
        je .do_create
        cmp cl, 'x'
        je .do_extract
        cmp cl, 't'
        je .do_list

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.do_create:
        call next_arg
        test eax, eax
        jz .usage
        ; save archive name
        push eax
        lea edi, [archive_name]
        mov [esi_save], esi
        mov esi, eax
.cp_arc:
        lodsb
        stosb
        test al, al
        jnz .cp_arc
        mov esi, [esi_save]
        pop eax
        call create_archive
        jmp .done

.do_extract:
        call next_arg
        test eax, eax
        jz .usage
        lea edi, [archive_name]
        push esi
        mov esi, eax
.cp_arc2: lodsb
stosb
test al, al
jnz .cp_arc2
        pop esi
        call extract_archive
        jmp .done

.do_list:
        call next_arg
        test eax, eax
        jz .usage
        lea edi, [archive_name]
        push esi
        mov esi, eax
.cp_arc3: lodsb
stosb
test al, al
jnz .cp_arc3
        pop esi
        call list_archive
        jmp .done

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;-----------------------------------------------
; create_archive - read files from args, write archive
create_archive:
        pushad
        ; Build archive in archive_buf
        lea edi, [archive_buf]
        ; Write magic
        lea esi, [magic_str]
        mov ecx, MAGIC_LEN
        rep movsb
        ; Skip count for now (fill in later)
        mov [edi], dword 0
        add edi, 4
        mov dword [entry_count], 0

.ca_next_file:
        call next_arg
        test eax, eax
        jz .ca_flush

        ; Save filename
        push esi
        mov esi, eax
        lea edi, [entry_name_buf]
.ca_fn_cp:
        lodsb
        stosb
        test al, al
        jnz .ca_fn_cp
        pop esi

        ; Read file
        mov eax, SYS_FREAD
        lea ebx, [entry_name_buf]
        lea ecx, [file_io_buf]
        mov edx, MAX_FILE_SIZE
        int 0x80
        test eax, eax
        js .ca_skip_file    ; skip unreadable files

        mov [file_io_size], eax

        ; Check archive space
        lea ecx, [archive_buf]
        mov edx, edi
        sub edx, ecx
        add edx, ENTRY_HDR_SIZE
        add edx, [file_io_size]
        cmp edx, MAX_ARCHIVE
        jge .ca_full

        ; Write entry header: name (256 bytes padded)
        ; Get current edi (points to next write position in archive_buf)
        push eax
        lea esi, [entry_name_buf]
        mov ecx, ENTRY_NAME_LEN
.ca_name_cp:
        cmp ecx, 0
        je .ca_name_done
        mov al, [esi]
        stosb
        test al, al
        jnz .ca_name_inc
        ; Pad rest with zeros
        dec ecx
.ca_pad: stosb
dec ecx
jns .ca_pad
        jmp .ca_name_done
.ca_name_inc:
        inc esi
        dec ecx
        jmp .ca_name_cp
.ca_name_done:
        pop eax

        ; Write size (4 bytes)
        mov eax, [file_io_size]
        stosd

        ; Write data
        lea esi, [file_io_buf]
        mov ecx, [file_io_size]
        rep movsb

        inc dword [entry_count]
        jmp .ca_next_file

.ca_skip_file:
        mov eax, SYS_PRINT
        lea ebx, [entry_name_buf]
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_skip
        int 0x80
        jmp .ca_next_file

.ca_full:
        mov eax, SYS_PRINT
        mov ebx, err_full
        int 0x80
        jmp .ca_flush

.ca_flush:
        ; Patch entry count into header
        mov eax, [entry_count]
        mov [archive_buf + MAGIC_LEN], eax

        ; Compute total archive size
        lea ecx, [archive_buf]
        sub edi, ecx
        mov edx, edi    ; total bytes

        ; Write archive file
        mov eax, SYS_FWRITE
        lea ebx, [archive_name]
        lea ecx, [archive_buf]
        mov esi, 0              ; type = binary (text)
        int 0x80
        test eax, eax
        js .ca_write_err

        ; Print success
        mov eax, SYS_PRINT
        mov ebx, msg_created
        int 0x80
        mov eax, [entry_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_files
        int 0x80
        popad
        ret

.ca_write_err:
        mov eax, SYS_PRINT
        mov ebx, err_write
        int 0x80
        popad
        ret

;-----------------------------------------------
; extract_archive - read archive, write files
extract_archive:
        pushad
        ; Read archive
        mov eax, SYS_FREAD
        lea ebx, [archive_name]
        lea ecx, [archive_buf]
        int 0x80
        test eax, eax
        js .ex_read_err
        mov [archive_size], eax

        ; Check magic
        lea esi, [archive_buf]
        lea edi, [magic_str]
        mov ecx, MAGIC_LEN
        repe cmpsb
        jne .ex_bad_magic

        ; Read entry count
        lodsd
        mov [entry_count], eax
        mov [extract_count], dword 0

        ; esi now points to first entry
.ex_loop:
        mov eax, [extract_count]
        cmp eax, [entry_count]
        jge .ex_done

        ; Read entry name (256 bytes)
        lea edi, [entry_name_buf]
        mov ecx, ENTRY_NAME_LEN
        rep movsb

        ; Read entry size
        lodsd
        mov [file_io_size], eax

        ; Bounds check
        lea ecx, [archive_buf]
        mov edx, esi
        sub edx, ecx
        add edx, eax
        cmp edx, [archive_size]
        jg .ex_corrupt

        ; Print extracting message
        push eax
        mov eax, SYS_PRINT
        mov ebx, msg_extract
        int 0x80
        lea ebx, [entry_name_buf]
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop eax

        ; Copy file data to file_io_buf
        push esi
        mov ecx, [file_io_size]
        lea edi, [file_io_buf]
        rep movsb
        pop esi
        add esi, [file_io_size]

        ; Write extracted file
        push esi
        mov eax, SYS_FWRITE
        lea ebx, [entry_name_buf]
        lea ecx, [file_io_buf]
        mov edx, [file_io_size]
        mov esi, 0
        int 0x80
        pop esi

        inc dword [extract_count]
        jmp .ex_loop

.ex_done:
        mov eax, SYS_PRINT
        mov ebx, msg_extracted
        int 0x80
        mov eax, [extract_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_files
        int 0x80
        popad
        ret

.ex_read_err:
        mov eax, SYS_PRINT
        mov ebx, err_read
        int 0x80
        popad
        ret

.ex_bad_magic:
        mov eax, SYS_PRINT
        mov ebx, err_magic
        int 0x80
        popad
        ret

.ex_corrupt:
        mov eax, SYS_PRINT
        mov ebx, err_corrupt
        int 0x80
        popad
        ret

;-----------------------------------------------
; list_archive - list files in archive
list_archive:
        pushad
        mov eax, SYS_FREAD
        lea ebx, [archive_name]
        lea ecx, [archive_buf]
        int 0x80
        test eax, eax
        js .la_read_err

        lea esi, [archive_buf]
        lea edi, [magic_str]
        mov ecx, MAGIC_LEN
        repe cmpsb
        jne .la_bad_magic

        lodsd
        mov [entry_count], eax

        ; Print header
        mov eax, SYS_PRINT
        mov ebx, list_hdr
        int 0x80

        xor ecx, ecx    ; file index
.la_loop:
        cmp ecx, [entry_count]
        jge .la_done
        push ecx

        ; Read name
        lea edi, [entry_name_buf]
        push esi
        mov ecx, ENTRY_NAME_LEN
        rep movsb
        pop esi
        add esi, ENTRY_NAME_LEN

        ; Read size
        lodsd
        push eax

        ; Print: "  filename  (size bytes)"
        mov eax, SYS_PRINT
        mov ebx, msg_list_indent
        int 0x80
        lea ebx, [entry_name_buf]
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, size_open
        int 0x80
        pop eax
        push eax
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, size_close
        int 0x80

        ; Advance esi past data
        pop eax
        add esi, eax

        pop ecx
        inc ecx
        jmp .la_loop

.la_done:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, [entry_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_files
        int 0x80
        popad
        ret

.la_read_err:
        mov eax, SYS_PRINT
        mov ebx, err_read
        int 0x80
        popad
        ret

.la_bad_magic:
        mov eax, SYS_PRINT
        mov ebx, err_magic
        int 0x80
        popad
        ret

;-----------------------------------------------
; Utility: skip_self - skip first NUL-terminated arg in [esi]
skip_self:
.ss: lodsb
test al, al
jnz .ss
        ret

; next_arg: return EAX=ptr to next arg or 0, advance ESI past it
next_arg:
.na_skip: cmp byte [esi], 0
jne .na_found
        cmp esi, arg_buf + 512
        jge .na_none
        inc esi
        jmp .na_skip
.na_found:
        mov eax, esi
.na_adv: lodsb
test al, al
jnz .na_adv
        ret
.na_none: xor eax, eax
ret

; print_dec is provided by syscalls.inc

;--- Data ---
magic_str:      db "HBTAR1.0"
usage_str:      db "Usage: tar c|x|t archive.tar [files...]", 0x0A, 0
msg_created:    db "Created archive with ", 0
msg_files:      db " file(s)", 0x0A, 0
msg_extract:    db "  extracting: ", 0
msg_extracted:  db "Extracted ", 0
list_hdr:       db "Contents:", 0x0A, 0
msg_list_indent: db "  ", 0
size_open:      db "  (", 0
size_close:     db " bytes)", 0x0A, 0
err_skip:       db ": skipped (unreadable)", 0x0A, 0
err_full:       db "tar: archive too large, some files skipped", 0x0A, 0
err_write:      db "tar: failed to write archive", 0x0A, 0
err_read:       db "tar: cannot read archive", 0x0A, 0
err_magic:      db "tar: not a valid HBTAR archive", 0x0A, 0
err_corrupt:    db "tar: archive appears corrupt", 0x0A, 0
dec_buf:        times 20 db 0   ; unused, kept for alignment

;--- BSS ---
arg_buf:            times 512 db 0
archive_name:       times 256 db 0
entry_name_buf:     times ENTRY_NAME_LEN db 0
entry_count:        dd 0
extract_count:      dd 0
archive_size:       dd 0
file_io_size:       dd 0
esi_save:           dd 0
esi2:               dd 0
file_io_buf:        times MAX_FILE_SIZE db 0
archive_buf:        times MAX_ARCHIVE db 0
