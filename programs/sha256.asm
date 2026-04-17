; sha256.asm - SHA-256 hash utility
; Usage: sha256 <filename>
;   Computes and prints the SHA-256 hash of a file.
; Usage: sha256 -s "string"
;   Computes the SHA-256 hash of a string.

%include "syscalls.inc"

MAX_FILE    equ 65536           ; 64 KB max file size

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov esi, arg_buf
        call skip_spaces

        ; Check for -s flag (string mode)
        cmp byte [esi], '-'
        jne .file_mode
        cmp byte [esi+1], 's'
        jne .file_mode
        add esi, 2
        call skip_spaces
        cmp byte [esi], 0
        je .usage
        ; Hash the string directly
        mov [msg_ptr], rsi
        ; Find string length
        xor ecx, ecx
.slen:
        cmp byte [esi + ecx], 0
        je .slen_done
        inc ecx
        jmp .slen
.slen_done:
        mov [msg_len], ecx
        mov rsi, [msg_ptr]
        call sha256_hash
        call print_hash
        ; Print newline
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        jmp .exit

.file_mode:
        cmp byte [esi], 0
        je .usage
        ; Copy filename
        mov edi, filename
        call copy_word
        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .not_found
        mov [msg_len], eax
        mov esi, file_buf
        call sha256_hash
        call print_hash
        ; Print "  filename\n"
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, filename
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        jmp .exit

.not_found:
        mov eax, SYS_PRINT
        mov ebx, msg_not_found
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; SHA-256 implementation
; Input: ESI = message pointer, [msg_len] = length
; Output: hash_out = 32-byte hash
;=======================================================================

; SHA-256 initial hash values (first 32 bits of fractional parts of sqrt(2..19))
sha256_h0: dd 0x6a09e667
sha256_h1: dd 0xbb67ae85
sha256_h2: dd 0x3c6ef372
sha256_h3: dd 0xa54ff53a
sha256_h4: dd 0x510e527f
sha256_h5: dd 0x9b05688c
sha256_h6: dd 0x1f83d9ab
sha256_h7: dd 0x5be0cd19

; SHA-256 round constants (first 32 bits of fractional parts of cube roots of first 64 primes)
sha256_k:
        dd 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
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

sha256_hash:
        PUSHALL

        ; Initialize hash values
        mov eax, [sha256_h0]
        mov [hash_a], eax
        mov eax, [sha256_h1]
        mov [hash_b], eax
        mov eax, [sha256_h2]
        mov [hash_c], eax
        mov eax, [sha256_h3]
        mov [hash_d], eax
        mov eax, [sha256_h4]
        mov [hash_e], eax
        mov eax, [sha256_h5]
        mov [hash_f], eax
        mov eax, [sha256_h6]
        mov [hash_g], eax
        mov eax, [sha256_h7]
        mov [hash_h], eax

        ; Pad the message into pad_buf
        ; Copy message
        mov rsi, [msg_ptr]
        cmp rsi, 0
        jne .hash_has_ptr
        lea esi, [file_buf]
.hash_has_ptr:
        mov edi, pad_buf
        mov ecx, [msg_len]
        mov [.orig_len], ecx
        cld
        rep movsb
        ; Append 0x80
        mov byte [edi], 0x80
        inc edi
        ; Pad with zeros until length % 64 == 56
        mov eax, [.orig_len]
        inc eax                 ; +1 for the 0x80 byte
.pad_zeros:
        mov ecx, eax
        and ecx, 63            ; ecx = current_len % 64
        cmp ecx, 56
        je .pad_done
        mov byte [edi], 0
        inc edi
        inc eax
        jmp .pad_zeros
.pad_done:
        ; Append original length in bits as 64-bit big-endian
        mov eax, [.orig_len]
        shl eax, 3             ; bits = bytes * 8
        bswap eax
        mov dword [edi], 0     ; High 32 bits = 0 (messages < 512 MB)
        mov dword [edi + 4], eax
        add edi, 8

        ; Total padded length
        mov eax, edi
        sub eax, pad_buf
        mov [.padded_len], eax

        ; Process each 64-byte (512-bit) block
        mov esi, pad_buf
