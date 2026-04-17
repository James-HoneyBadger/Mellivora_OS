; ==========================================================================
; md5sum - MD5 hash utility for Mellivora OS
;
; Usage: md5sum <filename>        Hash a file
;        md5sum -s "string"       Hash a string
;
; Outputs 32-character lowercase hex digest.
; Implements RFC 1321 (MD5 Message-Digest Algorithm).
; ==========================================================================
%include "syscalls.inc"

MAX_FILE equ 32768

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz show_usage

        mov esi, arg_buf

        ; Check for -s flag (string mode)
        cmp word [esi], '-s'
        jne .file_mode
        cmp byte [esi + 2], ' '
        jne .file_mode

        ; String mode: hash the rest of the argument
        add esi, 3
        ; Measure string length
        mov edi, esi
        xor ecx, ecx
.slen:
        cmp byte [edi + ecx], 0
        je .shash
        inc ecx
        jmp .slen
.shash:
        mov [data_ptr], rsi
        mov [data_len], ecx
        jmp do_hash

.file_mode:
        ; Read file
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, file_buf
        int 0x80
        test eax, eax
        jz file_err
        mov qword [data_ptr], file_buf
        mov [data_len], eax

do_hash:
        call md5_compute

        ; Print 32-hex-digit result
        mov esi, md5_state
        mov ecx, 4              ; 4 dwords = 16 bytes
.print_loop:
        lodsd                    ; load dword (little-endian)
        call print_hex_le
        dec ecx
        jnz .print_loop

        ; Print "  filename" or "  -"
        mov eax, SYS_PRINT
        mov ebx, msg_sep
        int 0x80
        mov eax, SYS_PRINT
        mov esi, arg_buf
        cmp word [esi], '-s'
        jne .print_filename
        mov ebx, msg_stdin
        int 0x80
        jmp .newline
.print_filename:
        mov ebx, arg_buf
        int 0x80
.newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
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

; ===================================================================
; MD5 implementation (RFC 1321)
; ===================================================================
; md5_compute - Hash data at [data_ptr], length [data_len]
; Result in md5_state (4 dwords: A, B, C, D)
md5_compute:
        PUSHALL

        ; Init state
        mov dword [md5_state + 0],  0x67452301
        mov dword [md5_state + 4],  0xEFCDAB89
        mov dword [md5_state + 8],  0x98BADCFE
        mov dword [md5_state + 12], 0x10325476

        ; Pad message: append 0x80, zeros, then 64-bit length
        ; Copy data to pad_buf
        mov rsi, [data_ptr]
        mov edi, pad_buf
        mov ecx, [data_len]
        rep movsb

        ; Append 0x80
        mov byte [edi], 0x80
        inc edi

        ; Calculate padding needed: pad to 56 mod 64
        mov eax, [data_len]
        inc eax                  ; +1 for 0x80 byte
        mov edx, eax
        and edx, 63             ; mod 64
        mov ecx, 56
        sub ecx, edx
        jge .pad_ok
        add ecx, 64             ; wraparound
.pad_ok:
        ; Write zero padding
        xor al, al
        rep stosb

        ; Append original length in bits (64-bit LE)
        mov eax, [data_len]
        shl eax, 3              ; bytes to bits
        mov [edi], eax
        mov dword [edi + 4], 0  ; high 32 bits (we only handle < 512MB)
        add edi, 8

        ; Calculate total padded length
        mov eax, edi
        sub eax, pad_buf
        mov [padded_len], eax

        ; Process each 64-byte block
        mov esi, pad_buf
.block_loop:
        cmp dword [padded_len], 0
        jle .md5_done

        call md5_block

        add esi, 64
        sub dword [padded_len], 64
        jmp .block_loop

.md5_done:
        POPALL
        ret

