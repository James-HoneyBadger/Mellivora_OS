;=======================================================================
; net.inc - User-space networking helper library
;
; Provides convenient wrappers for networking syscalls.
; Include after syscalls.inc in programs:
;   %include "lib/net.inc"
;=======================================================================

; Socket types
NET_TCP         equ 1
NET_UDP         equ 2

;---------------------------------------
; net_socket - Create a socket
;  EAX = type (NET_TCP or NET_UDP)
; Returns: EAX = socket fd (-1 on error)
;---------------------------------------
net_socket:
        mov ebx, eax
        mov eax, SYS_SOCKET
        int 0x80
        ret

;---------------------------------------
; net_connect - Connect socket to remote host
;  EAX = socket fd
;  EBX = IP address (little-endian)
;  ECX = port (host byte order)
; Returns: EAX = 0 success, -1 error
;---------------------------------------
net_connect:
        push edx
        mov edx, ecx            ; port
        mov ecx, ebx            ; IP
        mov ebx, eax            ; fd
        mov eax, SYS_CONNECT
        int 0x80
        pop edx
        ret

;---------------------------------------
; net_send - Send data on socket
;  EAX = socket fd
;  EBX = buffer pointer
;  ECX = length
; Returns: EAX = bytes sent (-1 on error)
;---------------------------------------
net_send:
        push edx
        mov edx, ecx            ; length
        mov ecx, ebx            ; buffer
        mov ebx, eax            ; fd
        mov eax, SYS_SEND
        int 0x80
        pop edx
        ret

;---------------------------------------
; net_recv - Receive data from socket
;  EAX = socket fd
;  EBX = buffer pointer
;  ECX = max length
; Returns: EAX = bytes received (0=no data, -1=error/closed)
;---------------------------------------
net_recv:
        push edx
        mov edx, ecx            ; max length
        mov ecx, ebx            ; buffer
        mov ebx, eax            ; fd
        mov eax, SYS_RECV
        int 0x80
        pop edx
        ret

;---------------------------------------
; net_close - Close socket
;  EAX = socket fd
;---------------------------------------
net_close:
        mov ebx, eax
        mov eax, SYS_SOCKCLOSE
        int 0x80
        ret

;---------------------------------------
; net_dns - Resolve hostname to IP
;  ESI = hostname (null-terminated)
; Returns: EAX = IP address (0 on failure)
;---------------------------------------
net_dns:
        mov ebx, esi
        mov eax, SYS_DNS
        int 0x80
        ret

;---------------------------------------
; net_ping - Send ICMP ping
;  EAX = destination IP
; Returns: EAX = RTT in ticks (-1 on timeout)
;---------------------------------------
net_ping:
        mov ebx, eax
        mov eax, SYS_PING
        int 0x80
        ret

;---------------------------------------
; net_bind - Bind socket to local port
;  EAX = socket fd
;  EBX = port
; Returns: EAX = 0 success, -1 error
;---------------------------------------
net_bind:
        mov ecx, ebx            ; port
        mov ebx, eax            ; fd
        mov eax, SYS_BIND
        int 0x80
        ret

;---------------------------------------
; net_listen - Set socket to listening state
;  EAX = socket fd
; Returns: EAX = 0 success, -1 error
;---------------------------------------
net_listen:
        mov ebx, eax
        mov eax, SYS_LISTEN
        int 0x80
        ret

;---------------------------------------
; net_accept - Accept incoming connection
;  EAX = listening socket fd
; Returns: EAX = new socket fd (-1 on timeout)
;---------------------------------------
net_accept:
        mov ebx, eax
        mov eax, SYS_ACCEPT
        int 0x80
        ret

;---------------------------------------
; net_send_line - Send null-terminated string + CRLF
;  EAX = socket fd
;  ESI = string
;---------------------------------------
net_send_line:
        push eax
        push ecx
        push esi
        ; Get string length
        mov ebx, esi
        xor ecx, ecx
.nsl_len:
        cmp byte [ebx + ecx], 0
        je .nsl_got_len
        inc ecx
        jmp .nsl_len
.nsl_got_len:
        ; Send the string
        mov ebx, esi
        call net_send
        ; Send CRLF
        pop esi
        push esi
        mov ebx, .crlf
        mov ecx, 2
        pop esi
        push esi
        push eax
        mov eax, [esp + 12]     ; original fd
        call net_send
        pop eax
        pop esi
        pop ecx
        pop eax
        ret
.crlf: db 0x0D, 0x0A

;---------------------------------------
; net_recv_line - Receive until LF into buffer
;  EAX = socket fd
;  EDI = destination buffer
;  ECX = max length
; Returns: EAX = bytes received, EDI buffer null-terminated
;---------------------------------------
net_recv_line:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.nrl_fd], eax
        mov [.nrl_buf], edi
        mov [.nrl_max], ecx
        mov dword [.nrl_pos], 0

.nrl_loop:
        ; Try to receive one byte at a time
        mov eax, [.nrl_fd]
        mov ebx, .nrl_tmp
        mov ecx, 1
        call net_recv

        cmp eax, 0
        jle .nrl_retry
        ; Got a byte
        movzx edx, byte [.nrl_tmp]
        cmp dl, 0x0A             ; LF
        je .nrl_done
        cmp dl, 0x0D             ; CR - skip
        je .nrl_loop
        ; Store byte
        mov edi, [.nrl_buf]
        add edi, [.nrl_pos]
        mov [edi], dl
        inc dword [.nrl_pos]
        mov ecx, [.nrl_max]
        dec ecx
        cmp [.nrl_pos], ecx
        jge .nrl_done
        jmp .nrl_loop

.nrl_retry:
        cmp eax, -1
        je .nrl_done
        ; No data yet, yield and retry
        mov eax, SYS_YIELD
        int 0x80
        jmp .nrl_loop

.nrl_done:
        ; Null-terminate
        mov edi, [.nrl_buf]
        add edi, [.nrl_pos]
        mov byte [edi], 0
        mov eax, [.nrl_pos]
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

.nrl_fd:   dd 0
.nrl_buf:  dd 0
.nrl_max:  dd 0
.nrl_pos:  dd 0
.nrl_tmp:  db 0

;---------------------------------------
; net_parse_ip - Parse dotted-decimal IP string
;  ESI = string
; Returns: EAX = IP (little-endian, 0 on error)
;---------------------------------------
net_parse_ip:
        push ebx
        push ecx
        push edx
        xor eax, eax
        xor ebx, ebx
        xor ecx, ecx
        xor edx, edx
.npi_loop:
        movzx eax, byte [esi]
        cmp al, '0'
        jb .npi_sep
        cmp al, '9'
        ja .npi_sep
        sub al, '0'
        imul edx, 10
        add edx, eax
        inc esi
        jmp .npi_loop
.npi_sep:
        cmp edx, 255
        ja .npi_fail
        shl ebx, 8
        or ebx, edx
        inc ecx
        xor edx, edx
        cmp al, '.'
        jne .npi_end
        cmp ecx, 4
        jge .npi_fail
        inc esi
        jmp .npi_loop
.npi_end:
        cmp ecx, 4
        jne .npi_fail
        bswap ebx
        mov eax, ebx
        pop edx
        pop ecx
        pop ebx
        ret
.npi_fail:
        xor eax, eax
        pop edx
        pop ecx
        pop ebx
        ret
