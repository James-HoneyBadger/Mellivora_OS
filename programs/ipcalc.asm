; ipcalc.asm - IP subnet calculator
; Usage: ipcalc <ip/prefix>
;   e.g.  ipcalc 192.168.1.100/24

%include "syscalls.inc"

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

        ; Parse IP address (4 octets separated by '.')
        call parse_octet
        mov [ip_b0], eax
        cmp byte [esi], '.'
        jne .bad_ip
        inc esi
        call parse_octet
        mov [ip_b1], eax
        cmp byte [esi], '.'
        jne .bad_ip
        inc esi
        call parse_octet
        mov [ip_b2], eax
        cmp byte [esi], '.'
        jne .bad_ip
        inc esi
        call parse_octet
        mov [ip_b3], eax

        ; Build 32-bit IP (big-endian for arithmetic: b0 is MSB)
        mov eax, [ip_b0]
        shl eax, 24
        mov ebx, [ip_b1]
        shl ebx, 16
        or eax, ebx
        mov ecx, [ip_b2]
        shl ecx, 8
        or eax, ecx
        or eax, [ip_b3]
        mov [ip_be], eax

        ; Parse prefix length
        cmp byte [esi], '/'
        jne .bad_prefix
        inc esi
        call parse_decimal
        cmp eax, 32
        jg .bad_prefix
        mov [prefix], eax

        ; Compute mask: 0xFFFFFFFF << (32 - prefix)
        mov ecx, 32
        sub ecx, [prefix]
        cmp ecx, 32
        jge .all_zeros_mask
        mov eax, 0xFFFFFFFF
        shl eax, cl
        mov [mask], eax
        jmp .compute
.all_zeros_mask:
        mov dword [mask], 0

.compute:
        ; Network = IP & mask
        mov eax, [ip_be]
        and eax, [mask]
        mov [network], eax

        ; Broadcast = network | (~mask)
        mov eax, [mask]
        not eax
        or eax, [network]
        mov [broadcast], eax

        ; First host = network + 1 (for /32 and /31 same as network)
        mov eax, [network]
        inc eax
        mov [first_host], eax

        ; Last host = broadcast - 1
        mov eax, [broadcast]
        dec eax
        mov [last_host], eax

        ; Host count = 2^(32-prefix) - 2 (for prefix < 31)
        mov ecx, 32
        sub ecx, [prefix]
        cmp ecx, 0
        jle .set_hostcount_0
        mov eax, 1
        shl eax, cl
        cmp ecx, 1
        jl .done_hostcount
        sub eax, 2
        jmp .done_hostcount
.set_hostcount_0:
        xor eax, eax
.done_hostcount:
        mov [host_count], eax

        ; Print results
        mov eax, SYS_PRINT
        mov ebx, msg_addr
        int 0x80
        mov eax, [ip_be]
        call print_ip
        mov eax, SYS_PRINT
        mov ebx, msg_slash
        int 0x80
        mov eax, [prefix]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_netmask
        int 0x80
        mov eax, [mask]
        call print_ip
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_network
        int 0x80
        mov eax, [network]
        call print_ip
        mov eax, SYS_PRINT
        mov ebx, msg_slash
        int 0x80
        mov eax, [prefix]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_broadcast
        int 0x80
        mov eax, [broadcast]
        call print_ip
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_host_min
        int 0x80
        mov eax, [first_host]
        call print_ip
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_host_max
        int 0x80
        mov eax, [last_host]
        call print_ip
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_hosts
        int 0x80
        mov eax, [host_count]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.bad_ip:
        mov eax, SYS_PRINT
        mov ebx, msg_bad_ip
        int 0x80
        jmp .exit

.bad_prefix:
        mov eax, SYS_PRINT
        mov ebx, msg_bad_prefix
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

;--------------------------------------------------
; parse_octet: parse decimal 0-255 from [ESI]
; Returns EAX = value, advances ESI
;--------------------------------------------------
parse_octet:
        xor eax, eax
.po:    mov bl, [esi]
        cmp bl, '0'
        jb .po_done
        cmp bl, '9'
        ja .po_done
        sub bl, '0'
        imul eax, 10
        movzx ebx, bl
        add eax, ebx
        inc esi
        jmp .po
.po_done:
        ret

;--------------------------------------------------
; parse_decimal: parse decimal from [ESI]
; Returns EAX = value, advances ESI
;--------------------------------------------------
parse_decimal:
        xor eax, eax
.pd:    mov bl, [esi]
        cmp bl, '0'
        jb .pd_done
        cmp bl, '9'
        ja .pd_done
        sub bl, '0'
        imul eax, 10
        movzx ebx, bl
        add eax, ebx
        inc esi
        jmp .pd
.pd_done:
        ret

;--------------------------------------------------
; print_ip: print EAX as a.b.c.d (big-endian dword)
;--------------------------------------------------
print_ip:
        pushad
        mov edx, eax
        ; Byte 3 (MSB)
        mov eax, edx
        shr eax, 24
        and eax, 0xFF
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ; Byte 2
        mov eax, edx
        shr eax, 16
        and eax, 0xFF
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ; Byte 1
        mov eax, edx
        shr eax, 8
        and eax, 0xFF
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ; Byte 0 (LSB)
        mov eax, edx
        and eax, 0xFF
        call print_dec
        popad
        ret


skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

msg_usage:      db "Usage: ipcalc <ip/prefix>", 10
                db "  e.g. ipcalc 192.168.1.100/24", 10, 0
msg_bad_ip:     db "ipcalc: invalid IP address", 10, 0
msg_bad_prefix: db "ipcalc: invalid prefix length (0-32)", 10, 0
msg_slash:      db "/", 0
msg_addr:       db "Address:   ", 0
msg_netmask:    db "Netmask:   ", 0
msg_network:    db "Network:   ", 0
msg_broadcast:  db "Broadcast: ", 0
msg_host_min:   db "HostMin:   ", 0
msg_host_max:   db "HostMax:   ", 0
msg_hosts:      db "Hosts:     ", 0

ip_b0:          dd 0
ip_b1:          dd 0
ip_b2:          dd 0
ip_b3:          dd 0
ip_be:          dd 0
prefix:         dd 0
mask:           dd 0
network:        dd 0
broadcast:      dd 0
first_host:     dd 0
last_host:      dd 0
host_count:     dd 0
arg_buf:        times 128 db 0
