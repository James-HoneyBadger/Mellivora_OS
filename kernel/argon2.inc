;=======================================================================
; argon2.inc - Argon2id, and the BLAKE2b it is built on
;
; SERVICES.md section 2 says a password is checked against an Argon2id hash.
; This is that function: version 0x13, one lane, no secret and no associated
; data - the shape a local account store needs, and the shape this machine can
; compute without threads.
;
; The parameters are compiled in and are part of what a stored hash means:
;
;   memory   1 MiB      (RFC 9106 suggests 64 MiB; a guest here may have 128 MB
;                        in total and the machine still has to log a user on)
;   passes   3
;   lanes    1
;   salt     8 bytes    (what the account record carries)
;   tag      32 bytes
;
; Changing any of them changes every hash, so the account record carries a
; scheme number too: a hash written before this existed is refused and re-set
; rather than compared against the wrong function.
;
; Everything runs under a2_lock. The BLAKE2b state and the block buffers are
; one set, not per caller, and a second logon can arrive while the first is
; still hashing.
;
; The algorithm is the one in RFC 9106 section 3: H0 over the parameters, two
; initial blocks per lane from H', then `passes' fills of the memory, then the
; tag from the lane's last block. Argon2id takes its reference indices from a
; generated address block for the first half of the first pass and from the
; previous block afterwards - implementing it as Argon2d is a silent format
; error, not a faster function.
;=======================================================================

A2_M_KIB        equ 1024        ; memory cost, KiB
A2_T_COST       equ 3           ; passes
A2_TAGLEN       equ 32
A2_VERSION      equ 0x13
A2_TYPE_ID      equ 2
A2_BLOCK_BYTES  equ 1024
A2_BLOCK_DWORDS equ A2_BLOCK_BYTES / 4
A2_SYNC_POINTS  equ 4
A2_ADDR_WORDS   equ 128         ; pseudo-random values in one address block

; a2_params - the description argon2id_hash reads
A2P_PWD         equ 0
A2P_PWDLEN      equ 4
A2P_SALT        equ 8
A2P_SALTLEN     equ 12
A2P_OUT         equ 16
A2P_OUTLEN      equ 20
A2P_MKIB        equ 24
A2P_TCOST       equ 28
A2P_MEM         equ 32          ; A2P_MKIB bytes the caller allocated
A2P_SIZE        equ 36

align 4
a2_lock:
        dd 0
        dd RANK_ARGON2
        dd -1
        dd a2_lock_name
a2_lock_name: db "argon2", 0

;=======================================================================
; BLAKE2b - RFC 7693, 64-bit words, output up to 64 bytes
;=======================================================================
B2B_IV:
        dd 0xF3BCC908, 0x6A09E667
        dd 0x84CAA73B, 0xBB67AE85
        dd 0xFE94F82B, 0x3C6EF372
        dd 0x5F1D36F1, 0xA54FF53A
        dd 0xADE682D1, 0x510E527F
        dd 0x2B3E6C1F, 0x9B05688C
        dd 0xFB41BD6B, 0x1F83D9AB
        dd 0x137E2179, 0x5BE0CD19

; The message schedule: twelve rows of sixteen byte indices.
b2b_sigma:
        db 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        db 14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3
        db 11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4
        db 7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8
        db 9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13
        db 2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9
        db 12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11
        db 13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10
        db 6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5
        db 10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0
        db 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        db 14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3

; The eight G calls of a round, as word indices.
a2_round_g:
        db 0,4,8,12
        db 1,5,9,13
        db 2,6,10,14
        db 3,7,11,15
        db 0,5,10,15
        db 1,6,11,12
        db 2,7,8,13
        db 3,4,9,14

align 16
b2b_h:      times 16 dd 0        ; eight 64-bit words
b2b_v:      times 32 dd 0        ; sixteen
b2b_m:      times 32 dd 0        ; sixteen message words
b2b_buf:    times 128 db 0
b2b_outlen: dd 0
b2b_buflen: dd 0
b2b_tlow:   dd 0
b2b_thigh:  dd 0

;-----------------------------------------------------------------------
; b2b_g - one G of the BLAKE2b round, with the message words added
;   In : EDI = &v[a], ESI = &v[b], EBP = &v[c], EBX = &v[d]
;        [esp+4] = &m[x], [esp+8] = &m[y]
;
; The four pointers are read-only; EAX, ECX and EDX are scratch.
;-----------------------------------------------------------------------
b2b_g:
        ; a = a + b + m[x]
        mov eax, [edi]
        mov edx, [edi + 4]
        add eax, [esi]
        adc edx, [esi + 4]
        mov ecx, [esp + 4]
        add eax, [ecx]
        adc edx, [ecx + 4]
        mov [edi], eax
        mov [edi + 4], edx
        ; d = rotr(d ^ a, 32): the halves swap
        mov eax, [ebx]
        xor eax, [edi]
        mov edx, [ebx + 4]
        xor edx, [edi + 4]
        mov [ebx], edx
        mov [ebx + 4], eax
        ; c = c + d
        mov eax, [ebp]
        mov edx, [ebp + 4]
        add eax, [ebx]
        adc edx, [ebx + 4]
        mov [ebp], eax
        mov [ebp + 4], edx
        ; b = rotr(b ^ c, 24)
        mov eax, [esi]
        xor eax, [ebp]
        mov edx, [esi + 4]
        xor edx, [ebp + 4]
        mov ecx, eax
        shrd eax, edx, 24
        shrd edx, ecx, 24
        mov [esi], eax
        mov [esi + 4], edx
        ; a = a + b + m[y]
        mov eax, [edi]
        mov edx, [edi + 4]
        add eax, [esi]
        adc edx, [esi + 4]
        mov ecx, [esp + 8]
        add eax, [ecx]
        adc edx, [ecx + 4]
        mov [edi], eax
        mov [edi + 4], edx
        ; d = rotr(d ^ a, 16)
        mov eax, [ebx]
        xor eax, [edi]
        mov edx, [ebx + 4]
        xor edx, [edi + 4]
        mov ecx, eax
        shrd eax, edx, 16
        shrd edx, ecx, 16
        mov [ebx], eax
        mov [ebx + 4], edx
        ; c = c + d
        mov eax, [ebp]
        mov edx, [ebp + 4]
        add eax, [ebx]
        adc edx, [ebx + 4]
        mov [ebp], eax
        mov [ebp + 4], edx
        ; b = rotr(b ^ c, 63), which is a rotate left by one
        mov eax, [esi]
        xor eax, [ebp]
        mov edx, [esi + 4]
        xor edx, [ebp + 4]
        mov ecx, eax
        shld eax, edx, 1
        shld edx, ecx, 1
        mov [esi], eax
        mov [esi + 4], edx
        ret