; -------------------------------------------------------------------
; md5_block - Process one 64-byte block at ESI
; Updates md5_state in place
; -------------------------------------------------------------------
md5_block:
        PUSHALL

        ; Copy block to M[0..15] (16 dwords)
        mov edi, md5_m
        mov ecx, 16
        rep movsd
        sub esi, 64             ; restore ESI (movsd advanced it)

        ; Load state into registers
        mov eax, [md5_state + 0]  ; A
        mov ebx, [md5_state + 4]  ; B
        mov ecx, [md5_state + 8]  ; C
        mov edx, [md5_state + 12] ; D

        ; 64 rounds, using macros for each operation type
        ; We unroll the 4 rounds of 16 operations each

        ; Temp storage for round function
        mov [md5_a], eax
        mov [md5_b], ebx
        mov [md5_c], ecx
        mov [md5_d], edx

        ; --- Round 1 (F function): i=0..15 ---
        ; F(B,C,D) = (B & C) | (~B & D)
        %assign i 0
        %rep 16
          call md5_round1_step
        %endrep

        ; --- Round 2 (G function): i=16..31 ---
        %rep 16
          call md5_round2_step
        %endrep

        ; --- Round 3 (H function): i=32..47 ---
        %rep 16
          call md5_round3_step
        %endrep

        ; --- Round 4 (I function): i=48..63 ---
        %rep 16
          call md5_round4_step
        %endrep

        ; Add to state
        mov eax, [md5_a]
        add [md5_state + 0], eax
        mov eax, [md5_b]
        add [md5_state + 4], eax
        mov eax, [md5_c]
        add [md5_state + 8], eax
        mov eax, [md5_d]
        add [md5_state + 12], eax

        POPALL
        ret

; -------------------------------------------------------------------
; MD5 round step functions
; Each reads/writes md5_a/b/c/d and advances md5_round_idx
; -------------------------------------------------------------------
md5_round1_step:
        ; F(B,C,D) = (B & C) | (~B & D)
        mov eax, [md5_b]
        mov ebx, eax
        and eax, [md5_c]
        not ebx
        and ebx, [md5_d]
        or eax, ebx              ; EAX = F
        jmp md5_common

md5_round2_step:
        ; G(B,C,D) = (D & B) | (~D & C)
        mov eax, [md5_d]
        mov ebx, eax
        and eax, [md5_b]
        not ebx
        and ebx, [md5_c]
        or eax, ebx
        jmp md5_common

md5_round3_step:
        ; H(B,C,D) = B ^ C ^ D
        mov eax, [md5_b]
        xor eax, [md5_c]
        xor eax, [md5_d]
        jmp md5_common

md5_round4_step:
        ; I(B,C,D) = C ^ (B | ~D)
        mov eax, [md5_d]
        not eax
        or eax, [md5_b]
        xor eax, [md5_c]
        ; fall through to md5_common

md5_common:
        ; EAX = F/G/H/I result
        ; temp = A + F + K[i] + M[g]
        add eax, [md5_a]
        mov ebx, [md5_round_idx]
        add eax, [md5_k + ebx * 4]

        ; Get g index (depends on round)
        push rax
        call md5_get_g           ; returns EAX = g
        mov ecx, eax
        pop rax
        add eax, [md5_m + ecx * 4]

        ; Rotate left by s[i]
        push rcx
        mov ecx, [md5_round_idx]
        movzx ecx, byte [md5_s + ecx]
        rol eax, cl
        pop rcx

        ; A = D, D = C, C = B, B = B + rotated
        add eax, [md5_b]

        mov ebx, [md5_d]
        mov [md5_a], ebx
        mov ebx, [md5_c]
        mov [md5_d], ebx
        mov ebx, [md5_b]
        mov [md5_c], ebx
        mov [md5_b], eax

        inc dword [md5_round_idx]
        ret

; md5_get_g - Return message index g for round md5_round_idx
; Returns EAX = g
md5_get_g:
        mov eax, [md5_round_idx]
        cmp eax, 16
        jb .g_r1
        cmp eax, 32
        jb .g_r2
        cmp eax, 48
        jb .g_r3
        ; Round 4: g = (7*i) mod 16
        imul eax, 7
        and eax, 15
        ret
.g_r1:  ; Round 1: g = i
        ret
.g_r2:  ; Round 2: g = (5*i + 1) mod 16
        imul eax, 5
        inc eax
        and eax, 15
        ret
