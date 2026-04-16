; httpd.asm - HTTP/1.1 Server for Mellivora OS
; Serves files from the filesystem with directory listing.
; Supports: GET, HEAD, Host header, keep-alive, content-type detection
; Usage: httpd [port]
; Default port: 8080

%include "syscalls.inc"
%include "lib/net.inc"

DEFAULT_PORT    equ 8080
MAX_REQ_SIZE    equ 4096
MAX_FILE_SIZE   equ 32768
MAX_RESP_HDR    equ 512
MAX_DIR_RESP    equ 8192
MAX_PATH        equ 128
MAX_REQUESTS    equ 100         ; max requests per keep-alive connection

start:
        ; Parse optional port argument
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp byte [arg_buf], 0
        je .use_default_port
        mov esi, arg_buf
        call parse_decimal
        test eax, eax
        jz .use_default_port
        mov [listen_port], eax
        jmp .port_ok
.use_default_port:
        mov dword [listen_port], DEFAULT_PORT
.port_ok:
        ; Print banner
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; cyan
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, banner_str
        int 0x80

        ; Print port
        mov eax, [listen_port]
        call print_number

        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Create TCP socket
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je httpd_sock_fail
        mov [server_fd], eax

        ; Bind to port
        mov eax, [server_fd]
        mov ebx, [listen_port]
        call net_bind
        cmp eax, -1
        je httpd_bind_fail

        ; Listen
        mov eax, [server_fd]
        call net_listen
        cmp eax, -1
        je httpd_listen_fail

        ; Print listening message
        mov eax, SYS_PRINT
        mov ebx, msg_listening
        int 0x80

;---------------------------------------
; Main server loop
;---------------------------------------
server_loop:
        ; Accept connection
        mov eax, [server_fd]
        call net_accept
        cmp eax, -1
        je .accept_retry
        mov [client_fd], eax

        ; Keep-alive request loop
        mov dword [keep_alive], 1
        mov dword [req_count], 0

.request_loop:
        ; Receive request
        mov eax, [client_fd]
        mov ebx, req_buf
        mov ecx, MAX_REQ_SIZE - 1
        call net_recv
        cmp eax, 0
        jle .close_client

        ; Null-terminate request
        mov byte [req_buf + eax], 0

        ; Log the request (first line)
        call log_request

        ; Parse the request (also parses headers for keep-alive)
        call parse_request
        test eax, eax
        jz .send_400

        ; Route the request
        call handle_request

        ; Check keep-alive
        inc dword [req_count]
        mov eax, [req_count]
        cmp eax, MAX_REQUESTS
        jge .close_client
        cmp dword [keep_alive], 1
        je .request_loop

.close_client:
        mov eax, [client_fd]
        call net_close

.accept_retry:
        ; Yield before next accept
        mov eax, SYS_YIELD
        int 0x80
        jmp server_loop

.send_400:
        mov esi, resp_400
        call send_string
        jmp .close_client

httpd_sock_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_sock_fail
        int 0x80
        jmp httpd_exit
httpd_bind_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_bind_fail
        int 0x80
        jmp httpd_exit
httpd_listen_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_listen_fail
        int 0x80
httpd_exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; parse_request - Parse HTTP request line
; Sets req_method, req_path
; Returns: EAX = 1 if valid, 0 if not
;---------------------------------------
parse_request:
        mov esi, req_buf
        mov edi, req_method

        ; Copy method (until space)
        xor ecx, ecx
.pr_method:
        lodsb
        cmp al, ' '
        je .pr_got_method
        cmp al, 0
        je .pr_bad
        cmp ecx, 7
        jge .pr_bad
        mov [edi + ecx], al
        inc ecx
        jmp .pr_method
.pr_got_method:
        mov byte [edi + ecx], 0

        ; Copy path (until space or ?)
        mov edi, req_path
        xor ecx, ecx
.pr_path:
        lodsb
        cmp al, ' '
        je .pr_got_path
        cmp al, '?'
        je .pr_skip_query
        cmp al, 0
        je .pr_got_path
        cmp ecx, MAX_PATH - 1
        jge .pr_skip_path
        mov [edi + ecx], al
        inc ecx
        jmp .pr_path
