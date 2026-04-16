; lzss.asm - LZSS compression/decompression utility
; Usage: lzss [-d] <filename>
;   lzss file.txt        - compress file, output to stdout
;   lzss -d file.lz      - decompress file, output to stdout
;
; LZSS format:
;   Stream of flag bytes followed by 8 items each.
;   Flag bit 1 = literal byte (1 byte follows)
;   Flag bit 0 = match reference (2 bytes: 12-bit offset, 4-bit length+3)
;   Window size: 4096 bytes, min match: 3, max match: 18

%include "syscalls.inc"

MAX_FILE        equ 32768
WINDOW_SIZE     equ 4096
WINDOW_MASK     equ (WINDOW_SIZE - 1)
MIN_MATCH       equ 3
MAX_MATCH       equ 18         ; 4-bit length + MIN_MATCH

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        ; Parse args
        mov esi, arg_buf
        call skip_spaces

        ; Check for -d flag
        mov byte [decode_mode], 0
        cmp byte [esi], '-'
        jne .get_filename
        cmp byte [esi+1], 'd'
        jne .get_filename
        mov byte [decode_mode], 1
        add esi, 2
        call skip_spaces

.get_filename:
        ; Copy filename
        mov edi, filename
.copy_fn:
        lodsb
        cmp al, ' '
        je .fn_done
        cmp al, 0
        je .fn_done
        stosb
        jmp .copy_fn
.fn_done:
        mov byte [edi], 0

        ; Check we got a filename
        cmp byte [filename], 0
        je .usage

        ; Read input file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, in_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, -1
        je .file_err
        mov [in_len], eax

        ; Dispatch to compress or decompress
        cmp byte [decode_mode], 1
        je decompress
        jmp compress

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp exit_ok

.file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_file_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;---------------------------------------
; compress - LZSS compression
; Reads in_buf[0..in_len-1], writes compressed output to stdout
;---------------------------------------
compress:
        ; Initialize sliding window to spaces (like classic LZSS)
        mov edi, window
        mov al, ' '
        mov ecx, WINDOW_SIZE
        rep stosb

        mov dword [win_pos], WINDOW_SIZE - MAX_MATCH
        mov esi, in_buf             ; ESI = input pointer
        mov dword [in_ptr], 0       ; current input offset
        mov edi, out_buf            ; EDI = output pointer

.comp_loop:
        mov eax, [in_ptr]
        cmp eax, [in_len]
        jge .comp_flush

        ; Start a new flag byte group (8 items)
        mov ebp, edi                ; Save flag byte position
        mov byte [edi], 0
        inc edi
        xor ecx, ecx               ; bit counter (0-7)

