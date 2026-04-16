; ==========================================================================
; wget - HTTP file downloader for Mellivora OS
;
; Usage: wget <url> [outfile]
;
; Downloads a file via HTTP GET and saves to disk.
; If outfile is omitted, derives filename from URL path.
;
; Supported URL formats:
;   http://host/path
;   host/path
;   host:port/path
; ==========================================================================
%include "syscalls.inc"
%include "lib/net.inc"

MAX_RECV    equ 1024
MAX_FILE    equ 32768

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf

        ; Strip "http://" prefix if present
        cmp dword [esi], 'http'
        jne .no_http
        cmp byte [esi + 4], ':'
        jne .no_http
        cmp byte [esi + 5], '/'
        jne .no_http
        cmp byte [esi + 6], '/'
        jne .no_http
        add esi, 7
.no_http:

        ; Parse hostname (up to '/', ':', or space)
        mov edi, hostname
.parse_host:
        lodsb
        cmp al, '/'
        je .host_done
        cmp al, ':'
        je .host_port
        cmp al, ' '
        je .host_done_space
        test al, al
        jz .host_done_null
        stosb
        jmp .parse_host
.host_port:
        mov byte [edi], 0
        ; Parse port number
        xor eax, eax
.parse_port:
        movzx edx, byte [esi]
        sub edx, '0'
        cmp edx, 9
        ja .port_done
        imul eax, 10
        add eax, edx
        inc esi
        jmp .parse_port
.port_done:
        mov [port], ax
        cmp byte [esi], '/'
        jne .host_done_null
        inc esi                 ; skip '/'
        jmp .parse_path
.host_done:
        mov byte [edi], 0
        jmp .parse_path
.host_done_space:
        mov byte [edi], 0
        dec esi
        jmp .parse_path_default
.host_done_null:
        mov byte [edi], 0
.parse_path_default:
        ; Default path = "/"
        mov byte [url_path], '/'
        mov byte [url_path + 1], 0
        jmp .check_outfile

.parse_path:
        ; Copy rest as path, prepend '/'
        mov edi, url_path
        mov byte [edi], '/'
        inc edi
.cp_path:
        lodsb
        cmp al, ' '
        je .path_done
        test al, al
        jz .path_done
        stosb
        jmp .cp_path
.path_done:
        mov byte [edi], 0

.check_outfile:
        ; Skip spaces
.skip_sp:
        cmp byte [esi], ' '
        jne .check_arg2
        inc esi
        jmp .skip_sp
.check_arg2:
        cmp byte [esi], 0
        je .derive_outfile
        ; Copy explicit output filename
        mov edi, outfile
.cp_out:
        lodsb
        cmp al, ' '
        je .out_done
        test al, al
        jz .out_done
        stosb
        jmp .cp_out
.out_done:
        mov byte [edi], 0
        jmp .do_download

.derive_outfile:
        ; Get basename from URL path for output filename
        mov esi, url_path
        mov edi, outfile
        xor ebx, ebx            ; last '/' position
.find_slash:
        cmp byte [esi], 0
        je .derive_copy
        cmp byte [esi], '/'
        jne .fs_skip
        lea ebx, [esi + 1]
.fs_skip:
        inc esi
        jmp .find_slash
.derive_copy:
        test ebx, ebx
        jz .use_index
        cmp byte [ebx], 0
        je .use_index
        mov esi, ebx
.dc_cp:
        lodsb
        stosb
        test al, al
        jnz .dc_cp
        jmp .do_download
.use_index:
        mov esi, default_name
.cp_idx:
        lodsb
        stosb
        test al, al
        jnz .cp_idx

.do_download:
        ; Print status
        mov eax, SYS_PRINT
        mov ebx, msg_resolving
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_dots
        int 0x80

        ; Resolve hostname
        mov esi, hostname
        call net_dns
        test eax, eax
        jz .dns_fail
        mov [server_ip], eax

        mov eax, SYS_PRINT
        mov ebx, msg_connecting
        int 0x80

        ; Create TCP socket
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .sock_fail
        mov [sockfd], eax

        ; Connect
        mov eax, [sockfd]
        mov ebx, [server_ip]
        movzx ecx, word [port]
        call net_connect
        cmp eax, -1
        je .connect_fail

        ; Build HTTP GET request
        mov edi, request_buf
        mov esi, str_get
.cp_get:
        lodsb
        stosb
        test al, al
        jnz .cp_get
        dec edi
        ; Path
        mov esi, url_path
.cp_rpath:
        lodsb
        test al, al
        jz .cp_proto
        stosb
        jmp .cp_rpath
.cp_proto:
        mov esi, str_http11
.cp_ver:
        lodsb
        stosb
        test al, al
        jnz .cp_ver
        dec edi
        ; Host header
        mov esi, str_host
.cp_host:
        lodsb
        stosb
        test al, al
        jnz .cp_host
        dec edi
        mov esi, hostname
.cp_hn:
        lodsb
        test al, al
        jz .cp_end
        stosb
        jmp .cp_hn
.cp_end:
        mov esi, str_conn_close
