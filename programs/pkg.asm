; pkg.asm - Mellivora Package Manager
; List, install, remove, and search programs on disk.
;
; Usage: pkg list              - List installed programs
;        pkg info <name>       - Show program info
;        pkg size              - Show disk usage summary
;        pkg search <pattern>  - Search for programs by name
;        pkg install <url>     - Download and install from HTTP URL
;        pkg remove <name>     - Remove (delete) a file from disk

%include "syscalls.inc"
%include "lib/net.inc"
%include "lib/http.inc"

MAX_ENTRIES     equ 200
ENTRY_SIZE      equ 288

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        ; Parse command
        mov esi, arg_buf
        call skip_spaces

        cmp byte [esi], 0
        je .show_help

        ; Check subcommands
        mov edi, cmd_list
        call match_cmd
        cmp eax, 1
        je do_list

        mov edi, cmd_info
        call match_cmd
        cmp eax, 1
        je do_info

        mov edi, cmd_size
        call match_cmd
        cmp eax, 1
        je do_size

        mov edi, cmd_search
        call match_cmd
        cmp eax, 1
        je do_search

        mov edi, cmd_install
        call match_cmd
        cmp eax, 1
        je do_install

        mov edi, cmd_remove
        call match_cmd
        cmp eax, 1
        je do_remove

        mov edi, cmd_help
        call match_cmd
        cmp eax, 1
        je .show_help

.show_help:
        mov eax, SYS_PRINT
        mov ebx, help_text
        int 0x80
        jmp exit

;---------------------------------------
; do_list - List all files on disk
;---------------------------------------
do_list:
        mov eax, SYS_PRINT
        mov ebx, msg_list_hdr
        int 0x80

        xor ebp, ebp           ; index
        xor edi, edi           ; file count
        mov dword [total_size], 0

.list_loop:
        mov eax, SYS_READDIR
        mov ebx, dirent_buf
        mov ecx, ebp
        int 0x80

        cmp eax, -1            ; end of directory
        je .list_done

        cmp eax, 0             ; free entry
        je .list_next

        ; Got a file entry
        inc edi

        ; Print type indicator
        push edi
        push ebp
        push ecx               ; ECX = file size from readdir

        ; Type character
        cmp eax, 1
        je .lt_text
        cmp eax, 2
        je .lt_dir
        cmp eax, 3
        je .lt_exec
        cmp eax, 4
        je .lt_batch
        mov ebx, type_unk
        jmp .lt_print
.lt_text:
        mov ebx, type_text
        jmp .lt_print
.lt_dir:
        mov ebx, type_dir
        jmp .lt_print
.lt_exec:
        mov ebx, type_exec
        jmp .lt_print
.lt_batch:
        mov ebx, type_batch
.lt_print:
        mov eax, SYS_PRINT
        int 0x80

        ; Print filename
        mov eax, SYS_PRINT
        mov ebx, dirent_buf
        int 0x80

        ; Pad filename to 24 chars
        mov esi, dirent_buf
        xor ecx, ecx
.pad_count:
        cmp byte [esi], 0
        je .pad_do
        inc esi
        inc ecx
        jmp .pad_count
.pad_do:
        cmp ecx, 24
        jge .pad_done
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc ecx
        jmp .pad_do
.pad_done:
        ; Print size
        pop ecx                 ; file size
        push ecx
        mov eax, ecx
        call print_size

        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        pop ecx
        add [total_size], ecx
        pop ebp
        pop edi

.list_next:
        inc ebp
        cmp ebp, MAX_ENTRIES
        jl .list_loop

.list_done:
        ; Print summary
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, edi
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_files_total
        int 0x80
        mov eax, [total_size]
        call print_size
        mov eax, SYS_PRINT
        mov ebx, msg_total_end
        int 0x80
        jmp exit

;---------------------------------------
; do_info - Show info about a specific file
;---------------------------------------
do_info:
        call skip_spaces
        cmp byte [esi], 0
        je .info_usage

        ; Save name pointer
        mov [search_name], esi

        ; Search directory entries
        xor ebp, ebp
