;=======================================================================
; lz.inc - the LZ codec HBFS uses for compressed chunks
;
; One chunk is at most 16 KiB of plaintext, encoded on its own: no match may
; reach outside the chunk, so a chunk is decodable without its neighbours and a
; torn chunk cannot poison the next one. That is what lets the container put a
; CRC on each chunk and lets a reader decompress only the chunk it needs.
;
; The bitstream
; -------------
; Tokens are byte-aligned, eight to a group. A group starts with one token-flag
; byte whose bit 0 selects the first token, bit 1 the second, and so on:
;
;   bit = 0  literal: one byte follows
;   bit = 1  match:   two bytes follow, little-endian word
;                    bits 0-11  distance - 1   (1..4096)
;                    bits 12-15 length - 3     (3..18)
;
; A group whose eighth token is not needed simply ends early: the decoder stops
; when it has produced the chunk's expected length, so trailing bits in the
; last flag byte are never read. Nothing in the stream records the plaintext
; length - the container does, and the decoder is told what to produce, which
; is also the check that catches a truncated stream.
;
; Why this and not something better: the format has to be frozen once it is
; tested, decoded by code that fits in this kernel, and independent of any
; library or version. 4096-byte window and 18-byte matches are the classic
; LZSS shape, and the encoder is one hash chain with a bounded search, so a
; chunk costs a bounded amount of time no matter what the data looks like.
;
; The encoder never returns an encoding that is not smaller than its input:
; it returns 0 and the caller stores the chunk raw, which is both cheaper and
; the reason incompressible data cannot grow a file.
;
; Neither entry point is reentrant: the hash chains and the loop state are one
; set of buffers, because a chunk is 16 KiB and giving every caller its own
; would cost more memory than the codec. A caller on the storage path already
; holds the volume lock, which is what makes that safe.
;=======================================================================

LZ_CHUNK_SIZE   equ 16384               ; plaintext bytes per chunk
LZ_MIN_MATCH    equ 3
LZ_MAX_MATCH    equ 18                  ; 4-bit length field
LZ_MAX_DIST     equ 4096                ; 12-bit distance field
LZ_NONE         equ 0xFFFF              ; "no candidate" in the hash chains
LZ_HASH_SIZE    equ 4096
LZ_MAX_CHAIN    equ 24                  ; candidates tried per position
; Worst case an encoding can be: every token a literal, eight to a flag byte.
LZ_TEST_ENC_SIZE equ (LZ_CHUNK_SIZE + LZ_CHUNK_SIZE / 8 + 64)

;-----------------------------------------------------------------------
; lz_compress - encode one chunk
;   In : ESI = plaintext, ECX = length (0..LZ_CHUNK_SIZE),
;        EDI = destination, EDX = destination capacity in bytes
;   Out: EAX = encoded length, 0 when the data did not shrink (store it raw);
;        CF = 1 when the arguments are out of range or the destination is full
;   All other registers preserved.
;-----------------------------------------------------------------------
lz_compress:
        pushad
        cmp  ecx, LZ_CHUNK_SIZE
        ja   .lc_full
        test edx, edx
        jz   .lc_full
        mov  [lz_c_src], esi
        mov  [lz_c_len], ecx
        mov  [lz_c_out], edi
        mov  [lz_c_start], edi
        lea  eax, [edi + edx]
        mov  [lz_c_end], eax
        mov  dword [lz_c_pos], 0
        mov  byte [lz_c_bits], 8        ; no group open yet
        mov  dword [lz_c_flag], 0
        test ecx, ecx
        jz   .lc_done                    ; nothing to encode

        ; Only the head table has to be reset: lz_prev is never read except
        ; through a chain that starts at a head entry inserted by this call.
        mov  edi, lz_head
        mov  ecx, LZ_HASH_SIZE
        mov  ax, LZ_NONE
        rep  stosw