.pr_skip_query:
.pr_skip_path:
        lodsb
        cmp al, ' '
        je .pr_got_path
        cmp al, 0
        je .pr_got_path
        jmp .pr_skip_path
.pr_got_path:
        mov byte [edi + ecx], 0

        ; Skip past HTTP version line (to CRLF)
.pr_skip_ver:
        lodsb
        cmp al, 0x0A
        je .pr_headers
        cmp al, 0
        je .pr_headers_done
        jmp .pr_skip_ver

        ; Parse headers for Connection: and Host:
.pr_headers:
        ; Default: keep-alive for HTTP/1.1
        mov dword [keep_alive], 1
        mov byte [req_host], 0

.pr_hdr_line:
        cmp byte [esi], 0x0D
        je .pr_blank_line
        cmp byte [esi], 0x0A
        je .pr_blank_line
        cmp byte [esi], 0
        je .pr_headers_done

        ; Check "Connection:"
        cmp byte [esi], 'C'
        jne .pr_check_host
        cmp byte [esi + 1], 'o'
        jne .pr_check_host
        cmp byte [esi + 2], 'n'
        jne .pr_check_host
        cmp byte [esi + 3], 'n'
        jne .pr_check_host
        ; Scan for "close"
        push rsi
.pr_conn_scan:
        lodsb
        cmp al, 0x0A
        je .pr_conn_end
        cmp al, 0
        je .pr_conn_end
        cmp al, 'c'
        jne .pr_conn_scan
        cmp byte [esi], 'l'
        jne .pr_conn_scan
        ; Found "cl" — assume "close"
        mov dword [keep_alive], 0
.pr_conn_end:
        pop rsi
        jmp .pr_hdr_skip

.pr_check_host:
        ; Check "Host:"
        cmp byte [esi], 'H'
        jne .pr_hdr_skip
        cmp byte [esi + 1], 'o'
        jne .pr_hdr_skip
        cmp byte [esi + 2], 's'
        jne .pr_hdr_skip
        cmp byte [esi + 3], 't'
        jne .pr_hdr_skip
        ; Copy host value
        push rsi
        add esi, 4              ; past "Host"
        cmp byte [esi], ':'
        jne .pr_host_end
        inc esi
        cmp byte [esi], ' '
        jne .pr_host_copy
        inc esi
.pr_host_copy:
        mov edi, req_host
        xor ecx, ecx
.pr_host_ch:
        lodsb
        cmp al, 0x0D
        je .pr_host_term
        cmp al, 0x0A
        je .pr_host_term
        cmp al, 0
        je .pr_host_term
        cmp ecx, 63
        jge .pr_host_ch
        mov [edi + ecx], al
        inc ecx
        jmp .pr_host_ch
.pr_host_term:
        mov byte [edi + ecx], 0
.pr_host_end:
        pop rsi

.pr_hdr_skip:
        ; Skip to next line
        lodsb
        cmp al, 0x0A
        je .pr_hdr_line
        cmp al, 0
        je .pr_headers_done
        jmp .pr_hdr_skip

.pr_blank_line:
.pr_headers_done:
        mov eax, 1
        ret
.pr_bad:
        xor eax, eax
        ret

;---------------------------------------
; handle_request - Route and serve the request
;---------------------------------------
handle_request:
        PUSHALL
        ; Check for HEAD method
        mov byte [head_only], 0
        cmp byte [req_method], 'H'
        jne .hr_check_get
        cmp byte [req_method + 1], 'E'
        jne .hr_405
        cmp byte [req_method + 2], 'A'
        jne .hr_405
        cmp byte [req_method + 3], 'D'
        jne .hr_405
        mov byte [head_only], 1
        jmp .hr_route

.hr_check_get:
        ; Check GET
        cmp byte [req_method], 'G'
        jne .hr_405
        cmp byte [req_method + 1], 'E'
        jne .hr_405
        cmp byte [req_method + 2], 'T'
        jne .hr_405