.info_loop:
        mov eax, SYS_READDIR
        mov ebx, dirent_buf
        mov ecx, ebp
        int 0x80

        cmp eax, -1
        je .info_notfound

        cmp eax, 0
        je .info_next

        ; Compare name
        push eax
        push ecx
        mov edi, dirent_buf
        mov esi, [search_name]
        call str_eq
        pop ecx
        pop eax
        cmp edx, 1
        je .info_found

.info_next:
        inc ebp
        cmp ebp, MAX_ENTRIES
        jl .info_loop

.info_notfound:
        mov eax, SYS_PRINT
        mov ebx, msg_not_found
        int 0x80
        jmp exit

.info_found:
        ; EAX = type, ECX = size
        push ecx
        push eax

        mov eax, SYS_PRINT
        mov ebx, msg_info_name
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, dirent_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Type
        mov eax, SYS_PRINT
        mov ebx, msg_info_type
        int 0x80
        pop eax
        cmp eax, 1
        je .it_text
        cmp eax, 2
        je .it_dir
        cmp eax, 3
        je .it_exec
        cmp eax, 4
        je .it_batch
        mov ebx, itype_unk
        jmp .it_print
.it_text:
        mov ebx, itype_text
        jmp .it_print
.it_dir:
        mov ebx, itype_dir
        jmp .it_print
.it_exec:
        mov ebx, itype_exec
        jmp .it_print
.it_batch:
        mov ebx, itype_batch
.it_print:
        mov eax, SYS_PRINT
        int 0x80

        ; Size
        mov eax, SYS_PRINT
        mov ebx, msg_info_size
        int 0x80
        pop ecx
        mov eax, ecx
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_bytes
        int 0x80

        ; Size in KB
        mov eax, SYS_PRINT
        mov ebx, msg_info_sizek
        int 0x80
        mov eax, ecx
        add eax, 512           ; round
        shr eax, 10            ; /1024
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_kb
        int 0x80

        ; Disk blocks
        mov eax, SYS_PRINT
        mov ebx, msg_info_blocks
        int 0x80
        mov eax, ecx
        add eax, 8191
        shr eax, 13            ; /8192
        inc eax                ; at least 1
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        jmp exit

.info_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_info_usage
        int 0x80
        jmp exit

;---------------------------------------
; do_size - Show disk usage summary
;---------------------------------------
do_size:
        mov eax, SYS_PRINT
        mov ebx, msg_size_hdr
        int 0x80

        xor ebp, ebp
        xor edi, edi            ; total files
        mov dword [total_size], 0
        mov dword [count_text], 0
        mov dword [count_exec], 0
        mov dword [count_dir], 0
        mov dword [count_batch], 0
        mov dword [count_other], 0

.size_loop:
        mov eax, SYS_READDIR
        mov ebx, dirent_buf
        mov ecx, ebp
        int 0x80

        cmp eax, -1
        je .size_done
        cmp eax, 0
        je .size_next

        inc edi
        add [total_size], ecx

        cmp eax, 1
        je .sz_text
        cmp eax, 2
        je .sz_dir
        cmp eax, 3
        je .sz_exec
        cmp eax, 4
        je .sz_batch
        inc dword [count_other]
        jmp .size_next
.sz_text:
        inc dword [count_text]
        jmp .size_next
.sz_dir:
        inc dword [count_dir]
        jmp .size_next
.sz_exec:
        inc dword [count_exec]
        jmp .size_next
.sz_batch:
        inc dword [count_batch]

.size_next:
        inc ebp
        cmp ebp, MAX_ENTRIES
        jl .size_loop

.size_done:
        ; Print stats
        mov eax, SYS_PRINT
        mov ebx, msg_sz_total
        int 0x80
        mov eax, edi
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_sz_exec
        int 0x80
        mov eax, [count_exec]
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_sz_text
        int 0x80
        mov eax, [count_text]
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_sz_dir
        int 0x80
        mov eax, [count_dir]
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_sz_batch
        int 0x80
        mov eax, [count_batch]
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_sz_other
        int 0x80
        mov eax, [count_other]
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_sz_disk
        int 0x80
        mov eax, [total_size]
        call print_size
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        jmp exit