.lc_loop:
        mov  ebx, [lz_c_pos]
        cmp  ebx, [lz_c_len]
        jae  .lc_done
        mov  eax, [lz_c_len]
        sub  eax, ebx
        cmp  eax, LZ_MIN_MATCH
        jb   .lc_literal                ; too few bytes left to match
        mov  dword [lz_best_len], 0
        mov  dword [lz_best_pos], 0

        ; Hash the three bytes at this position and walk the chain of earlier
        ; positions with the same hash, newest first, so the first candidate
        ; whose distance is already too large ends the search.
        mov  esi, [lz_c_src]
        add  esi, ebx
        movzx eax, word [esi]
        movzx edx, byte [esi + 2]
        shl  edx, 16
        or   eax, edx
        imul eax, 0x1E35A7BD
        shr  eax, 20
        movzx edi, word [lz_head + eax*2]
        mov  dword [lz_budget], LZ_MAX_CHAIN
.lc_chain:
        cmp  edi, LZ_NONE
        je   .lc_chain_done
        mov  eax, ebx
        sub  eax, edi
        cmp  eax, LZ_MAX_DIST
        ja   .lc_chain_done
        mov  edx, [lz_c_len]
        sub  edx, ebx
        cmp  edx, LZ_MAX_MATCH
        jbe  .lc_cmp_cap
        mov  edx, LZ_MAX_MATCH
.lc_cmp_cap:
        mov  esi, [lz_c_src]
        add  esi, ebx
        mov  ebp, [lz_c_src]
        add  ebp, edi
        xor  ecx, ecx
.lc_cmp:
        cmp  ecx, edx
        jae  .lc_cmp_done
        mov  al, [esi + ecx]
        cmp  al, [ebp + ecx]
        jne  .lc_cmp_done
        inc  ecx
        jmp  .lc_cmp
.lc_cmp_done:
        cmp  ecx, [lz_best_len]
        jbe  .lc_next_cand
        mov  [lz_best_len], ecx
        mov  [lz_best_pos], edi
        cmp  ecx, LZ_MAX_MATCH
        jae  .lc_chain_done             ; nothing can beat a full-length match
.lc_next_cand:
        movzx eax, word [lz_prev + edi*2]
        mov  edi, eax
        dec  dword [lz_budget]
        jnz  .lc_chain
.lc_chain_done:
        mov  ebx, [lz_c_pos]
        call lz_insert
        cmp  dword [lz_best_len], LZ_MIN_MATCH
        jae  .lc_match

.lc_literal:
        xor  al, al
        call lz_token
        jc   .lc_full
        mov  esi, [lz_c_src]
        add  esi, [lz_c_pos]
        mov  al, [esi]
        call lz_put
        jc   .lc_full
        inc  dword [lz_c_pos]
        jmp  .lc_loop

.lc_match:
        mov  al, 1
        call lz_token
        jc   .lc_full
        mov  eax, [lz_c_pos]
        sub  eax, [lz_best_pos]
        dec  eax                        ; distance - 1, the on-disk field
        mov  edx, [lz_best_len]
        sub  edx, LZ_MIN_MATCH
        shl  edx, 12
        or   eax, edx
        mov  [lz_tok], eax
        mov  al, [lz_tok]
        call lz_put
        jc   .lc_full
        mov  al, [lz_tok + 1]
        call lz_put
        jc   .lc_full

        ; Every position the match covers goes into the chains as well, or a
        ; later match cannot point into the middle of this one and the encoder
        ; loses the repeats it just found.
        mov  ebx, [lz_c_pos]
        mov  ebp, [lz_best_len]
.lc_match_ins:
        inc  ebx
        dec  ebp
        jz   .lc_match_adv
        mov  eax, [lz_c_len]
        sub  eax, ebx
        cmp  eax, LZ_MIN_MATCH
        jb   .lc_match_adv
        call lz_insert
        jmp  .lc_match_ins
.lc_match_adv:
        mov  eax, [lz_best_len]
        add  [lz_c_pos], eax
        jmp  .lc_loop

.lc_done:
        mov  eax, [lz_c_out]
        sub  eax, [lz_c_start]
        cmp  eax, [lz_c_len]
        jb   .lc_ok
        xor  eax, eax                   ; no gain: say so, the caller stores raw
.lc_ok:
        mov  [esp + 28], eax
        popad
        clc
        ret