.hr_route:

        ; Check if requesting root "/"
        cmp byte [req_path], '/'
        jne .hr_404
        cmp byte [req_path + 1], 0
        je .serve_directory

        ; Strip leading /
        lea esi, [req_path + 1]
        ; Check if file exists via SYS_STAT
        mov ebx, esi
        mov eax, SYS_STAT
        int 0x80
        cmp eax, -1
        je .hr_404

        ; File exists — serve it
        mov [file_size], eax
        call serve_file
        POPALL
        ret

.serve_directory:
        call serve_dir_listing
        POPALL
        ret

.hr_404:
        mov esi, resp_404
        call send_string
        POPALL
        ret

.hr_405:
        mov esi, resp_405
        call send_string
        POPALL
        ret

;---------------------------------------
; serve_file - Read file and send HTTP response
;---------------------------------------
serve_file:
        PUSHALL

        ; Read file into file_buf
        lea ebx, [req_path + 1]
        mov ecx, file_buf
        mov eax, SYS_FREAD
        int 0x80
        cmp eax, -1
        je .sf_fail
        mov [file_size], eax

        ; Determine content type
        call detect_content_type

        ; Build response header
        mov edi, resp_hdr_buf
        ; "HTTP/1.1 200 OK\r\n"
        mov esi, http_200
        call copy_str
        ; "Content-Type: xxx\r\n"
        mov esi, hdr_content_type
        call copy_str
        mov rsi, [content_type_ptr]
        call copy_str
        mov esi, crlf
        call copy_str
        ; "Content-Length: xxx\r\n"
        mov esi, hdr_content_len
        call copy_str
        mov eax, [file_size]
        call int_to_str
        mov esi, num_buf
        call copy_str
        mov esi, crlf
        call copy_str
        ; "Server: httpd/1.1 Mellivora\r\n"
        mov esi, hdr_server
        call copy_str
        ; Connection header based on keep-alive state
        cmp dword [keep_alive], 1
        jne .sf_conn_close
        mov esi, hdr_conn_alive
        call copy_str
        jmp .sf_conn_done
.sf_conn_close:
        mov esi, hdr_conn_close
        call copy_str
.sf_conn_done:
        mov esi, crlf
        call copy_str

        ; Null terminate
        mov byte [edi], 0

        ; Send header
        mov esi, resp_hdr_buf
        call send_string

        ; Send file body (unless HEAD-only)
        cmp byte [head_only], 1
        je .sf_head_skip
        mov eax, [client_fd]
        mov ebx, file_buf
        mov ecx, [file_size]
        call net_send
.sf_head_skip:

        POPALL
        ret

.sf_fail:
        mov esi, resp_500
        call send_string
        POPALL
        ret

;---------------------------------------
; serve_dir_listing - Generate and send directory listing
;---------------------------------------
serve_dir_listing:
        PUSHALL

        ; Build HTML directory listing into dir_buf
        mov edi, dir_buf
        mov esi, dir_html_header
        call copy_str

        ; Enumerate files
        xor ecx, ecx           ; entry index
.sdl_loop:
        push rcx
        mov eax, SYS_READDIR
        mov ebx, dir_name_buf
        int 0x80
        pop rcx

        cmp eax, -1
        je .sdl_done
        cmp eax, 0
        je .sdl_next            ; free slot
        push rcx
        push rax                ; save type
        ; Save file size 
        mov [.sdl_fsize], ecx

        ; "<tr><td><a href=\"/filename\">filename</a></td>"
        mov esi, dir_row_start
        call copy_str
        mov esi, dir_name_buf
        call copy_str
        mov esi, dir_link_mid
        call copy_str
        mov esi, dir_name_buf
        call copy_str
        mov esi, dir_link_end
        call copy_str

        ; "<td>size</td>"
        mov esi, dir_td_start
        call copy_str
        mov eax, [.sdl_fsize]
        call int_to_str
        mov esi, num_buf
        call copy_str
        mov esi, dir_td_end
        call copy_str

        ; "<td>type</td></tr>\n"
        mov esi, dir_td_start
        call copy_str
        pop rax                 ; type
        cmp eax, FTYPE_EXEC
        je .sdl_type_exec
        cmp eax, FTYPE_DIR
        je .sdl_type_dir
        mov esi, type_text
        jmp .sdl_type_done