;---------------------------------------
; do_search - Search files by pattern
;---------------------------------------
do_search:
        call skip_spaces
        cmp byte [esi], 0
        je .search_usage

        mov [search_name], esi
        xor ebp, ebp
        xor edi, edi            ; match count

        mov eax, SYS_PRINT
        mov ebx, msg_search_hdr
        int 0x80

.search_loop:
        mov eax, SYS_READDIR
        mov ebx, dirent_buf
        mov ecx, ebp
        int 0x80

        cmp eax, -1
        je .search_done
        cmp eax, 0
        je .search_next

        ; Check if filename contains search string
        push eax
        push ecx
        mov edi, dirent_buf
        mov esi, [search_name]
        call str_contains
        pop ecx
        pop eax
        cmp edx, 1
        jne .search_next

        ; Match found
        push ecx
        push eax

        ; Type indicator
        cmp eax, 1
        je .sr_text
        cmp eax, 2
        je .sr_dir
        cmp eax, 3
        je .sr_exec
        cmp eax, 4
        je .sr_batch
        mov ebx, type_unk
        jmp .sr_print
.sr_text:
        mov ebx, type_text
        jmp .sr_print
.sr_dir:
        mov ebx, type_dir
        jmp .sr_print
.sr_exec:
        mov ebx, type_exec
        jmp .sr_print
.sr_batch:
        mov ebx, type_batch
.sr_print:
        mov eax, SYS_PRINT
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, dirent_buf
        int 0x80

        ; Pad to 24
        push edi
        mov esi, dirent_buf
        xor ecx, ecx
.sr_pad:
        cmp byte [esi], 0
        je .sr_pad_do
        inc esi
        inc ecx
        jmp .sr_pad
.sr_pad_do:
        cmp ecx, 24
        jge .sr_pad_done
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc ecx
        jmp .sr_pad_do
.sr_pad_done:
        pop edi

        pop eax
        pop ecx
        mov eax, ecx
        call print_size
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc edi

.search_next:
        inc ebp
        cmp ebp, MAX_ENTRIES
        jl .search_loop

.search_done:
        cmp edi, 0
        jne .search_count
        mov eax, SYS_PRINT
        mov ebx, msg_no_match
        int 0x80
        jmp exit
.search_count:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, edi
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_matches
        int 0x80
        jmp exit

.search_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_search_usage
        int 0x80
        jmp exit

;---------------------------------------
; do_remove - Delete a file from disk
; Usage: pkg remove <filename>
;---------------------------------------
do_remove:
        call skip_spaces
        cmp byte [esi], 0
        je .remove_usage

        ; Copy filename from ESI into filename_buf
        mov edi, filename_buf
        xor ecx, ecx
.copy_name:
        mov al, [esi]
        cmp al, ' '
        je .name_done
        cmp al, 0
        je .name_done
        mov [edi], al
        inc edi
        inc esi
        inc ecx
        cmp ecx, 127
        jb .copy_name
.name_done:
        mov byte [edi], 0

        cmp byte [filename_buf], 0
        je .remove_usage

        ; Delete via SYS_DELETE
        mov eax, SYS_DELETE
        mov ebx, filename_buf
        int 0x80
        cmp eax, -1
        je .remove_fail

        mov eax, SYS_PRINT
        mov ebx, msg_removed
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, filename_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp exit

.remove_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_remove_fail
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, filename_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp exit

.remove_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_remove_usage
        int 0x80
        jmp exit

;---------------------------------------
; do_install - Download and install from HTTP URL
; Usage: pkg install http://host/path
;---------------------------------------
do_install:
        call skip_spaces
        cmp byte [esi], 0
        je .install_usage

        ; Parse URL: must start with "http://"
        push esi
        mov edi, install_http_prefix
