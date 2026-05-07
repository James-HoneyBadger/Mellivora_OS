; curl.asm - HTTP GET/POST client (v9.0)
; Uses Mellivora OS networking stack via UDP/TCP syscalls.
; Usage: curl [-X POST] [-d "data"] <host> <path>
; Sends an HTTP/1.0 request and prints the response.

%include "syscalls.inc"

HTTP_PORT   equ 80
BUF_SIZE    equ 4096
URL_MAX     equ 256
DATA_MAX    equ 512
REQ_MAX     equ 1024

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        mov [arg_len], eax

        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Defaults
        mov byte [method], 'G'  ; 'G' = GET, 'P' = POST
        mov byte [post_data], 0
        mov byte [host_buf], 0
        mov byte [path_buf], 0

.parse_loop:
        cmp byte [esi], 0
        je .parse_done
        ; Check for -X flag
        cmp byte [esi], '-'
        jne .parse_host
        inc esi
        cmp byte [esi], 'X'
        jne .maybe_d
        inc esi
        call skip_spaces
        ; Read method (GET or POST)
        cmp byte [esi], 'P'
        jne .set_get
        mov byte [method], 'P'
        add esi, 4              ; skip POST
        call skip_spaces
        jmp .parse_loop
.set_get:
        mov byte [method], 'G'
        add esi, 3              ; skip GET
        call skip_spaces
        jmp .parse_loop
.maybe_d:
        cmp byte [esi], 'd'
        jne .skip_flag
        inc esi
        call skip_spaces
        ; Skip surrounding quotes if present
        cmp byte [esi], '"'
        jne .no_quote
        inc esi
.no_quote:
        mov edi, post_data
        mov ecx, DATA_MAX - 1
.copy_data:
        lodsb
        cmp al, '"'
        je .data_done
        cmp al, ' '
        je .data_done
        cmp al, 0
        je .data_done
        stosb
        dec ecx
        jnz .copy_data
.data_done:
        mov byte [edi], 0
        call skip_spaces
        jmp .parse_loop
.skip_flag:
        ; Unknown flag — skip to next token
        call skip_token
        call skip_spaces
        jmp .parse_loop

.parse_host:
        ; First non-flag token = host
        cmp byte [host_buf], 0
        jne .parse_path
        mov edi, host_buf
        mov ecx, URL_MAX - 1
.cp_host:
        lodsb
        cmp al, ' '
        je .host_done
        cmp al, 0
        je .host_done
        stosb
        dec ecx
        jnz .cp_host
.host_done:
        mov byte [edi], 0
        call skip_spaces
        jmp .parse_loop

.parse_path:
        cmp byte [path_buf], 0
        jne .parse_skip
        mov edi, path_buf
        mov ecx, URL_MAX - 1
.cp_path:
        lodsb
        cmp al, ' '
        je .path_done
        cmp al, 0
        je .path_done
        stosb
        dec ecx
        jnz .cp_path
.path_done:
        mov byte [edi], 0
        call skip_spaces
        jmp .parse_loop

.parse_skip:
        call skip_token
        call skip_spaces
        jmp .parse_loop

.parse_done:
        ; Validate host
        cmp byte [host_buf], 0
        je .usage

        ; Default path
        cmp byte [path_buf], 0
        jne .has_path
        mov byte [path_buf], '/'
        mov byte [path_buf + 1], 0
.has_path:

        ; Resolve host IP via DNS
        mov eax, SYS_DNS
        mov ebx, host_buf
        mov esi, resolve_buf
        int 0x80
        cmp eax, -1
        je .dns_fail

        ; Parse IP from resolve_buf (dotted-decimal) into 32-bit BE
        mov esi, resolve_buf
        call parse_ip           ; returns EAX = IP dword (host order)
        mov [dest_ip], eax

        ; Connect via TCP (using network stack)
        mov eax, SYS_CONNECT
        mov ebx, [dest_ip]
        mov ecx, HTTP_PORT
        int 0x80
        cmp eax, -1
        je .conn_fail
        mov [sock_fd], eax

        ; Build HTTP request
        call build_http_request

        ; Send request
        mov eax, SYS_SEND
        mov ebx, [sock_fd]
        mov esi, req_buf
        mov ecx, [req_len]
        int 0x80
        cmp eax, -1
        je .send_fail

        ; Receive and print response
.recv_loop:
        mov eax, SYS_RECV
        mov ebx, [sock_fd]
        mov esi, resp_buf
        mov ecx, BUF_SIZE - 1
        int 0x80
        cmp eax, 0
        jle .recv_done
        ; Null-terminate and print
        mov byte [resp_buf + eax], 0
        push eax
        mov eax, SYS_PRINT
        mov ebx, resp_buf
        int 0x80
        pop eax
        jmp .recv_loop

.recv_done:
        ; Close socket
        mov eax, SYS_SOCKCLOSE
        mov ebx, [sock_fd]
        int 0x80
        jmp .exit

.dns_fail:
        mov eax, SYS_PRINT
        mov ebx, err_dns
        int 0x80
        jmp .exit

.conn_fail:
        mov eax, SYS_PRINT
        mov ebx, err_conn
        int 0x80
        jmp .exit

.send_fail:
        mov eax, SYS_PRINT
        mov ebx, err_send
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; build_http_request — construct HTTP request in req_buf
;=======================================================================
build_http_request:
        pushad
        mov edi, req_buf

        ; Method line: "GET /path HTTP/1.0\r\n"
        cmp byte [method], 'P'
        jne .req_get
        mov esi, str_post
        call append_str
        jmp .req_method_done