.comp_item:
        cmp ecx, 8
        jge .comp_loop

        mov eax, [in_ptr]
        cmp eax, [in_len]
        jge .comp_pad

        ; Find longest match in window
        push rcx
        call find_match             ; Returns: EAX=match_len, EBX=match_offset
        pop rcx

        cmp eax, MIN_MATCH
        jb .comp_literal

        ; Encode match: 2 bytes (offset:12 | (len-3):4)
        ; Low byte  = offset & 0xFF
        ; High byte = ((offset >> 4) & 0xF0) | (len - 3)
        push rcx
        mov ecx, eax               ; ECX = match_len
        ; Flag bit 0 = match (don't set bit)
        mov edx, ebx               ; EDX = offset
        mov al, dl                  ; low 8 bits of offset
        stosb
        mov al, dh
        and al, 0x0F               ; high 4 bits of offset
        shl al, 4
        mov edx, ecx
        sub edx, MIN_MATCH
        and edx, 0x0F
        or al, dl
        stosb

        ; Advance input and window by match_len
        push rcx
        mov eax, [in_ptr]
.comp_adv_match:
        cmp ecx, 0
        jle .comp_adv_match_done
        ; Copy byte to window
        movzx edx, byte [in_buf + eax]
        mov ebx, [win_pos]
        mov [window + ebx], dl
        inc ebx
        and ebx, WINDOW_MASK
        mov [win_pos], ebx
        inc eax
        dec ecx
        jmp .comp_adv_match
.comp_adv_match_done:
        mov [in_ptr], eax
        pop rcx
        pop rcx                     ; restore bit counter
        inc ecx
        jmp .comp_item

.comp_literal:
        ; Flag bit 1 = literal
        mov eax, 1
        shl eax, cl
        or [ebp], al               ; Set bit in flag byte

        mov eax, [in_ptr]
        mov al, [in_buf + eax]
        stosb                       ; Write literal byte

        ; Advance input and window by 1
        mov eax, [in_ptr]
        movzx edx, byte [in_buf + eax]
        mov ebx, [win_pos]
        mov [window + ebx], dl
        inc ebx
        and ebx, WINDOW_MASK
        mov [win_pos], ebx
        inc dword [in_ptr]

        inc ecx
        jmp .comp_item

.comp_pad:
        ; Pad remaining bits as literals with zero
        cmp ecx, 8
        jge .comp_flush
        mov eax, 1
        shl eax, cl
        or [ebp], al
        mov byte [edi], 0
        inc edi
        inc ecx
        jmp .comp_pad

.comp_flush:
        ; Write output to stdout
        mov eax, [in_len]
        mov [out_len], eax
        sub edi, out_buf
        mov [out_len], edi
        ; Print compressed bytes
        mov esi, out_buf
        mov ecx, [out_len]
.comp_write:
        cmp ecx, 0
        jle .comp_stats
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [esi]
        int 0x80
        inc esi
        dec ecx
        jmp .comp_write

.comp_stats:
        ; Print stats to stderr (just exit for now)
        jmp exit_ok

;---------------------------------------
; find_match - Find longest match in sliding window
; Input: in_ptr, in_len, window, win_pos
; Output: EAX = match length (0 if no match >= MIN_MATCH)
;         EBX = match offset in window
;---------------------------------------
find_match:
        push rsi
        push rdi
        push rbp

        xor ebp, ebp               ; best length
        xor edx, edx               ; best offset

        ; Try all positions in window
        mov ecx, WINDOW_SIZE
        xor esi, esi                ; search position
.fm_loop:
        cmp ecx, 0
        jle .fm_done

        ; Compare window[esi..] with input[in_ptr..]
        xor edi, edi                ; match length
        mov eax, [in_ptr]
.fm_cmp:
        lea ebx, [eax + edi]
        cmp ebx, [in_len]
        jge .fm_check               ; End of input

        cmp edi, MAX_MATCH
        jge .fm_check               ; Max match reached

        lea ebx, [esi + edi]
        and ebx, WINDOW_MASK
        movzx ebx, byte [window + ebx]
        cmp bl, [in_buf + eax + edi]
        jne .fm_check
        inc edi
        jmp .fm_cmp

.fm_check:
        cmp edi, ebp
        jle .fm_next
        mov ebp, edi
        mov edx, esi

.fm_next:
        inc esi
        and esi, WINDOW_MASK
        dec ecx
        jmp .fm_loop

.fm_done:
        mov eax, ebp                ; best length
        mov ebx, edx                ; best offset
        pop rbp
        pop rdi
        pop rsi
        ret

;---------------------------------------
; decompress - LZSS decompression
; Reads in_buf[0..in_len-1], writes decompressed output to stdout
;---------------------------------------
decompress:
        ; Initialize window
        mov edi, window
        mov al, ' '
        mov ecx, WINDOW_SIZE
        rep stosb

        mov dword [win_pos], WINDOW_SIZE - MAX_MATCH
        mov esi, in_buf             ; ESI = input pointer
        mov ebx, [in_len]
        add ebx, in_buf
        mov [in_end], ebx           ; end of input

.dec_loop:
        cmp esi, [in_end]
        jge exit_ok

        ; Read flag byte
        movzx ebp, byte [esi]
        inc esi
        xor ecx, ecx               ; bit counter

.dec_item:
        cmp ecx, 8
        jge .dec_loop
        cmp esi, [in_end]
        jge exit_ok

        bt ebp, ecx
        jc .dec_literal

        ; Match reference: read 2 bytes
        cmp esi, [in_end]
        jge exit_ok
        movzx eax, byte [esi]      ; low offset
        inc esi
        cmp esi, [in_end]
        jge exit_ok
        movzx edx, byte [esi]      ; high nibble offset | length
        inc esi

        ; Decode offset: (high >> 4) << 8 | low
        push rcx
        mov ecx, edx
        shr ecx, 4
        shl ecx, 8
        or ecx, eax                ; ECX = offset

        ; Decode length: (byte2 & 0x0F) + MIN_MATCH
        and edx, 0x0F
        add edx, MIN_MATCH         ; EDX = length

        ; Copy from window
.dec_copy:
        cmp edx, 0
        jle .dec_copy_done
        mov eax, ecx
        and eax, WINDOW_MASK
        movzx eax, byte [window + eax]
        ; Output byte
        push rdx
        push rcx
        push rbx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rbx
        pop rcx
        pop rdx
        ; Store in window
        push rdx
        mov edx, [win_pos]
        mov [window + edx], al
        inc edx
        and edx, WINDOW_MASK
        mov [win_pos], edx
        pop rdx
        inc ecx
        dec edx
        jmp .dec_copy
.dec_copy_done:
        pop rcx
        inc ecx
        jmp .dec_item

.dec_literal:
        ; Literal byte
        movzx eax, byte [esi]
        inc esi
        ; Output byte
        push rcx
        push rbx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rbx
        pop rcx
        ; Store in window
        push rcx
        mov ecx, [win_pos]
        mov [window + ecx], al
        inc ecx
        and ecx, WINDOW_MASK
        mov [win_pos], ecx
        pop rcx
        inc ecx
        jmp .dec_item

exit_ok:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; skip_spaces - Skip spaces in ESI
;---------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

;---------------------------------------
; Data
;---------------------------------------
msg_usage:    db "Usage: lzss [-d] <filename>", 0x0A
              db "  lzss file.txt    - compress to stdout", 0x0A
              db "  lzss -d file.lz  - decompress to stdout", 0x0A, 0
msg_file_err: db "Error: cannot read file", 0x0A, 0

decode_mode:  db 0
in_ptr:       dd 0
in_len:       dd 0
out_len:      dd 0
in_end:       dd 0
win_pos:      dd 0

arg_buf:      times 256 db 0
filename:     times 256 db 0
window:       times WINDOW_SIZE db 0
in_buf:       times MAX_FILE db 0
out_buf:      times MAX_FILE db 0
