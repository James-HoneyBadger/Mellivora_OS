; tail.asm - Print last N lines of a file [HBU]
; Usage: tail [-n NUM] FILE
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

        ; Parse arguments: tail [-n NUM] FILE
        mov dword [num_lines], MAX_LINES
        mov dword [filename], 0
        mov esi, args_buf

.parse_loop:
        call skip_spaces
        cmp byte [esi], 0
        je .parse_done

        cmp byte [esi], '-'
        jne .is_filename

        cmp byte [esi+1], 'n'
        jne .skip_word
        add esi, 2
        call skip_spaces
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

        ; Count total newlines in file
        mov esi, file_buf
        xor ecx, ecx           ; total newlines
        xor edx, edx           ; position
.count_loop:
        cmp edx, [file_size]
        jge .count_done
        cmp byte [esi+edx], 0x0A
        jne .count_next
        inc ecx
.count_next:
        inc edx
        jmp .count_loop

.count_done:
        ; ecx = total newlines
        ; We want to skip (total - num_lines) newlines, then print the rest
        mov eax, ecx
        sub eax, [num_lines]
        cmp eax, 0
        jle .print_all          ; fewer lines than requested, print all

        ; Skip eax newlines
        mov edx, eax            ; newlines to skip
        mov esi, file_buf
.skip_lines:
        cmp edx, 0
        je .found_start
        cmp byte [esi], 0x0A
        jne .skip_next
        dec edx
.skip_next:
        inc esi
        jmp .skip_lines

.found_start:
        ; esi points to start of tail portion
        jmp .print_from

.print_all:
        mov esi, file_buf

.print_from:
        ; Print from esi to end of file
        mov edi, file_buf
        add edi, [file_size]

.print_loop:
        cmp esi, edi
        jge .done

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

msg_usage:      db "Usage: tail [-n NUM] FILE", 10, 0
msg_err:        db "tail: cannot open file", 10, 0

section .bss
args_buf:       resb 256
filename:       resd 1
num_lines:      resd 1
file_size:      resd 1
file_buf:       resb 32768
    stack: resd 256
    stack_top:
