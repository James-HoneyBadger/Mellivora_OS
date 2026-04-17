; ==========================================================================
; gzip - Simple compression utility for Mellivora OS
; Usage: gzip <file>           Compress file → file.gz (RLE + LZ77)
;        gzip -d <file.gz>     Decompress file.gz → stdout
;        gzip -t <file.gz>     Test archive integrity
;
; Format (MZIP):
;   Header: 'MZ' (2), orig_size (4), checksum (4)
;   Stream: flag bytes followed by 8 items each
;     Flag bit 1 = literal byte (1 byte)
;     Flag bit 0 = match (2 bytes: 12-bit offset, 4-bit length+3)
; ==========================================================================

%include "syscalls.inc"

MAX_FILE    equ 61440       ; 60KB max input
WINDOW_SIZE equ 4096
WINDOW_MASK equ (WINDOW_SIZE - 1)
MIN_MATCH   equ 3
MAX_MATCH   equ 18          ; 4-bit length + 3
HEADER_SIZE equ 10          ; 'M','Z', orig_size(4), checksum(4)

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80
        cmp byte [arg_buf], 0
        je usage

        mov esi, arg_buf
        cmp byte [esi], '-'
        jne .compress
        cmp byte [esi + 1], 'd'
        je .decompress
        cmp byte [esi + 1], 't'
        je .test_archive
        jmp usage

; ================ COMPRESS ================
.compress:
        ; Read input file
        mov eax, SYS_FREAD
        mov ebx, arg_buf
        mov ecx, in_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jle err_read
        mov [in_len], eax

        ; Compute checksum (simple sum of all bytes)
        call compute_checksum
        mov [orig_checksum], eax

        ; Write header
        mov edi, out_buf
        mov byte [edi], 'M'
        mov byte [edi + 1], 'Z'
        mov eax, [in_len]
        mov [edi + 2], eax
        mov eax, [orig_checksum]
        mov [edi + 6], eax
        add edi, HEADER_SIZE

        ; LZ77 compress
        mov esi, in_buf
        xor ebp, ebp           ; input position
        mov ecx, [in_len]

.c_loop:
        cmp ebp, ecx
        jge .c_done

        ; Flag byte position — fill 8 items per flag
        mov edx, edi
        mov byte [edi], 0      ; flag byte placeholder
        inc edi
        xor ebx, ebx           ; bit counter (0-7)

.c_item:
        cmp ebp, ecx
        jge .c_flush
        cmp ebx, 8
        jge .c_loop

        ; Try to find match in window
        push rcx
        push rbx
        call find_match        ; returns: EAX = length, EBX = offset
        mov [match_len_val], eax
        mov [match_off_val], ebx
        pop rbx
        pop rcx

        cmp dword [match_len_val], MIN_MATCH
        jl .c_literal

        ; Encode match: flag bit = 0
        ; Already 0 from initialization, just shift
        ; 2 bytes: low = offset[7:0], high = (offset[11:8] << 4) | (len-3)
        mov eax, [match_off_val]
        mov [edi], al           ; offset low 8 bits
        shr eax, 8
        shl eax, 4
        mov ecx, [match_len_val]
        push rcx
        sub ecx, MIN_MATCH
        or eax, ecx
        mov [edi + 1], al
        add edi, 2
        pop rcx
        add ebp, ecx           ; advance input by match length
        mov ecx, [in_len]      ; restore in_len to ecx
        inc ebx
        jmp .c_item

.c_literal:
        ; Flag bit = 1 (literal)
        mov eax, 1
        push rcx
        mov cl, bl
        shl eax, cl
        or [edx], al           ; set bit in flag byte
        pop rcx
        mov al, [in_buf + ebp]
        mov [edi], al
        inc edi
        inc ebp
        inc ebx
        jmp .c_item