.req_get:
        mov esi, str_get
        call append_str
.req_method_done:
        mov esi, path_buf
        call append_str
        mov esi, str_http10
        call append_str

        ; Host header
        mov esi, str_host_hdr
        call append_str
        mov esi, host_buf
        call append_str
        mov esi, str_crlf
        call append_str

        ; If POST, Content-Length header + data
        cmp byte [method], 'P'
        jne .req_headers_done
        cmp byte [post_data], 0
        je .req_headers_done

        ; Compute post_data length
        mov esi, post_data
        xor ecx, ecx
.pd_len:
        cmp byte [esi + ecx], 0
        je .pd_len_done
        inc ecx
        jmp .pd_len
.pd_len_done:
        mov [post_len], ecx

        mov esi, str_content_len
        call append_str
        ; Print length as decimal into req_buf
        push edi
        mov eax, [post_len]
        call uint_to_str        ; writes to EDI, returns updated EDI
        pop ecx                 ; ignore old EDI — uint_to_str updated EDI
        ; Append CRLF
        mov esi, str_crlf
        call append_str
        mov esi, str_content_type
        call append_str

.req_headers_done:
        ; Empty line (end of headers)
        mov esi, str_crlf
        call append_str

        ; POST body
        cmp byte [method], 'P'
        jne .req_done
        mov esi, post_data
        call append_str

.req_done:
        mov byte [edi], 0
        ; Compute length
        sub edi, req_buf
        mov [req_len], edi
        popad
        ret

;---------------------------------------
; append_str: copy null-terminated ESI to [EDI], advance EDI
;---------------------------------------
append_str:
.as_loop:
        lodsb
        test al, al
        jz .as_done
        stosb
        jmp .as_loop
.as_done:
        ret

;---------------------------------------
; uint_to_str: convert EAX to decimal string at EDI, advance EDI
;---------------------------------------
uint_to_str:
        push eax
        push ecx
        push edx
        mov ecx, 10
        xor edx, edx
        cmp eax, 0
        jne .us_nonzero
        mov byte [edi], '0'
        inc edi
        jmp .us_done
.us_nonzero:
        ; Reverse-write digits to tmp
        push edi
        mov edi, uint_tmp + 15
        mov byte [edi], 0
        dec edi
.us_divloop:
        xor edx, edx
        div ecx
        add dl, '0'
        mov [edi], dl
        dec edi
        test eax, eax
        jnz .us_divloop
        inc edi
        ; Copy from tmp to real EDI
        pop ecx                 ; saved EDI
        push edi                ; start of number in tmp
        mov edi, ecx
.us_copy:
        lodsb
        test al, al
        jz .us_copy_done
        stosb
        jmp .us_copy
.us_copy_done:
        pop esi                 ; restore esi to avoid clobbering
.us_done:
        pop edx
        pop ecx
        pop eax
        ret

;---------------------------------------
; parse_ip: parse "a.b.c.d" from ESI → EAX (big-endian dword)
;---------------------------------------
parse_ip:
        xor eax, eax
        mov ecx, 4
.pi_byte:
        push ecx
        push eax
        xor eax, eax
.pi_digit:
        movzx edx, byte [esi]
        cmp dl, '0'
        jl .pi_byte_done
        cmp dl, '9'
        jg .pi_byte_done
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pi_digit
.pi_byte_done:
        ; Skip dot
        cmp byte [esi], '.'
        jne .pi_no_dot
        inc esi
.pi_no_dot:
        mov edx, eax            ; this byte
        pop eax
        shl eax, 8
        or al, dl
        pop ecx
        dec ecx
        jnz .pi_byte
        ret

;---------------------------------------
; skip_spaces / skip_token
;---------------------------------------
skip_spaces:
        lodsb
        cmp al, ' '
        je skip_spaces
        dec esi
        ret

skip_token:
        lodsb
        cmp al, ' '
        je .st_done
        cmp al, 0
        je .st_done
        jmp skip_token
.st_done:
        dec esi
        ret

;=======================================================================
; DATA
;=======================================================================
msg_usage:      db "Usage: curl [-X POST] [-d data] <host> <path>", 0x0A,
                db "  Example: curl example.com /index.html", 0x0A, 0
err_dns:        db "curl: DNS resolution failed", 0x0A, 0
err_conn:       db "curl: connection failed", 0x0A, 0
err_send:       db "curl: send failed", 0x0A, 0

str_get:        db "GET ", 0
str_post:       db "POST ", 0
str_http10:     db " HTTP/1.0", 13, 10, 0
str_host_hdr:   db "Host: ", 0
str_crlf:       db 13, 10, 0
str_content_len: db "Content-Length: ", 0
str_content_type: db "Content-Type: application/x-www-form-urlencoded", 13, 10, 0

arg_buf:        times 512 db 0
arg_len:        dd 0
host_buf:       times URL_MAX db 0
path_buf:       times URL_MAX db 0
post_data:      times DATA_MAX db 0
resolve_buf:    times 32 db 0
method:         db 'G'
sock_fd:        dd 0
dest_ip:        dd 0
post_len:       dd 0
req_len:        dd 0
uint_tmp:       times 16 db 0

req_buf:        times REQ_MAX db 0
resp_buf:       times BUF_SIZE db 0
