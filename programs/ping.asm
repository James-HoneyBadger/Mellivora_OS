; ping.asm - ICMP Ping utility
; Usage: ping <host>
;
; Sends 4 ICMP echo requests and reports round-trip time.

%include "syscalls.inc"
%include "lib/net.inc"

start:
        ; Get command-line arguments
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80
        test eax, eax
        jz .usage

        ; Parse host - try IP first, then DNS
        mov esi, arg_buf
        call net_parse_ip
        test eax, eax
        jnz .have_ip

        ; Try DNS
        mov esi, arg_buf
        call net_dns
        test eax, eax
        jz .dns_fail

.have_ip:
        mov [target_ip], eax

        ; Print header
        mov eax, SYS_PRINT
        mov ebx, msg_ping
        int 0x80

        ; Print target IP
        mov eax, [target_ip]
        call print_ip

        mov eax, SYS_PRINT
        mov ebx, msg_dots
        int 0x80

        ; Send 4 pings
        mov dword [count], 0
        mov dword [success], 0

.ping_loop:
        cmp dword [count], 4
        jge .stats

        ; Ping
        mov eax, [target_ip]
        call net_ping
        cmp eax, -1
        je .timeout

        ; Got reply
        inc dword [success]
        push eax
        mov eax, SYS_PRINT
        mov ebx, msg_reply
        int 0x80
        pop eax

        ; Print RTT (ticks * 10 = ms)
        imul eax, 10
        call print_dec

        mov eax, SYS_PRINT
        mov ebx, msg_ms
        int 0x80
        jmp .next

.timeout:
        mov eax, SYS_PRINT
        mov ebx, msg_timeout
        int 0x80

.next:
        inc dword [count]

        ; Sleep 1 second between pings
        mov eax, SYS_SLEEP
        mov ebx, 100
        int 0x80
        jmp .ping_loop

.stats:
        ; Print summary
        mov eax, SYS_PRINT
        mov ebx, msg_stats
        int 0x80

        mov eax, [success]
        call print_dec

        mov eax, SYS_PRINT
        mov ebx, msg_of
        int 0x80

        mov eax, [count]
        call print_dec

        mov eax, SYS_PRINT
        mov ebx, msg_received
        int 0x80

        ; Exit
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
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;---------------------------------------
; print_ip - Print IP from EAX
;---------------------------------------
print_ip:
        push eax
        push ecx
        mov ecx, eax
        movzx eax, cl
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        movzx eax, ch
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        shr ecx, 16
        movzx eax, cl
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        movzx eax, ch
        call print_dec
        pop ecx
        pop eax
        ret

; Strings
msg_ping:     db "PING ", 0
msg_dots:     db ":", 0x0A, 0
msg_reply:    db "  Reply: time=", 0
msg_ms:       db "ms", 0x0A, 0
msg_timeout:  db "  Request timed out", 0x0A, 0
msg_stats:    db "--- Ping statistics: ", 0
msg_of:       db "/", 0
msg_received: db " packets received", 0x0A, 0
msg_usage:    db "Usage: ping <host>", 0x0A, 0
msg_dns_fail: db "DNS resolution failed", 0x0A, 0

; Data
target_ip:    dd 0
count:        dd 0
success:      dd 0
arg_buf:      times 256 db 0
