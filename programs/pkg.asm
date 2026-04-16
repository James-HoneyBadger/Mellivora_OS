; pkg.asm - Mellivora Package Manager
; List, install, remove, and download programs.
;
; Usage: pkg list              - List installed programs
;        pkg info <name>       - Show program info
;        pkg size              - Show disk usage summary
;        pkg search <pattern>  - Search for programs by name
;        pkg install <url>     - Download and install from HTTP URL
;        pkg remove <name>     - Remove a file from disk

%include "syscalls.inc"

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
        push rdi
        push rbp
        push rcx               ; ECX = file size from readdir

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
        pop rcx                 ; file size
        push rcx
        mov eax, ecx
        call print_size

        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        pop rcx
        add [total_size], ecx
        pop rbp
        pop rdi

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
        push rax
        push rcx
        mov edi, dirent_buf
        mov esi, [search_name]
        call str_eq
        pop rcx
        pop rax
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
        push rcx
        push rax

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
        pop rax
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
        pop rcx
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
        push rax
        push rcx
        mov edi, dirent_buf
        mov esi, [search_name]
        call str_contains
        pop rcx
        pop rax
        cmp edx, 1
        jne .search_next

        ; Match found
        push rcx
        push rax

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
        push rdi
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
        pop rdi

        pop rax
        pop rcx
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
; do_install - Download file via HTTP and save to /bin
;---------------------------------------
do_install:
        call skip_spaces
        cmp byte [esi], 0
        je .inst_usage

        ; ESI points to URL, e.g. "192.168.1.10/prog"
        ; Parse host and path from URL
        mov edi, http_host
        mov [url_start], esi
.inst_copy_host:
        lodsb
        cmp al, '/'
        je .inst_got_host
        cmp al, ' '
        je .inst_host_only
        test al, al
        jz .inst_host_only
        stosb
        jmp .inst_copy_host
.inst_host_only:
        mov byte [edi], 0
        mov esi, http_path_root
        jmp .inst_copy_path_start
.inst_got_host:
        mov byte [edi], 0
.inst_copy_path_start:
        mov edi, http_path
        mov byte [edi], '/'
        inc edi
.inst_copy_path:
        lodsb
        cmp al, ' '
        je .inst_path_done
        test al, al
        jz .inst_path_done
        stosb
        jmp .inst_copy_path
.inst_path_done:
        mov byte [edi], 0

        ; Extract filename from path (last component)
        mov esi, http_path
        mov edi, esi
.inst_find_name:
        lodsb
        test al, al
        jz .inst_found_name
        cmp al, '/'
        jne .inst_find_name
        mov edi, esi            ; point after last '/'
        jmp .inst_find_name
.inst_found_name:
        mov [inst_filename], edi

        ; Resolve host via DNS
        mov eax, SYS_DNS
        mov ebx, http_host
        int 0x80
        cmp eax, 0
        je .inst_dns_fail
        mov [inst_ip], eax

        ; Create TCP socket
        mov eax, SYS_SOCKET
        mov ebx, 1              ; SOCK_STREAM
        int 0x80
        cmp eax, -1
        je .inst_sock_fail
        mov [inst_sock], eax

        ; Connect to port 80
        mov eax, SYS_CONNECT
        mov ebx, [inst_sock]
        mov ecx, [inst_ip]
        mov edx, 80
        int 0x80
        cmp eax, 0
        jne .inst_conn_fail

        ; Build HTTP GET request
        mov edi, http_req_buf
        mov esi, http_get
        call inst_strcpy
        mov esi, http_path
        call inst_strcpy
        mov esi, http_ver
        call inst_strcpy
        mov esi, http_host_hdr
        call inst_strcpy
        mov esi, http_host
        call inst_strcpy
        mov esi, http_req_end
        call inst_strcpy

        ; Send request
        mov eax, SYS_SEND
        mov ebx, [inst_sock]
        mov ecx, http_req_buf
        mov edx, edi
        sub edx, http_req_buf
        int 0x80

        ; Receive response
        mov dword [inst_total], 0
        mov edi, http_recv_buf
.inst_recv_loop:
        mov eax, SYS_RECV
        mov ebx, [inst_sock]
        mov ecx, edi
        mov edx, 4096
        int 0x80
        cmp eax, 0
        jle .inst_recv_done
        add edi, eax
        add [inst_total], eax
        cmp dword [inst_total], 60000
        jge .inst_recv_done
        jmp .inst_recv_loop

.inst_recv_done:
        mov eax, SYS_SOCKCLOSE
        mov ebx, [inst_sock]
        int 0x80

        cmp dword [inst_total], 0
        je .inst_no_data

        ; Find end of HTTP headers (\r\n\r\n)
        mov esi, http_recv_buf
        mov ecx, [inst_total]
.inst_find_body:
        cmp ecx, 4
        jl .inst_no_body
        cmp dword [esi], 0x0A0D0A0D     ; \r\n\r\n
        je .inst_body_found
        inc esi
        dec ecx
        jmp .inst_find_body

.inst_body_found:
        add esi, 4              ; skip past headers
        mov eax, http_recv_buf
        add eax, [inst_total]
        sub eax, esi            ; body length
        mov [inst_body_len], eax

        ; Save to /bin/<filename>
        mov edi, inst_path_buf
        push rsi
        mov esi, inst_bin_prefix
.inst_cp_prefix:
        lodsb
        test al, al
        jz .inst_cp_name
        stosb
        jmp .inst_cp_prefix
.inst_cp_name:
        mov esi, [inst_filename]
.inst_cp_fname:
        lodsb
        test al, al
        jz .inst_cp_done
        stosb
        jmp .inst_cp_fname
