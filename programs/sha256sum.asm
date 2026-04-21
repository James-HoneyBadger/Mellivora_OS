; sha256sum.asm - Compute SHA-256 hash of a file (FIPS 180-4)
; Usage: sha256sum <filename>
; Output: <64 hex chars>  <filename>

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
        mov ecx, pad_buf
        int 0x80
        cmp eax, -1
        je .err_read
        mov [msg_len], eax

        call sha256
        call print_hash

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
; sha256 - SHA-256 of pad_buf[0..msg_len-1], result in H0..H7
;=======================================================================
sha256:
        ; Init hash values (first 32 bits of fractional parts of sqrt of primes)
        mov dword [H0], 0x6a09e667
        mov dword [H1], 0xbb67ae85
        mov dword [H2], 0x3c6ef372
        mov dword [H3], 0xa54ff53a
        mov dword [H4], 0x510e527f
        mov dword [H5], 0x9b05688c
        mov dword [H6], 0x1f83d9ab
        mov dword [H7], 0x5be0cd19

        ; Padding: append 0x80
        mov eax, [msg_len]
        mov esi, pad_buf
        add esi, eax
        mov byte [esi], 0x80
        inc esi
        inc eax

        ; Pad zeros until length ≡ 56 (mod 64)
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
        ; Append 8-byte big-endian bit length
        ; bit_len = msg_len * 8 (fits in 32 bits for < 512MB files)
        mov eax, [msg_len]
        shl eax, 3
        ; Write as big-endian 64-bit
        mov dword [esi], 0      ; high 32 bits = 0
        ; low 32 bits in big-endian
        bswap eax
        mov [esi + 4], eax

        ; Total padded length
        mov eax, [msg_len]
        add eax, 9
        add eax, 63
        and eax, ~63
        mov [pad_len], eax

        ; Process 64-byte blocks
        xor esi, esi
.block_loop:
        cmp esi, [pad_len]
        jge .sha_done
        lea eax, [pad_buf + esi]
        push esi
        call sha256_block
        pop esi
        add esi, 64
        jmp .block_loop
.sha_done:
        ret

;=======================================================================
; sha256_block - process one 64-byte block at [EAX]
;=======================================================================
sha256_block:
        pushad
        mov esi, eax

        ; Load W[0..15] from block (big-endian → native)
        xor edi, edi
.load_w:
        cmp edi, 16
        jge .expand_w
        mov eax, [esi + edi * 4]
        bswap eax
        mov [W + edi * 4], eax
        inc edi
        jmp .load_w

        ; Expand W[16..63]
.expand_w:
        ; edi = 16 at this point
.expand_loop:
        cmp edi, 64
        jge .w_done
        ; s0 = ROTR7(W[i-15]) ^ ROTR18(W[i-15]) ^ SHR3(W[i-15])
        mov eax, [W + (edi - 15) * 4]
        mov ecx, eax
        ror ecx, 7
        mov ebx, eax
        ror ebx, 18
        xor ecx, ebx
        shr eax, 3
        xor eax, ecx
        ; s1 = ROTR17(W[i-2]) ^ ROTR19(W[i-2]) ^ SHR10(W[i-2])
        mov ebx, [W + (edi - 2) * 4]
        mov ecx, ebx
        ror ecx, 17
        mov edx, ebx
        ror edx, 19
        xor ecx, edx
        shr ebx, 10
        xor ebx, ecx
        ; W[i] = W[i-16] + s0 + W[i-7] + s1
        add eax, [W + (edi - 16) * 4]
        add eax, [W + (edi - 7) * 4]
        add eax, ebx
        mov [W + edi * 4], eax
        inc edi
        jmp .expand_loop

.w_done:
        ; Init working vars from H0..H7
        mov eax, [H0]
        mov [wa], eax
        mov eax, [H1]
        mov [wb], eax
        mov eax, [H2]
        mov [wc], eax
        mov eax, [H3]
        mov [wd], eax
        mov eax, [H4]
        mov [we], eax
        mov eax, [H5]
        mov [wf], eax
        mov eax, [H6]
        mov [wg2], eax
        mov eax, [H7]
        mov [wh], eax

        xor ebp, ebp
