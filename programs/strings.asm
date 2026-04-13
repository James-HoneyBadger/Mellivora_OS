; strings.asm - Extract printable strings from a file
; Usage: strings [-n min] <filename>
; Displays sequences of printable characters (min length, default 4)

%include "syscalls.inc"

MAX_FILE    equ 32768
DEF_MIN     equ 4

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov dword [min_len], DEF_MIN
        mov esi, arg_buf
        call skip_spaces

        ; Check for -n flag
        cmp byte [esi], '-'
        jne .get_filename
        cmp byte [esi+1], 'n'
        jne .get_filename
        add esi, 2
        call skip_spaces
        ; Parse number
        call parse_num
        mov [min_len], eax
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

        cmp byte [filename], 0
        je usage

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jl file_error
        mov [file_len], eax

        ; Scan for printable strings
        mov esi, file_buf
        mov ecx, [file_len]
        mov edi, str_buf        ; current string accumulator
        xor ebx, ebx           ; current string length

.scan:
        cmp ecx, 0
        jle .flush
        movzx eax, byte [esi]
        inc esi
        dec ecx

        ; Check if printable (0x20-0x7E) or tab
        cmp al, 9
        je .add_char
        cmp al, 0x20
        jb .end_str
        cmp al, 0x7E
        ja .end_str

.add_char:
        mov [edi + ebx], al
        inc ebx
        jmp .scan

.end_str:
        ; If accumulated string >= min_len, print it
        cmp ebx, [min_len]
        jl .reset
        mov byte [edi + ebx], 0
        push ecx
        push esi
        mov eax, SYS_PRINT
        mov ebx, str_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop esi
        pop ecx
.reset:
        xor ebx, ebx
        jmp .scan

.flush:
        ; Check last accumulated string
        cmp ebx, [min_len]
        jl .done
        mov byte [edi + ebx], 0
        mov eax, SYS_PRINT
        mov ebx, str_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
.done:
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
; parse_num: Parse decimal number from ESI, return in EAX
;--------------------------------------
parse_num:
        xor eax, eax
.loop:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jb .done
        cmp cl, '9'
        ja .done
        imul eax, 10
        sub cl, '0'
        add eax, ecx
        inc esi
        jmp .loop
.done:
        ret

;--------------------------------------
; skip_spaces
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
usage_str:  db "Usage: strings [-n min] <filename>", 10
            db "Extract printable strings from a file", 10
            db "  -n min  minimum string length (default 4)", 10, 0
err_str:    db "Error: cannot read file", 10, 0

filename:    times 64 db 0
arg_buf:     times 256 db 0
min_len:     dd DEF_MIN
file_len:    dd 0
file_buf:    times MAX_FILE db 0
str_buf:     times 1024 db 0
