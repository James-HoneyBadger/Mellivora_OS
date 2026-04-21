; nc.asm - Netcat: TCP/UDP connections
; Usage: nc [-u] <host> <port>
;   nc host port     TCP connect to host:port, relay stdin/stdout
;   nc -u host port  UDP mode

%include "syscalls.inc"
%include "lib/net.inc"

RECV_BUF_SIZE   equ 4096
SEND_BUF_SIZE   equ 1024

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Default: TCP
        mov dword [use_udp], 0

        ; Check -u flag
        cmp byte [esi], '-'
        jne .get_host
        cmp byte [esi + 1], 'u'
        jne .get_host
        mov dword [use_udp], 1
        add esi, 2
        call skip_spaces

.get_host:
        ; Copy hostname
        mov edi, hostname
        xor ecx, ecx
.copy_host:
        mov al, [esi]
        cmp al, ' '
        je .host_done
        cmp al, 0
        je .usage
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_host
.host_done:
        mov byte [edi + ecx], 0
        call skip_spaces

        ; Parse port number
        xor ecx, ecx
.parse_port:
        mov al, [esi]
        cmp al, '0'
        jb .port_done
        cmp al, '9'
        ja .port_done
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_port
.port_done:
        cmp ecx, 0
        je .usage
        mov [port], ecx

        ; Resolve hostname
        mov eax, SYS_DNS
        mov ebx, hostname
        int 0x80
        test eax, eax
        jz .dns_fail
        mov [target_ip], eax

        ; Open socket
        mov eax, SYS_SOCKET
        mov ebx, 1              ; TCP
        cmp dword [use_udp], 1
        jne .open_socket
        mov ebx, 2              ; UDP
.open_socket:
        int 0x80
        cmp eax, -1
        je .sock_fail
        mov [fd], eax

        ; Connect
        mov eax, SYS_CONNECT
        mov ebx, [fd]
        mov ecx, [target_ip]
        mov edx, [port]
        int 0x80
        cmp eax, -1
        je .conn_fail

        ; Print "Connected\n"
        mov eax, SYS_PRINT
        mov ebx, msg_connected
        int 0x80

        ; Main relay loop
.relay_loop:
        ; Read from stdin
        mov eax, SYS_STDIN_READ
        mov ebx, send_buf
        mov ecx, SEND_BUF_SIZE - 1
        int 0x80
        test eax, eax
        jle .check_recv

        ; Send to socket
        mov ecx, eax
        mov eax, SYS_SEND
        mov ebx, [fd]
        mov ecx, send_buf
        mov edx, ecx
        int 0x80

.check_recv:
        ; Receive from socket
        mov eax, SYS_RECV
        mov ebx, [fd]
        mov ecx, recv_buf
        mov edx, RECV_BUF_SIZE - 1
        int 0x80
        cmp eax, -1
        je .disconnect
        test eax, eax
        jle .relay_loop

        ; Null-terminate and print
        mov [recv_buf + eax], byte 0
        mov eax, SYS_PRINT
        mov ebx, recv_buf
        int 0x80
        jmp .relay_loop

.disconnect:
        mov eax, SYS_SOCKCLOSE
        mov ebx, [fd]
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns_fail
        int 0x80
        jmp .exit

.sock_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_sock_fail
        int 0x80
        jmp .exit

.conn_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_conn_fail
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

.exit:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

msg_usage:      db "Usage: nc [-u] <host> <port>", 10, 0
msg_connected:  db "Connected.", 10, 0
msg_dns_fail:   db "nc: DNS resolution failed", 10, 0
msg_sock_fail:  db "nc: cannot create socket", 10, 0
msg_conn_fail:  db "nc: connection refused", 10, 0

use_udp:        dd 0
hostname:       times 256 db 0
port:           dd 0
target_ip:      dd 0
fd:             dd -1
arg_buf:        times 512 db 0
send_buf:       times SEND_BUF_SIZE db 0
recv_buf:       times RECV_BUF_SIZE db 0