;-----------------------------------------------------------------------
; b2b_round - the eight G calls of one round
;   In : EDI = base of the sixteen 64-bit v words
;        ESI = pointer to the round's sixteen message indices
;        EBP = base of the sixteen 64-bit message words
;-----------------------------------------------------------------------
b2b_round:
        push ebx
        push ebp
        mov [b2b_rd_base], edi
        mov [b2b_rd_sig], esi
        mov [b2b_rd_msg], ebp
        mov dword [b2b_rd_k], 0
.b2r_loop:
        ; the two message words this G adds
        mov esi, [b2b_rd_sig]
        mov eax, [b2b_rd_k]
        movzx ecx, byte [esi + eax*2]
        movzx edx, byte [esi + eax*2 + 1]
        mov esi, [b2b_rd_msg]
        lea ecx, [esi + ecx*8]
        lea edx, [esi + edx*8]
        push edx
        push ecx
        ; the four v pointers
        mov esi, a2_round_g
        mov eax, [b2b_rd_k]
        shl eax, 2
        add esi, eax
        mov edi, [b2b_rd_base]
        movzx eax, byte [esi]
        lea edi, [edi + eax*8]
        movzx eax, byte [esi + 1]
        mov ebx, [b2b_rd_base]
        lea ebx, [ebx + eax*8]
        movzx eax, byte [esi + 2]
        mov ebp, [b2b_rd_base]
        lea ebp, [ebp + eax*8]
        movzx eax, byte [esi + 3]
        mov ecx, [b2b_rd_base]
        lea ecx, [ecx + eax*8]
        mov esi, ebx
        mov ebx, ecx
        call b2b_g
        add esp, 8
        cmp dword [b2b_cp_round], 0
        jne .b2r_nodbg
.b2r_nodbg:
        inc dword [b2b_rd_k]
        cmp dword [b2b_rd_k], 8
        jb .b2r_loop
        pop ebp
        pop ebx
        ret
b2b_rd_base: dd 0
b2b_rd_sig:  dd 0
b2b_rd_msg:  dd 0
b2b_rd_k:    dd 0

;-----------------------------------------------------------------------
; b2b_init - In : EAX = output length in bytes (1..64)
;-----------------------------------------------------------------------
b2b_init:
        pushad
        mov [b2b_outlen], eax
        mov dword [b2b_buflen], 0
        mov dword [b2b_tlow], 0
        mov dword [b2b_thigh], 0
        mov esi, B2B_IV
        mov edi, b2b_h
        mov ecx, 16
        cld
        rep movsd
        mov eax, 0x01010000
        xor eax, [b2b_outlen]
        xor [b2b_h], eax
        popad
        ret

;-----------------------------------------------------------------------
; b2b_compress - one compression over b2b_buf
;   In : EDX = 1 for the last block
;-----------------------------------------------------------------------
b2b_compress:
        pushad
        mov [b2b_cp_final], edx
        mov esi, b2b_h
        mov edi, b2b_v
        mov ecx, 16
        cld
        rep movsd                       ; v[0..7] = h
        mov esi, B2B_IV
        mov ecx, 16
        rep movsd                       ; v[8..15] = IV
        mov eax, [b2b_tlow]
        xor [b2b_v + 12*8], eax
        mov eax, [b2b_thigh]
        xor [b2b_v + 13*8], eax
        cmp dword [b2b_cp_final], 0
        je .b2c_no_final
        xor dword [b2b_v + 14*8], 0xFFFFFFFF       ; the whole 64-bit word:
        xor dword [b2b_v + 14*8 + 4], 0xFFFFFFFF   ; v[14] ^= -1
.b2c_no_final:
        mov esi, b2b_buf
        mov edi, b2b_m
        mov ecx, 32
        rep movsd                       ; the sixteen message words
        mov dword [b2b_cp_round], 0
.b2c_round:
        mov eax, [b2b_cp_round]
        shl eax, 4
        mov esi, b2b_sigma
        add esi, eax
        mov edi, b2b_v
        mov ebp, b2b_m
        call b2b_round
        inc dword [b2b_cp_round]
        cmp dword [b2b_cp_round], 12
        jb .b2c_round
        ; h[i] ^= v[i] ^ v[i+8]
        mov ecx, 8
        mov esi, b2b_v
        mov edi, b2b_h