.url_check:
        mov al, [edi]
        cmp al, 0
        je .url_prefix_ok
        cmp al, [esi]
        jne .url_bad
        inc esi
        inc edi
        jmp .url_check
.url_bad:
        pop esi
        mov eax, SYS_PRINT
        mov ebx, msg_install_badurl
        int 0x80
        jmp exit
.url_prefix_ok:
        pop eax                 ; discard saved ESI

        ; Extract hostname (up to first '/')
        mov edi, install_host
        xor ecx, ecx
.host_copy:
        mov al, [esi]
        cmp al, '/'
        je .host_done
        cmp al, 0
        je .host_done
        mov [edi], al
        inc edi
        inc esi
        inc ecx
        cmp ecx, 127
        jb .host_copy
.host_done:
        mov byte [edi], 0

        ; Extract path (starting at '/')
        mov edi, install_path
        cmp byte [esi], '/'
        jne .path_default
        xor ecx, ecx
.path_copy:
        mov al, [esi]
        cmp al, ' '
        je .path_end
        cmp al, 0
        je .path_end
        mov [edi], al
        inc edi
        inc esi
        inc ecx
        cmp ecx, 255
        jb .path_copy
.path_end:
        mov byte [edi], 0
        jmp .path_ready
.path_default:
        mov byte [install_path], '/'
        mov byte [install_path+1], 0
.path_ready:

        ; Derive filename from last '/' component of path
        mov esi, install_path
        mov ebx, install_path
.find_slash:
        mov al, [esi]
        cmp al, 0
        je .slash_done
        cmp al, '/'
        jne .slash_next
        lea ebx, [esi + 1]
.slash_next:
        inc esi
        jmp .find_slash
.slash_done:
        mov esi, ebx
        mov edi, filename_buf
        xor ecx, ecx
.fname_copy:
        mov al, [esi]
        cmp al, 0
        je .fname_done
        mov [edi], al
        inc edi
        inc esi
        inc ecx
        cmp ecx, 127
        jb .fname_copy
.fname_done:
        mov byte [edi], 0

        cmp byte [filename_buf], 0
        je .install_usage

        ; Print download status
        mov eax, SYS_PRINT
        mov ebx, msg_install_from
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, install_host
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, install_path
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; DNS resolve hostname
        mov esi, install_host
        call net_dns
        test eax, eax
        jz .install_dns_fail
        mov [install_ip], eax

        ; Set Host header for virtual hosting
        mov dword [http_req_host], install_host

        ; HTTP GET request
        mov eax, [install_ip]
        mov ebx, 80
        mov ecx, install_path
        mov edx, http_resp_buf
        mov esi, 65535
        call http_get
        cmp eax, -1
        je .install_conn_fail
        test eax, eax
        jz .install_conn_fail
        mov [install_size], eax

        ; Detect file type by extension
        mov esi, filename_buf
        mov dword [install_ftype], 1    ; default: text
.ext_scan:
        mov al, [esi]
        cmp al, 0
        je .ext_scan_done
        inc esi
        jmp .ext_scan
.ext_scan_done:
        ; Check for .bin -> executable
        cmp byte [esi - 4], '.'
        jne .check_bat
        cmp byte [esi - 3], 'b'
        jne .check_bat
        cmp byte [esi - 2], 'i'
        jne .check_bat
        cmp byte [esi - 1], 'n'
        jne .check_bat
        mov dword [install_ftype], 3
        jmp .do_write
.check_bat:
        ; Check for .bat -> batch
        cmp byte [esi - 4], '.'
        jne .do_write
        cmp byte [esi - 3], 'b'
        jne .do_write
        cmp byte [esi - 2], 'a'
        jne .do_write
        cmp byte [esi - 1], 't'
        jne .do_write
        mov dword [install_ftype], 4
.do_write:
        mov eax, SYS_FWRITE
        mov ebx, filename_buf
        mov ecx, http_resp_buf
        mov edx, [install_size]
        mov esi, [install_ftype]
        int 0x80
        cmp eax, -1
        je .install_write_fail

        ; Success
        mov eax, SYS_PRINT
        mov ebx, msg_install_ok
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, filename_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, [install_size]
        call print_size
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp exit

