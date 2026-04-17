; ==========================================================================
; encrypt - File encryption/decryption utility for Mellivora OS
;
; Usage: encrypt <key> <infile> <outfile>     Encrypt file
;        encrypt -d <key> <infile> <outfile>  Decrypt file
;
; Algorithm: RC4 stream cipher (symmetric — same key encrypts/decrypts).
; The -d flag is optional; RC4 is symmetric so encrypt and decrypt are
; the same operation. It exists for clarity.
;
; Key: 1-256 character passphrase.
; Max file size: 32 KB.
; ==========================================================================
%include "syscalls.inc"

MAX_FILE    equ 32768
MAX_KEY     equ 256

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz show_usage

        ; Parse arguments
        mov esi, arg_buf

        ; Check for -d flag
        cmp word [esi], '-d'
        jne .no_decrypt_flag
        cmp byte [esi + 2], ' '
        jne .no_decrypt_flag
        add esi, 3
        call skip_sp
.no_decrypt_flag:

        ; Arg 1: key
        mov edi, key_buf
        call copy_token
        call skip_sp

        ; Measure key length
        xor ecx, ecx
        mov edi, key_buf
.klen:
        cmp byte [edi + ecx], 0
        je .klen_done
        inc ecx
        cmp ecx, MAX_KEY
        jge .klen_done
        jmp .klen
.klen_done:
        test ecx, ecx
        jz show_usage
        mov [key_len], ecx

        ; Arg 2: input file
        mov edi, infile
        call copy_token
        call skip_sp
        cmp byte [infile], 0
        je show_usage

        ; Arg 3: output file
        mov edi, outfile
        call copy_token
        cmp byte [outfile], 0
        je show_usage

        ; Read input file
        mov eax, SYS_FREAD
        mov ebx, infile
        mov ecx, file_buf
        int 0x80
        test eax, eax
        jz file_err
        mov [file_len], eax

        ; Initialize RC4 state (KSA)
        call rc4_ksa

        ; Encrypt/decrypt (PRGA XOR)
        call rc4_crypt

        ; Write output file
        mov eax, SYS_FWRITE
        mov ebx, outfile
        mov ecx, file_buf
        mov edx, [file_len]
        mov esi, FTYPE_TEXT
        int 0x80
        cmp eax, -1
        je write_err

        ; Success message
        mov eax, SYS_PRINT
        mov ebx, infile
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_arrow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, outfile
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_paren
        int 0x80
        mov eax, [file_len]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_bytes_nl
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_file_err
        int 0x80
        jmp exit_err

write_err:
        mov eax, SYS_PRINT
        mov ebx, msg_write_err
        int 0x80
        jmp exit_err

show_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
exit_err:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; ===================================================================
; RC4 Key Scheduling Algorithm (KSA)
; Initializes S-box from key
; ===================================================================
rc4_ksa:
        PUSHALL
        ; Initialize S[0..255] = identity
        xor ecx, ecx
.ksa_init:
        mov [rc4_s + ecx], cl
        inc ecx
        cmp ecx, 256
        jb .ksa_init

        ; Permute S using key
        xor ecx, ecx            ; i = 0
        xor edx, edx            ; j = 0
.ksa_loop:
        cmp ecx, 256
        jge .ksa_done

        ; j = (j + S[i] + key[i mod keylen]) mod 256
        movzx eax, byte [rc4_s + ecx]
        add edx, eax
        ; key[i mod keylen]
        mov eax, ecx
        push rdx
        xor edx, edx
        div dword [key_len]      ; EDX = i mod keylen
        movzx eax, byte [key_buf + edx]
        pop rdx
        add edx, eax
        and edx, 0xFF

        ; Swap S[i], S[j]
        movzx eax, byte [rc4_s + ecx]
        movzx ebx, byte [rc4_s + edx]
        mov [rc4_s + ecx], bl
        mov [rc4_s + edx], al

        inc ecx
        jmp .ksa_loop

.ksa_done:
        POPALL
        ret

; ===================================================================
; RC4 PRGA — XOR file_buf in place
; ===================================================================
rc4_crypt:
        PUSHALL
        xor ecx, ecx            ; i = 0
        xor edx, edx            ; j = 0
        mov esi, file_buf
        mov edi, [file_len]

.prga_loop:
        test edi, edi
        jz .prga_done

        ; i = (i + 1) mod 256
        inc ecx
        and ecx, 0xFF

        ; j = (j + S[i]) mod 256
        movzx eax, byte [rc4_s + ecx]
        add edx, eax
        and edx, 0xFF

        ; Swap S[i], S[j]
        movzx eax, byte [rc4_s + ecx]
        movzx ebx, byte [rc4_s + edx]
        mov [rc4_s + ecx], bl
        mov [rc4_s + edx], al

        ; K = S[(S[i] + S[j]) mod 256]
        movzx eax, byte [rc4_s + ecx]
        movzx ebx, byte [rc4_s + edx]
        add eax, ebx
        and eax, 0xFF
        movzx eax, byte [rc4_s + eax]

        ; XOR data byte
        xor [esi], al
        inc esi
        dec edi
        jmp .prga_loop

.prga_done:
        POPALL
        ret

; ===================================================================
; Helpers
; ===================================================================
skip_sp:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_sp
.done:  ret

copy_token:
.ct_loop:
        lodsb
        test al, al
        jz .ct_end
        cmp al, ' '
        je .ct_end
        stosb
        jmp .ct_loop
.ct_end:
        mov byte [edi], 0
        ret

; -------------------------------------------------------------------
; Strings
; -------------------------------------------------------------------
msg_usage:      db "Usage: encrypt <key> <infile> <outfile>", 0x0A
                db "       encrypt -d <key> <infile> <outfile>", 0x0A
                db "RC4 stream cipher (symmetric: same command decrypts).", 0x0A, 0
msg_file_err:   db "encrypt: cannot read input file", 0x0A, 0
msg_write_err:  db "encrypt: cannot write output file", 0x0A, 0
msg_arrow:      db " -> ", 0
msg_paren:      db " (", 0
msg_bytes_nl:   db " bytes)", 0x0A, 0

; -------------------------------------------------------------------
; BSS
; -------------------------------------------------------------------
key_len:        dd 0
file_len:       dd 0
arg_buf:        times 512 db 0
key_buf:        times (MAX_KEY + 1) db 0
infile:         times 256 db 0
outfile:        times 256 db 0
rc4_s:          times 256 db 0          ; RC4 S-box
file_buf:       times MAX_FILE db 0