.b2c_xor:
        mov eax, [esi]
        xor eax, [esi + 64]
        xor [edi], eax
        mov eax, [esi + 4]
        xor eax, [esi + 68]
        xor [edi + 4], eax
        add esi, 8
        add edi, 8
        loop .b2c_xor
        popad
        ret
b2b_cp_final: dd 0
b2b_cp_round: dd 0

;-----------------------------------------------------------------------
; b2b_update - In : EAX = data, ECX = length
;-----------------------------------------------------------------------
b2b_update:
        pushad
        mov [b2b_up_ptr], eax
        mov [b2b_up_len], ecx
.b2u_loop:
        cmp dword [b2b_up_len], 0
        je .b2u_done
        mov eax, [b2b_buflen]
        cmp eax, 128
        jb .b2u_copy
        mov eax, [b2b_tlow]
        add eax, 128
        mov [b2b_tlow], eax
        adc dword [b2b_thigh], 0
        xor edx, edx
        call b2b_compress
        mov dword [b2b_buflen], 0
.b2u_copy:
        mov eax, 128
        sub eax, [b2b_buflen]
        mov ecx, [b2b_up_len]
        cmp ecx, eax
        jbe .b2u_fits
        mov ecx, eax
.b2u_fits:
        mov edi, b2b_buf
        add edi, [b2b_buflen]
        mov esi, [b2b_up_ptr]
        add [b2b_up_ptr], ecx
        sub [b2b_up_len], ecx
        add [b2b_buflen], ecx
        cld
        rep movsb
        jmp .b2u_loop
.b2u_done:
        popad
        ret
b2b_up_ptr: dd 0
b2b_up_len: dd 0

;-----------------------------------------------------------------------
; b2b_final - In : EAX = output buffer
;-----------------------------------------------------------------------
b2b_final:
        pushad
        mov [b2b_fn_out], eax
        mov eax, [b2b_buflen]
        add [b2b_tlow], eax
        adc dword [b2b_thigh], 0
        mov ecx, 128
        sub ecx, [b2b_buflen]
        mov edi, b2b_buf
        add edi, [b2b_buflen]
        xor eax, eax
        cld
        rep stosb                       ; the padding is defined
        mov edx, 1
        call b2b_compress
        mov esi, b2b_h
        mov edi, [b2b_fn_out]
        mov ecx, [b2b_outlen]
        cld
        rep movsb
        popad
        ret
b2b_fn_out: dd 0

;-----------------------------------------------------------------------
; b2b_hash - init, one update, final
;   In : EAX = out, ECX = outlen, EDX = data, EBX = length
;-----------------------------------------------------------------------
b2b_hash:
        pushad
        mov [b2b_hs_out], eax
        mov [b2b_hs_data], edx
        mov [b2b_hs_len], ebx
        mov eax, ecx
        call b2b_init
        mov eax, [b2b_hs_data]
        mov ecx, [b2b_hs_len]
        call b2b_update
        mov eax, [b2b_hs_out]
        call b2b_final
        popad
        ret
b2b_hs_out:  dd 0
b2b_hs_data: dd 0
b2b_hs_len:  dd 0

;-----------------------------------------------------------------------
; a2_hprime - H'^outlen over the 72-byte seed in a2_seed
;   In : EAX = out, ECX = outlen
;
; blake2b_long: a short output is one hash; a long one is a chain of 64-byte
; hashes of which 32 bytes each are kept, and a shorter final block.
;-----------------------------------------------------------------------
a2_hprime:
        pushad
        mov [a2h_out], eax
        mov [a2h_outlen], ecx
        mov [a2h_lenbytes], ecx
        cmp ecx, 64
        ja .a2h_long
        mov eax, ecx
        call b2b_init
        mov eax, a2h_lenbytes
        mov ecx, 4
        call b2b_update
        mov eax, a2_seed
        mov ecx, 72
        call b2b_update
        mov eax, [a2h_out]
        call b2b_final
        jmp .a2h_done
.a2h_long:
        mov eax, 64
        call b2b_init
        mov eax, a2h_lenbytes
        mov ecx, 4
        call b2b_update
        mov eax, a2_seed
        mov ecx, 72
        call b2b_update
        mov eax, a2h_chain
        call b2b_final
        mov esi, a2h_chain
        mov edi, [a2h_out]
        mov ecx, 32
        cld
        rep movsb
        mov eax, [a2h_outlen]
        sub eax, 32
        mov [a2h_left], eax
.a2h_chain_loop:
        cmp dword [a2h_left], 64
        jbe .a2h_last
        mov eax, a2h_chain
        mov ecx, 64
        mov edx, a2h_chain
        mov ebx, 64
        call b2b_hash
        mov esi, a2h_chain
        mov edi, [a2h_out]
        add edi, [a2h_outlen]
        sub edi, [a2h_left]
        mov ecx, 32
        cld
        rep movsb
        sub dword [a2h_left], 32
        jmp .a2h_chain_loop
.a2h_last:
        mov eax, a2h_tail
        mov ecx, [a2h_left]
        mov edx, a2h_chain
        mov ebx, 64
        call b2b_hash
        mov esi, a2h_tail
        mov edi, [a2h_out]
        add edi, [a2h_outlen]
        sub edi, [a2h_left]
        mov ecx, [a2h_left]
        cld
        rep movsb
.a2h_done:
        popad
        ret
a2h_out:      dd 0
a2h_outlen:   dd 0
a2h_left:     dd 0
a2h_chain:    times 64 db 0
a2h_tail:     times 64 db 0
a2h_lenbytes: dd 0