.lc_full:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; lz_insert - add the position in EBX to the hash chain of its three bytes
;   In : EBX = position; requires at least three bytes from EBX to the end
;   Out: EAX, EDX, ESI, EDI clobbered; EBX, ECX, EBP preserved
;-----------------------------------------------------------------------
lz_insert:
        mov  esi, [lz_c_src]
        add  esi, ebx
        movzx eax, word [esi]
        movzx edx, byte [esi + 2]
        shl  edx, 16
        or   eax, edx
        imul eax, 0x1E35A7BD
        shr  eax, 20
        movzx edx, word [lz_head + eax*2]
        mov  [lz_prev + ebx*2], dx
        mov  [lz_head + eax*2], bx
        ret

;-----------------------------------------------------------------------
; lz_token - claim the next token slot in the current group
;   In : AL = 1 for a match, 0 for a literal
;   Out: CF = 1 when the destination is full
;   The flag byte is written when the group opens and patched in place, so the
;   encoder never has to buffer eight tokens before it knows them.
;-----------------------------------------------------------------------
lz_token:
        push eax
        cmp  byte [lz_c_bits], 8
        jb   .lt_open
        mov  edi, [lz_c_out]
        cmp  edi, [lz_c_end]
        jae  .lt_full
        mov  byte [edi], 0
        mov  [lz_c_flag], edi
        inc  edi
        mov  [lz_c_out], edi
        mov  byte [lz_c_bits], 0
.lt_open:
        inc  byte [lz_c_bits]
        pop  eax
        test al, al
        jz   .lt_done
        mov  edi, [lz_c_flag]
        movzx ecx, byte [lz_c_bits]
        dec  ecx
        mov  dl, 1
        shl  dl, cl
        or   [edi], dl
.lt_done:
        clc
        ret
.lt_full:
        pop  eax
        stc
        ret

;-----------------------------------------------------------------------
; lz_put - append the byte in AL to the output
;   Out: CF = 1 when the destination is full
;-----------------------------------------------------------------------
lz_put:
        mov  edi, [lz_c_out]
        cmp  edi, [lz_c_end]
        jae  .lp_full
        mov  [edi], al
        inc  edi
        mov  [lz_c_out], edi
        clc
        ret
.lp_full:
        stc
        ret

;-----------------------------------------------------------------------
; lz_decompress - decode one chunk
;   In : ESI = encoded bytes, ECX = encoded length,
;        EDI = destination, EDX = plaintext length the chunk must produce
;   Out: CF = 0 when exactly EDX bytes were produced from a well-formed stream;
;        CF = 1 when the stream is truncated or refers before the chunk
;   A stream that ends early is an error, which is why the plaintext length is
;   an argument and not a field: the container knows it, and asking for it is
;   the cheapest integrity check the format can have.
;-----------------------------------------------------------------------
lz_decompress:
        pushad
        cmp  edx, LZ_CHUNK_SIZE
        ja   .ld_bad
        mov  [lz_d_src], esi
        lea  eax, [esi + ecx]
        mov  [lz_d_src_end], eax
        mov  [lz_d_dst], edi
        mov  [lz_d_out], edi
        lea  eax, [edi + edx]
        mov  [lz_d_dst_end], eax
        cld

.ld_group:
        mov  eax, [lz_d_out]
        cmp  eax, [lz_d_dst_end]
        jae  .ld_ok
        mov  esi, [lz_d_src]
        cmp  esi, [lz_d_src_end]
        jae  .ld_bad                     ; group flags missing
        movzx ebx, byte [esi]
        inc  esi
        mov  [lz_d_src], esi
        mov  dword [lz_d_bits], 8

.ld_token:
        mov  eax, [lz_d_out]
        cmp  eax, [lz_d_dst_end]
        jae  .ld_ok
        mov  ecx, 8
        sub  ecx, [lz_d_bits]
        bt   ebx, ecx
        jc   .ld_match

        mov  esi, [lz_d_src]
        cmp  esi, [lz_d_src_end]
        jae  .ld_bad                     ; literal missing
        mov  al, [esi]
        inc  esi
        mov  [lz_d_src], esi
        mov  edi, [lz_d_out]
        mov  [edi], al
        inc  edi
        mov  [lz_d_out], edi
        jmp  .ld_next

