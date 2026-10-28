;=======================================================================
; aes.inc - AES-256 and XTS, for HBFS volume encryption
;
; docs/spec/STORAGE.md section 8: file data is AES-256 in XTS mode over 4 KiB
; units. This module is the primitive - key expansion, one-block encrypt and
; decrypt, and the XTS wrap that turns a 4 KiB block into ciphertext in place.
;
; Design notes:
;   * Byte-oriented. An S-box table is the only data; the column mixes are
;     computed with xtime and a double-and-add multiply. 4 KiB is 256 AES
;     blocks, which costs milliseconds on a Pentium 4 - the volume is encrypted
;     per block, so that is disk latency, not a hot loop, and correctness is
;     worth more than the constant factor.
;   * The XTS tweak is the absolute block number, so every 4 KiB unit the
;     volume writes has a unique tweak for the life of the key, and a block
;     moved by defrag is re-encrypted under its new number.
;   * Two independent XTS keys are derived from the one 32-byte secret an
;     operator keeps, each under its own domain constant, rather than asking
;     for a second secret.
;   * The state is static, so the module is not re-entrant. The block cache
;     lock serialises every caller and the keys are set up at mount, before any
;     block I/O.
;=======================================================================

AES_ROUNDS_256       equ 14
AES_EXPANDED_BYTES   equ 240        ; 15 round keys x 16 bytes
AES_BLOCK_BYTES      equ 16
AES_UNIT_BYTES       equ 4096
AES_UNIT_BLOCKS      equ AES_UNIT_BYTES / AES_BLOCK_BYTES   ; 256
AES_SECRET_BYTES     equ 32
AES_KEY_BYTES        equ 32

; Domain blocks. Exactly 16 bytes each; the second half of each derived key is
; the same block with its last byte incremented, so one AES input yields each
; half.
aes_domain_data:     db 'HBFS-XTS-DATA-K1'
aes_domain_tweak:    db 'HBFS-XTS-TWK-K1'
aes_domain_check:    db 'HBFS-CRYPT-CHECK'

align 16
aes_sbox:
        db 0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76
        db 0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0
        db 0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15
        db 0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75
        db 0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84
        db 0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf
        db 0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8
        db 0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2
        db 0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73
        db 0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb
        db 0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79
        db 0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08
        db 0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a
        db 0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e
        db 0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf
        db 0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16

aes_inv_sbox:
        db 0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb
        db 0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb
        db 0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e
        db 0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25
        db 0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92
        db 0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84
        db 0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06
        db 0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b
        db 0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73
        db 0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e
        db 0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b
        db 0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4
        db 0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f
        db 0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef
        db 0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61
        db 0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d

aes_rcon:
        db 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40

align 16
aes_key_data:    times AES_EXPANDED_BYTES db 0
aes_key_tweak:   times AES_EXPANDED_BYTES db 0
aes_ready:       dd 0
aes_state:       times AES_BLOCK_BYTES db 0
aes_scratch:     times AES_BLOCK_BYTES db 0
aes_tweak:       times AES_BLOCK_BYTES db 0

;-----------------------------------------------------------------------
; aes_xtime - multiply a byte by x in GF(2^8)
;   In : EAX = byte   Out: EAX = product
;-----------------------------------------------------------------------
aes_xtime:
        shl eax, 1
        test eax, 0x100
        jz .axt_done
        xor eax, 0x11B
.axt_done:
        and eax, 0xFF
        ret

;-----------------------------------------------------------------------
; aes_gmul - multiply two bytes in GF(2^8)
;   In : EAX = a, EBX = b   Out: EAX = a*b
;-----------------------------------------------------------------------
aes_gmul:
        push ecx
        push edx
        push esi
        xor esi, esi
        mov ecx, 8
.agm_loop:
        test bl, 1
        jz .agm_skip
        xor esi, eax
.agm_skip:
        call aes_xtime
        shr ebx, 1
        dec ecx
        jnz .agm_loop
        mov eax, esi
        pop esi
        pop edx
        pop ecx
        ret