;=======================================================================
; The block permutation, with BlaMka's G
;=======================================================================

;-----------------------------------------------------------------------
; a2_g - one BlaMka G
;   In : EDI = &a, ESI = &b, EBP = &c, EBX = &d
;
; a = a + b + 2 * lo32(a) * lo32(b). EAX, ECX and EDX are scratch.
;-----------------------------------------------------------------------
a2_g:
        mov eax, [edi]
        mul dword [esi]
        add eax, eax
        adc edx, edx
        add eax, [esi]
        adc edx, [esi + 4]
        add eax, [edi]
        adc edx, [edi + 4]
        mov [edi], eax
        mov [edi + 4], edx
        ; d = rotr(d ^ a, 32)
        mov eax, [ebx]
        xor eax, [edi]
        mov edx, [ebx + 4]
        xor edx, [edi + 4]
        mov [ebx], edx
        mov [ebx + 4], eax
        ; c = c + d + 2*lo(c)*lo(d)
        mov eax, [ebp]
        mul dword [ebx]
        add eax, eax
        adc edx, edx
        add eax, [ebx]
        adc edx, [ebx + 4]
        add eax, [ebp]
        adc edx, [ebp + 4]
        mov [ebp], eax
        mov [ebp + 4], edx
        ; b = rotr(b ^ c, 24)
        mov eax, [esi]
        xor eax, [ebp]
        mov edx, [esi + 4]
        xor edx, [ebp + 4]
        mov ecx, eax
        shrd eax, edx, 24
        shrd edx, ecx, 24
        mov [esi], eax
        mov [esi + 4], edx
        ; a = a + b + 2*lo(a)*lo(b)
        mov eax, [edi]
        mul dword [esi]
        add eax, eax
        adc edx, edx
        add eax, [esi]
        adc edx, [esi + 4]
        add eax, [edi]
        adc edx, [edi + 4]
        mov [edi], eax
        mov [edi + 4], edx
        ; d = rotr(d ^ a, 16)
        mov eax, [ebx]
        xor eax, [edi]
        mov edx, [ebx + 4]
        xor edx, [edi + 4]
        mov ecx, eax
        shrd eax, edx, 16
        shrd edx, ecx, 16
        mov [ebx], eax
        mov [ebx + 4], edx
        ; c = c + d + 2*lo(c)*lo(d)
        mov eax, [ebp]
        mul dword [ebx]
        add eax, eax
        adc edx, edx
        add eax, [ebx]
        adc edx, [ebx + 4]
        add eax, [ebp]
        adc edx, [ebp + 4]
        mov [ebp], eax
        mov [ebp + 4], edx
        ; b = rotr(b ^ c, 63), a rotate left by one
        mov eax, [esi]
        xor eax, [ebp]
        mov edx, [esi + 4]
        xor edx, [ebp + 4]
        mov ecx, eax
        shld eax, edx, 1
        shld edx, ecx, 1
        mov [esi], eax
        mov [esi + 4], edx
        ret

;-----------------------------------------------------------------------
; a2_round - the eight G calls over sixteen words
;   In : EDI = base of the sixteen 64-bit words
;-----------------------------------------------------------------------
a2_round:
        push ebx
        push ebp
        mov [a2r_base], edi
        mov dword [a2r_k], 0
.a2r_loop:
        mov esi, a2_round_g
        mov eax, [a2r_k]
        shl eax, 2
        add esi, eax
        mov edi, [a2r_base]
        movzx eax, byte [esi]
        lea edi, [edi + eax*8]
        movzx eax, byte [esi + 1]
        mov ebx, [a2r_base]
        lea ebx, [ebx + eax*8]
        movzx eax, byte [esi + 2]
        mov ebp, [a2r_base]
        lea ebp, [ebp + eax*8]
        movzx eax, byte [esi + 3]
        mov ecx, [a2r_base]
        lea ecx, [ecx + eax*8]
        mov esi, ebx
        mov ebx, ecx
        call a2_g
        inc dword [a2r_k]
        cmp dword [a2r_k], 8
        jb .a2r_loop
        pop ebp
        pop ebx
        ret
a2r_base: dd 0
a2r_k:    dd 0

;-----------------------------------------------------------------------
; a2_permute - the block permutation P
;   In : EAX = block
;
; Eight rounds over the rows, then eight over the columns - the columns are
; gathered because a column is not contiguous.
;-----------------------------------------------------------------------
a2_permute:
        pushad
        mov [a2p_blk], eax
        mov dword [a2p_i], 0
.a2p_row:
        mov eax, [a2p_i]
        shl eax, 7                      ; 16 words = 128 bytes
        add eax, [a2p_blk]
        mov edi, eax
        call a2_round
        inc dword [a2p_i]
        cmp dword [a2p_i], 8
        jb .a2p_row
        mov dword [a2p_i], 0
.a2p_col:
        mov esi, [a2p_blk]
        mov edx, [a2p_i]
        shl edx, 4                      ; column i starts at word 2*i
        add esi, edx
        mov edi, a2p_col
        mov ecx, 8
.a2p_gather:
        mov eax, [esi]
        mov [edi], eax
        mov eax, [esi + 4]
        mov [edi + 4], eax
        mov eax, [esi + 8]
        mov [edi + 8], eax
        mov eax, [esi + 12]
        mov [edi + 12], eax
        add esi, 128
        add edi, 16
        loop .a2p_gather
        mov edi, a2p_col
        call a2_round
        mov esi, a2p_col
        mov edi, [a2p_blk]
        mov edx, [a2p_i]
        shl edx, 4
        add edi, edx
        mov ecx, 8