.g_r3:  ; Round 3: g = (3*i + 5) mod 16
        imul eax, 3
        add eax, 5
        and eax, 15
        ret

; -------------------------------------------------------------------
; print_hex_le - Print EAX as 8 hex chars in little-endian byte order
; (byte 0 first, then byte 1, etc.)
; -------------------------------------------------------------------
print_hex_le:
        PUSHALL
        mov edx, eax
        mov ecx, 4
.phl_byte:
        mov eax, edx
        and eax, 0xFF
        shr edx, 8
        ; Print high nibble then low nibble
        push rax
        push rcx
        push rdx
        shr eax, 4
        call print_nibble
        pop rdx
        pop rcx
        pop rax
        push rcx
        push rdx
        and eax, 0x0F
        call print_nibble
        pop rdx
        pop rcx
        dec ecx
        jnz .phl_byte
        POPALL
        ret

; print_nibble - Print nibble in AL as hex char
print_nibble:
        cmp al, 10
        jb .pn_dig
        add al, ('a' - 10)
        jmp .pn_out
.pn_dig:
        add al, '0'
.pn_out:
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        ret

; -------------------------------------------------------------------
; Strings
; -------------------------------------------------------------------
msg_usage:    db "Usage: md5sum <filename>", 0x0A
              db "       md5sum -s <string>", 0x0A, 0
msg_err:      db "md5sum: cannot read file", 0x0A, 0
msg_sep:      db "  ", 0
msg_stdin:    db "-", 0

; -------------------------------------------------------------------
; MD5 Constants
; -------------------------------------------------------------------

; Per-round shift amounts (s[0..63])
md5_s:
        db 7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22
        db 5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20
        db 4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23
        db 6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21

; K[0..63] = floor(2^32 * abs(sin(i+1)))
md5_k:
        dd 0xD76AA478, 0xE8C7B756, 0x242070DB, 0xC1BDCEEE
        dd 0xF57C0FAF, 0x4787C62A, 0xA8304613, 0xFD469501
        dd 0x698098D8, 0x8B44F7AF, 0xFFFF5BB1, 0x895CD7BE
        dd 0x6B901122, 0xFD987193, 0xA679438E, 0x49B40821
        dd 0xF61E2562, 0xC040B340, 0x265E5A51, 0xE9B6C7AA
        dd 0xD62F105D, 0x02441453, 0xD8A1E681, 0xE7D3FBC8
        dd 0x21E1CDE6, 0xC33707D6, 0xF4D50D87, 0x455A14ED
        dd 0xA9E3E905, 0xFCEFA3F8, 0x676F02D9, 0x8D2A4C8A
        dd 0xFFFA3942, 0x8771F681, 0x6D9D6122, 0xFDE5380C
        dd 0xA4BEEA44, 0x4BDECFA9, 0xF6BB4B60, 0xBEBFBC70
        dd 0x289B7EC6, 0xEAA127FA, 0xD4EF3085, 0x04881D05
        dd 0xD9D4D039, 0xE6DB99E5, 0x1FA27CF8, 0xC4AC5665
        dd 0xF4292244, 0x432AFF97, 0xAB9423A7, 0xFC93A039
        dd 0x655B59C3, 0x8F0CCC92, 0xFFEFF47D, 0x85845DD1
        dd 0x6FA87E4F, 0xFE2CE6E0, 0xA3014314, 0x4E0811A1
        dd 0xF7537E82, 0xBD3AF235, 0x2AD7D2BB, 0xEB86D391

; -------------------------------------------------------------------
; BSS / working state
; -------------------------------------------------------------------
data_ptr:       dq 0
data_len:       dd 0
padded_len:     dd 0
md5_round_idx:  dd 0
md5_state:      times 4 dd 0    ; A, B, C, D
md5_a:          dd 0
md5_b:          dd 0
md5_c:          dd 0
md5_d:          dd 0
md5_m:          times 16 dd 0   ; message schedule (16 dwords)
arg_buf:        times 256 db 0
file_buf:       times MAX_FILE db 0
pad_buf:        times (MAX_FILE + 128) db 0