.install_dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_install_dns
        int 0x80
        jmp exit

.install_conn_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_install_conn
        int 0x80
        jmp exit

.install_write_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_install_write
        int 0x80
        jmp exit

.install_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_install_usage
        int 0x80
        jmp exit

exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=== Utility functions ===

;---------------------------------------
; match_cmd - Check if ESI starts with string at EDI
; Returns: EAX=1 if match (and ESI advanced past command), 0 if not
;---------------------------------------
match_cmd:
        push edi
        push esi
.mc_loop:
        mov al, [edi]
        cmp al, 0
        je .mc_match
        mov ah, [esi]
        ; Case-insensitive
        or al, 0x20
        or ah, 0x20
        cmp al, ah
        jne .mc_fail
        inc esi
        inc edi
        jmp .mc_loop
.mc_match:
        ; Check that next char in input is space or null
        mov al, [esi]
        cmp al, ' '
        je .mc_skip_space
        cmp al, 0
        je .mc_ok
        jmp .mc_fail
.mc_skip_space:
        inc esi
.mc_ok:
        pop eax             ; discard saved ESI
        pop edi
        mov eax, 1
        ret
.mc_fail:
        pop esi
        pop edi
        xor eax, eax
        ret

;---------------------------------------
; skip_spaces - Advance ESI past spaces
;---------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

;---------------------------------------
; str_eq - Compare null-terminated strings EDI and ESI
; Returns: EDX=1 if equal
;---------------------------------------
str_eq:
        push esi
        push edi
.se_loop:
        mov al, [esi]
        mov ah, [edi]
        cmp al, ah
        jne .se_no
        cmp al, 0
        je .se_yes
        inc esi
        inc edi
        jmp .se_loop
.se_yes:
        mov edx, 1
        pop edi
        pop esi
        ret
.se_no:
        xor edx, edx
        pop edi
        pop esi
        ret

;---------------------------------------
; str_contains - Check if string at EDI contains substring at ESI
; Returns: EDX=1 if found
;---------------------------------------
str_contains:
        push esi
        push edi
        push ebx
        mov ebx, esi           ; save pattern start
.sc_outer:
        cmp byte [edi], 0
        je .sc_no
        mov esi, ebx
.sc_inner:
        cmp byte [esi], 0
        je .sc_yes             ; end of pattern = match
        mov al, [edi]
        cmp al, 0
        je .sc_no
        ; Case-insensitive compare
        mov ah, [esi]
        or al, 0x20
        or ah, 0x20
        cmp al, ah
        jne .sc_next
        inc esi
        inc edi
        jmp .sc_inner
.sc_next:
        sub edi, esi
        add edi, ebx           ; reset to start+1
        inc edi
        jmp .sc_outer
.sc_yes:
        mov edx, 1
        pop ebx
        pop edi
        pop esi
        ret
.sc_no:
        xor edx, edx
        pop ebx
        pop edi
        pop esi
        ret

;---------------------------------------
; print_size - Print EAX as human-readable size
;---------------------------------------
print_size:
        pushad
        cmp eax, 1048576
        jge .ps_mb
        cmp eax, 1024
        jge .ps_kb
        ; Bytes
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, unit_b
        int 0x80
        popad
        ret
.ps_kb:
        add eax, 512
        shr eax, 10
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, unit_kb
        int 0x80
        popad
        ret
.ps_mb:
        add eax, 524288
        shr eax, 20
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, unit_mb
        int 0x80
        popad
        ret

;---------------------------------------
; print_decimal - Print EAX as decimal number
;---------------------------------------
print_decimal:
        pushad
        cmp eax, 0
        jne .pd_nonzero
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        popad
        ret
.pd_nonzero:
        xor ecx, ecx
        mov ebx, 10
.pd_div:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        cmp eax, 0
        jne .pd_div
.pd_out:
        pop ebx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .pd_out
        popad
        ret

;=======================================
; Data
;=======================================

