; head.asm - Print first N lines of a file [HBU]
; Usage: head [-n NUM] FILE
; Default: 10 lines
%include "syscalls.inc"

MAX_LINES       equ 10

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Parse arguments: head [-n NUM] FILE
        mov dword [num_lines], MAX_LINES
        mov dword [filename], 0
        mov esi, args_buf

.parse_loop:
        call skip_spaces
        cmp byte [esi], 0
        je .parse_done

        cmp byte [esi], '-'
        jne .is_filename

        ; Check for -n
        cmp byte [esi+1], 'n'
        jne .skip_word
        add esi, 2
        call skip_spaces
        ; Parse number
        xor ecx, ecx
.parse_num:
        movzx eax, byte [esi]
        cmp al, '0'
        jb .num_done
        cmp al, '9'
        ja .num_done
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_num
.num_done:
        mov [num_lines], ecx
        jmp .parse_loop

.is_filename:
        mov [filename], esi
.skip_word:
        cmp byte [esi], 0
        je .parse_done
        cmp byte [esi], ' '
        je .parse_loop
        inc esi
        jmp .skip_word

.parse_done:
        cmp dword [filename], 0
        je .usage

        ; Read entire file
        mov eax, SYS_FREAD
        mov ebx, [filename]
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .file_err
        mov [file_size], eax

        ; Print first N lines
        mov esi, file_buf
        mov ecx, [num_lines]

.print_loop:
        cmp ecx, 0
        je .done
        mov eax, esi
        sub eax, file_buf
        cmp eax, [file_size]
        jge .done

        movzx eax, byte [esi]
        cmp al, 0x0A
        jne .not_newline
        dec ecx                 ; one less line to print
.not_newline:
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [esi]
        int 0x80
        inc esi
        jmp .print_loop

.done:
        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

skip_spaces:
.loop:
        cmp byte [esi], ' '
        je .skip
        cmp byte [esi], 9
        je .skip
        ret
.skip:
        inc esi
        jmp .loop

msg_usage:      db "Usage: head [-n NUM] FILE", 10, 0
msg_err:        db "head: cannot open file", 10, 0

section .bss
args_buf:       resb 256
filename:       resd 1
num_lines:      resd 1
file_size:      resd 1
file_buf:       resb 32768
