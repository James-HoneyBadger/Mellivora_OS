; telnet.asm - Telnet client
; Usage: telnet <host> [port]
;
; Connects to a remote host via TCP and provides interactive
; terminal session. Default port is 23.

%include "syscalls.inc"
%include "lib/net.inc"

start:
        ; Get command line args
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80
        test eax, eax
        jz .usage

        ; Parse hostname
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

        ; Parse optional port
        mov word [port], 23     ; default telnet port
        cmp byte [esi], ' '
        jne .resolve
        inc esi
        ; Skip spaces
.skip_sp:
        cmp byte [esi], ' '
        jne .parse_port
        inc esi
        jmp .skip_sp
.parse_port:
        xor eax, eax
.pp_loop:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jb .pp_done
        cmp bl, '9'
        ja .pp_done
        sub bl, '0'
        imul eax, 10
        add eax, ebx
        inc esi
        jmp .pp_loop
.pp_done:
        test eax, eax
        jz .resolve
        mov [port], ax

.resolve:
        ; Print connecting message
        mov eax, SYS_PRINT
        mov ebx, msg_conn
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80

        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
        movzx eax, word [port]
        call print_dec
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
        je .conn_fail

        mov eax, SYS_PRINT
        mov ebx, msg_connected
        int 0x80

        ; Main loop: check for key input and received data
.main_loop:
        ; Check for received data
        mov eax, [sockfd]
        mov ebx, recv_buf
        mov ecx, 1024
        call net_recv

        cmp eax, -1
        je .disconnected
        cmp eax, 0
        je .check_key

        ; Print received data (filter telnet control sequences)
        mov esi, recv_buf
        mov ecx, eax
.print_loop:
        test ecx, ecx
        jz .check_key
        lodsb
        dec ecx
        ; Skip IAC sequences (0xFF followed by 2+ bytes)
        cmp al, 0xFF
        je .handle_iac
        ; Print normal characters
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .print_loop

.handle_iac:
        ; Simple telnet protocol handling: skip IAC + command + option
        cmp ecx, 2
        jb .check_key
        lodsb                   ; command byte
        dec ecx
        ; For DO/DONT/WILL/WONT, skip option byte
        cmp al, 0xFB            ; WILL
        jb .iac_skip1
        cmp al, 0xFE            ; DONT
        ja .iac_skip1
        lodsb                   ; option byte
        dec ecx
.iac_skip1:
        jmp .print_loop

.check_key:
        ; Check for keyboard input (non-blocking)
        mov eax, SYS_READ_KEY
        int 0x80
        cmp eax, 0
        je .yield

        ; Check for Ctrl+C (exit)
        cmp eax, 3
        je .quit

        ; Check for special keys (escape sequences)
        cmp eax, 0x100
        jge .yield              ; Skip special keys for now

        ; Send the character
        mov [send_char], al
        mov eax, [sockfd]
        mov ebx, send_char
        mov ecx, 1
        call net_send
        jmp .main_loop

.yield:
        mov eax, SYS_YIELD
        int 0x80
        jmp .main_loop

.disconnected:
        mov eax, SYS_PRINT
        mov ebx, msg_disconn
        int 0x80
        jmp .cleanup

.quit:
        mov eax, SYS_PRINT
        mov ebx, msg_quit
        int 0x80

.cleanup:
        mov eax, [sockfd]
        call net_close
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
        jmp .exit_err

.exit_err:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; Strings
msg_usage:     db "Usage: telnet <host> [port]", 0x0A, 0
msg_conn:      db "Connecting to ", 0
msg_newline:   db 0x0A, 0
msg_connected: db "Connected. Press Ctrl+C to quit.", 0x0A, 0
msg_dns_fail:  db "Error: DNS resolution failed", 0x0A, 0
msg_sock_fail: db "Error: Could not create socket", 0x0A, 0
msg_conn_fail: db "Error: Connection failed", 0x0A, 0
msg_disconn:   db 0x0A, "Connection closed by remote host.", 0x0A, 0
msg_quit:      db 0x0A, "Connection closed.", 0x0A, 0

; Data
hostname:    times 256 db 0
port:        dw 23
server_ip:   dd 0
sockfd:      dd 0
send_char:   db 0
arg_buf:     times 512 db 0
recv_buf:    times 2048 db 0