.block_loop:
        cmp dword [.padded_len], 0
        je .hash_finalize

        ; Prepare message schedule W[0..63]
        ; W[0..15]: Copy 16 dwords from block (big-endian to native)
        mov edi, w_buf
        mov ecx, 16
.copy_w:
        lodsd                   ; Load 4 bytes from message block
        bswap eax               ; Convert big-endian to little-endian
        stosd
        dec ecx
        jnz .copy_w

        ; W[16..63]: Extend
        mov ecx, 16
.extend_w:
        cmp ecx, 64
        jge .extend_done
        ; s0 = ROTR(W[i-15], 7) ^ ROTR(W[i-15], 18) ^ SHR(W[i-15], 3)
        mov eax, [w_buf + (ecx - 15) * 4]
        mov ebx, eax
        mov edx, eax
        ror eax, 7
        ror ebx, 18
        shr edx, 3
        xor eax, ebx
        xor eax, edx
        mov [.s0_val], eax
        ; s1 = ROTR(W[i-2], 17) ^ ROTR(W[i-2], 19) ^ SHR(W[i-2], 10)
        mov eax, [w_buf + (ecx - 2) * 4]
        mov ebx, eax
        mov edx, eax
        ror eax, 17
        ror ebx, 19
        shr edx, 10
        xor eax, ebx
        xor eax, edx
        ; W[i] = W[i-16] + s0 + W[i-7] + s1
        add eax, [.s0_val]
        add eax, [w_buf + (ecx - 16) * 4]
        add eax, [w_buf + (ecx - 7) * 4]
        mov [w_buf + ecx * 4], eax
        inc ecx
        jmp .extend_w
.extend_done:

        ; Initialize working variables
        mov eax, [hash_a]
        mov [wa], eax
        mov eax, [hash_b]
        mov [wb], eax
        mov eax, [hash_c]
        mov [wc], eax
        mov eax, [hash_d]
        mov [wd], eax
        mov eax, [hash_e]
        mov [we], eax
        mov eax, [hash_f]
        mov [wf], eax
        mov eax, [hash_g]
        mov [wg], eax
        mov eax, [hash_h]
        mov [wh], eax

        ; Compression: 64 rounds
        push rsi
        xor ecx, ecx
.round:
        cmp ecx, 64
        jge .round_done

        ; S1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25)
        mov eax, [we]
        mov ebx, eax
        mov edx, eax
        ror eax, 6
        ror ebx, 11
        ror edx, 25
        xor eax, ebx
        xor eax, edx
        mov [.S1_val], eax

        ; ch = (e & f) ^ (~e & g)
        mov eax, [we]
        mov ebx, eax
        and eax, [wf]
        not ebx
        and ebx, [wg]
        xor eax, ebx
        mov [.ch_val], eax

        ; temp1 = h + S1 + ch + k[i] + w[i]
        mov eax, [wh]
        add eax, [.S1_val]
        add eax, [.ch_val]
        add eax, [sha256_k + ecx * 4]
        add eax, [w_buf + ecx * 4]
        mov [.temp1], eax

        ; S0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22)
        mov eax, [wa]
        mov ebx, eax
        mov edx, eax
        ror eax, 2
        ror ebx, 13
        ror edx, 22
        xor eax, ebx
        xor eax, edx
        mov [.S0_val], eax

        ; maj = (a & b) ^ (a & c) ^ (b & c)
        mov eax, [wa]
        mov ebx, eax
        and eax, [wb]
        and ebx, [wc]
        xor eax, ebx
        mov ebx, [wb]
        and ebx, [wc]
        xor eax, ebx
        mov [.maj_val], eax

        ; temp2 = S0 + maj
        mov eax, [.S0_val]
        add eax, [.maj_val]
        mov [.temp2], eax

        ; Rotate working variables
        mov eax, [wg]
        mov [wh], eax
        mov eax, [wf]
        mov [wg], eax
        mov eax, [we]
        mov [wf], eax
        mov eax, [wd]
        add eax, [.temp1]
        mov [we], eax
        mov eax, [wc]
        mov [wd], eax
        mov eax, [wb]
        mov [wc], eax
        mov eax, [wa]
        mov [wb], eax
        mov eax, [.temp1]
        add eax, [.temp2]
        mov [wa], eax

        inc ecx
        jmp .round