; Working storage for the expansion and the S-box round, module scope so that
; RotWord/SubWord can reach it without ending the expander's local labels.
aes_ss_secret: dd 0
aes_exp_i:   dd 0
aes_exp_t:   dd 0
aes_exp_src: dd 0
aes_exp_dst: dd 0

;-----------------------------------------------------------------------
; aes_expand_256 - expand a 256-bit key into 15 round keys
;   In : ESI = 32-byte key, EDI = 240-byte destination
;-----------------------------------------------------------------------
aes_expand_256:
        pushad
        mov [aes_exp_dst], edi
        mov [aes_exp_src], esi

        mov ecx, 8                      ; w[0..7] = the key
        mov esi, [aes_exp_src]
        mov edi, [aes_exp_dst]
        rep movsd

        mov dword [aes_exp_i], 8
.aex_loop:
        mov eax, [aes_exp_i]
        cmp eax, 60
        jae .aex_done

        mov ecx, eax
        dec ecx
        shl ecx, 2
        add ecx, [aes_exp_dst]
        mov eax, [ecx]                  ; temp = w[i-1]
        mov [aes_exp_t], eax

        mov eax, [aes_exp_i]
        test eax, 7
        jnz .aex_try4
        call aes_rot_word               ; RotWord
        call aes_sub_word               ; SubWord
        mov eax, [aes_exp_i]
        shr eax, 3
        lea esi, [aes_rcon]
        movzx ecx, byte [esi + eax - 1]
        xor [aes_exp_t], ecx               ; xor Rcon
        jmp .aex_mix
.aex_try4:
        mov eax, [aes_exp_i]
        and eax, 7
        cmp eax, 4
        jne .aex_mix
        mov eax, [aes_exp_t]
        call aes_sub_word               ; SubWord only
.aex_mix:
        mov ecx, [aes_exp_i]
        sub ecx, 8
        shl ecx, 2
        add ecx, [aes_exp_dst]
        mov eax, [ecx]
        xor eax, [aes_exp_t]
        mov ecx, [aes_exp_i]
        shl ecx, 2
        add ecx, [aes_exp_dst]
        mov [ecx], eax
        inc dword [aes_exp_i]
        jmp .aex_loop
.aex_done:
        popad
        ret

; RotWord: b0 b1 b2 b3 -> b1 b2 b3 b0, on the dword held in [aes_exp_t]
aes_rot_word:
        mov eax, [aes_exp_t]
        ror eax, 8
        mov [aes_exp_t], eax
        ret

; SubWord: each byte of [aes_exp_t] through the S-box
aes_sub_word:
        push ecx
        push edx
        push esi
        mov eax, [aes_exp_t]
        xor edx, edx                    ; result
        xor ecx, ecx                    ; bit offset
.asw_loop:
        mov eax, [aes_exp_t]
        shr eax, cl
        and eax, 0xFF
        lea esi, [aes_sbox]
        movzx eax, byte [esi + eax]
        mov esi, ecx
        push ecx
        mov ecx, esi
        shl eax, cl
        pop ecx
        or  edx, eax
        add ecx, 8
        cmp ecx, 32
        jb .asw_loop
        mov [aes_exp_t], edx
        pop esi
        pop edx
        pop ecx
        ret

;-----------------------------------------------------------------------
; aes_add_round_key - state ^= 16 bytes at ESI
;-----------------------------------------------------------------------
aes_add_round_key:
        push eax
        push ecx
        push edi
        mov edi, aes_state
        mov ecx, 4
.aar_loop:
        mov eax, [esi]
        xor [edi], eax
        add esi, 4
        add edi, 4
        dec ecx
        jnz .aar_loop
        pop edi
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; State transforms, column-major state[r + 4c].
;-----------------------------------------------------------------------
aes_sub_bytes:
        push eax
        push ecx
        push esi
        push edi
        lea esi, [aes_sbox]
        mov edi, aes_state
        mov ecx, 16
.asb_loop:
        movzx eax, byte [edi]
        mov al, [esi + eax]
        mov [edi], al
        inc edi
        dec ecx
        jnz .asb_loop
        pop edi
        pop esi
        pop ecx
        pop eax
        ret

aes_inv_sub_bytes:
        push eax
        push ecx
        push esi
        push edi
        lea esi, [aes_inv_sbox]
        mov edi, aes_state
        mov ecx, 16