.compress:
        cmp ebp, 64
        jge .comp_done

        ; S1 = ROTR6(e) ^ ROTR11(e) ^ ROTR25(e)
        mov eax, [we]
        mov ecx, eax
        ror ecx, 6
        mov ebx, eax
        ror ebx, 11
        xor ecx, ebx
        mov ebx, eax
        ror ebx, 25
        xor ecx, ebx
        ; ch = (e & f) ^ (~e & g)
        mov edx, eax
        and edx, [wf]
        mov esi, eax
        not esi
        and esi, [wg2]
        xor edx, esi
        ; T1 = h + S1 + ch + K[i] + W[i]
        mov eax, [wh]
        add eax, ecx
        add eax, edx
        add eax, [CK + ebp * 4]
        add eax, [W + ebp * 4]
        mov [T1], eax

        ; S0 = ROTR2(a) ^ ROTR13(a) ^ ROTR22(a)
        mov eax, [wa]
        mov ecx, eax
        ror ecx, 2
        mov ebx, eax
        ror ebx, 13
        xor ecx, ebx
        mov ebx, eax
        ror ebx, 22
        xor ecx, ebx
        ; maj = (a & b) ^ (a & c) ^ (b & c)
        mov edx, eax
        and edx, [wb]
        mov esi, eax
        and esi, [wc]
        xor edx, esi
        mov esi, [wb]
        and esi, [wc]
        xor edx, esi
        ; T2 = S0 + maj
        add ecx, edx
        mov [T2], ecx

        ; Shift working vars
        ; h=g, g=f, f=e, e=d+T1, d=c, c=b, b=a, a=T1+T2
        mov eax, [wg2]
        mov [wh], eax
        mov eax, [wf]
        mov [wg2], eax
        mov eax, [we]
        mov [wf], eax
        mov eax, [wd]
        add eax, [T1]
        mov [we], eax
        mov eax, [wc]
        mov [wd], eax
        mov eax, [wb]
        mov [wc], eax
        mov eax, [wa]
        mov [wb], eax
        mov eax, [T1]
        add eax, [T2]
        mov [wa], eax

        inc ebp
        jmp .compress

.comp_done:
        ; Add compressed chunk to hash
        mov eax, [wa]
        add [H0], eax
        mov eax, [wb]
        add [H1], eax
        mov eax, [wc]
        add [H2], eax
        mov eax, [wd]
        add [H3], eax
        mov eax, [we]
        add [H4], eax
        mov eax, [wf]
        add [H5], eax
        mov eax, [wg2]
        add [H6], eax
        mov eax, [wh]
        add [H7], eax

        popad
        ret

;=======================================================================
; print_hash - print H0..H7 as 64 lowercase hex chars (big-endian)
;=======================================================================
print_hash:
        push esi
        push ecx
        lea esi, [H0]
        mov ecx, 8
.ph_loop:
        mov eax, [esi]
        ; SHA-256 is big-endian: print from MSB to LSB
        push ecx
        push esi
        mov ecx, 8          ; 8 nibbles per dword
.ph_nib:
        push ecx
        push eax
        rol eax, 4          ; rotate MSB nibble into position
        and al, 0x0F
        add al, '0'
        cmp al, '9'
        jle .ph_nok
        add al, ('a' - '0' - 10)
.ph_nok:
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop eax
        rol eax, 4          ; advance to next nibble
        pop ecx
        dec ecx
        jnz .ph_nib
        pop esi
        pop ecx
        add esi, 4
        dec ecx
        jnz .ph_loop
.ph_done:
        pop ecx
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

; SHA-256 round constants (cube roots of first 64 primes)
CK: dd 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
    dd 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
    dd 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
    dd 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
    dd 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
    dd 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
    dd 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
    dd 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
    dd 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
    dd 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
    dd 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
    dd 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
    dd 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
    dd 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
    dd 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
    dd 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2

msg_usage:  db "Usage: sha256sum <file>", 10, 0
msg_err:    db "sha256sum: cannot read file", 10, 0
msg_sep:    db "  ", 0

filename:   times 128 db 0
msg_len:    dd 0
pad_len:    dd 0
H0:         dd 0
H1:         dd 0
H2:         dd 0
H3:         dd 0
H4:         dd 0
H5:         dd 0
H6:         dd 0
H7:         dd 0
wa:         dd 0
wb:         dd 0
wc:         dd 0
wd:         dd 0
we:         dd 0
wf:         dd 0
wg2:        dd 0
wh:         dd 0
T1:         dd 0
T2:         dd 0
W:          times 64 dd 0
arg_buf:    times 256 db 0
pad_buf:    times (MAX_FILE + 128) db 0
