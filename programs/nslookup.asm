; ==========================================================================
; nslookup - DNS lookup utility for Mellivora OS
;
; Usage: nslookup <hostname>       Resolve hostname to IP address
;        nslookup -r <ip>          Reverse format: display IP in dotted form
;
; Uses SYS_DNS to resolve hostnames via the kernel's DNS resolver.
; ==========================================================================
%include "syscalls.inc"

start:
        ; Get command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz show_usage

        ; Check for -r flag (just format an IP)
        mov esi, arg_buf
        cmp word [esi], '-r'
        je show_usage           ; -r not yet implemented

        ; Skip leading spaces
        call skip_spaces

        ; Copy hostname
        mov edi, hostname
        call copy_arg
        cmp byte [hostname], 0
        je show_usage

        ; Print "Resolving: "
        mov eax, SYS_PRINT
        mov ebx, msg_resolving
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_dots
        int 0x80

        ; Perform DNS lookup
        mov eax, SYS_DNS
        mov ebx, hostname
        int 0x80

        ; Check result
        test eax, eax
        jz .dns_fail

        ; Save IP
        mov [resolved_ip], eax

        ; Print result
        mov eax, SYS_PRINT
        mov ebx, msg_address
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_has
        int 0x80

        ; Format IP address as dotted decimal
        mov eax, [resolved_ip]
        call print_ip

        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; Exit success
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

show_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -------------------------------------------------------------------
; print_ip - Print EAX as dotted decimal IP (a.b.c.d)
; IP is in network byte order (big-endian): byte 0 = first octet
; -------------------------------------------------------------------
print_ip:
        PUSHALL
        mov [.ip_val], eax

        ; Octet 1 (low byte)
        movzx eax, byte [.ip_val]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80

        ; Octet 2
        movzx eax, byte [.ip_val + 1]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80

        ; Octet 3
        movzx eax, byte [.ip_val + 2]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80

        ; Octet 4
        movzx eax, byte [.ip_val + 3]
        call print_dec

        POPALL
        ret
.ip_val: dd 0

; -------------------------------------------------------------------
; skip_spaces - Advance ESI past spaces
; -------------------------------------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

; -------------------------------------------------------------------
; copy_arg - Copy from ESI to EDI until space or null
; -------------------------------------------------------------------
copy_arg:
        lodsb
        cmp al, ' '
        je .ca_done
        test al, al
        jz .ca_done
        stosb
        jmp copy_arg
.ca_done:
        mov byte [edi], 0
        ret

; -------------------------------------------------------------------
; Data
; -------------------------------------------------------------------
msg_usage:      db "Usage: nslookup <hostname>", 0x0A
                db "Resolve a hostname to an IP address.", 0x0A, 0
msg_resolving:  db "Resolving: ", 0
msg_dots:       db "...", 0x0A, 0
msg_address:    db "Name:    ", 0
msg_has:        db 0x0A, "Address: ", 0
msg_fail:       db "** DNS lookup failed for: ", 0

; BSS
hostname:       times 256 db 0
arg_buf:        times 256 db 0
resolved_ip:    dd 0