.aisb_loop:
        movzx eax, byte [edi]
        mov al, [esi + eax]
        mov [edi], al
        inc edi
        dec ecx
        jnz .aisb_loop
        pop edi
        pop esi
        pop ecx
        pop eax
        ret

; ShiftRows through a scratch copy: out[r + 4c] = in[r + 4((c + r) mod 4)]
aes_shift_rows:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        xor ebx, ebx                    ; r
.asr_row:
        xor ecx, ecx                    ; c
.asr_col:
        mov edx, ecx
        add edx, ebx
        and edx, 3                      ; (c + r) mod 4
        shl edx, 2
        add edx, ebx                    ; in index
        lea esi, [aes_state]
        movzx eax, byte [esi + edx]
        mov edx, ecx
        shl edx, 2
        add edx, ebx                    ; out index
        lea edi, [aes_scratch]
        mov [edi + edx], al
        inc ecx
        cmp ecx, 4
        jb .asr_col
        inc ebx
        cmp ebx, 4
        jb .asr_row
        ; copy back
        lea esi, [aes_scratch]
        lea edi, [aes_state]
        mov ecx, 16
        cld
        rep movsb
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; InvShiftRows: out[r + 4c] = in[r + 4((c - r) mod 4)]
aes_inv_shift_rows:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        xor ebx, ebx
.aisr_row:
        xor ecx, ecx
.aisr_col:
        mov edx, ecx
        sub edx, ebx
        add edx, 4
        and edx, 3
        shl edx, 2
        add edx, ebx
        lea esi, [aes_state]
        movzx eax, byte [esi + edx]
        mov edx, ecx
        shl edx, 2
        add edx, ebx
        lea edi, [aes_scratch]
        mov [edi + edx], al
        inc ecx
        cmp ecx, 4
        jb .aisr_col
        inc ebx
        cmp ebx, 4
        jb .aisr_row
        lea esi, [aes_scratch]
        lea edi, [aes_state]
        mov ecx, 16
        cld
        rep movsb
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret


; MixColumns: b = M * a per column, M = {{2,3,1,1},{1,2,3,1},{1,1,2,3},{3,1,1,2}}
aes_mix_columns:
        pushad
        xor ecx, ecx
.amc_loop:
        lea edi, [aes_state + ecx * 4]
        lea esi, [aes_mix_matrix]
        call aes_col_mul
        inc ecx
        cmp ecx, 4
        jb .amc_loop
        popad
        ret

; InvMixColumns: M = {{14,11,13,9},{9,14,11,13},{13,9,14,11},{11,13,9,14}}
aes_inv_mix_columns:
        pushad
        xor ecx, ecx
.aimc_loop:
        lea edi, [aes_state + ecx * 4]
        lea esi, [aes_inv_mix_matrix]
        call aes_col_mul
        inc ecx
        cmp ecx, 4
        jb .aimc_loop
        popad
        ret

aes_mix_matrix:      db 2,3,1,1, 1,2,3,1, 1,1,2,3, 3,1,1,2
aes_inv_mix_matrix:  db 14,11,13,9, 9,14,11,13, 13,9,14,11, 11,13,9,14

;-----------------------------------------------------------------------
; aes_col_mul - one column through a 4x4 matrix over GF(2^8)
;   In : EDI = 4 bytes (in place), ESI = 16-byte row-major matrix
;-----------------------------------------------------------------------
aes_col_mul:
        pushad
        mov [.acm_p], edi
        mov [.acm_m], esi
        mov eax, [edi]
        mov [aes_col_in], eax
        mov dword [.acm_i], 0
.acm_row:
        mov dword [.acm_acc], 0
        mov dword [.acm_j], 0
.acm_col:
        mov ecx, [.acm_j]
        shl ecx, 3
        mov eax, [aes_col_in]
        shr eax, cl
        and eax, 0xFF
        jz .acm_next           ; 0 * anything is 0
        mov [.acm_a], eax
        mov ecx, [.acm_i]
        shl ecx, 2
        add ecx, [.acm_j]
        mov esi, [.acm_m]
        movzx ebx, byte [esi + ecx]
        test ebx, ebx
        jz .acm_next
        mov eax, [.acm_a]
        call aes_gmul          ; EAX = a * m
        xor [.acm_acc], eax
