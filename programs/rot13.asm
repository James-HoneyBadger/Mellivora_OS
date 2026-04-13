; rot13.asm - ROT13 cipher encoder/decoder
; Usage: rot13 <filename>    - rot13 encode/decode file contents
;        rot13               - read from keyboard until Ctrl+D

%include "syscalls.inc"

MAX_FILE    equ 32768

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov esi, arg_buf
        call skip_spaces

        cmp byte [esi], 0
        je interactive_mode

        ; File mode: copy filename
        mov edi, filename
.copy:
        lodsb
        cmp al, ' '
        je .cdone
        cmp al, 0
        je .cdone
        stosb
        jmp .copy
.cdone:
        mov byte [edi], 0

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jl file_error
        mov [file_len], eax

        ; Rot13 the buffer and print
        mov esi, file_buf
        mov ecx, [file_len]
.process:
        cmp ecx, 0
        jle .done
        movzx eax, byte [esi]
        call rot13_char
        push ecx
        push esi
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop esi
        pop ecx
        inc esi
        dec ecx
        jmp .process
.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; Interactive mode: read chars and rot13 them
interactive_mode:
        mov eax, SYS_PRINT
        mov ebx, info_str
        int 0x80

.loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 4              ; Ctrl+D
        je .quit
        cmp al, 0
        je .quit

        movzx eax, al
        call rot13_char
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .loop

.quit:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

file_error:
        mov eax, SYS_PRINT
        mov ebx, err_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;--------------------------------------
; rot13_char: ROT13 on char in EAX, return in EAX
;--------------------------------------
rot13_char:
        cmp al, 'A'
        jb .done
        cmp al, 'Z'
        jbe .upper
        cmp al, 'a'
        jb .done
        cmp al, 'z'
        ja .done
        ; lowercase
        sub al, 'a'
        add al, 13
        cmp al, 26
        jb .lower_ok
        sub al, 26
.lower_ok:
        add al, 'a'
        ret
.upper:
        sub al, 'A'
        add al, 13
        cmp al, 26
        jb .upper_ok
        sub al, 26
.upper_ok:
        add al, 'A'
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
info_str:   db "ROT13 cipher - type text (Ctrl+D to exit):", 10, 0
err_str:    db "Error: cannot read file", 10, 0

filename:    times 64 db 0
arg_buf:     times 256 db 0
file_len:    dd 0
file_buf:    times MAX_FILE db 0
