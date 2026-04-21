; dig.asm - DNS lookup tool
; Usage: dig <hostname>
;        dig @server <hostname>  (server arg is accepted but ignored)

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

        ; Skip optional @server argument
        cmp byte [esi], '@'
        jne .get_host
        ; Skip to next token
.skip_server:
        mov al, [esi]
        cmp al, ' '
        je .after_server
        cmp al, 0
        je .usage
        inc esi
        jmp .skip_server
.after_server:
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
        je .host_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_host
.host_done:
        mov byte [edi + ecx], 0

        ; Print query header
        mov eax, SYS_PRINT
        mov ebx, msg_hdr
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Perform DNS lookup
        mov eax, SYS_DNS
        mov ebx, hostname
        int 0x80
        test eax, eax
        jz .dns_fail
        mov [resolved_ip], eax

        ; Print result
        mov eax, SYS_PRINT
        mov ebx, msg_answer
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_in_a
        int 0x80

        ; Print IP address in dotted decimal (little-endian)
        mov eax, [resolved_ip]
        ; Byte 0 = LSB
        movzx ebx, al
        call print_dec_small
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        mov eax, [resolved_ip]
        shr eax, 8
        movzx ebx, al
        call print_dec_small
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        mov eax, [resolved_ip]
        shr eax, 16
        movzx ebx, al
        call print_dec_small
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        mov eax, [resolved_ip]
        shr eax, 24
        movzx ebx, al
        call print_dec_small
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_nxdomain
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
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

; print EBX as decimal (0-255)
print_dec_small:
        pushad
        mov eax, ebx
        cmp eax, 0
        jne .pd_nz
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        popad
        ret
.pd_nz:
        xor ecx, ecx
        mov ebx, 10
.pd_div:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        cmp eax, 0
        jne .pd_div
.pd_out:
        pop ebx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .pd_out
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

msg_usage:      db "Usage: dig [@server] <hostname>", 10, 0
msg_hdr:        db ";; Query: ", 0
msg_answer:     db ";; ANSWER SECTION:", 10
                db "; ", 0
msg_in_a:       db "       IN      A       ", 0
msg_nxdomain:   db ";; NXDOMAIN: ", 0

hostname:       times 256 db 0
resolved_ip:    dd 0
arg_buf:        times 512 db 0
