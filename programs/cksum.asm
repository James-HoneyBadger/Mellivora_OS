; cksum.asm - Compute CRC-32 (IEEE 802.3) checksum and byte count
; Usage: cksum <filename>
; Output: CRC32  BYTECOUNT  FILENAME

%include "syscalls.inc"

MAX_SIZE    equ 524288          ; 512 KB max

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
        mov al, [esi + ecx]
        cmp al, ' '
        je .fn_done
        cmp al, 0
        je .fn_done
        mov [edi + ecx], al
        inc ecx
        jmp .copy_fn
.fn_done:
        mov byte [edi + ecx], 0

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .err_read
        mov [file_size], eax

        ; Build CRC-32 table and compute checksum
        call build_crc_table
        mov esi, file_buf
        mov ecx, [file_size]
        call crc32_compute
        ; EAX = CRC32 (before final XOR)
        xor eax, 0xFFFFFFFF    ; finalize
        mov [result_crc], eax

        ; Print CRC32 (unsigned decimal)
        mov eax, [result_crc]
        call print_udec
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        ; Print size
        mov eax, [file_size]
        call print_udec
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        ; Print filename
        mov eax, SYS_PRINT
        mov ebx, filename
        int 0x80
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

.err_read:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80

.exit:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;---------------------------------------
; build_crc_table - Populate crc_table[256]
;---------------------------------------
CRC_POLY    equ 0xEDB88320     ; Reflected IEEE polynomial

build_crc_table:
        pushad
        xor edi, edi            ; table index i
.bt_loop:
        cmp edi, 256
        jge .bt_done
        mov eax, edi            ; CRC = i
        mov ecx, 8
.bt_bit:
        test eax, 1
        jz .bt_nobit
        shr eax, 1
        xor eax, CRC_POLY
        jmp .bt_next
.bt_nobit:
        shr eax, 1
.bt_next:
        dec ecx
        jnz .bt_bit
        ; Store table entry
        mov [crc_table + edi * 4], eax
        inc edi
        jmp .bt_loop
.bt_done:
        popad
        ret

;---------------------------------------
; crc32_compute: compute CRC-32
; ESI = data pointer
; ECX = byte count
; Returns: EAX = CRC (call xor 0xFFFFFFFF to finalize)
;---------------------------------------
crc32_compute:
        mov eax, 0xFFFFFFFF    ; init register
        test ecx, ecx
        jz .cc_done
.cc_loop:
        movzx edx, byte [esi]
        xor dl, al              ; dl = (crc ^ byte) & 0xFF
        movzx edx, dl
        mov ebx, [crc_table + edx * 4]
        shr eax, 8
        xor eax, ebx
        inc esi
        dec ecx
        jnz .cc_loop
.cc_done:
        ret

;---------------------------------------
; print_udec - print EAX as unsigned decimal
;---------------------------------------
print_udec:
        pushad
        ; EAX may be large (e.g., 0xFFFFFFFF = 4294967295)
        ; Use stack-based digit reversal
        xor ecx, ecx
        mov ebx, 10
        test eax, eax
        jnz .pu_nonzero
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        popad
        ret
.pu_nonzero:
.pu_div:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        cmp eax, 0
        jnz .pu_div
.pu_out:
        pop ebx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .pu_out
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

msg_usage:  db "Usage: cksum <file>", 10, 0
msg_err:    db "cksum: cannot read file", 10, 0

arg_buf:    times 256 db 0
filename:   times 128 db 0
result_crc: dd 0
file_size:  dd 0
crc_table:  times 256 dd 0
file_buf:   times MAX_SIZE db 0
