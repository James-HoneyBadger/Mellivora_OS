; base64.asm - Base64 encoder/decoder
; Usage: base64 [-d] <filename>
;   base64 file.txt       - encode file to base64
;   base64 -d file.b64    - decode base64 file

%include "syscalls.inc"

MAX_FILE    equ 32768

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
        je usage

        ; Read the file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jl file_error
        mov [file_len], eax

        cmp byte [decode_mode], 1
        je do_decode

        ; === ENCODE ===
do_encode:
        mov esi, file_buf
        mov edi, out_buf
        mov ecx, [file_len]

.enc_loop:
        cmp ecx, 0
        jle .enc_done

        ; Get 3 bytes (pad with 0)
        xor eax, eax
        xor ebx, ebx
        xor edx, edx

        movzx eax, byte [esi]
        cmp ecx, 2
        jl .pad1
        movzx ebx, byte [esi+1]
.pad1:
        cmp ecx, 3
        jl .pad2
        movzx edx, byte [esi+2]
.pad2:
        ; Combine into 24-bit value: (a<<16)|(b<<8)|c
        shl eax, 16
        shl ebx, 8
        or eax, ebx
        or eax, edx
        ; EAX = 24-bit value

        ; Extract 4 6-bit groups
        mov ebx, eax
        shr ebx, 18
        and ebx, 0x3F
        movzx ebx, byte [b64_table + ebx]
        mov [edi], bl
        inc edi

        mov ebx, eax
        shr ebx, 12
        and ebx, 0x3F
        movzx ebx, byte [b64_table + ebx]
        mov [edi], bl
        inc edi

        ; Third char (or =)
        cmp ecx, 2
        jl .eq3
        mov ebx, eax
        shr ebx, 6
        and ebx, 0x3F
        movzx ebx, byte [b64_table + ebx]
        mov [edi], bl
        jmp .c3done
.eq3:
        mov byte [edi], '='
.c3done:
        inc edi

        ; Fourth char (or =)
        cmp ecx, 3
        jl .eq4
        mov ebx, eax
        and ebx, 0x3F
        movzx ebx, byte [b64_table + ebx]
        mov [edi], bl
        jmp .c4done
.eq4:
        mov byte [edi], '='
.c4done:
        inc edi

        add esi, 3
        sub ecx, 3
        jmp .enc_loop

.enc_done:
        mov byte [edi], 10     ; trailing newline
        inc edi
        mov byte [edi], 0

        mov eax, SYS_PRINT
        mov ebx, out_buf
        int 0x80
        jmp exit_ok

        ; === DECODE ===
do_decode:
        mov esi, file_buf
        mov edi, out_buf
        mov ecx, [file_len]

.dec_loop:
        ; Skip whitespace in input
        cmp ecx, 0
        jle .dec_done
        movzx eax, byte [esi]
        cmp al, 10
        je .dec_skip
        cmp al, 13
        je .dec_skip
        cmp al, ' '
        je .dec_skip

        ; Need 4 base64 chars
        cmp ecx, 4
        jl .dec_done

        ; Decode 4 chars to 3 bytes
        push rcx
        xor edx, edx       ; accumulator

        ; Char 1
        movzx eax, byte [esi]
        call b64_val
        shl eax, 18
        or edx, eax

        ; Char 2
        movzx eax, byte [esi+1]
        call b64_val
        shl eax, 12
        or edx, eax

        ; Char 3
        movzx eax, byte [esi+2]
        cmp al, '='
        je .pad_2
        call b64_val
        shl eax, 6
        or edx, eax

        ; Char 4
        movzx eax, byte [esi+3]
        cmp al, '='
        je .pad_1
        call b64_val
        or edx, eax

        ; Output 3 bytes
        mov eax, edx
        shr eax, 16
        mov [edi], al
        mov eax, edx
        shr eax, 8
        mov [edi+1], al
        mov eax, edx
        mov [edi+2], al
        add edi, 3
        jmp .dec_adv

.pad_1:
        ; 2 output bytes
        mov eax, edx
        shr eax, 16
        mov [edi], al
        mov eax, edx
        shr eax, 8
        mov [edi+1], al
        add edi, 2
        jmp .dec_adv

.pad_2:
        ; 1 output byte
        mov eax, edx
        shr eax, 16
        mov [edi], al
        inc edi
        jmp .dec_adv

.dec_adv:
        pop rcx
        add esi, 4
        sub ecx, 4
        jmp .dec_loop

.dec_skip:
        inc esi
        dec ecx
        jmp .dec_loop

.dec_done:
        ; Calculate output length
        mov eax, edi
        sub eax, out_buf
        mov [out_len], eax

        ; Print the decoded output byte by byte
        mov esi, out_buf
        mov ecx, [out_len]
.print_loop:
        cmp ecx, 0
        jle exit_ok
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [esi]
        int 0x80
        inc esi
        dec ecx
        jmp .print_loop

exit_ok:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

file_error:
        mov eax, SYS_PRINT
        mov ebx, err_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;--------------------------------------
; b64_val: Convert base64 char in AL to 6-bit value in EAX
;--------------------------------------
b64_val:
        cmp al, 'A'
        jb .not_upper
        cmp al, 'Z'
        ja .not_upper
        sub al, 'A'
        movzx eax, al
        ret
.not_upper:
        cmp al, 'a'
        jb .not_lower
        cmp al, 'z'
        ja .not_lower
        sub al, 'a'
        add al, 26
        movzx eax, al
        ret
.not_lower:
        cmp al, '0'
        jb .not_digit
        cmp al, '9'
        ja .not_digit
        sub al, '0'
        add al, 52
        movzx eax, al
        ret
.not_digit:
        cmp al, '+'
        jne .not_plus
        mov eax, 62
        ret
.not_plus:
        mov eax, 63         ; '/' or anything else
        ret

;--------------------------------------
; skip_spaces: advance ESI past spaces
;--------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

;=======================================
; Data
;=======================================
usage_str:  db "Usage: base64 [-d] <filename>", 10
            db "  base64 file.txt    - encode to base64", 10
            db "  base64 -d file.b64 - decode from base64", 10, 0
err_str:    db "Error: cannot read file", 10, 0

b64_table:  db "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

decode_mode: db 0
filename:    times 64 db 0
arg_buf:     times 256 db 0
file_len:    dd 0
out_len:     dd 0
file_buf:    times MAX_FILE db 0
out_buf:     times MAX_FILE db 0