.a2p_scatter:
        mov eax, [esi]
        mov [edi], eax
        mov eax, [esi + 4]
        mov [edi + 4], eax
        mov eax, [esi + 8]
        mov [edi + 8], eax
        mov eax, [esi + 12]
        mov [edi + 12], eax
        add esi, 16
        add edi, 128
        loop .a2p_scatter
        inc dword [a2p_i]
        cmp dword [a2p_i], 8
        jb .a2p_col
        popad
        ret
a2p_blk: dd 0
a2p_i:   dd 0
a2p_col: times 128 db 0

;-----------------------------------------------------------------------
; a2_fill_block - one Argon2 block function
;   In : EAX = previous block, EBX = reference block, ECX = current block,
;        EDX = 1 when the old contents are XORed in (pass > 0)
;
; out = (R ^ old) ^ P(R), where R = previous ^ reference. P is applied to R,
; not to the copy the old block was mixed into - getting that the other way
; round gives a hash that is self-consistent and wrong.
;-----------------------------------------------------------------------
a2_fill_block:
        pushad
        mov [a2f_prev], eax
        mov [a2f_ref], ebx
        mov [a2f_curr], ecx
        mov [a2f_xor], edx

        mov esi, [a2f_prev]
        mov edi, a2_r
        mov ecx, A2_BLOCK_DWORDS
        cld
        rep movsd
        mov esi, [a2f_ref]
        mov edi, a2_r
        mov ecx, A2_BLOCK_DWORDS
.a2f_xr:
        mov eax, [esi]
        xor [edi], eax
        add esi, 4
        add edi, 4
        loop .a2f_xr

        mov esi, a2_r
        mov edi, a2_t
        mov ecx, A2_BLOCK_DWORDS
        rep movsd
        cmp dword [a2f_xor], 0
        je .a2f_permute
        mov esi, [a2f_curr]
        mov edi, a2_t
        mov ecx, A2_BLOCK_DWORDS
.a2f_xt:
        mov eax, [esi]
        xor [edi], eax
        add esi, 4
        add edi, 4
        loop .a2f_xt

.a2f_permute:
        mov eax, a2_r
        call a2_permute

        mov esi, a2_t
        mov edi, a2_r
        mov edx, [a2f_curr]
        mov ecx, A2_BLOCK_DWORDS
.a2f_out:
        mov eax, [esi]
        xor eax, [edi]
        mov [edx], eax
        add esi, 4
        add edi, 4
        add edx, 4
        loop .a2f_out
        popad
        ret
a2f_prev: dd 0
a2f_ref:  dd 0
a2f_curr: dd 0
a2f_xor:  dd 0

;=======================================================================
; Argon2id
;=======================================================================
align 16
a2_r:        times A2_BLOCK_BYTES db 0
a2_t:        times A2_BLOCK_BYTES db 0
a2_addr:     times A2_BLOCK_BYTES db 0
a2_input:    times A2_BLOCK_BYTES db 0
a2_zero:     times A2_BLOCK_BYTES db 0
a2_seed:     times 72 db 0
a2_tagbuf:   times A2_BLOCK_BYTES db 0
a2_zeroed:   dd 0

;-----------------------------------------------------------------------
; argon2_init - zero the buffers once, at boot
;-----------------------------------------------------------------------
argon2_init:
        pushad
        mov edi, a2_zero
        xor eax, eax
        mov ecx, A2_BLOCK_DWORDS
        cld
        rep stosd
        mov dword [a2_zeroed], 1
        popad
        ret

;-----------------------------------------------------------------------
; a2_next_addresses - the address block Argon2id uses in its first half
;
;   address = G(zero, G(zero, input)), with the input counter incremented
;   first. p=1, so the lane and the index are the whole of the input.
;-----------------------------------------------------------------------
a2_next_addresses:
        pushad
        inc dword [a2_input + 6*8]
        mov eax, a2_zero
        mov ebx, a2_input
        mov ecx, a2_addr
        xor edx, edx
        call a2_fill_block
        mov eax, a2_zero
        mov ebx, a2_addr
        mov ecx, a2_addr
        xor edx, edx
        call a2_fill_block
        popad
        ret

;-----------------------------------------------------------------------
; a2_index - index_alpha from RFC 9106, one lane
;   In : EAX = pass, EBX = slice, ECX = index in segment, EDX = j1
;   Out: EAX = absolute block index within the lane
;-----------------------------------------------------------------------
a2_index:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [a2i_pass], eax
        mov [a2i_slice], ebx
        mov [a2i_index], ecx
        mov [a2i_j1], edx

        cmp eax, 0
        jne .a2i_later
        cmp ebx, 0
        jne .a2i_first_slice
        mov eax, ecx
        dec eax
        jmp .a2i_area
.a2i_first_slice:
        mov eax, ebx
        imul eax, [a2_seg]
        add eax, ecx
        dec eax
        jmp .a2i_area
.a2i_later:
        mov eax, [a2_lane_len]
        sub eax, [a2_seg]
        add eax, ecx
        dec eax
.a2i_area:
        mov [a2i_area], eax
        ; relative = area - 1 - ((area * ((j1*j1) >> 32)) >> 32)
        mov eax, [a2i_j1]
        mul eax
        mov [a2i_rel], edx
        mov eax, [a2i_area]
        mul dword [a2i_rel]
        mov [a2i_rel], edx
        mov eax, [a2i_area]
        dec eax
        sub eax, [a2i_rel]

        mov edx, 0
        cmp dword [a2i_pass], 0
        je .a2i_have_start
        cmp dword [a2i_slice], A2_SYNC_POINTS - 1
        je .a2i_have_start
        mov edx, [a2i_slice]
        inc edx
        imul edx, [a2_seg]
