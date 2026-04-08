; diff.asm - Simple line-by-line file comparison [HBU]
; Usage: diff FILE1 FILE2
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

        ; Second filename
        call skip_spaces
        cmp byte [esi], 0
        je .usage
        mov [file2_name], esi
        ; Null-terminate at space if any
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

        ; Compare line by line
        mov esi, file1_buf      ; file1 pointer
        mov edi, file2_buf      ; file2 pointer
        mov dword [line_num], 0

.diff_loop:
        inc dword [line_num]

        ; Check if file1 exhausted
        mov eax, esi
        sub eax, file1_buf
        cmp eax, [file1_size]
        jge .check_f2_remaining

        ; Check if file2 exhausted
        mov eax, edi
        sub eax, file2_buf
        cmp eax, [file2_size]
        jge .f1_remaining

        ; Compare current lines
        push esi
        push edi
        call compare_lines
        pop edi
        pop esi
        cmp eax, 0
        je .lines_same

        ; Lines differ - print them
        ; Print "< " + line from file1
        mov eax, SYS_PUTCHAR
        mov ebx, '<'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        push esi
        call print_line
        pop esi

        ; Print "> " + line from file2
        mov eax, SYS_PUTCHAR
        mov ebx, '>'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        push edi
        call print_line_edi
        pop edi

.lines_same:
        ; Advance both pointers past current line
        call advance_esi
        call advance_edi
        jmp .diff_loop

.f1_remaining:
        ; File1 has more lines
        mov eax, esi
        sub eax, file1_buf
        cmp eax, [file1_size]
        jge .done
        mov eax, SYS_PUTCHAR
        mov ebx, '<'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        push esi
        call print_line
        pop esi
        call advance_esi
        jmp .f1_remaining

.check_f2_remaining:
        mov eax, edi
        sub eax, file2_buf
        cmp eax, [file2_size]
        jge .done
        mov eax, SYS_PUTCHAR
        mov ebx, '>'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        push edi
        call print_line_edi
        pop edi
        call advance_edi
        jmp .check_f2_remaining

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

; Compare lines at ESI and EDI, return EAX=0 if equal
compare_lines:
        push esi
        push edi
.cl_loop:
        movzx eax, byte [esi]
        movzx ebx, byte [edi]
        ; End-of-line checks
        cmp al, 0x0A
        je .cl_eol1
        cmp al, 0
        je .cl_eol1
        cmp bl, 0x0A
        je .cl_neq
        cmp bl, 0
        je .cl_neq
        cmp al, bl
        jne .cl_neq
        inc esi
        inc edi
        jmp .cl_loop
.cl_eol1:
        cmp bl, 0x0A
        je .cl_eq
        cmp bl, 0
        je .cl_eq
        jmp .cl_neq
.cl_eq:
        xor eax, eax
        pop edi
        pop esi
        ret
.cl_neq:
        mov eax, 1
        pop edi
        pop esi
        ret

; Print line at ESI (until newline), then newline
print_line:
.pl_loop:
        movzx eax, byte [esi]
        cmp al, 0x0A
        je .pl_nl
        cmp al, 0
        je .pl_nl
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .pl_loop
.pl_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

; Print line at EDI
print_line_edi:
.pld_loop:
        movzx eax, byte [edi]
        cmp al, 0x0A
        je .pld_nl
        cmp al, 0
        je .pld_nl
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        inc edi
        jmp .pld_loop
.pld_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

; Advance ESI past current line (past newline)
advance_esi:
        mov eax, esi
        sub eax, file1_buf
        cmp eax, [file1_size]
        jge .ae_done
        cmp byte [esi], 0x0A
        je .ae_skip
        inc esi
        jmp advance_esi
.ae_skip:
        inc esi
.ae_done:
        ret

; Advance EDI past current line
advance_edi:
        mov eax, edi
        sub eax, file2_buf
        cmp eax, [file2_size]
        jge .ad_done
        cmp byte [edi], 0x0A
        je .ad_skip
        inc edi
        jmp advance_edi
.ad_skip:
        inc edi
.ad_done:
        ret

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

msg_usage:      db "Usage: diff FILE1 FILE2", 10, 0
msg_err:        db "diff: cannot open file", 10, 0

section .bss
args_buf:       resb 256
file1_name:     resd 1
file2_name:     resd 1
file1_size:     resd 1
file2_size:     resd 1
line_num:       resd 1
file1_buf:      resb 16384
file2_buf:      resb 16384