.round_done:
        pop rsi

        ; Add compressed chunk to hash values
        mov eax, [wa]
        add [hash_a], eax
        mov eax, [wb]
        add [hash_b], eax
        mov eax, [wc]
        add [hash_c], eax
        mov eax, [wd]
        add [hash_d], eax
        mov eax, [we]
        add [hash_e], eax
        mov eax, [wf]
        add [hash_f], eax
        mov eax, [wg]
        add [hash_g], eax
        mov eax, [wh]
        add [hash_h], eax

        sub dword [.padded_len], 64
        jmp .block_loop

.hash_finalize:
        ; Copy final hash to hash_out (big-endian format)
        mov edi, hash_out
        mov eax, [hash_a]
        bswap eax
        stosd
        mov eax, [hash_b]
        bswap eax
        stosd
        mov eax, [hash_c]
        bswap eax
        stosd
        mov eax, [hash_d]
        bswap eax
        stosd
        mov eax, [hash_e]
        bswap eax
        stosd
        mov eax, [hash_f]
        bswap eax
        stosd
        mov eax, [hash_g]
        bswap eax
        stosd
        mov eax, [hash_h]
        bswap eax
        stosd

        POPALL
        ret

.orig_len:    dd 0
.padded_len:  dd 0
.s0_val:      dd 0
.S0_val:      dd 0
.S1_val:      dd 0
.ch_val:      dd 0
.maj_val:     dd 0
.temp1:       dd 0
.temp2:       dd 0

;---------------------------------------
; print_hash - Print 32-byte hash as 64 hex chars
;---------------------------------------
print_hash:
        PUSHALL
        mov esi, hash_out
        mov ecx, 32
.ph_loop:
        movzx eax, byte [esi]
        ; High nibble
        mov ebx, eax
        shr ebx, 4
        and ebx, 0x0F
        movzx ebx, byte [hex_chars + ebx]
        push rax
        push rcx
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        pop rax
        ; Low nibble
        and eax, 0x0F
        movzx ebx, byte [hex_chars + eax]
        push rcx
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        inc esi
        dec ecx
        jnz .ph_loop
        POPALL
        ret

;---------------------------------------
; Helper: skip leading spaces
;---------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .sk_done
        inc esi
        jmp skip_spaces
.sk_done:
        ret

;---------------------------------------
; Helper: copy word from ESI to EDI
;---------------------------------------
copy_word:
        lodsb
        cmp al, ' '
        je .cw_done
        cmp al, 0
        je .cw_end
        stosb
        jmp copy_word
.cw_done:
        mov byte [edi], 0
        ret
.cw_end:
        mov byte [edi], 0
        dec esi
        ret

;=======================================================================
; Data
;=======================================================================
hex_chars: db "0123456789abcdef"
msg_usage: db "Usage: sha256 <filename>", 0x0A
           db "       sha256 -s <string>", 0x0A, 0
msg_not_found: db "File not found", 0x0A, 0

section .bss
arg_buf:    resb 256
filename:   resb 256
msg_ptr:    resq 1
msg_len:    resd 1
file_buf:   resb MAX_FILE
pad_buf:    resb MAX_FILE + 128 ; message + padding + length
w_buf:      resd 64             ; Message schedule (64 dwords)

; Hash state
hash_a:     resd 1
hash_b:     resd 1
hash_c:     resd 1
hash_d:     resd 1
hash_e:     resd 1
hash_f:     resd 1
hash_g:     resd 1
hash_h:     resd 1

; Working variables
wa:         resd 1
wb:         resd 1
wc:         resd 1
wd:         resd 1
we:         resd 1
wf:         resd 1
wg:         resd 1
wh:         resd 1

; Output
hash_out:   resb 32