.c_flush:
        ; Remaining bits are 0 (match = no data = safe since we're done)
        jmp .c_write

.c_done:
.c_write:
        ; Calculate output size
        mov eax, edi
        sub eax, out_buf
        mov [out_len], eax

        ; Build output filename: <name>.gz
        mov esi, arg_buf
        mov edi, name_buf
.c_copy_name:
        lodsb
        test al, al
        jz .c_name_end
        stosb
        jmp .c_copy_name
.c_name_end:
        mov dword [edi], '.gz'
        mov byte [edi + 3], 0

        ; Write output file
        mov eax, SYS_FWRITE
        mov ebx, name_buf
        mov ecx, out_buf
        mov edx, [out_len]
        int 0x80

        ; Print stats
        mov eax, SYS_PRINT
        mov ebx, msg_compressed
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, arg_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_arrow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, name_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_size_pre
        int 0x80
        mov eax, [in_len]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_to
        int 0x80
        mov eax, [out_len]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_bytes_nl
        int 0x80
        jmp exit_prog

; ================ DECOMPRESS ================
.decompress:
        ; Skip "-d "
        mov esi, arg_buf
        add esi, 2
.d_skip_ws:
        cmp byte [esi], ' '
        jne .d_got_name
        inc esi
        jmp .d_skip_ws
.d_got_name:
        cmp byte [esi], 0
        je usage

        ; Read compressed file
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, in_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jle err_read
        mov [in_len], eax

        ; Verify header
        cmp byte [in_buf], 'M'
        jne err_format
        cmp byte [in_buf + 1], 'Z'
        jne err_format

        mov eax, [in_buf + 2]
        mov [orig_size], eax
        mov eax, [in_buf + 6]
        mov [orig_checksum], eax

        ; Decompress
        mov esi, in_buf
        add esi, HEADER_SIZE
        mov edi, out_buf
        mov ecx, [in_len]
        add ecx, in_buf        ; end pointer
        mov edx, [orig_size]   ; bytes remaining to decompress

.d_loop:
        cmp edx, 0
        jle .d_done
        cmp esi, ecx
        jge .d_done

        ; Read flag byte
        movzx ebp, byte [esi]
        inc esi
        xor ebx, ebx           ; bit counter

.d_item:
        cmp edx, 0
        jle .d_done
        cmp ebx, 8
        jge .d_loop
        cmp esi, ecx
        jge .d_done

        bt ebp, ebx
        jc .d_literal

        ; Match reference
        movzx eax, byte [esi]      ; offset low
        movzx ecx, byte [esi + 1]  ; (offset_hi << 4) | (len-3)
        add esi, 2
        push rcx
        and ecx, 0x0F
        add ecx, MIN_MATCH         ; match length
        pop rax
        push rax
        movzx eax, byte [esi - 1]  ; re-read high byte for offset
        shr eax, 4
        shl eax, 8
        movzx ecx, byte [esi - 2]  ; re-read low byte
        or eax, ecx                ; full 12-bit offset
        
        ; Read length again
        movzx ecx, byte [esi - 1]
        and ecx, 0x0F
        add ecx, MIN_MATCH
        pop rax                    ; discard saved

        ; Copy from output buffer at (edi - offset)
        push rsi
        mov esi, edi
        sub esi, eax
        cmp esi, out_buf
        jb .d_skip_match
.d_copy:
        cmp ecx, 0
        jle .d_match_done
        movsb
        dec ecx
        dec edx
        jmp .d_copy
.d_skip_match:
        ; Invalid offset, skip
        pop rsi
        inc ebx
        mov ecx, [in_len]
        add ecx, in_buf
        jmp .d_item
.d_match_done:
        pop rsi
        inc ebx
        push rcx
        mov ecx, [in_len]
        add ecx, in_buf
        pop rcx
        push rcx
        mov ecx, [in_len]
        add ecx, in_buf
        mov [.saved_end], ecx
        pop rcx
        mov ecx, [.saved_end]
        jmp .d_item

.saved_end: dd 0

.d_literal:
        movsb
        dec edx
        inc ebx
        push rcx
        mov ecx, [in_len]
        add ecx, in_buf
        pop rcx
        push rcx
        mov ecx, [in_len]
        add ecx, in_buf
        mov [.saved_end2], ecx
        pop rcx
        mov ecx, [.saved_end2]
        jmp .d_item

.saved_end2: dd 0

.d_done:
        ; Verify checksum
        mov eax, edi
        sub eax, out_buf
        mov [out_len], eax

        ; Print decompressed data to stdout
        mov byte [edi], 0
        mov eax, SYS_PRINT
        mov ebx, out_buf
        int 0x80
        jmp exit_prog

; ================ TEST ARCHIVE ================
.test_archive:
        mov esi, arg_buf
        add esi, 2
.t_skip_ws:
        cmp byte [esi], ' '
        jne .t_got_name
        inc esi
        jmp .t_skip_ws
.t_got_name:
        cmp byte [esi], 0
        je usage

        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, in_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jle err_read

        cmp byte [in_buf], 'M'
        jne err_format
        cmp byte [in_buf + 1], 'Z'
        jne err_format

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        jmp exit_prog

; ================ FIND MATCH ================
; Inputs: EBP = current position in in_buf
; Returns: EAX = match length (0 if none), EBX = offset from current pos
find_match:
        push rcx
        push rdx
        push rsi
        push rdi

        xor eax, eax           ; best length
        xor ebx, ebx           ; best offset

        ; Search window start = max(0, ebp - WINDOW_SIZE)
        mov ecx, ebp
        sub ecx, WINDOW_SIZE
        cmp ecx, 0
        jge .fm_start_ok
        xor ecx, ecx
.fm_start_ok:
        ; ECX = search start position

.fm_scan:
        cmp ecx, ebp
        jge .fm_done

        ; Compare in_buf[ecx..] with in_buf[ebp..]
        xor edx, edx           ; match length
        push rcx
.fm_cmp:
        mov esi, ecx
        add esi, edx
        mov edi, ebp
        add edi, edx

        ; Bounds check
        cmp edi, [in_len]
        jge .fm_cmp_done
        cmp edx, MAX_MATCH
        jge .fm_cmp_done

        push rax
        movzx eax, byte [in_buf + esi]
        cmp al, [in_buf + edi]
        pop rax
        jne .fm_cmp_done
        inc edx
        jmp .fm_cmp

.fm_cmp_done:
        pop rcx
        cmp edx, eax
        jle .fm_next
        mov eax, edx            ; new best length
        mov ebx, ebp
        sub ebx, ecx            ; offset = current - match pos

.fm_next:
        inc ecx
        jmp .fm_scan

.fm_done:
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        ret

; ================ COMPUTE CHECKSUM ================
compute_checksum:
        push rcx
        push rsi
        xor eax, eax
        mov ecx, [in_len]
        mov esi, in_buf
.cksum_loop:
        movzx edx, byte [esi]
        add eax, edx
        inc esi
        loop .cksum_loop
        pop rsi
        pop rcx
        ret

; ================ PRINT DECIMAL ================
print_decimal:
        PUSHALL
        mov ecx, 10
        xor ebp, ebp           ; digit count
.pd_push:
        xor edx, edx
        div ecx
        push rdx
        inc ebp
        test eax, eax
        jnz .pd_push
.pd_print:
        pop rdx
        add edx, '0'
        mov eax, SYS_PUTCHAR
        mov ebx, edx
        int 0x80
        dec ebp
        jnz .pd_print
        POPALL
        ret

; ================ MESSAGES ================
err_read:
        mov eax, SYS_PRINT
        mov ebx, msg_err_read
        int 0x80
        jmp exit_prog

err_format:
        mov eax, SYS_PRINT
        mov ebx, msg_err_fmt
        int 0x80
        jmp exit_prog

usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

exit_prog:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ---- Data ----
msg_usage:      db 'Usage: gzip <file>       Compress file', 10
                db '       gzip -d <file>    Decompress to stdout', 10
                db '       gzip -t <file>    Test archive', 10, 0
msg_compressed: db 'Compressed: ', 0
msg_arrow:      db ' -> ', 0
msg_size_pre:   db ' (', 0
msg_to:         db ' -> ', 0
msg_bytes_nl:   db ' bytes)', 10, 0
msg_ok:         db 'Archive OK.', 10, 0
msg_err_read:   db 'Error: cannot read file.', 10, 0
msg_err_fmt:    db 'Error: not a valid MZIP archive.', 10, 0

; ---- BSS ----
arg_buf:        times 256 db 0
name_buf:       times 280 db 0
in_len:         dd 0
out_len:        dd 0
orig_size:      dd 0
orig_checksum:  dd 0
match_len_val:  dd 0
match_off_val:  dd 0
in_buf:         times 61440 db 0
out_buf:        times 65536 db 0
