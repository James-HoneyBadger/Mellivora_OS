; daytime.asm - Daytime protocol client (RFC 867)
; Usage: daytime <host>
; Connects to host:13, reads and prints time string

%include "syscalls.inc"
%include "lib/net.inc"
%include "lib/string.inc"

DAYTIME_PORT    equ 13
RECV_BUF_SIZE   equ 256

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

        ; Copy hostname
        mov edi, hostname
        xor ecx, ecx
.copy_h:
        mov al, [esi]
        cmp al, ' '
        je .h_done
        cmp al, 0
        je .h_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_h
.h_done:
        mov byte [edi + ecx], 0

        ; Resolve hostname
        mov esi, hostname
        call net_dns
        test eax, eax
        jz .dns_fail
        mov [server_ip], eax

        ; TCP socket
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .sock_fail
        mov [fd], eax

        ; Connect
        mov eax, [fd]
        mov ebx, [server_ip]
        mov ecx, DAYTIME_PORT
        call net_connect
        cmp eax, -1
        je .conn_fail

        ; Read response (server sends time string immediately)
        mov eax, [fd]
        mov ebx, recv_buf
        mov ecx, RECV_BUF_SIZE - 1
        call net_recv
        cmp eax, -1
        je .done
        test eax, eax
        jz .done
        mov byte [recv_buf + eax], 0
        push eax
        mov eax, SYS_PRINT
        mov ebx, recv_buf
        int 0x80
        pop eax
        ; Ensure newline
        cmp byte [recv_buf + eax - 1], 10
        je .done
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.done:
        mov eax, [fd]
        call net_close
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

msg_usage:      db "Usage: daytime <host>", 10, 0
msg_dns_fail:   db "daytime: DNS resolution failed", 10, 0
msg_sock_fail:  db "daytime: cannot create socket", 10, 0
msg_conn_fail:  db "daytime: connection refused", 10, 0

hostname:       times 256 db 0
server_ip:      dd 0
fd:             dd -1
arg_buf:        times 256 db 0
recv_buf:       times RECV_BUF_SIZE db 0