.ld_match:
        mov  esi, [lz_d_src]
        lea  eax, [esi + 2]
        cmp  eax, [lz_d_src_end]
        ja   .ld_bad                     ; match operand truncated
        movzx edx, word [esi]
        add  esi, 2
        mov  [lz_d_src], esi
        mov  eax, edx
        and  eax, 0x0FFF
        inc  eax                         ; distance
        mov  ecx, edx
        shr  ecx, 12
        add  ecx, LZ_MIN_MATCH           ; length
        mov  edi, [lz_d_out]
        mov  esi, edi
        sub  esi, eax
        cmp  esi, [lz_d_dst]
        jb   .ld_bad                     ; refers before the chunk
        mov  edx, edi
        add  edx, ecx
        cmp  edx, [lz_d_dst_end]
        ja   .ld_bad                     ; would overrun the chunk
        rep  movsb                       ; forward, so a match may overlap itself
        mov  [lz_d_out], edi

.ld_next:
        dec  dword [lz_d_bits]
        jnz  .ld_token
        jmp  .ld_group

.ld_ok:
        mov  dword [esp + 28], 1
        popad
        clc
        ret
.ld_bad:
        mov  dword [esp + 28], 0
        popad
        stc
        ret

;-----------------------------------------------------------------------
; lz_selftest - round-trip a corpus, including data that must not compress
;   Prints LZ_SELFTEST_OK or LZ_SELFTEST_FAIL <n> on COM1 and the screen.
;
; A codec with an encoder and a decoder that agree with each other can be wrong
; together, so the cases are chosen to fail differently: data with long repeats
; has to shrink, data with none has to be refused rather than stored larger,
; a repeat at exactly the maximum distance has to be found (an off-by-one in
; the distance field still round-trips through its own decoder but would miss
; it), and a stream that refers before its own start has to be rejected.
;-----------------------------------------------------------------------
lz_selftest:
        pushad

        ; 1: nothing at all
        mov  esi, lz_test_src
        xor  ecx, ecx
        xor  edx, edx
        call lz_st_case
        test eax, eax
        jz   .lst_fail1

        ; 2: one byte, shorter than any match
        mov  esi, lz_test_src
        mov  byte [esi], 'Z'
        mov  ecx, 1
        xor  edx, edx
        call lz_st_case
        test eax, eax
        jz   .lst_fail2

        ; 3: three equal bytes - encodable, but not smaller than the input
        mov  esi, lz_test_src
        mov  word [esi], 'AA'
        mov  byte [esi + 2], 'A'
        mov  ecx, 3
        xor  edx, edx
        call lz_st_case
        test eax, eax
        jz   .lst_fail3

        ; 4: prose-like repetition, 16 KiB: a 108-byte sentence repeated, so
        ; matches are long and the encoding should be a small fraction
        call lz_st_fill_phrase
        mov  esi, lz_test_src
        mov  ecx, LZ_CHUNK_SIZE
        mov  edx, 4096
        call lz_st_case
        test eax, eax
        jz   .lst_fail4

        ; 5: one repeated byte: the limit of compressibility, and the case
        ; where the decoder must copy each match over itself
        mov  esi, lz_test_src
        mov  edi, lz_test_src
        mov  ecx, LZ_CHUNK_SIZE
        mov  al, 'A'
        rep  stosb
        mov  esi, lz_test_src
        mov  ecx, LZ_CHUNK_SIZE
        mov  edx, 4096
        call lz_st_case
        test eax, eax
        jz   .lst_fail5

        ; 6: incompressible data must be refused, not grown
        call lz_st_fill_lcg
        mov  esi, lz_test_src
        mov  ecx, LZ_CHUNK_SIZE
        xor  edx, edx
        call lz_st_case
        test eax, eax
        jz   .lst_fail6

        ; 7: the same 4 KiB four times, so every match sits at exactly
        ; LZ_MAX_DIST: the distance field has no room to spare, and an
        ; off-by-one there still round-trips through its own decoder while
        ; finding nothing
        call lz_st_fill_quarters
        mov  esi, lz_test_src
        mov  ecx, LZ_CHUNK_SIZE
        mov  edx, 8192
        call lz_st_case
        test eax, eax
        jz   .lst_fail7

        ; 8: an eight-byte period, so every match is short and near: the
        ; path where the chain is dense and the length cap is never reached
        mov  esi, lz_test_src
        mov  edi, lz_test_src
        mov  ecx, LZ_CHUNK_SIZE
        xor  edx, edx