.acm_next:
        inc dword [.acm_j]
        cmp dword [.acm_j], 4
        jb .acm_col
        mov ecx, [.acm_i]
        mov edi, [.acm_p]
        mov eax, [.acm_acc]
        mov [edi + ecx], al
        inc dword [.acm_i]
        cmp dword [.acm_i], 4
        jb .acm_row
        popad
        ret
.acm_p:   dd 0
.acm_m:   dd 0
.acm_i:   dd 0
.acm_j:   dd 0
.acm_a:   dd 0
.acm_acc: dd 0

align 16
aes_col_in: times 4 db 0

;-----------------------------------------------------------------------
; aes_encrypt_block - one block, ECB
;   In : ESI = 16-byte input, EDI = 16-byte output, EBX = round keys
;   The input and output buffers may be the same address.
;-----------------------------------------------------------------------
aes_encrypt_block:
        pushad
        mov [.aeb_in], esi
        mov [.aeb_out], edi
        mov [.aeb_rk], ebx

        ; state = input ^ rk0
        mov esi, [.aeb_in]
        mov edi, aes_state
        mov ecx, 4
.aeb_load:
        mov eax, [esi]
        xor eax, [ebx]
        mov [edi], eax
        add esi, 4
        add edi, 4
        add ebx, 4
        dec ecx
        jnz .aeb_load

        mov dword [.aeb_round], 1
.aeb_rounds:
        call aes_sub_bytes
        call aes_shift_rows
        cmp dword [.aeb_round], AES_ROUNDS_256
        je .aeb_nomix
        call aes_mix_columns
.aeb_nomix:
        mov eax, [.aeb_round]
        shl eax, 4
        mov esi, [.aeb_rk]
        add esi, eax
        call aes_add_round_key
        inc dword [.aeb_round]
        cmp dword [.aeb_round], AES_ROUNDS_256
        jbe .aeb_rounds

        mov esi, aes_state
        mov edi, [.aeb_out]
        mov ecx, 4
        rep movsd
        popad
        ret
.aeb_in:    dd 0
.aeb_out:   dd 0
.aeb_rk:    dd 0
.aeb_round: dd 0

;-----------------------------------------------------------------------
; aes_decrypt_block - one block, ECB inverse
;   In : ESI = 16-byte input, EDI = 16-byte output, EBX = round keys
;-----------------------------------------------------------------------
aes_decrypt_block:
        pushad
        mov [.adb_in], esi
        mov [.adb_out], edi
        mov [.adb_rk], ebx

        ; state = input ^ rk14
        mov eax, AES_EXPANDED_BYTES - AES_BLOCK_BYTES
        add eax, ebx
        mov esi, [.adb_in]
        mov edi, aes_state
        mov ecx, 4
.adb_load:
        mov edx, [eax]
        mov ebp, [esi]
        xor ebp, edx
        mov [edi], ebp
        add esi, 4
        add edi, 4
        add eax, 4
        dec ecx
        jnz .adb_load

        ; FIPS-197 Figure 12: rounds Nr-1..1 each end with InvMixColumns, and
        ; the final round (no InvMixColumns) is separate.
        mov dword [.adb_round], AES_ROUNDS_256 - 1
.adb_rounds:
        call aes_inv_shift_rows
        call aes_inv_sub_bytes
        mov eax, [.adb_round]
        shl eax, 4
        mov esi, [.adb_rk]
        add esi, eax
        call aes_add_round_key
        call aes_inv_mix_columns
        dec dword [.adb_round]
        cmp dword [.adb_round], 0
        jg .adb_rounds

        call aes_inv_shift_rows
        call aes_inv_sub_bytes
        mov esi, [.adb_rk]
        call aes_add_round_key

        mov esi, aes_state
        mov edi, [.adb_out]
        mov ecx, 4
        rep movsd
        popad
        ret
.adb_in:    dd 0
.adb_out:   dd 0
.adb_rk:    dd 0
.adb_round: dd 0

