; gopher.asm - Gopher protocol client
; Usage: gopher <host> [path] [port]
;
; Browse Gopher space. Default port 70.
; Type 0 = text file, Type 1 = menu/directory

%include "syscalls.inc"
%include "lib/net.inc"

start:
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80
        test eax, eax
        jz .usage

        ; Parse: host [path] [port]
        mov esi, arg_buf
        mov edi, hostname
        xor ecx, ecx
.parse_host:
        mov al, [esi]
        cmp al, ' '
        je .host_done
        cmp al, 0
        je .host_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, 255
        jb .parse_host
.host_done:
        mov byte [edi + ecx], 0

        ; Default path and port
        mov byte [gopher_path], 0x0D
        mov byte [gopher_path+1], 0x0A
        mov byte [gopher_path+2], 0
        mov word [port], 70

        ; Parse optional path
        cmp byte [esi], ' '
        jne .resolve
        inc esi
        cmp byte [esi], 0
        je .resolve

        ; Check if next arg is a number (port) or path
        cmp byte [esi], '/'
        je .copy_path
        cmp byte [esi], '0'
        jb .copy_path
        cmp byte [esi], '9'
        ja .copy_path
        jmp .parse_port_arg

.copy_path:
        mov edi, gopher_path
        xor ecx, ecx
.cp_loop:
        lodsb
        cmp al, ' '
        je .path_done
        cmp al, 0
        je .path_end
        stosb
        inc ecx
        jmp .cp_loop
.path_done:
        ; Add CRLF
        mov byte [edi], 0x0D
        mov byte [edi+1], 0x0A
        mov byte [edi+2], 0
        ; Check for port
        cmp byte [esi], 0
        je .resolve
.parse_port_arg:
        ; Skip spaces
        cmp byte [esi], ' '
        jne .pp
        inc esi
        jmp .parse_port_arg
.pp:
        xor eax, eax
.pp_loop:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jb .pp_end
        cmp bl, '9'
        ja .pp_end
        sub bl, '0'
        imul eax, 10
        add eax, ebx
        inc esi
        jmp .pp_loop
.pp_end:
        test eax, eax
        jz .resolve
        mov [port], ax
        jmp .resolve

.path_end:
        mov byte [edi], 0x0D
        mov byte [edi+1], 0x0A
        mov byte [edi+2], 0

.resolve:
        ; Resolve and connect
        mov esi, hostname
        call net_dns
        test eax, eax
        jz .dns_fail
        mov [server_ip], eax

        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .sock_fail
        mov [sockfd], eax

        mov eax, [sockfd]
        mov ebx, [server_ip]
        movzx ecx, word [port]
        call net_connect
        cmp eax, -1
        je .conn_fail

        ; Send selector (path + CRLF)
        mov esi, gopher_path
        xor ecx, ecx
.gp_len:
        cmp byte [esi + ecx], 0
        je .gp_send
        inc ecx
        jmp .gp_len
.gp_send:
        mov eax, [sockfd]
        mov ebx, gopher_path
        call net_send

        ; Receive and display response
        mov dword [retry_count], 0
.recv_loop:
        mov eax, [sockfd]
        mov ebx, recv_buf
        mov ecx, 2048
        call net_recv

        cmp eax, -1
        je .recv_done
        cmp eax, 0
        je .recv_retry

        mov dword [retry_count], 0

        ; Display received data with Gopher menu formatting
        mov esi, recv_buf
        mov ecx, eax
.display:
        test ecx, ecx
        jz .recv_loop
        lodsb
        dec ecx

        ; Check for '.' alone on a line (end marker)
        cmp al, '.'
        jne .not_dot
        cmp byte [line_start], 1
        jne .not_dot
        cmp ecx, 0
        je .recv_done
        cmp byte [esi], 0x0D
        je .recv_done
        cmp byte [esi], 0x0A
        je .recv_done

.not_dot:
        ; Gopher item type indicator at start of line
        cmp byte [line_start], 1
        jne .normal_char

        mov byte [line_start], 0
        ; Type indicators: 0=text, 1=dir, i=info, 3=error
        cmp al, 'i'
        je .type_info
        cmp al, '0'
        je .type_text
        cmp al, '1'
        je .type_dir
        cmp al, '3'
        je .type_error
        ; Unknown type, just print
        jmp .normal_char

.type_info:
        ; Info line - just print text until tab
        jmp .print_until_tab
.type_text:
        mov eax, SYS_PRINT
        mov ebx, str_text
        int 0x80
        jmp .print_until_tab
.type_dir:
        mov eax, SYS_PRINT
        mov ebx, str_dir
        int 0x80
        jmp .print_until_tab
.type_error:
        mov eax, SYS_PRINT
        mov ebx, str_err
        int 0x80
        jmp .print_until_tab

.print_until_tab:
        ; Print chars until TAB, then skip rest of line
        test ecx, ecx
        jz .recv_loop
        lodsb
        dec ecx
        cmp al, 0x09            ; TAB
        je .skip_to_eol
        cmp al, 0x0A
        je .got_newline
        cmp al, 0x0D
        je .display
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .print_until_tab

.skip_to_eol:
        test ecx, ecx
        jz .recv_loop
        lodsb
        dec ecx
        cmp al, 0x0A
        je .got_newline
        jmp .skip_to_eol

.got_newline:
        mov byte [line_start], 1
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        jmp .display

.normal_char:
        cmp al, 0x0A
        je .got_newline
        cmp al, 0x0D
        je .display
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .display

.recv_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 300
        jge .recv_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .recv_loop

.recv_done:
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
.conn_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_conn_fail
        int 0x80
        mov eax, [sockfd]
        call net_close
.exit_err:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; Strings
msg_usage:    db "Usage: gopher <host> [path] [port]", 0x0A, 0
msg_dns_fail: db "Error: DNS resolution failed", 0x0A, 0
msg_sock_fail: db "Error: Could not create socket", 0x0A, 0
msg_conn_fail: db "Error: Connection failed", 0x0A, 0
msg_newline:  db 0x0A, 0
str_text:     db "[TXT] ", 0
str_dir:      db "[DIR] ", 0
str_err:      db "[ERR] ", 0

; Data
hostname:     times 256 db 0
gopher_path:  times 256 db 0
port:         dw 70
server_ip:    dd 0
sockfd:       dd 0
line_start:   db 1
retry_count:  dd 0
arg_buf:      times 512 db 0
recv_buf:     times 2048 db 0