.inst_cp_done:
        mov byte [edi], 0
        pop rsi

        mov eax, SYS_FWRITE
        mov ebx, inst_path_buf
        mov ecx, esi
        mov edx, [inst_body_len]
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_installed
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, inst_path_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_inst_size
        int 0x80
        mov eax, [inst_body_len]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_inst_bytes
        int 0x80
        jmp exit

.inst_dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns_fail
        int 0x80
        jmp exit
.inst_sock_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_sock_fail
        int 0x80
        jmp exit
.inst_conn_fail:
        mov eax, SYS_SOCKCLOSE
        mov ebx, [inst_sock]
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_conn_fail
        int 0x80
        jmp exit
.inst_no_data:
        mov eax, SYS_PRINT
        mov ebx, msg_no_data
        int 0x80
        jmp exit
.inst_no_body:
        mov eax, SYS_PRINT
        mov ebx, msg_no_body
        int 0x80
        jmp exit
.inst_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_inst_usage
        int 0x80
        jmp exit

; Helper: copy null-terminated string from ESI to EDI
inst_strcpy:
        push rax
.is_loop:
        lodsb
        test al, al
        jz .is_done
        stosb
        jmp .is_loop
.is_done:
        pop rax
        ret

;---------------------------------------
; do_remove - Delete a file from disk
;---------------------------------------
do_remove:
        call skip_spaces
        cmp byte [esi], 0
        je .rm_usage

        mov eax, SYS_DELETE
        mov ebx, esi
        int 0x80
        cmp eax, 0
        jne .rm_fail

        mov eax, SYS_PRINT
        mov ebx, msg_removed
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp exit

.rm_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_rm_fail
        int 0x80
        jmp exit
.rm_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_rm_usage
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
        push rdi
        push rsi
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
        pop rax             ; discard saved ESI
        pop rdi
        mov eax, 1
        ret
.mc_fail:
        pop rsi
        pop rdi
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
        push rsi
        push rdi
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
        pop rdi
        pop rsi
        ret
.se_no:
        xor edx, edx
        pop rdi
        pop rsi
        ret

;---------------------------------------
; str_contains - Check if string at EDI contains substring at ESI
; Returns: EDX=1 if found
;---------------------------------------
str_contains:
        push rsi
        push rdi
        push rbx
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
        pop rbx
        pop rdi
        pop rsi
        ret
.sc_no:
        xor edx, edx
        pop rbx
        pop rdi
        pop rsi
        ret

;---------------------------------------
; print_size - Print EAX as human-readable size
;---------------------------------------
print_size:
        PUSHALL
        cmp eax, 1048576
        jge .ps_mb
        cmp eax, 1024
        jge .ps_kb
        ; Bytes
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, unit_b
        int 0x80
        POPALL
        ret
.ps_kb:
        add eax, 512
        shr eax, 10
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, unit_kb
        int 0x80
        POPALL
        ret
.ps_mb:
        add eax, 524288
        shr eax, 20
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, unit_mb
        int 0x80
        POPALL
        ret

;---------------------------------------
; print_decimal - Print EAX as decimal number
;---------------------------------------
print_decimal:
        PUSHALL
        cmp eax, 0
        jne .pd_nonzero
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        POPALL
        ret
.pd_nonzero:
        xor ecx, ecx
        mov ebx, 10
.pd_div:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        cmp eax, 0
        jne .pd_div
.pd_out:
        pop rbx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .pd_out
        POPALL
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
        db "Mellivora Package Manager v2.0", 10
        db "Usage: pkg <command> [args]", 10, 10
        db "Commands:", 10
        db "  list              List all files on disk", 10
        db "  info <name>       Show detailed file info", 10
        db "  size              Show disk usage summary", 10
        db "  search <pattern>  Search files by name", 10
        db "  install <url>     Download & install via HTTP", 10
        db "  remove <name>     Remove a file from disk", 10
        db "  help              Show this help", 10, 0

http_get:       db "GET ", 0
http_ver:       db " HTTP/1.1", 13, 10, 0
http_host_hdr:  db "Host: ", 0
http_req_end:   db 13, 10, "Connection: close", 13, 10, 13, 10, 0
http_path_root: db "/", 0
inst_bin_prefix: db "/bin/", 0
msg_installed:  db "Installed: ", 0
msg_inst_size:  db " (", 0
msg_inst_bytes: db " bytes)", 10, 0
msg_dns_fail:   db "Error: DNS resolution failed.", 10, 0
msg_sock_fail:  db "Error: cannot create socket.", 10, 0
msg_conn_fail:  db "Error: connection failed.", 10, 0
msg_no_data:    db "Error: no data received.", 10, 0
msg_no_body:    db "Error: invalid HTTP response.", 10, 0
msg_inst_usage: db "Usage: pkg install <host/path>", 10, 0
msg_removed:    db "Removed: ", 0
msg_rm_fail:    db "Error: cannot remove file.", 10, 0
msg_rm_usage:   db "Usage: pkg remove <filename>", 10, 0

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

unit_b:    db " B", 0
unit_kb:   db " KB", 0
unit_mb:   db " MB", 0

; BSS
arg_buf:        times 256 db 0
dirent_buf:     times 288 db 0
search_name:    dd 0
total_size:     dd 0
count_text:     dd 0
count_exec:     dd 0
count_dir:      dd 0
count_batch:    dd 0
count_other:    dd 0

; Install data
url_start:      dd 0
inst_filename:  dd 0
inst_sock:      dd 0
inst_ip:        dd 0
inst_total:     dd 0
inst_body_len:  dd 0
http_host:      times 128 db 0
http_path:      times 256 db 0
http_req_buf:   times 512 db 0
inst_path_buf:  times 280 db 0
http_recv_buf:  times 61440 db 0
