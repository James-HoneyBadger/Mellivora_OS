; daytime.asm - Daytime protocol client (RFC 867)
; Usage: daytime <host>
; Connects to host:13, reads and prints time string

%include "syscalls.inc"
%include "lib/net.inc"

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
        mov eax, SYS_DNS
        mov ebx, hostname
        int 0x80
        test eax, eax
        jz .dns_fail
        mov [server_ip], eax

        ; TCP socket + connect
        mov eax, SYS_SOCKET
        mov ebx, 1
        int 0x80
        cmp eax, -1
        je .sock_fail
        mov [fd], eax

        mov eax, SYS_CONNECT
        mov ebx, [fd]
        mov ecx, [server_ip]
        mov edx, DAYTIME_PORT
        int 0x80
        cmp eax, -1
        je .conn_fail

        ; Read response (server sends time string immediately)
        mov eax, SYS_RECV
        mov ebx, [fd]
        mov ecx, recv_buf
        mov edx, RECV_BUF_SIZE - 1
        int 0x80
        cmp eax, -1
        je .done
        test eax, eax
        jz .done
        mov byte [recv_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, recv_buf
        int 0x80
        ; Ensure newline
        cmp byte [recv_buf + eax - 1], 10
        je .done
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.done:
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

msg_usage:      db "Usage: daytime <host>", 10, 0
msg_dns_fail:   db "daytime: DNS resolution failed", 10, 0
msg_sock_fail:  db "daytime: cannot create socket", 10, 0
msg_conn_fail:  db "daytime: connection refused", 10, 0

hostname:       times 256 db 0
server_ip:      dd 0
fd:             dd -1
arg_buf:        times 256 db 0
recv_buf:       times RECV_BUF_SIZE db 0