.sdl_type_exec:
        mov esi, type_exec
        jmp .sdl_type_done
.sdl_type_dir:
        mov esi, type_dir
.sdl_type_done:
        call copy_str
        mov esi, dir_td_end_row
        call copy_str
        pop rcx

.sdl_next:
        inc ecx
        cmp ecx, 200
        jl .sdl_loop

.sdl_done:
        ; Close HTML
        mov esi, dir_html_footer
        call copy_str
        mov byte [edi], 0

        ; Calculate body length
        mov eax, edi
        sub eax, dir_buf
        mov [file_size], eax

        ; Build header
        mov edi, resp_hdr_buf
        mov esi, http_200
        call copy_str
        mov esi, hdr_content_type
        call copy_str
        mov esi, ct_html
        call copy_str
        mov esi, crlf
        call copy_str
        mov esi, hdr_content_len
        call copy_str
        mov eax, [file_size]
        call int_to_str
        mov esi, num_buf
        call copy_str
        mov esi, crlf
        call copy_str
        mov esi, hdr_server
        call copy_str
        cmp dword [keep_alive], 1
        jne .sdl_conn_close
        mov esi, hdr_conn_alive
        call copy_str
        jmp .sdl_conn_done
.sdl_conn_close:
        mov esi, hdr_conn_close
        call copy_str
.sdl_conn_done:
        mov esi, crlf
        call copy_str
        mov byte [edi], 0

        ; Send header + body
        mov esi, resp_hdr_buf
        call send_string

        ; Send body (unless HEAD-only)
        cmp byte [head_only], 1
        je .sdl_head_skip
        mov eax, [client_fd]
        mov ebx, dir_buf
        mov ecx, [file_size]
        call net_send
.sdl_head_skip:

        POPALL
        ret

.sdl_fsize: dd 0

;---------------------------------------
; detect_content_type - Set content_type_ptr based on filename extension
;---------------------------------------
detect_content_type:
        ; Find last '.' in req_path
        lea esi, [req_path + 1]
        xor edx, edx           ; last dot position
.dct_scan:
        lodsb
        test al, al
        jz .dct_check
        cmp al, '.'
        jne .dct_scan
        lea edx, [esi]         ; edx points past the dot
        jmp .dct_scan
.dct_check:
        test edx, edx
        jz .dct_default

        ; Compare extension
        mov al, [edx]
        or al, 0x20
        cmp al, 'h'
        je .dct_check_htm
        cmp al, 't'
        je .dct_text
        cmp al, 'b'
        je .dct_check_bmp
        cmp al, 'c'
        je .dct_check_css
        cmp al, 'j'
        je .dct_check_js
        cmp al, 'p'
        je .dct_check_png
        cmp al, 'g'
        je .dct_check_gz
        jmp .dct_default

.dct_check_htm:
        mov al, [edx + 1]
        or al, 0x20
        cmp al, 't'
        jne .dct_default
        mov qword [content_type_ptr], ct_html
        ret
.dct_text:
        mov al, [edx + 1]
        or al, 0x20
        cmp al, 'x'
        jne .dct_default
        mov qword [content_type_ptr], ct_text
        ret
.dct_check_bmp:
        mov al, [edx + 1]
        or al, 0x20
        cmp al, 'm'
        jne .dct_default
        mov qword [content_type_ptr], ct_bmp
        ret
.dct_check_css:
        mov al, [edx + 1]
        or al, 0x20
        cmp al, 's'
        jne .dct_default
        mov qword [content_type_ptr], ct_css
        ret
.dct_check_js:
        mov al, [edx + 1]
        or al, 0x20
        cmp al, 's'
        jne .dct_check_json
        cmp byte [edx + 2], 0   ; .js (2 chars)
        je .dct_js_ok
        cmp byte [edx + 2], '.'
        je .dct_js_ok
        jmp .dct_check_json
.dct_js_ok:
        mov qword [content_type_ptr], ct_js
        ret
