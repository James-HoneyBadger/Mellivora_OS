; http.asm - HTTP client (wget-like)
; Usage: http <url>
;
; Fetches a web page via HTTP/1.0 GET and displays the response body.
; Supports: http://host/path or just host/path

%include "syscalls.inc"
%include "lib/net.inc"

start:
        ; Get command-line arguments
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80
        test eax, eax
        jz .usage

        ; Parse URL: skip "http://" if present
        mov esi, arg_buf
        cmp dword [esi], 'http'
        jne .no_scheme
        cmp word [esi+4], '://'
        jne .no_scheme
        add esi, 7
.no_scheme:

        ; Extract hostname (up to '/' or end)
        mov edi, hostname
        xor ecx, ecx
.parse_host:
        mov al, [esi]
        cmp al, '/'
        je .host_done
        cmp al, ':'
        je .host_port
        cmp al, 0
        je .host_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, 255
        jb .parse_host
.host_done:
        mov byte [edi + ecx], 0
        mov word [port], 80
        jmp .parse_path

.host_port:
        mov byte [edi + ecx], 0
        inc esi
        ; Parse port number
        xor eax, eax
        xor ecx, ecx
.parse_port:
        movzx ebx, byte [esi]
        cmp bl, '/'
        je .port_done
        cmp bl, 0
        je .port_done
        sub bl, '0'
        imul eax, 10
        add eax, ebx
        inc esi
        jmp .parse_port
.port_done:
        mov [port], ax

.parse_path:
        ; Copy path (default to "/" if none)
        mov edi, path
        cmp byte [esi], '/'
        je .copy_path
        mov byte [edi], '/'
        mov byte [edi+1], 0
        jmp .resolve

.copy_path:
        mov ecx, 255
.cp_loop:
        lodsb
        stosb
        test al, al
        jz .resolve
        dec ecx
        jnz .cp_loop

.resolve:
        ; Print connecting message
        mov eax, SYS_PRINT
        mov ebx, msg_connecting
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80

        ; Resolve hostname
        mov esi, hostname
        call net_dns
        test eax, eax
        jz .dns_fail
        mov [server_ip], eax

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

        ; Build HTTP request: "GET /path HTTP/1.0\r\nHost: hostname\r\n\r\n"
        mov edi, request_buf
        ; "GET "
        mov dword [edi], 'GET '
        add edi, 4
        ; path
        mov esi, path
.copy_req_path:
        lodsb
        test al, al
        jz .req_proto
        stosb
        jmp .copy_req_path
.req_proto:
        ; " HTTP/1.0\r\n"
        mov esi, http_proto
.copy_proto:
        lodsb
        stosb
        test al, al
        jnz .copy_proto
        dec edi                 ; back over null
        ; "Host: "
        mov esi, host_hdr
.copy_host_hdr:
        lodsb
        stosb
        test al, al
        jnz .copy_host_hdr
        dec edi
        ; hostname
        mov esi, hostname
.copy_hostname:
        lodsb
        test al, al
        jz .req_end
        stosb
        jmp .copy_hostname
.req_end:
        ; "\r\nConnection: close\r\n\r\n"
        mov esi, conn_close
.copy_cc:
        lodsb
        stosb
        test al, al
        jnz .copy_cc
        dec edi

        ; Calculate request length
        mov ecx, edi
        sub ecx, request_buf

        ; Send request
        mov eax, [sockfd]
        mov ebx, request_buf
        call net_send
        cmp eax, -1
        je .send_fail

        ; Receive and display response
        mov byte [in_headers], 1
        mov byte [skip_mode], 0

.recv_loop:
        mov eax, [sockfd]
        mov ebx, recv_buf
        mov ecx, 1024
        call net_recv

        cmp eax, -1
        je .recv_done
        cmp eax, 0
        je .recv_retry

        ; If still in headers, look for blank line
        cmp byte [in_headers], 1
        jne .print_body

        ; Scan for \r\n\r\n
        mov esi, recv_buf
        mov ecx, eax
.scan_headers:
        cmp ecx, 4
        jb .recv_loop_cont
        cmp dword [esi], 0x0A0D0A0D  ; \r\n\r\n
        je .found_body
        inc esi
        dec ecx
        jmp .scan_headers

.found_body:
        add esi, 4
        sub ecx, 4
        mov byte [in_headers], 0

        ; Print remaining body
        test ecx, ecx
        jz .recv_loop_cont
        mov byte [esi + ecx], 0
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        jmp .recv_loop_cont

.print_body:
        mov byte [recv_buf + eax], 0
        push eax
        mov eax, SYS_PRINT
        mov ebx, recv_buf
        int 0x80
        pop eax

.recv_loop_cont:
        jmp .recv_loop

.recv_retry:
        ; No data, yield and retry (with timeout)
        inc dword [retry_count]
        cmp dword [retry_count], 500
        jge .recv_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .recv_loop

.recv_done:
        ; Close socket
        mov eax, [sockfd]
        call net_close

        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns_fail
        int 0x80
        jmp .exit_err

.sock_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_sock_fail
        int 0x80
        jmp .exit_err

.connect_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_conn_fail
        int 0x80
        mov eax, [sockfd]
        call net_close
        jmp .exit_err

.send_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_send_fail
        int 0x80
        mov eax, [sockfd]
        call net_close
        jmp .exit_err

.exit_err:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; Strings
http_proto:   db " HTTP/1.0", 0x0D, 0x0A, 0
host_hdr:     db "Host: ", 0
conn_close:   db 0x0D, 0x0A, "Connection: close", 0x0D, 0x0A, 0x0D, 0x0A, 0
msg_usage:    db "Usage: http <url>", 0x0A, "  Example: http example.com/index.html", 0x0A, 0
msg_connecting: db "Connecting to ", 0
msg_newline:  db 0x0A, 0
msg_dns_fail: db "Error: DNS resolution failed", 0x0A, 0
msg_sock_fail: db "Error: Could not create socket", 0x0A, 0
msg_conn_fail: db "Error: Connection failed", 0x0A, 0
msg_send_fail: db "Error: Send failed", 0x0A, 0

; Data
hostname:     times 256 db 0
path:         times 256 db 0
port:         dw 80
server_ip:    dd 0
sockfd:       dd 0
in_headers:   db 1
skip_mode:    db 0
retry_count:  dd 0
arg_buf:      times 512 db 0
request_buf:  times 1024 db 0
recv_buf:     times 2048 db 0