.lst_period:
        mov  al, [lz_st_period + edx]
        stosb
        inc  edx
        and  edx, 7
        dec  ecx
        jnz  .lst_period
        mov  esi, lz_test_src
        mov  ecx, LZ_CHUNK_SIZE
        mov  edx, 8192
        call lz_st_case
        test eax, eax
        jz   .lst_fail8

        ; 9: a match with nothing behind it is corruption, not a file
        mov  esi, lz_st_bad
        mov  ecx, 3
        mov  edi, lz_test_out
        mov  edx, 8
        call lz_decompress
        jnc  .lst_fail9

        mov  esi, msg_lz_ok
        call serial_print
        call vga_print
        popad
        ret

.lst_fail1:
        mov  esi, msg_lz_fail
        mov  al, '1'
        jmp  .lst_fail
.lst_fail2:
        mov  esi, msg_lz_fail
        mov  al, '2'
        jmp  .lst_fail
.lst_fail3:
        mov  esi, msg_lz_fail
        mov  al, '3'
        jmp  .lst_fail
.lst_fail4:
        mov  esi, msg_lz_fail
        mov  al, '4'
        jmp  .lst_fail
.lst_fail5:
        mov  esi, msg_lz_fail
        mov  al, '5'
        jmp  .lst_fail
.lst_fail6:
        mov  esi, msg_lz_fail
        mov  al, '6'
        jmp  .lst_fail
.lst_fail7:
        mov  esi, msg_lz_fail
        mov  al, '7'
        jmp  .lst_fail
.lst_fail8:
        mov  esi, msg_lz_fail
        mov  al, '8'
        jmp  .lst_fail
.lst_fail9:
        mov  esi, msg_lz_fail
        mov  al, '9'
.lst_fail:
        mov  [lz_st_stagename], al
        call serial_print
        mov  esi, lz_st_stagename
        call serial_print
        mov  esi, msg_lz_nl
        call serial_print
        popad
        ret

;-----------------------------------------------------------------------
; lz_st_case - round-trip one corpus case
;   In : ESI = plaintext, ECX = length,
;        EDX = 0 when the case must be stored raw, or the encoded length the
;        case must come in under
;   Out: EAX = 1 when it behaved and the bytes came back identical
;   The cap is a real assertion, not decoration: a compressor that emits one
;   literal per byte "round-trips" perfectly and saves nothing, so a case that
;   must find structure has to prove it did.
;-----------------------------------------------------------------------
lz_st_case:
        pushad
        mov  [lz_st_src], esi
        mov  [lz_st_len], ecx
        mov  [lz_st_cap], edx
        mov  edi, lz_test_enc
        mov  edx, LZ_TEST_ENC_SIZE
        call lz_compress
        jc   .lsc_bad
        mov  [lz_st_clen], eax
        test eax, eax
        jnz  .lsc_compressed
        cmp  dword [lz_st_cap], 0
        jne  .lsc_bad                     ; it had to shrink and did not
        mov  eax, 1
        jmp  .lsc_ret
.lsc_compressed:
        cmp  eax, [lz_st_cap]
        jae  .lsc_bad                     ; shrank, but not by what it should
        mov  esi, lz_test_enc
        mov  ecx, [lz_st_clen]
        mov  edi, lz_test_out
        mov  edx, [lz_st_len]
        call lz_decompress
        jc   .lsc_bad
        mov  esi, lz_test_out
        mov  edi, [lz_st_src]
        mov  ecx, [lz_st_len]
        cld
        repe cmpsb
        jne  .lsc_bad
        mov  eax, 1
        jmp  .lsc_ret