.dct_check_json:
        cmp byte [edx + 1], 's'
        jne .dct_default
        ; Could be "json" — just check 'o','n'
        cmp byte [edx + 2], 'o'
        jne .dct_default
        mov qword [content_type_ptr], ct_json
        ret
.dct_check_png:
        mov al, [edx + 1]
        or al, 0x20
        cmp al, 'n'
        jne .dct_default
        mov qword [content_type_ptr], ct_png
        ret
.dct_check_gz:
        mov al, [edx + 1]
        or al, 0x20
        cmp al, 'z'
        jne .dct_default
        mov qword [content_type_ptr], ct_gz
        ret
.dct_default:
        mov qword [content_type_ptr], ct_octet
        ret

;---------------------------------------
; send_string - Send null-terminated string on client_fd
; ESI = string
;---------------------------------------
send_string:
        push rax
        push rcx
        push rbx
        ; Get length
        mov ebx, esi
        xor ecx, ecx
.ss_len:
        cmp byte [ebx + ecx], 0
        je .ss_send
        inc ecx
        jmp .ss_len
.ss_send:
        mov eax, [client_fd]
        mov ebx, esi
        call net_send
        pop rbx
        pop rcx
        pop rax
        ret

;---------------------------------------
; copy_str - Copy null-terminated string from ESI to EDI, advance EDI
;---------------------------------------
copy_str:
        push rax
.cs_loop:
        lodsb
        test al, al
        jz .cs_done
        stosb
        jmp .cs_loop
.cs_done:
        pop rax
        ret

;---------------------------------------
; log_request - Print first line of request to console
;---------------------------------------
log_request:
        PUSHALL
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; green
        int 0x80

        mov esi, req_buf
        ; Print until CR/LF
.lr_loop:
        lodsb
        cmp al, 0x0D
        je .lr_done
        cmp al, 0x0A
        je .lr_done
        cmp al, 0
        je .lr_done
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .lr_loop
.lr_done:
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        POPALL
        ret

;---------------------------------------
; int_to_str - Convert EAX to decimal string in num_buf
;---------------------------------------
int_to_str:
        push rbx
        push rcx
        push rdx
        push rdi
        mov edi, num_buf + 15
        mov byte [edi], 0
        dec edi
        mov ebx, 10
        test eax, eax
        jnz .its_loop
        mov byte [edi], '0'
        dec edi
        jmp .its_done
.its_loop:
        test eax, eax
        jz .its_done
        xor edx, edx
        div ebx
        add dl, '0'
        mov [edi], dl
        dec edi
        jmp .its_loop
.its_done:
        inc edi
        ; Shift to start of num_buf
        mov esi, edi
        mov edi, num_buf
.its_copy:
        lodsb
        stosb
        test al, al
        jnz .its_copy
        pop rdi
        pop rdx
        pop rcx
        pop rbx
        ret

;---------------------------------------
; parse_decimal - Parse decimal number from ESI
; Returns: EAX = number
;---------------------------------------
parse_decimal:
        xor eax, eax
        xor ecx, ecx
.pd_loop:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jb .pd_done
        cmp cl, '9'
        ja .pd_done
        imul eax, 10
        sub cl, '0'
        add eax, ecx
        inc esi
        jmp .pd_loop
.pd_done:
        ret

;---------------------------------------
; print_number - Print EAX as decimal
;---------------------------------------
print_number:
        push rax
        call int_to_str
        mov eax, SYS_PRINT
        mov ebx, num_buf
        int 0x80
        pop rax
        ret

;=======================================
; Data
;=======================================

banner_str:     db "Mellivora HTTP Server v2.0 (HTTP/1.1)", 0x0D, 0x0A
                db "Listening on port: ", 0
msg_listening:  db "Waiting for connections...", 0x0D, 0x0A, 0
msg_sock_fail:  db "Error: Could not create socket", 0x0D, 0x0A, 0
msg_bind_fail:  db "Error: Could not bind to port", 0x0D, 0x0A, 0
msg_listen_fail: db "Error: Could not listen", 0x0D, 0x0A, 0
newline:        db 0x0D, 0x0A, 0

