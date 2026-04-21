; crc.asm - CRC calculator (CRC-16 and CRC-32)
; Usage: crc <file>
; Prints CRC-16/CCITT and CRC-32/IEEE checksums

%include "syscalls.inc"

BUF_SIZE    equ 65536

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

        ; Copy filename
        mov edi, filename
        xor ecx, ecx
.copy_fn:
        mov al, [esi]
        cmp al, ' '
        je .fn_done
        cmp al, 0
        je .fn_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_fn
.fn_done:
        mov byte [edi + ecx], 0

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .read_fail
        mov [file_len], eax

        ; Build CRC-32 table
        call build_crc32_table

        ; Compute CRC-32
        mov dword [crc32_val], 0xFFFFFFFF
        xor esi, esi
.crc32_loop:
        cmp esi, [file_len]
        jge .crc32_done
        movzx eax, byte [file_buf + esi]
        xor eax, [crc32_val]
        and eax, 0xFF
        mov eax, [crc32_table + eax*4]
        mov ecx, [crc32_val]
        shr ecx, 8
        xor eax, ecx
        mov [crc32_val], eax
        inc esi
        jmp .crc32_loop
.crc32_done:
        xor dword [crc32_val], 0xFFFFFFFF

        ; Compute CRC-16/CCITT (poly 0x1021, init 0xFFFF)
        mov word [crc16_val], 0xFFFF
        xor esi, esi
.crc16_loop:
        cmp esi, [file_len]
        jge .crc16_done
        movzx ecx, byte [file_buf + esi]
        movzx eax, word [crc16_val]
        shl ecx, 8
        xor eax, ecx
        mov [crc16_val], ax
        ; Process 8 bits
        mov ecx, 8
.crc16_bit:
        movzx eax, word [crc16_val]
        test eax, 0x8000
        jz .crc16_0
        shl eax, 1
        xor eax, 0x1021
        jmp .crc16_store
.crc16_0:
        shl eax, 1
.crc16_store:
        mov [crc16_val], ax
        dec ecx
        jnz .crc16_bit
        inc esi
        jmp .crc16_loop
.crc16_done:

        ; Print results
        mov eax, SYS_PRINT
        mov ebx, msg_file
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, filename
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_crc16
        int 0x80
        movzx eax, word [crc16_val]
        call print_hex32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_crc32
        int 0x80
        mov eax, [crc32_val]
        call print_hex32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_bytes
        int 0x80
        mov eax, [file_len]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .exit

.read_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
.exit:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; Build 256-entry CRC-32 table (poly 0xEDB88320 reflected)
build_crc32_table:
        xor ebx, ebx                ; table index
.bt_loop:
        cmp ebx, 256
        jge .bt_done
        mov eax, ebx
        mov ecx, 8
.bt_bit:
        test eax, 1
        jz .bt_0
        shr eax, 1
        xor eax, 0xEDB88320
        jmp .bt_next
.bt_0:
        shr eax, 1
.bt_next:
        dec ecx
        jnz .bt_bit
        mov [crc32_table + ebx*4], eax
        inc ebx
        jmp .bt_loop
.bt_done:
        ret

print_hex32:
        pushad
        mov edi, 8
        mov ecx, eax
.ph32:
        rol ecx, 4
        mov eax, ecx
        and eax, 0x0F
        cmp eax, 10
        jl .ph32_d
        add eax, 'A' - 10
        jmp .ph32_p
.ph32_d:
        add eax, '0'
.ph32_p:
        push ecx
        push eax
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        pop ecx
        dec edi
        jnz .ph32
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

msg_usage:  db "Usage: crc <file>", 10, 0
msg_err:    db "crc: cannot read file", 10, 0
msg_file:   db "File:   ", 0
msg_crc16:  db "CRC-16: 0x", 0
msg_crc32:  db "CRC-32: 0x", 0
msg_bytes:  db "Bytes:  ", 0

filename:       times 256 db 0
arg_buf:        times 256 db 0
file_len:       dd 0
crc32_val:      dd 0
crc16_val:      dw 0
crc32_table:    times 256 dd 0
file_buf:       times BUF_SIZE db 0