.cp_cc:
        lodsb
        stosb
        test al, al
        jnz .cp_cc
        dec edi

        ; Calculate length
        mov ecx, edi
        sub ecx, request_buf

        ; Send request
        mov eax, SYS_PRINT
        mov ebx, msg_sending
        int 0x80

        mov eax, [sockfd]
        mov ebx, request_buf
        call net_send
        cmp eax, -1
        je .send_fail

        ; Receive response
        mov dword [total_bytes], 0
        mov byte [in_headers], 1
        mov qword [file_ptr], file_buf

.recv_loop:
        mov eax, [sockfd]
        mov ebx, recv_buf
        mov ecx, MAX_RECV
        call net_recv
        cmp eax, -1
        je .recv_done
        cmp eax, 0
        je .recv_retry

        ; Process received data
        mov esi, recv_buf
        mov ecx, eax

        cmp byte [in_headers], 1
        jne .copy_body

        ; Scan for end of headers (\r\n\r\n)
.scan_hdr:
        cmp ecx, 4
        jb .recv_loop
        cmp dword [esi], 0x0A0D0A0D
        je .found_body
        inc esi
        dec ecx
        jmp .scan_hdr

.found_body:
        add esi, 4
        sub ecx, 4
        mov byte [in_headers], 0

.copy_body:
        ; Copy body data to file buffer
        test ecx, ecx
        jz .recv_loop
        mov rdi, [file_ptr]
        ; Check for overflow
        mov eax, edi
        sub eax, file_buf
        add eax, ecx
        cmp eax, MAX_FILE
        jg .recv_done           ; buffer full

        rep movsb
        mov [file_ptr], rdi

        ; Update total
        mov eax, edi
        sub eax, file_buf
        mov [total_bytes], eax
        jmp .recv_loop

.recv_retry:
        ; Sleep briefly and retry
        mov eax, SYS_SLEEP
        mov ebx, 5
        int 0x80
        inc dword [retry_count]
        cmp dword [retry_count], 100
        jl .recv_loop

.recv_done:
        ; Close socket
        mov eax, [sockfd]
        call net_close

        ; Check if we got any data
        cmp dword [total_bytes], 0
        je .no_data

        ; Save to file
        mov eax, SYS_FWRITE
        mov ebx, outfile
        mov ecx, file_buf
        mov edx, [total_bytes]
        xor esi, esi             ; type = text
        int 0x80
        cmp eax, -1
        je .write_fail

        ; Print success
        mov eax, SYS_PRINT
        mov ebx, msg_saved
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, outfile
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_paren
        int 0x80
        mov eax, [total_bytes]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_bytes_nl
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -------------------------------------------------------------------
; Error handlers
; -------------------------------------------------------------------
.dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns_fail
        int 0x80
        jmp .fail_exit
.sock_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_sock_fail
        int 0x80
        jmp .fail_exit
.connect_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_conn_fail
        int 0x80
        mov eax, [sockfd]
        call net_close
        jmp .fail_exit
.send_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_send_fail
        int 0x80
        mov eax, [sockfd]
        call net_close
        jmp .fail_exit
.no_data:
        mov eax, SYS_PRINT
        mov ebx, msg_no_data
        int 0x80
        jmp .fail_exit
.write_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_write_fail
        int 0x80
        jmp .fail_exit

.fail_exit:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -------------------------------------------------------------------
; Data
; -------------------------------------------------------------------
msg_usage:      db "Usage: wget <url> [outfile]", 0x0A
                db "Download a file via HTTP.", 0x0A
                db "  wget http://host/path", 0x0A
                db "  wget host/file.txt output.txt", 0x0A, 0
msg_resolving:  db "Resolving ", 0
msg_dots:       db "...", 0x0A, 0
msg_connecting: db "Connecting...", 0x0A, 0
msg_sending:    db "Sending request...", 0x0A, 0
msg_saved:      db "Saved: ", 0
msg_paren:      db " (", 0
msg_bytes_nl:   db " bytes)", 0x0A, 0
msg_dns_fail:   db "Error: DNS resolution failed", 0x0A, 0
msg_sock_fail:  db "Error: Could not create socket", 0x0A, 0
msg_conn_fail:  db "Error: Connection failed", 0x0A, 0
msg_send_fail:  db "Error: Send failed", 0x0A, 0
msg_no_data:    db "Error: No data received", 0x0A, 0
msg_write_fail: db "Error: Could not write file", 0x0A, 0
default_name:   db "index.html", 0

str_get:        db "GET ", 0
str_http11:     db " HTTP/1.1", 0x0D, 0x0A, 0
str_host:       db "Host: ", 0
str_conn_close: db 0x0D, 0x0A, "Connection: close", 0x0D, 0x0A, 0x0D, 0x0A, 0

; BSS
port:           dw 80
server_ip:      dd 0
sockfd:         dd 0
total_bytes:    dd 0
in_headers:     db 0
retry_count:    dd 0
file_ptr:       dq 0
hostname:       times 128 db 0
url_path:       times 256 db 0
outfile:        times 128 db 0
arg_buf:        times 512 db 0
request_buf:    times 512 db 0
recv_buf:       times MAX_RECV db 0
file_buf:       times MAX_FILE db 0
