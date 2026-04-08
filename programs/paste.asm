; paste.asm - Merge lines from files side-by-side [HBU]
; Usage: paste FILE1 FILE2
%include "syscalls.inc"

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Parse two filenames
        mov esi, args_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; First filename
        mov [file1_name], esi
.find_sep1:
        cmp byte [esi], 0
        je .usage
        cmp byte [esi], ' '
        je .got_sep1
        inc esi
        jmp .find_sep1
.got_sep1:
        mov byte [esi], 0
        inc esi
        call skip_spaces
        cmp byte [esi], 0
        je .usage
        mov [file2_name], esi
        ; Null-terminate
.find_end2:
        cmp byte [esi], 0
        je .files_ready
        cmp byte [esi], ' '
        je .term2
        inc esi
        jmp .find_end2
.term2:
        mov byte [esi], 0

.files_ready:
        ; Read file 1
        mov eax, SYS_FREAD
        mov ebx, [file1_name]
        mov ecx, file1_buf
        int 0x80
        cmp eax, 0
        jle .file_err
        mov [file1_size], eax

        ; Read file 2
        mov eax, SYS_FREAD
        mov ebx, [file2_name]
        mov ecx, file2_buf
        int 0x80
        cmp eax, 0
        jle .file_err
        mov [file2_size], eax

        ; Merge line by line
        mov esi, file1_buf      ; file1 position
        mov edi, file2_buf      ; file2 position

.merge_loop:
        ; Check if both exhausted
        mov eax, esi
        sub eax, file1_buf
        cmp eax, [file1_size]
        jge .check_f2_only

        ; Print line from file1 (without newline)
.print_f1:
        mov eax, esi
        sub eax, file1_buf
        cmp eax, [file1_size]
        jge .print_tab
        cmp byte [esi], 0x0A
        je .skip_f1_nl
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [esi]
        int 0x80
        inc esi
        jmp .print_f1
.skip_f1_nl:
        inc esi                 ; skip newline

.print_tab:
        ; Tab separator
        mov eax, SYS_PUTCHAR
        mov ebx, 9
        int 0x80

        ; Print line from file2
        mov eax, edi
        sub eax, file2_buf
        cmp eax, [file2_size]
        jge .print_nl
.print_f2:
        mov eax, edi
        sub eax, file2_buf
        cmp eax, [file2_size]
        jge .print_nl
        cmp byte [edi], 0x0A
        je .skip_f2_nl
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [edi]
        int 0x80
        inc edi
        jmp .print_f2
.skip_f2_nl:
        inc edi

.print_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .merge_loop

.check_f2_only:
        ; File1 done, check file2
        mov eax, edi
        sub eax, file2_buf
        cmp eax, [file2_size]
        jge .done

        ; Print remaining file2 lines with leading tab
        mov eax, SYS_PUTCHAR
        mov ebx, 9
        int 0x80
.print_f2_rem:
        mov eax, edi
        sub eax, file2_buf
        cmp eax, [file2_size]
        jge .rem_nl
        cmp byte [edi], 0x0A
        je .skip_f2_rem_nl
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [edi]
        int 0x80
        inc edi
        jmp .print_f2_rem
.skip_f2_rem_nl:
        inc edi
.rem_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .check_f2_only

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

msg_usage:      db "Usage: paste FILE1 FILE2", 10, 0
msg_err:        db "paste: cannot open file", 10, 0

section .bss
args_buf:       resb 256
file1_name:     resd 1
file2_name:     resd 1
file1_size:     resd 1
file2_size:     resd 1
file1_buf:      resb 16384
file2_buf:      resb 16384
