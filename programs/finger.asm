; finger.asm - Finger protocol client (RFC 1288)
; Usage: finger [user@]host
;        finger user@host   query user info on host
;        finger host        list logged-in users (empty query)

%include "syscalls.inc"
%include "lib/net.inc"
%include "lib/string.inc"

FINGER_PORT     equ 79
RECV_BUF_SIZE   equ 4096

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

        ; Parse [user@]host
        ; Find '@' — if present, split into user and host
        mov edi, esi
.find_at:
        mov al, [edi]
        test al, al
        jz .no_at
        cmp al, '@'
        je .found_at
        inc edi
        jmp .find_at

.found_at:
        ; esi = start, edi = '@' position
        mov byte [edi], 0       ; terminate user string
        ; Copy user
        mov ecx, esi
        mov esi, edi
        inc esi                 ; host starts after '@'
        mov edi, username
        xor ebx, ebx
.copy_user:
        mov al, [ecx + ebx]
        mov [edi + ebx], al
        test al, al
        jz .user_copied
        inc ebx
        jmp .copy_user
.user_copied:
        jmp .copy_hostname

.no_at:
        ; No '@' — hostname is the whole arg, no user
        mov byte [username], 0

.copy_hostname:
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

        ; Build query: "user\r\n" or "\r\n"
        mov edi, send_buf
        mov esi, username
.q_user:
        lodsb
        test al, al
        jz .q_crlf
        stosb
        jmp .q_user
.q_crlf:
        mov word [edi], 0x0A0D
        add edi, 2
        sub edi, send_buf
        mov [send_len], edi

        ; Resolve hostname
        mov esi, hostname
        call net_dns
        test eax, eax
        jz .dns_fail
        mov [server_ip], eax

        ; Create TCP socket and connect
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .sock_fail
        mov [fd], eax

        mov eax, [fd]
        mov ebx, [server_ip]
        mov ecx, FINGER_PORT
        call net_connect
        cmp eax, -1
        je .conn_fail

        ; Send query
        mov eax, [fd]
        mov ebx, send_buf
        mov ecx, [send_len]
        call net_send

        ; Print response
.recv_loop:
        mov eax, [fd]
        mov ebx, recv_buf
        mov ecx, RECV_BUF_SIZE - 1
        call net_recv
        cmp eax, -1
        je .done
        test eax, eax
        jz .done
        mov byte [recv_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, recv_buf
        int 0x80
        jmp .recv_loop

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

msg_usage:      db "Usage: finger [user@]host", 10, 0
msg_dns_fail:   db "finger: DNS resolution failed", 10, 0
msg_sock_fail:  db "finger: cannot create socket", 10, 0
msg_conn_fail:  db "finger: connection refused", 10, 0

username:       times 64 db 0
hostname:       times 256 db 0
server_ip:      dd 0
fd:             dd -1
send_len:       dd 0
arg_buf:        times 256 db 0
send_buf:       times 70 db 0
recv_buf:       times RECV_BUF_SIZE db 0