cmd_list:       db "list", 0
cmd_info:       db "info", 0
cmd_size:       db "size", 0
cmd_search:     db "search", 0
cmd_install:    db "install", 0
cmd_remove:     db "remove", 0
cmd_help:       db "help", 0

help_text:
        db "Mellivora Package Manager v1.0", 10
        db "Usage: pkg <command> [args]", 10, 10
        db "Commands:", 10
        db "  list              List all files on disk", 10
        db "  info <name>       Show detailed file info", 10
        db "  size              Show disk usage summary", 10
        db "  search <pattern>  Search files by name", 10
        db "  install <url>     Download and install from HTTP URL", 10
        db "  remove <name>     Remove a file from disk", 10
        db "  help              Show this help", 10, 0

msg_list_hdr:
        db "     Name                     Size", 10
        db "     ----                     ----", 10, 0

type_text:      db " [T] ", 0
type_exec:      db " [X] ", 0
type_dir:       db " [D] ", 0
type_batch:     db " [B] ", 0
type_unk:       db " [?] ", 0

itype_text:     db "Text file", 10, 0
itype_exec:     db "Executable", 10, 0
itype_dir:      db "Directory", 10, 0
itype_batch:    db "Batch script", 10, 0
itype_unk:      db "Unknown", 10, 0

msg_files_total:   db " files, total size: ", 0
msg_total_end:     db 10, 0
msg_not_found:     db "File not found.", 10, 0
msg_info_name:     db "  Name:   ", 0
msg_info_type:     db "  Type:   ", 0
msg_info_size:     db "  Size:   ", 0
msg_info_sizek:    db "          ", 0
msg_info_blocks:   db "  Blocks: ", 0
msg_info_usage:    db "Usage: pkg info <filename>", 10, 0
msg_bytes:         db " bytes", 10, 0
msg_kb:            db " KB", 10, 0
msg_size_hdr:      db "Disk Usage Summary", 10
                   db "==================", 10, 0
msg_sz_total:      db "  Total files:    ", 0
msg_sz_exec:       db "  Executables:    ", 0
msg_sz_text:       db "  Text files:     ", 0
msg_sz_dir:        db "  Directories:    ", 0
msg_sz_batch:      db "  Batch scripts:  ", 0
msg_sz_other:      db "  Other:          ", 0
msg_sz_disk:       db "  Total disk use: ", 0
msg_search_hdr:    db "Search results:", 10, 0
msg_no_match:      db "No matching files found.", 10, 0
msg_matches:       db " match(es) found.", 10, 0
msg_search_usage:  db "Usage: pkg search <pattern>", 10, 0
msg_removed:       db "Removed: ", 0
msg_remove_fail:   db "pkg: cannot remove: ", 0
msg_remove_usage:  db "Usage: pkg remove <filename>", 10, 0
msg_install_from:  db "Downloading ", 0
msg_install_ok:    db "Installed: ", 0
msg_install_dns:   db "pkg: DNS resolution failed", 10, 0
msg_install_conn:  db "pkg: connection failed or empty response", 10, 0
msg_install_write: db "pkg: disk write failed (disk full?)", 10, 0
msg_install_badurl:db "pkg: URL must start with http://", 10, 0
msg_install_usage: db "Usage: pkg install http://host/path/file.bin", 10, 0
install_http_prefix: db "http://", 0

unit_b:    db " B", 0
unit_kb:   db " KB", 0
unit_mb:   db " MB", 0

; BSS
arg_buf:        times 256 db 0
dirent_buf:     times 288 db 0
filename_buf:   times 128 db 0
search_name:    dd 0
total_size:     dd 0
count_text:     dd 0
count_exec:     dd 0
count_dir:      dd 0
count_batch:    dd 0
count_other:    dd 0
install_ip:     dd 0
install_size:   dd 0
install_ftype:  dd 1
install_host:   times 128 db 0
install_path:   times 256 db 0
; HTTP library buffers (required by lib/http.inc)
http_req_buf:   times 512 db 0
http_resp_buf:  times 65536 db 0