;-----------------------------------------------------------------------
; aes_setup_secret - derive both XTS keys from the 32-byte secret
;   In : ESI = 32-byte secret
;   Out: CF clear on success; aes_ready set
;
; Each XTS key is 32 bytes, built from two AES inputs: the domain block and the
; same block with its last byte incremented. The secret encrypts both, so the
; secret is an AES-256 key and the volume keys are functions of it.
;-----------------------------------------------------------------------
aes_setup_secret:
        pushad
        mov [aes_ss_secret], esi
        mov esi, [aes_ss_secret]
        mov edi, aes_key_work
        call aes_expand_256           ; round keys for the secret itself

        ; --- data key ---
        lea esi, [aes_domain_data]
        mov edi, aes_input
        mov ecx, 4
        cld
        rep movsd
        mov esi, aes_input
        mov edi, aes_input
        mov ebx, aes_key_work
        call aes_encrypt_block
        mov esi, aes_input
        mov edi, aes_key_raw
        mov ecx, 4
        rep movsd                     ; raw[0..15]

        lea esi, [aes_domain_data]
        mov edi, aes_input
        mov ecx, 4
        rep movsd
        inc byte [aes_input + 15]     ; the second half's domain block
        mov esi, aes_input
        mov edi, aes_input
        mov ebx, aes_key_work
        call aes_encrypt_block
        mov esi, aes_input
        mov edi, aes_key_raw + 16
        mov ecx, 4
        rep movsd
        mov esi, aes_key_raw
        mov edi, aes_key_data
        call aes_expand_256

        ; --- tweak key ---
        lea esi, [aes_domain_tweak]
        mov edi, aes_input
        mov ecx, 4
        rep movsd
        mov esi, aes_input
        mov edi, aes_input
        mov ebx, aes_key_work
        call aes_encrypt_block
        mov esi, aes_input
        mov edi, aes_key_raw
        mov ecx, 4
        rep movsd

        lea esi, [aes_domain_tweak]
        mov edi, aes_input
        mov ecx, 4
        rep movsd
        inc byte [aes_input + 15]
        mov esi, aes_input
        mov edi, aes_input
        mov ebx, aes_key_work
        call aes_encrypt_block
        mov esi, aes_input
        mov edi, aes_key_raw + 16
        mov ecx, 4
        rep movsd
        mov esi, aes_key_raw
        mov edi, aes_key_tweak
        call aes_expand_256

        ; The secret's expanded form has done its work; forget it.
        mov edi, aes_key_work
        mov ecx, AES_EXPANDED_BYTES / 4
        xor eax, eax
        rep stosd

        mov dword [aes_ready], 1
        popad
        clc
        ret

;-----------------------------------------------------------------------
; aes_key_check - the 8-byte value that proves a stored secret is the volume's
;   In : ESI = 32-byte secret, EDI = 8-byte destination
;-----------------------------------------------------------------------
aes_key_check:
        pushad
        mov [.akc_dst], edi
        mov edi, aes_key_work
        call aes_expand_256
        lea esi, [aes_domain_check]
        mov edi, aes_input
        mov ecx, 4
        cld
        rep movsd
        mov esi, aes_input
        mov edi, aes_input
        mov ebx, aes_key_work
        call aes_encrypt_block
        mov esi, aes_input
        mov edi, [aes_key_check.akc_dst]
        mov ecx, 2
        rep movsd
        mov edi, aes_key_work
        mov ecx, AES_EXPANDED_BYTES / 4
        xor eax, eax
        rep stosd
        popad
        ret
.akc_dst: dd 0

;-----------------------------------------------------------------------
; aes_clear - forget the keys (unmount, failed unlock)
;-----------------------------------------------------------------------
aes_clear:
        pushad
        mov edi, aes_key_data
        mov ecx, AES_EXPANDED_BYTES / 4
        xor eax, eax
        rep stosd
        mov edi, aes_key_tweak
        mov ecx, AES_EXPANDED_BYTES / 4
        rep stosd
        mov edi, aes_key_raw
        mov ecx, AES_KEY_BYTES / 4
        rep stosd
        mov dword [aes_ready], 0
        popad
        ret

