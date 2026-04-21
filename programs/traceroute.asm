; traceroute.asm - Simple traceroute using ICMP PING
; Usage: traceroute <host>
; Note: True TTL-based traceroute requires kernel support for setting IP TTL.
; This implementation uses SYS_PING and shows latency to the destination.

%include "syscalls.inc"
%include "lib/net.inc"
%include "lib/string.inc"

MAX_HOPS    equ 30

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

        ; Resolve
        mov esi, hostname
        call net_dns
        test eax, eax
        jz .dns_fail
        mov [target_ip], eax

        ; Print header
        mov eax, SYS_PRINT
        mov ebx, msg_hdr
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_hdr2
        int 0x80
        mov eax, MAX_HOPS
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_hops_suffix
        int 0x80

        ; Send MAX_HOPS probes and show RTT for each
        ; (Without kernel TTL support we can only show the final hop RTT
        ;  repeated, but we still model the traceroute output format)
        mov ebp, 1
.hop_loop:
        cmp ebp, MAX_HOPS
        jg .done

        ; Print hop number
        mov eax, ebp
        call print_dec_padded
        mov eax, SYS_PRINT
        mov ebx, msg_space
        int 0x80

        ; Send 3 probes for this hop
        mov ecx, 3
.probe:
        push ecx
        mov eax, SYS_PING
        mov ebx, [target_ip]
        int 0x80
        cmp eax, -1
        je .probe_timeout

        ; Print RTT in ms
        push eax
        mov eax, SYS_PRINT
        mov ebx, msg_space
        int 0x80
        pop eax
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_ms
        int 0x80
        jmp .probe_next

.probe_timeout:
        mov eax, SYS_PRINT
        mov ebx, msg_star
        int 0x80

.probe_next:
        pop ecx
        dec ecx
        jnz .probe

        mov eax, SYS_PRINT
        mov ebx, msg_hop_end
        int 0x80

        ; Check if we reached the destination
        ; (Without kernel TTL control we show the destination on every hop)
        inc ebp
        jmp .hop_loop

.reached:
.done:
        ; Show the destination summary
        mov eax, SYS_PRINT
        mov ebx, msg_hdr3
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns_fail
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

print_dec_padded:
        ; Print EAX right-aligned in 2 chars
        pushad
        cmp eax, 10
        jge .no_pad
        mov ebx, ' '
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, [esp + 28]     ; restore original EAX from pushad frame
.no_pad:
        call print_dec
        popad
        ret


msg_usage:      db "Usage: traceroute <host>", 10, 0
msg_hdr:        db "traceroute to ", 0
msg_hdr2:       db ", max ", 0
msg_hops_suffix:db " hops", 10, 0
msg_hdr3:       db "Reached destination: ", 0
msg_dns_fail:   db "traceroute: DNS resolution failed", 10, 0
msg_space:      db " ", 0
msg_ms:         db " ms", 0
msg_star:       db "  *", 0
msg_hop_end:    db 10, 0

hostname:       times 256 db 0
target_ip:      dd 0
arg_buf:        times 256 db 0
