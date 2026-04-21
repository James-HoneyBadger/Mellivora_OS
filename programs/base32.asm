; base32.asm - Encode or decode a file using Base32 (RFC 4648)
; Usage: base32 [-d] <filename>
;   base32 file        encode file to stdout
;   base32 -d file     decode file to stdout

%include "syscalls.inc"

MAX_FILE    equ 524288          ; 512 KB max

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

        xor ebp, ebp            ; 0 = encode, 1 = decode

        ; Check -d flag
        cmp byte [esi], '-'
        jne .get_file
        cmp byte [esi + 1], 'd'
        jne .get_file
        mov ebp, 1
        add esi, 2
        call skip_spaces

.get_file:
        cmp byte [esi], 0
        je .usage

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

        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .err_read
        mov [file_len], eax

        test ebp, ebp
        jnz .do_decode
        call b32_encode
        jmp .done

.do_decode:
        call b32_decode

.done:
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

;=======================================================================
; b32_encode: encode file_buf[0..file_len-1] to stdout
;=======================================================================
b32_encode:
        pushad
        mov dword [enc_pos], 0          ; current byte position in file_buf

.enc_group:
        ; How many bytes remain?
        mov eax, [file_len]
        sub eax, [enc_pos]
        test eax, eax
        jle .enc_done

        ; Store in n_actual (1-5)
        cmp eax, 5
        jle .enc_few
        mov eax, 5
.enc_few:
        mov [n_actual], eax

        ; Zero out group5
        mov dword [group5], 0
        mov byte [group5 + 4], 0

        ; Copy n_actual bytes from file_buf[enc_pos]
        mov esi, [enc_pos]
        add esi, file_buf
        mov ecx, [n_actual]
        xor edi, edi
.fill_group:
        cmp edi, ecx
        jge .group_filled
        mov al, [esi + edi]
        mov [group5 + edi], al
        inc edi
        jmp .fill_group
.group_filled:

        ; Compute 8 5-bit indices
        movzx eax, byte [group5]        ; b0
        movzx ebx, byte [group5 + 1]    ; b1
        movzx ecx, byte [group5 + 2]    ; b2
        movzx edx, byte [group5 + 3]    ; b3
        movzx esi, byte [group5 + 4]    ; b4

        ; Save b0-b4 to temporaries, compute 8 5-bit chars into chars[]
        mov [b0], eax
        mov [b1], ebx
        mov [b2], ecx
        mov [b3], edx
        mov [b4], esi

        ; c0 = b0 >> 3
        mov eax, [b0]
        shr eax, 3
        and eax, 0x1F
        mov [chars + 0], al

        ; c1 = ((b0 & 7) << 2) | (b1 >> 6)
        mov eax, [b0]
        and eax, 7
        shl eax, 2
        mov ebx, [b1]
        shr ebx, 6
        or eax, ebx
        mov [chars + 1], al

        ; c2 = (b1 >> 1) & 0x1F
        mov eax, [b1]
        shr eax, 1
        and eax, 0x1F
        mov [chars + 2], al

        ; c3 = ((b1 & 1) << 4) | (b2 >> 4)
        mov eax, [b1]
        and eax, 1
        shl eax, 4
        mov ebx, [b2]
        shr ebx, 4
        or eax, ebx
        mov [chars + 3], al

        ; c4 = ((b2 & 0xF) << 1) | (b3 >> 7)
        mov eax, [b2]
        and eax, 0x0F
        shl eax, 1
        mov ebx, [b3]
        shr ebx, 7
        or eax, ebx
        mov [chars + 4], al

        ; c5 = (b3 >> 2) & 0x1F
        mov eax, [b3]
        shr eax, 2
        and eax, 0x1F
        mov [chars + 5], al

        ; c6 = ((b3 & 3) << 3) | (b4 >> 5)
        mov eax, [b3]
        and eax, 3
        shl eax, 3
        mov ebx, [b4]
        shr ebx, 5
        or eax, ebx
        mov [chars + 6], al

        ; c7 = b4 & 0x1F
        mov eax, [b4]
        and eax, 0x1F
        mov [chars + 7], al

        ; Number of valid chars from n_actual bytes:
        ; 1→2, 2→4, 3→5, 4→7, 5→8
        mov eax, [n_actual]
        imul eax, 8
        add eax, 4
        xor edx, edx
        mov ebx, 5
        div ebx
        mov [n_valid], eax

        ; Print 8 chars (valid chars from alphabet, rest '=')
        xor ecx, ecx
.print_chars:
        cmp ecx, 8
        jge .chars_done
        cmp ecx, [n_valid]
        jge .print_pad_char

        movzx eax, byte [chars + ecx]
        cmp eax, 26
        jl .enc_alpha
        sub eax, 26
        add eax, '2'
        jmp .enc_putchar