.a2i_have_start:
        add eax, edx
        xor edx, edx
        div dword [a2_lane_len]
        mov eax, edx
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
a2i_pass:  dd 0
a2i_slice: dd 0
a2i_index: dd 0
a2i_j1:    dd 0
a2i_area:  dd 0
a2i_rel:   dd 0

;-----------------------------------------------------------------------
; argon2id_hash - the whole function
;   In : EAX = pointer to an a2_params, whose A2P_MEM buffer is A2P_MKIB
;        bytes and stays the caller's to free
;   Out: EAX = 0 on success, -1 when the parameters cannot be honoured
;-----------------------------------------------------------------------
argon2id_hash:
        pushad
        mov [a2_par], eax
        mov dword [a2_status], 0
        test eax, eax
        jz .a2_bad

        mov esi, eax
        cmp dword [esi + A2P_OUTLEN], A2_TAGLEN
        jne .a2_bad
        cmp dword [esi + A2P_SALTLEN], 8
        jb .a2_bad
        cmp dword [esi + A2P_MKIB], 8
        jb .a2_bad
        mov eax, [esi + A2P_MKIB]
        test eax, 3
        jnz .a2_bad
        mov eax, [esi + A2P_TCOST]
        cmp eax, 1
        jb .a2_bad
        cmp dword [esi + A2P_MEM], 0
        je .a2_bad
        cmp dword [esi + A2P_PWD], 0
        je .a2_bad

        cmp dword [a2_zeroed], 0
        jne .a2_zero_ok
        call argon2_init
.a2_zero_ok:
        mov edi, a2_lock
        call spin_lock

        mov esi, [a2_par]
        mov eax, [esi + A2P_MKIB]
        mov [a2_blocks], eax
        shr eax, 2
        mov [a2_seg], eax
        shl eax, 2
        mov [a2_lane_len], eax

        ; ---- H0 ----
        mov eax, 64
        call b2b_init
        mov dword [a2_u32], 1           ; lanes
        call .a2_h0_u32
        mov esi, [a2_par]
        mov eax, [esi + A2P_OUTLEN]
        mov [a2_u32], eax
        call .a2_h0_u32
        mov esi, [a2_par]
        mov eax, [esi + A2P_MKIB]
        mov [a2_u32], eax
        call .a2_h0_u32
        mov esi, [a2_par]
        mov eax, [esi + A2P_TCOST]
        mov [a2_u32], eax
        call .a2_h0_u32
        mov dword [a2_u32], A2_VERSION
        call .a2_h0_u32
        mov dword [a2_u32], A2_TYPE_ID
        call .a2_h0_u32
        ; password
        mov esi, [a2_par]
        mov eax, [esi + A2P_PWDLEN]
        mov [a2_u32], eax
        call .a2_h0_u32
        mov esi, [a2_par]
        mov eax, [esi + A2P_PWD]
        mov ecx, [esi + A2P_PWDLEN]
        call b2b_update
        ; salt
        mov esi, [a2_par]
        mov eax, [esi + A2P_SALTLEN]
        mov [a2_u32], eax
        call .a2_h0_u32
        mov esi, [a2_par]
        mov eax, [esi + A2P_SALT]
        mov ecx, [esi + A2P_SALTLEN]
        call b2b_update
        ; no secret, no associated data
        mov dword [a2_u32], 0
        call .a2_h0_u32
        call .a2_h0_u32
        mov eax, a2_seed
        call b2b_final
        jmp .a2_h0_done
.a2_h0_u32:
        mov eax, a2_u32
        mov ecx, 4
        call b2b_update
        ret
.a2_h0_done:

        ; ---- the first two blocks of the lane ----
        mov dword [a2_seed + 64], 0
        mov dword [a2_seed + 68], 0
        mov eax, [a2_par]
        mov eax, [eax + A2P_MEM]
        mov ecx, A2_BLOCK_BYTES
        call a2_hprime
        mov dword [a2_seed + 64], 1
        mov eax, [a2_par]
        mov eax, [eax + A2P_MEM]
        add eax, A2_BLOCK_BYTES
        mov ecx, A2_BLOCK_BYTES
        call a2_hprime

        ; ---- the memory ----
        mov dword [a2_pass], 0
.a2_pass_loop:
        mov dword [a2_slice], 0
.a2_slice_loop:
        mov dword [a2_indep], 0
        cmp dword [a2_pass], 0
        jne .a2_not_indep
        cmp dword [a2_slice], 2
        jae .a2_not_indep
        mov dword [a2_indep], 1
        ; input block: pass, lane, slice, m_blocks, passes, type
        mov edi, a2_input
        xor eax, eax
        mov ecx, A2_BLOCK_DWORDS
        cld
        rep stosd
        mov eax, [a2_pass]
        mov [a2_input + 0*8], eax
        mov dword [a2_input + 1*8], 0
        mov eax, [a2_slice]
        mov [a2_input + 2*8], eax
        mov eax, [a2_blocks]
        mov [a2_input + 3*8], eax
        mov eax, [a2_par]
        mov eax, [eax + A2P_TCOST]
        mov [a2_input + 4*8], eax
        mov dword [a2_input + 5*8], A2_TYPE_ID
.a2_not_indep:

        mov dword [a2_i], 0
        cmp dword [a2_pass], 0
        jne .a2_have_start
        cmp dword [a2_slice], 0
        jne .a2_have_start
        mov dword [a2_i], 2
        cmp dword [a2_indep], 0
        je .a2_have_start
        call a2_next_addresses
