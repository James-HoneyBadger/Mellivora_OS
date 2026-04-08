; uniq.asm - Remove/count duplicate consecutive lines [HBU]
; Usage: uniq [-c] [-d] FILE
; -c: prefix each line with count
; -d: only print duplicated lines
%include "syscalls.inc"

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Parse flags and filename
        mov dword [flag_count], 0
        mov dword [flag_dup], 0
        mov dword [filename], 0
        mov esi, args_buf

.parse_args:
        call skip_spaces
        cmp byte [esi], 0
        je .parse_done

        cmp byte [esi], '-'
        jne .is_file

        cmp byte [esi+1], 'c'
        jne .check_d
        mov dword [flag_count], 1
        add esi, 2
        jmp .parse_args

.check_d:
        cmp byte [esi+1], 'd'
        jne .skip_opt
        mov dword [flag_dup], 1
        add esi, 2
        jmp .parse_args

.skip_opt:
        ; Unknown option, skip
        inc esi
        jmp .parse_args

.is_file:
        mov [filename], esi
        ; Skip to end of word
.skip_word:
        cmp byte [esi], 0
        je .parse_done
        cmp byte [esi], ' '
        je .term_word
        inc esi
        jmp .skip_word
.term_word:
        mov byte [esi], 0
        inc esi
        jmp .parse_args

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

        ; Process line by line, comparing consecutive lines
        mov esi, file_buf       ; current position
        mov dword [dup_count], 1

        ; Copy first line to prev_line
        call copy_line_to_prev

.read_loop:
        ; Check if at end of file
        mov eax, esi
        sub eax, file_buf
        cmp eax, [file_size]
        jge .flush_last

        ; Compare current line with prev_line
        push esi
        mov edi, prev_line
        call cmp_lines
        pop esi
        cmp eax, 0
        je .same_line

        ; Different line: output prev_line
        call output_prev_line

        ; Copy current line to prev
        mov dword [dup_count], 1
        call copy_line_to_prev
        jmp .read_loop

.same_line:
        inc dword [dup_count]
        ; Skip current line
.skip_line:
        mov eax, esi
        sub eax, file_buf
        cmp eax, [file_size]
        jge .flush_last
        cmp byte [esi], 0x0A
        je .skip_nl
        inc esi
        jmp .skip_line
.skip_nl:
        inc esi
        jmp .read_loop

.flush_last:
        call output_prev_line

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

; Output the previous line according to flags
output_prev_line:
        ; If -d flag set, only output if dup_count > 1
        cmp dword [flag_dup], 1
        jne .opl_check_count
        cmp dword [dup_count], 1
        jle .opl_ret

.opl_check_count:
        ; If -c flag, print count prefix
        cmp dword [flag_count], 1
        jne .opl_print_line
        mov eax, [dup_count]
        call print_number
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

.opl_print_line:
        mov edi, prev_line
.opl_loop:
        cmp byte [edi], 0
        je .opl_nl
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [edi]
        int 0x80
        inc edi
        jmp .opl_loop
.opl_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
.opl_ret:
        ret

; Copy line at ESI to prev_line, advance ESI past it
copy_line_to_prev:
        mov edi, prev_line
        xor ecx, ecx
.copy:
        mov eax, esi
        sub eax, file_buf
        cmp eax, [file_size]
        jge .copy_done
        movzx eax, byte [esi]
        cmp al, 0x0A
        je .copy_skip_nl
        cmp ecx, 254
        jge .copy_skip_rest
        mov [edi+ecx], al
        inc ecx
.copy_skip_rest:
        inc esi
        jmp .copy
.copy_skip_nl:
        inc esi                 ; skip newline
.copy_done:
        mov byte [edi+ecx], 0  ; null terminate
        ret

; Compare line at ESI with null-terminated string at EDI
; Returns EAX=0 if equal, 1 if different
cmp_lines:
        push esi
        push edi
        mov edx, edi            ; save edi
.cloop:
        movzx eax, byte [esi]
        movzx ebx, byte [edx]
        ; Check for end-of-line in esi
        cmp al, 0x0A
        je .cmp_eol1
        cmp al, 0
        je .cmp_eol1
        ; Check for end in edx
        cmp bl, 0
        je .cmp_neq
        ; Compare
        cmp al, bl
        jne .cmp_neq
        inc esi
        inc edx
        jmp .cloop
.cmp_eol1:
        ; ESI at end, check if EDI also at end
        cmp bl, 0
        jne .cmp_neq
        xor eax, eax
        jmp .cmp_ret
.cmp_neq:
        mov eax, 1
.cmp_ret:
        pop edi
        pop esi
        ret

; Print decimal number in EAX
print_number:
        push esi
        mov esi, esp
        mov ecx, 0
        mov ebx, 10
.pn_div:
        xor edx, edx
        div ebx
        add dl, '0'
        push edx
        inc ecx
        cmp eax, 0
        jne .pn_div
.pn_print:
        cmp ecx, 0
        je .pn_done
        pop ebx
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jmp .pn_print
.pn_done:
        mov esp, esi
        pop esi
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

msg_usage:      db "Usage: uniq [-c] [-d] FILE", 10, 0
msg_err:        db "uniq: cannot open file", 10, 0

section .bss
args_buf:       resb 256
filename:       resd 1
flag_count:     resd 1
flag_dup:       resd 1
dup_count:      resd 1
file_size:      resd 1
prev_line:      resb 256
file_buf:       resb 32768