.lsc_bad:
        xor  eax, eax
.lsc_ret:
        mov  [esp + 28], eax
        popad
        ret

; Fill the corpus with a sentence repeated, the way prose repeats itself.
lz_st_fill_phrase:
        pushad
        mov  edi, lz_test_src
        mov  ecx, LZ_CHUNK_SIZE
        xor  edx, edx
.lsf_loop:
        mov  al, [lz_st_phrase + edx]
        test al, al
        jnz  .lsf_have
        xor  edx, edx
        mov  al, [lz_st_phrase]
.lsf_have:
        inc  edx
        stosb
        dec  ecx
        jnz  .lsf_loop
        popad
        ret

; Fill the corpus with a linear congruential sequence: no repeats to find.
lz_st_fill_lcg:
        pushad
        mov  edi, lz_test_src
        mov  ecx, LZ_CHUNK_SIZE
        mov  eax, 0x12345678
.lsl_loop:
        imul eax, eax, 1664525
        add  eax, 1013904223
        mov  edx, eax
        shr  edx, 24                      ; the best-mixed byte
        mov  [edi], dl
        inc  edi
        dec  ecx
        jnz  .lsl_loop
        popad
        ret

; Fill the first 4 KiB with noise and repeat it over the rest of the chunk, so
; every match the encoder can find is at exactly the maximum distance.
lz_st_fill_quarters:
        pushad
        mov  edi, lz_test_src
        mov  ecx, 4096
        mov  eax, 0x9E3779B9
.lsp_noise:
        imul eax, eax, 1664525
        add  eax, 1013904223
        mov  edx, eax
        shr  edx, 24
        mov  [edi], dl
        inc  edi
        dec  ecx
        jnz  .lsp_noise
        mov  esi, lz_test_src
        mov  edi, lz_test_src + 4096
        mov  ecx, 4096 * 3
        rep  movsb                       ; forward, so each copy seeds the next
        popad
        ret

msg_lz_ok:      db "LZ_SELFTEST_OK", 0x0A, 0
msg_lz_fail:    db "LZ_SELFTEST_FAIL ", 0
msg_lz_nl:      db 0x0A, 0
lz_st_phrase:   db "the quick brown fox jumps over the lazy dog, and the "
                db "lazy dog does not mind at all. ", 0
lz_st_period:   db "HBFSv5!!"
lz_st_bad:      db 0x01, 0xE7, 0x03    ; a match of distance 1000 at offset 0

lz_st_stagename: db 0, 0
lz_st_src:      dd 0
lz_st_len:      dd 0
lz_st_cap:      dd 0
lz_st_clen:     dd 0

;-----------------------------------------------------------------------
; Encoder state. Kept in memory rather than in registers because the search
; loop needs every register it has; a chunk is at most 16 KiB, so the cost of
; a store is lost in the noise of the comparisons.
;-----------------------------------------------------------------------
lz_c_src:       dd 0
lz_c_len:       dd 0
lz_c_out:       dd 0
lz_c_start:     dd 0
lz_c_end:       dd 0
lz_c_pos:       dd 0
lz_c_flag:      dd 0
lz_c_bits:      db 0
lz_best_len:    dd 0
lz_best_pos:    dd 0
lz_budget:      dd 0
lz_tok:         dd 0

; Decoder state.
lz_d_src:       dd 0
lz_d_src_end:   dd 0
lz_d_dst:       dd 0
lz_d_dst_end:   dd 0
lz_d_out:       dd 0
lz_d_bits:      dd 0

; Hash chains: head is the newest position for a three-byte hash, prev links
; each position to the previous one with the same hash. Both hold chunk-local
; offsets, which fit in a word because a chunk is 16 KiB.
lz_head:        resw LZ_HASH_SIZE
lz_prev:        resw LZ_CHUNK_SIZE

; Corpus and scratch for the self-test.
lz_test_src:    resb LZ_CHUNK_SIZE
lz_test_enc:    resb LZ_TEST_ENC_SIZE
lz_test_out:    resb LZ_CHUNK_SIZE