.a2_have_start:

.a2_i_loop:
        mov eax, [a2_i]
        cmp eax, [a2_seg]
        jae .a2_slice_done

        mov eax, [a2_slice]
        imul eax, [a2_seg]
        add eax, [a2_i]
        mov [a2_curr], eax
        test eax, eax
        jnz .a2_prev_plain
        mov eax, [a2_lane_len]
        dec eax
        jmp .a2_prev_have
.a2_prev_plain:
        dec eax
.a2_prev_have:
        mov [a2_prev], eax

        cmp dword [a2_indep], 0
        je .a2_dependent
        mov eax, [a2_i]
        test eax, A2_ADDR_WORDS - 1
        jnz .a2_addr_ready
        call a2_next_addresses
.a2_addr_ready:
        mov eax, [a2_i]
        and eax, A2_ADDR_WORDS - 1
        mov eax, [a2_addr + eax*8]
        jmp .a2_pseudo_have
.a2_dependent:
        mov eax, [a2_par]
        mov eax, [eax + A2P_MEM]
        mov ecx, [a2_prev]
        shl ecx, 10
        add eax, ecx
        mov eax, [eax]
.a2_pseudo_have:
        mov [a2_pseudo], eax

        mov eax, [a2_pass]
        mov ebx, [a2_slice]
        mov ecx, [a2_i]
        mov edx, [a2_pseudo]
        call a2_index
        mov [a2_ref], eax

        mov eax, [a2_par]
        mov eax, [eax + A2P_MEM]
        mov ecx, [a2_prev]
        shl ecx, 10
        add eax, ecx
        mov ebx, [a2_par]
        mov ebx, [ebx + A2P_MEM]
        mov ecx, [a2_ref]
        shl ecx, 10
        add ebx, ecx
        mov ecx, [a2_par]
        mov ecx, [ecx + A2P_MEM]
        mov esi, [a2_curr]
        shl esi, 10
        add ecx, esi
        mov edx, 0
        cmp dword [a2_pass], 0
        je .a2_fill
        mov edx, 1
.a2_fill:
        call a2_fill_block
        inc dword [a2_i]
        jmp .a2_i_loop

.a2_slice_done:
        inc dword [a2_slice]
        cmp dword [a2_slice], A2_SYNC_POINTS
        jb .a2_slice_loop
        inc dword [a2_pass]
        mov eax, [a2_par]
        mov eax, [eax + A2P_TCOST]
        cmp [a2_pass], eax
        jb .a2_pass_loop

        ; ---- the tag: H'^32 over the lane's last block ----
        mov esi, [a2_par]
        mov esi, [esi + A2P_MEM]
        mov ecx, [a2_lane_len]
        dec ecx
        shl ecx, 10
        add esi, ecx
        mov edi, a2_tagbuf
        mov ecx, A2_BLOCK_DWORDS
        cld
        rep movsd                       ; keep the block before unlocking
        mov eax, A2_TAGLEN
        call b2b_init
        mov dword [a2_u32], A2_TAGLEN
        mov eax, a2_u32
        mov ecx, 4
        call b2b_update
        mov eax, a2_tagbuf
        mov ecx, A2_BLOCK_BYTES
        call b2b_update
        mov eax, [a2_par]
        mov eax, [eax + A2P_OUT]
        call b2b_final

        mov edi, a2_lock
        call spin_unlock
        jmp .a2_done

.a2_bad:
        mov dword [a2_status], -1
.a2_done:
        mov eax, [a2_status]
        mov [esp + 28], eax
        popad
        ret
a2_par:    dd 0
a2_status: dd 0
a2_u32:    dd 0
a2_blocks: dd 0
a2_seg:    dd 0
a2_lane_len: dd 0
a2_pass:   dd 0
a2_slice:  dd 0
a2_i:      dd 0
a2_indep:  dd 0
a2_curr:   dd 0
a2_prev:   dd 0
a2_ref:    dd 0
a2_pseudo: dd 0

;-----------------------------------------------------------------------
; argon2_selftest - a vector small enough to run at every boot
;   Prints ARGON2_SELFTEST_OK or ARGON2_SELFTEST_FAIL on COM1.
;
; m = 8 KiB, t = 1: eight blocks and one pass, which still exercises BLAKE2b,
; the address blocks, the permutation and the indexing. The expected tag is
; what the reference implementation produces for the same inputs (the
; `argon2` command: echo -n password | argon2 somesalt -id -t 1 -k 8 -p 1).
;-----------------------------------------------------------------------
argon2_selftest:
        pushad
        call argon2_init
        mov dword [a2_st_par + A2P_PWD], a2_st_pwd
        mov dword [a2_st_par + A2P_PWDLEN], 8
        mov dword [a2_st_par + A2P_SALT], a2_st_salt
        mov dword [a2_st_par + A2P_SALTLEN], 8
        mov dword [a2_st_par + A2P_OUT], a2_st_out
        mov dword [a2_st_par + A2P_OUTLEN], A2_TAGLEN

        ; BLAKE2b-512("abc") first, so a hash failure is not blamed on Argon2
        mov eax, a2_st_b2out
        mov ecx, 64
        mov edx, a2_st_abc
        mov ebx, 3
        call b2b_hash
        mov esi, a2_st_b2out
        mov edi, a2_st_b2expect
        mov ecx, 64
        call a2_st_compare
        test eax, eax
        jz .a2st_fail

        mov eax, 8                      ; one pass: the plain fill path
        mov ebx, 1
        mov edi, a2_st_vec1
        call a2_st_vector
        test eax, eax
        jz .a2st_fail

        mov eax, 64                     ; three passes: the XOR path too
        mov ebx, 3
        mov edi, a2_st_vec2
        call a2_st_vector
        test eax, eax
        jz .a2st_fail

        mov eax, A2_M_KIB               ; and the parameters a logon uses
        mov ebx, A2_T_COST
        mov edi, a2_st_vec3
        call a2_st_vector
        test eax, eax
        jz .a2st_fail

        mov esi, msg_argon2_ok
        call serial_print
        call vga_print
        popad
        ret