.enc_alpha:
        add eax, 'A'
.enc_putchar:
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        inc ecx
        jmp .print_chars

.print_pad_char:
        mov eax, SYS_PUTCHAR
        mov ebx, '='
        int 0x80
        inc ecx
        jmp .print_chars

.chars_done:
        ; Advance position
        mov eax, [n_actual]
        add [enc_pos], eax
        jmp .enc_group

.enc_done:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        popad
        ret

;=======================================================================
; b32_decode: decode file_buf to stdout
;=======================================================================
b32_decode:
        push esi
        push edi
        push ebp

        mov esi, file_buf
        mov ecx, [file_len]

.dec_group:
        cmp ecx, 0
        je .dec_done

        ; Read 8 base32 chars (skip whitespace)
        xor edi, edi
.read8:
        cmp edi, 8
        jge .decode8
        cmp ecx, 0
        je .decode8

        mov al, [esi]
        inc esi
        dec ecx

        ; Skip whitespace
        cmp al, ' '
        je .read8
        cmp al, 10
        je .read8
        cmp al, 13
        je .read8

        ; Convert to 5-bit value
        cmp al, '='
        je .is_pad

        ; Uppercase A-Z → 0-25
        cmp al, 'A'
        jl .try_lower
        cmp al, 'Z'
        jg .try_lower
        sub al, 'A'
        mov [chars + edi], al
        inc edi
        jmp .read8
.try_lower:
        ; Lowercase a-z → 0-25
        cmp al, 'a'
        jl .try_digit
        cmp al, 'z'
        jg .try_digit
        sub al, 'a'
        mov [chars + edi], al
        inc edi
        jmp .read8
.try_digit:
        ; '2'-'7' → 26-31
        cmp al, '2'
        jl .skip_char
        cmp al, '7'
        jg .skip_char
        sub al, '2'
        add al, 26
        mov [chars + edi], al
        inc edi
        jmp .read8
.is_pad:
        mov byte [chars + edi], 0xFF    ; sentinel for padding
        inc edi
        jmp .read8
.skip_char:
        jmp .read8

.decode8:
        ; edi = number of chars read (up to 8)
        test edi, edi
        jz .dec_done

        ; Decode 5 bytes from 8 base32 chars
        ; b0 = (c0 << 3) | (c1 >> 2)
        movzx eax, byte [chars]
        shl eax, 3
        movzx ebx, byte [chars + 1]
        cmp bl, 0xFF
        je .dec_out0
        shr ebx, 2
        or eax, ebx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        ; b1 = ((c1 & 3) << 6) | (c2 << 1) | (c3 >> 4)
        movzx eax, byte [chars + 1]
        and eax, 3
        shl eax, 6
        movzx ebx, byte [chars + 2]
        cmp bl, 0xFF
        je .dec_out0
        shl ebx, 1
        or eax, ebx
        movzx ebx, byte [chars + 3]
        cmp bl, 0xFF
        je .dec_out1
        shr ebx, 4
        or eax, ebx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        ; b2 = ((c3 & 0xF) << 4) | (c4 >> 1)
        movzx eax, byte [chars + 3]
        and eax, 0x0F
        shl eax, 4
        movzx ebx, byte [chars + 4]
        cmp bl, 0xFF
        je .dec_out1
        shr ebx, 1
        or eax, ebx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        ; b3 = ((c4 & 1) << 7) | (c5 << 2) | (c6 >> 3)
        movzx eax, byte [chars + 4]
        and eax, 1
        shl eax, 7
        movzx ebx, byte [chars + 5]
        cmp bl, 0xFF
        je .dec_out1
        shl ebx, 2
        or eax, ebx
        movzx ebx, byte [chars + 6]
        cmp bl, 0xFF
        je .dec_out2
        shr ebx, 3
        or eax, ebx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        ; b4 = ((c6 & 7) << 5) | c7
        movzx eax, byte [chars + 6]
        and eax, 7
        shl eax, 5
        movzx ebx, byte [chars + 7]
        cmp bl, 0xFF
        je .dec_out2
        or eax, ebx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80

.dec_out0:
.dec_out1:
.dec_out2:
        jmp .dec_group

.dec_done:
        pop ebp
        pop edi
        pop esi
        ret

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

msg_usage:  db "Usage: base32 [-d] <file>", 10, 0
msg_err:    db "base32: cannot read file", 10, 0

filename:   times 128 db 0
file_len:   dd 0
enc_pos:    dd 0
n_actual:   dd 0
chars:      times 8 db 0
n_valid:    dd 0
group5:     times 5 db 0
b0:         dd 0
b1:         dd 0
b2:         dd 0
b3:         dd 0
b4:         dd 0
arg_buf:    times 256 db 0
file_buf:   times MAX_FILE db 0
