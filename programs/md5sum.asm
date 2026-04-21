; md5sum.asm - Compute MD5 hash of a file (RFC 1321)
; Usage: md5sum <filename>
; Output: <32 hex chars>  <filename>

%include "syscalls.inc"

MAX_FILE    equ 524288          ; 512 KB max read

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

        ; Read file into pad_buf directly
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, pad_buf
        int 0x80
        cmp eax, -1
        je .err_read
        mov [msg_len], eax

        ; Compute MD5 and print hash
        call md5
        call print_hash

        ; "  filename\n"
        mov eax, SYS_PRINT
        mov ebx, msg_sep
        int 0x80
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

;=======================================================================
; md5 - Compute MD5 of pad_buf[0..msg_len-1], result in h0..h3
;=======================================================================
md5:
        ; Init state
        mov dword [h0], 0x67452301
        mov dword [h1], 0xEFCDAB89
        mov dword [h2], 0x98BADCFE
        mov dword [h3], 0x10325476

        ; Pad: append 0x80
        mov eax, [msg_len]
        mov esi, pad_buf
        add esi, eax
        mov byte [esi], 0x80
        inc esi
        inc eax

        ; Append zeros until len ≡ 56 (mod 64)
.zero_loop:
        mov ecx, eax
        and ecx, 63
        cmp ecx, 56
        je .pad_done
        mov byte [esi], 0
        inc esi
        inc eax
        jmp .zero_loop
.pad_done:
        ; Append 8-byte little-endian bit length
        mov eax, [msg_len]
        shl eax, 3
        mov [esi], eax
        mov dword [esi + 4], 0

        ; Compute total padded length (must be multiple of 64)
        mov eax, [msg_len]
        add eax, 9              ; +1 (0x80) +8 (length)
        add eax, 63
        and eax, ~63
        mov [pad_len], eax

        ; Process 64-byte blocks
        xor esi, esi            ; block offset
.block_loop:
        cmp esi, [pad_len]
        jge .md5_done
        lea eax, [pad_buf + esi]
        push esi
        call md5_block
        pop esi
        add esi, 64
        jmp .block_loop
.md5_done:
        ret

;=======================================================================
; md5_block - process one 64-byte block at [EAX]
;=======================================================================
md5_block:
        pushad

        ; Load M[0..15]
        mov esi, eax
        mov edi, M
        mov ecx, 16
.load_m:
        mov eax, [esi]
        mov [edi], eax
        add esi, 4
        add edi, 4
        dec ecx
        jnz .load_m

        ; Init a..d from hash state
        mov eax, [h0]
        mov [wa], eax
        mov eax, [h1]
        mov [wb], eax
        mov eax, [h2]
        mov [wc], eax
        mov eax, [h3]
        mov [wd], eax

        ; 64 rounds using only memory for state
        xor ebp, ebp
.rounds:
        cmp ebp, 64
        jge .rounds_done

        ; Compute F and g based on round number
        cmp ebp, 16
        jl .f0
        cmp ebp, 32
        jl .f1
        cmp ebp, 48
        jl .f2

.f3:    ; F = C ^ (B | ~D),  g = 7i mod 16
        mov eax, [wd]
        not eax
        or eax, [wb]
        xor eax, [wc]
        mov [wF], eax
        mov eax, ebp
        imul eax, 7
        and eax, 15
        mov [wg], eax
        jmp .do_round

.f0:    ; F = (B & C) | (~B & D),  g = i
        mov eax, [wb]
        and eax, [wc]
        mov ecx, [wb]
        not ecx
        and ecx, [wd]
        or eax, ecx
        mov [wF], eax
        mov [wg], ebp
        jmp .do_round

.f1:    ; F = (D & B) | (~D & C),  g = (5i+1) mod 16
        mov eax, [wd]
        and eax, [wb]
        mov ecx, [wd]
        not ecx
        and ecx, [wc]
        or eax, ecx
        mov [wF], eax
        mov eax, ebp
        imul eax, 5
        inc eax
        and eax, 15
        mov [wg], eax
        jmp .do_round

.f2:    ; F = B ^ C ^ D,  g = (3i+5) mod 16
        mov eax, [wb]
        xor eax, [wc]
        xor eax, [wd]
        mov [wF], eax
        mov eax, ebp
        imul eax, 3
        add eax, 5
        and eax, 15
        mov [wg], eax

.do_round:
        ; temp = A + F + K[i] + M[g]
        mov eax, [wa]
        add eax, [wF]
        add eax, [K + ebp * 4]
        mov ecx, [wg]
        add eax, [M + ecx * 4]
        ; leftrotate(temp, s[i])
        movzx ecx, byte [Sv + ebp]
        rol eax, cl
        ; new B = old B + rotated temp
        add eax, [wb]

        ; Shift: A=D, D=C, C=B, B=new_val
        mov ecx, [wd]
        mov [wa], ecx
        mov ecx, [wc]
        mov [wd], ecx
        mov ecx, [wb]
        mov [wc], ecx
        mov [wb], eax

        inc ebp
        jmp .rounds

.rounds_done:
        mov eax, [wa]
        add [h0], eax
        mov eax, [wb]
        add [h1], eax
        mov eax, [wc]
        add [h2], eax
        mov eax, [wd]
        add [h3], eax

        popad
        ret

;=======================================================================
; print_hash - print h0..h3 as 32 lowercase hex chars (little-endian)
;=======================================================================
print_hash:
        push esi
        push ecx
        lea esi, [h0]
        mov ecx, 4
.ph_loop:
        ; Each 32-bit word printed as 4 bytes, LSB first (little-endian)
        mov eax, [esi]
        push ecx
        push esi
        mov ecx, 4
.ph_byte:
        push ecx
        push eax
        movzx eax, al
        call print_hex_byte
        pop eax
        shr eax, 8
        pop ecx
        dec ecx
        jnz .ph_byte
        pop esi
        pop ecx
        add esi, 4
        dec ecx
        jnz .ph_loop
        pop ecx
        pop esi
        ret

; Print AL as 2 lowercase hex digits
print_hex_byte:
        push eax
        push ebx
        push ecx
        movzx ecx, al
        mov al, cl
        shr al, 4
        call nibble_to_hex
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        mov al, cl
        and al, 0x0F
        call nibble_to_hex
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ecx
        pop ebx
        pop eax
        ret

nibble_to_hex:
        and al, 0x0F
        add al, '0'
        cmp al, '9'
        jle .ok
        add al, ('a' - '0' - 10)
.ok:    ret

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

; Shift amounts per round
Sv: db 7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22
    db 5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20
    db 4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23
    db 6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21

; K constants = floor(abs(sin(i+1)) * 2^32)
K:  dd 0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee
    dd 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501
    dd 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be
    dd 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821
    dd 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa
    dd 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8
    dd 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed
    dd 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a
    dd 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c
    dd 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70
    dd 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05
    dd 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665
    dd 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039
    dd 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1
    dd 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1
    dd 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391

msg_usage:  db "Usage: md5sum <file>", 10, 0
msg_err:    db "md5sum: cannot read file", 10, 0
msg_sep:    db "  ", 0

filename:   times 128 db 0
msg_len:    dd 0
pad_len:    dd 0
h0:         dd 0
h1:         dd 0
h2:         dd 0
h3:         dd 0
wa:         dd 0
wb:         dd 0
wc:         dd 0
wd:         dd 0
wF:         dd 0
wg:         dd 0
M:          times 16 dd 0
arg_buf:    times 256 db 0
pad_buf:    times (MAX_FILE + 128) db 0