;-----------------------------------------------------------------------
; xts_crypt_unit - encrypt or decrypt one 4 KiB unit in place
;   In : EAX = unit number (absolute block number), EDI = 4 KiB buffer
;        DL  = 0 encrypt, 1 decrypt
;   Out: CF clear on success, set if the keys are not set up
;
; The tweak for block j is tweak * alpha^j, and the unit's tweak is
; AES(key_tweak, unit number as a 128-bit little-endian value).
;-----------------------------------------------------------------------
xts_crypt_unit:
        pushad
        cmp dword [aes_ready], 0
        je .xtu_nokey
        mov [.xtu_base], edi
        mov [.xtu_dir], edx
        mov [.xtu_unit], eax

        ; tweak input = unit number, 64-bit little endian, high half zero
        mov edi, aes_tweak_in
        mov ecx, 4
        xor eax, eax
        rep stosd
        mov eax, [.xtu_unit]
        mov [aes_tweak_in], eax

        mov esi, aes_tweak_in
        mov edi, aes_tweak
        mov ebx, aes_key_tweak
        call aes_encrypt_block       ; aes_tweak = T0

        mov dword [.xtu_j], 0
.xtu_block:
        mov edi, [.xtu_base]
        mov eax, [.xtu_j]
        shl eax, 4
        add edi, eax                 ; this block
        mov [.xtu_cur], edi
        call aes_xor_tweak           ; block ^= tweak (advances EDI)

        mov esi, [.xtu_cur]
        mov edi, [.xtu_cur]
        mov ebx, aes_key_data
        cmp dword [.xtu_dir], 0
        jne .xtu_dec
        call aes_encrypt_block
        jmp .xtu_after
.xtu_dec:
        call aes_decrypt_block
.xtu_after:
        mov edi, [.xtu_cur]
        call aes_xor_tweak           ; block ^= tweak

        call aes_tweak_mul_alpha
        inc dword [.xtu_j]
        cmp dword [.xtu_j], AES_UNIT_BLOCKS
        jb .xtu_block

        popad
        clc
        ret
.xtu_nokey:
        popad
        stc
        ret
.xtu_base: dd 0
.xtu_cur:  dd 0
.xtu_dir:  dd 0
.xtu_unit: dd 0
.xtu_j:    dd 0

;-----------------------------------------------------------------------
; aes_xor_tweak - XOR the 16-byte tweak into the block at EDI
;-----------------------------------------------------------------------
aes_xor_tweak:
        push eax
        push ecx
        push esi
        lea esi, [aes_tweak]
        mov ecx, 4
.axtw_loop:
        mov eax, [esi]
        xor [edi], eax
        add esi, 4
        add edi, 4
        dec ecx
        jnz .axtw_loop
        pop esi
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; aes_tweak_mul_alpha - tweak = tweak * x in GF(2^128), little-endian
;   The reduction polynomial is x^128 + x^7 + x^2 + x + 1.
;-----------------------------------------------------------------------
aes_tweak_mul_alpha:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        lea esi, [aes_tweak]
        mov ecx, 16
        xor ebx, ebx                 ; carry byte
.atma_loop:
        movzx eax, byte [esi]
        mov edx, eax
        shl eax, 1
        and eax, 0xFF
        or  eax, ebx
        mov ebx, edx
        shr ebx, 7                   ; carry out
        mov [esi], al
        inc esi
        dec ecx
        jnz .atma_loop
        test ebx, ebx
        jz .atma_done
        lea esi, [aes_tweak]
        xor byte [esi], 0x87         ; x^7 + x^2 + x + 1