.a2st_fail:
        mov esi, msg_argon2_fail
        call serial_print
        call vga_print
        popad
        ret

;-----------------------------------------------------------------------
; a2_st_vector - hash one vector and compare it with its published tag
;   In : EAX = m in KiB, EBX = passes, EDI = expected tag
;   Out: EAX = 1 when the tag matches
;-----------------------------------------------------------------------
a2_st_vector:
        push esi
        push edi
        push ecx
        mov [a2_st_par + A2P_MKIB], eax
        mov [a2_st_par + A2P_TCOST], ebx
        mov [a2_st_vexp], edi
        ; the memory cost comes from the allocator, as it does at logon
        mov ecx, A2_M_KIB * 1024 / 4096
        call pmm_alloc_pages
        test eax, eax
        jz .a2sv_bad
        mov [a2_st_par + A2P_MEM], eax
        mov eax, a2_st_par
        call argon2id_hash
        mov [a2_st_vstatus], eax
        mov eax, [a2_st_par + A2P_MEM]
        mov ecx, A2_M_KIB * 1024 / 4096
        call pmm_free_pages
        cmp dword [a2_st_vstatus], 0
        jne .a2sv_bad
        mov esi, a2_st_out
        mov edi, [a2_st_vexp]
        mov ecx, A2_TAGLEN
        call a2_st_compare
        jmp .a2sv_out
.a2sv_bad:
        xor eax, eax
.a2sv_out:
        pop ecx
        pop edi
        pop esi
        ret
a2_st_vexp:   dd 0
a2_st_vstatus: dd 0

;-----------------------------------------------------------------------
; a2_st_compare - In : ESI, EDI, ECX. Out: EAX = 1 when equal
;-----------------------------------------------------------------------
a2_st_compare:
        push esi
        push edi
        push ecx
        mov eax, 1
.a2sc_loop:
        mov dl, [esi]
        cmp dl, [edi]
        jne .a2sc_diff
        inc esi
        inc edi
        loop .a2sc_loop
        jmp .a2sc_out
.a2sc_diff:
        xor eax, eax
.a2sc_out:
        pop ecx
        pop edi
        pop esi
        ret

a2_st_par:  times A2P_SIZE db 0
a2_st_abc:  db "abc"
a2_st_b2out: times 64 db 0
a2_st_pwd:  db "password"
a2_st_salt: db "somesalt"
a2_st_out:  times A2_TAGLEN db 0

a2_st_b2expect:
        db 0xBA,0x80,0xA5,0x3F,0x98,0x1C,0x4D,0x0D
        db 0x6A,0x27,0x97,0xB6,0x9F,0x12,0xF6,0xE9
        db 0x4C,0x21,0x2F,0x14,0x68,0x5A,0xC4,0xB7
        db 0x4B,0x12,0xBB,0x6F,0xDB,0xFF,0xA2,0xD1
        db 0x7D,0x87,0xC5,0x39,0x2A,0xAB,0x79,0x2D
        db 0xC2,0x52,0xD5,0xDE,0x45,0x33,0xCC,0x95
        db 0x18,0xD3,0x8A,0xA8,0xDB,0xF1,0x92,0x5A
        db 0xB9,0x23,0x86,0xED,0xD4,0x00,0x99,0x23

; m=8, t=1, password/somesalt
a2_st_vec1:
        db 0xF1,0x37,0xF8,0xE1,0x86,0xA4,0x03,0xA6
        db 0x79,0xCC,0xD0,0x60,0x6E,0x5A,0xB5,0xDC
        db 0xDA,0xFE,0x43,0xC1,0x64,0x08,0x55,0xAC
        db 0x8C,0x6E,0x33,0xE9,0xBD,0x63,0xEE,0xB3

; m=64, t=3, password/somesalt
a2_st_vec2:
        db 0xB9,0xE3,0xBF,0xDA,0x58,0xFB,0x95,0xF1
        db 0xE1,0xE4,0x79,0xAF,0x4E,0x01,0xBF,0xAD
        db 0xE5,0xA0,0xDB,0xA3,0x4D,0x40,0x0A,0x25
        db 0x4F,0x77,0x04,0x62,0x67,0x41,0x69,0xE6

; m=1024, t=3, password/somesalt - the parameters a logon uses
a2_st_vec3:
        db 0x85,0x34,0x9F,0x83,0x4F,0x99,0xC5,0x45
        db 0x40,0xE8,0x0C,0xBC,0x12,0xDD,0x35,0xB1
        db 0xE3,0x6D,0xDA,0xDC,0x75,0x4F,0x5F,0x17
        db 0x87,0x97,0xA5,0xC4,0xD8,0x6B,0x34,0x99

msg_argon2_ok:   db "ARGON2_SELFTEST_OK", 0x0A, 0
msg_argon2_fail: db "ARGON2_SELFTEST_FAIL", 0x0A, 0
