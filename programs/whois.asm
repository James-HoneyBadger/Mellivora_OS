; whois.asm - WHOIS client
; Usage: whois <domain>
; Connects to whois.iana.org:43, sends "domain\r\n", prints response

%include "syscalls.inc"
%include "lib/net.inc"

WHOIS_PORT      equ 43
RECV_BUF_SIZE   equ 8192

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

        ; Copy query domain
        mov edi, query
        xor ecx, ecx
.copy_q:
        mov al, [esi]
        cmp al, ' '
        je .q_done
        cmp al, 0
        je .q_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_q
.q_done:
        mov byte [edi + ecx], 0

        ; Build query string: "domain\r\n"
        mov esi, query
        mov edi, send_buf
.copy_to_send:
        lodsb
        test al, al
        jz .q_copied
        stosb
        jmp .copy_to_send
.q_copied:
        mov word [edi], 0x0A0D  ; \r\n
        add edi, 2
        sub edi, send_buf
        mov [send_len], edi

        ; Resolve WHOIS server
        mov eax, SYS_DNS
        mov ebx, whois_server
        int 0x80
        test eax, eax
        jz .dns_fail
        mov [server_ip], eax

        ; Create TCP socket
        mov eax, SYS_SOCKET
        mov ebx, 1
        int 0x80
        cmp eax, -1
        je .sock_fail
        mov [fd], eax

        ; Connect to port 43
        mov eax, SYS_CONNECT
        mov ebx, [fd]
        mov ecx, [server_ip]
        mov edx, WHOIS_PORT
        int 0x80
        cmp eax, -1
        je .conn_fail

        ; Send query
        mov eax, SYS_SEND
        mov ebx, [fd]
        mov ecx, send_buf
        mov edx, [send_len]
        int 0x80

        ; Receive and print response
.recv_loop:
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
        jmp .recv_loop

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

whois_server:   db "whois.iana.org", 0
msg_usage:      db "Usage: whois <domain>", 10, 0
msg_dns_fail:   db "whois: DNS resolution failed", 10, 0
msg_sock_fail:  db "whois: cannot create socket", 10, 0
msg_conn_fail:  db "whois: connection failed", 10, 0

query:          times 256 db 0
server_ip:      dd 0
fd:             dd -1
send_len:       dd 0
arg_buf:        times 256 db 0
send_buf:       times 260 db 0
recv_buf:       times RECV_BUF_SIZE db 0