.atma_done:
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; aes_selftest - the FIPS-197 AES-256 vector and an XTS round trip
;   Prints AES_SELFTEST_OK or AES_SELFTEST_FAIL on COM1. Runs once at boot.
;-----------------------------------------------------------------------
aes_selftest:
        pushad

        ; --- 1. AES-256 known answer (FIPS-197 C.3) ---
        mov esi, aes_test_key
        mov edi, aes_key_work
        call aes_expand_256
        mov esi, aes_test_plain
        mov edi, aes_test_out
        mov ebx, aes_key_work
        call aes_encrypt_block
        mov esi, aes_test_out
        mov edi, aes_test_expected
        mov ecx, 16
        call aes_memcmp
        jne .ast_fail

        mov esi, aes_test_expected
        mov edi, aes_test_out
        mov ebx, aes_key_work
        call aes_decrypt_block
        mov esi, aes_test_out
        mov edi, aes_test_plain
        mov ecx, 16
        call aes_memcmp
        jne .ast_fail

        ; --- 2. XTS: set up from a fixed secret, encrypt then decrypt a unit ---
        ; One 4 KiB buffer: a pattern is written, the first block is kept as the
        ; reference, the unit is encrypted in place (must change), then
        ; decrypted in place (must come back).
        mov esi, aes_test_secret
        call aes_setup_secret
        mov edi, aes_test_unit
        mov ecx, AES_UNIT_BYTES
        xor edx, edx
.ast_fill:
        mov eax, edx
        imul eax, 2654435761
        mov [edi + edx], al
        inc edx
        cmp edx, ecx
        jb .ast_fill

        mov esi, aes_test_unit
        mov edi, aes_test_ref
        mov ecx, 32 / 4
        rep movsd

        mov eax, 4096
        mov edi, aes_test_unit
        xor edx, edx                 ; encrypt
        call xts_crypt_unit
        jc .ast_fail

        ; ciphertext must differ from the plaintext it was made from
        mov esi, aes_test_unit
        mov edi, aes_test_ref
        mov ecx, 32
        call aes_memcmp
        je .ast_fail                 ; unchanged means it did nothing

        mov eax, 4096
        mov edi, aes_test_unit
        mov edx, 1                   ; decrypt
        call xts_crypt_unit
        jc .ast_fail

        mov esi, aes_test_unit
        mov edi, aes_test_ref
        mov ecx, 32
        call aes_memcmp
        jne .ast_fail
        ; and the whole unit must round-trip, not just its first 32 bytes
        mov edi, aes_test_verify
        mov ecx, AES_UNIT_BYTES
        xor edx, edx
.ast_regen:
        mov eax, edx
        imul eax, 2654435761
        mov [edi + edx], al
        inc edx
        cmp edx, ecx
        jb .ast_regen
        mov esi, aes_test_unit
        mov edi, aes_test_verify
        mov ecx, AES_UNIT_BYTES
        call aes_memcmp
        jne .ast_fail

        call aes_clear
        mov esi, aes_st_ok
        call serial_print
        popad
        ret
.ast_fail:
        call aes_clear
        mov esi, aes_st_fail
        call serial_print
        popad
        ret

; memcmp: ZF set when equal
aes_memcmp:
        push ecx
        push esi
        push edi
        cld
.aml_loop:
        test ecx, ecx
        jz .aml_equal
        mov al, [esi]
        cmp al, [edi]
        jne .aml_diff
        inc esi
        inc edi
        dec ecx
        jmp .aml_loop
.aml_equal:
        xor eax, eax
        pop edi
        pop esi
        pop ecx
        ret
.aml_diff:
        mov eax, 1
        pop edi
        pop esi
        pop ecx
        ret

aes_st_ok:       db "AES_SELFTEST_OK", 10, 0
aes_st_fail:     db "AES_SELFTEST_FAIL", 10, 0

; FIPS-197 C.3: AES-256, key 000102..1f, plaintext 00112233445566778899aabbccddeeff
aes_test_key:
        db 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
        db 0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
aes_test_plain:
        db 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
aes_test_expected:
        db 0x8e,0xa2,0xb7,0xca,0x51,0x67,0x45,0xbf,0xea,0xfc,0x49,0x90,0x4b,0x49,0x60,0x89
aes_test_secret:
        times AES_SECRET_BYTES db 0x42

align 16
aes_key_work:      times AES_EXPANDED_BYTES db 0
aes_key_raw:       times AES_KEY_BYTES db 0
aes_input:         times 16 db 0
aes_tweak_in:      times 16 db 0
aes_test_out:      times 16 db 0
aes_test_unit:     times AES_UNIT_BYTES db 0
aes_test_ref:      times 32 db 0
aes_test_verify:   times AES_UNIT_BYTES db 0