; HTTP response templates
http_200:       db "HTTP/1.1 200 OK", 0x0D, 0x0A, 0
hdr_content_type: db "Content-Type: ", 0
hdr_content_len:  db "Content-Length: ", 0
hdr_conn_close:   db "Connection: close", 0x0D, 0x0A, 0
hdr_conn_alive:   db "Connection: keep-alive", 0x0D, 0x0A, 0
hdr_server:       db "Server: httpd/2.0 Mellivora", 0x0D, 0x0A, 0
crlf:           db 0x0D, 0x0A, 0

resp_400:       db "HTTP/1.1 400 Bad Request", 0x0D, 0x0A
                db "Content-Type: text/plain", 0x0D, 0x0A
                db "Content-Length: 11", 0x0D, 0x0A
                db "Connection: close", 0x0D, 0x0A, 0x0D, 0x0A
                db "Bad Request", 0

resp_404:       db "HTTP/1.1 404 Not Found", 0x0D, 0x0A
                db "Content-Type: text/html", 0x0D, 0x0A
                db "Content-Length: 52", 0x0D, 0x0A
                db "Connection: close", 0x0D, 0x0A, 0x0D, 0x0A
                db "<h1>404 Not Found</h1><p>File not found.</p>", 0

resp_405:       db "HTTP/1.1 405 Method Not Allowed", 0x0D, 0x0A
                db "Content-Type: text/plain", 0x0D, 0x0A
                db "Content-Length: 18", 0x0D, 0x0A
                db "Connection: close", 0x0D, 0x0A, 0x0D, 0x0A
                db "Method Not Allowed", 0

resp_500:       db "HTTP/1.1 500 Internal Server Error", 0x0D, 0x0A
                db "Content-Type: text/plain", 0x0D, 0x0A
                db "Content-Length: 21", 0x0D, 0x0A
                db "Connection: close", 0x0D, 0x0A, 0x0D, 0x0A
                db "Internal Server Error", 0

; Content types
ct_html:        db "text/html", 0
ct_text:        db "text/plain", 0
ct_bmp:         db "image/bmp", 0
ct_css:         db "text/css", 0
ct_js:          db "application/javascript", 0
ct_json:        db "application/json", 0
ct_png:         db "image/png", 0
ct_gz:          db "application/gzip", 0
ct_octet:       db "application/octet-stream", 0

; File type names for directory listing
type_text:      db "Text", 0
type_exec:      db "Executable", 0
type_dir:       db "Directory", 0

; HTML templates for directory listing
dir_html_header:
        db "<html><head><title>Mellivora File Server</title>"
        db "<style>body{font-family:monospace;background:#1a1a2e;color:#e0e0e0;}"
        db "a{color:#4fc3f7;}table{border-collapse:collapse;width:80%;}"
        db "th,td{text-align:left;padding:4px 12px;}"
        db "th{background:#16213e;color:#4fc3f7;}tr:hover{background:#16213e;}</style>"
        db "</head><body><h2>Mellivora OS - File Server</h2>"
        db "<table><tr><th>Name</th><th>Size</th><th>Type</th></tr>", 0

dir_row_start:  db '<tr><td><a href="/', 0
dir_link_mid:   db '">', 0
dir_link_end:   db "</a></td>", 0
dir_td_start:   db "<td>", 0
dir_td_end:     db "</td>", 0
dir_td_end_row: db "</td></tr>", 0x0A, 0

dir_html_footer:
        db "</table><hr><em>httpd/2.0 on Mellivora OS</em></body></html>", 0

;=======================================
; BSS
;=======================================

server_fd:      dd 0
client_fd:      dd 0
listen_port:    dd 0
file_size:      dd 0
content_type_ptr: dq ct_octet
keep_alive:     dd 0
req_count:      dd 0
head_only:      db 0

arg_buf:        times 64 db 0
req_buf:        times MAX_REQ_SIZE db 0
req_method:     times 8 db 0
req_path:       times MAX_PATH db 0
req_host:       times 64 db 0
num_buf:        times 16 db 0
dir_name_buf:   times 64 db 0
resp_hdr_buf:   times MAX_RESP_HDR db 0
dir_buf:        times MAX_DIR_RESP db 0
file_buf:       times MAX_FILE_SIZE db 0
