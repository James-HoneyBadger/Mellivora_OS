; comm.asm - Compare two sorted files line by line [HBU]
; Usage: comm [-1] [-2] [-3] <file1> <file2>
; -1 suppress lines unique to file1
; -2 suppress lines unique to file2
; -3 suppress lines common to both
;
%include "syscalls.inc"

MAX_FILE        equ 32768
MAX_LINE        equ 256

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle show_usage

        ; Parse arguments
        mov esi, args_buf
        mov byte [suppress_1], 0
        mov byte [suppress_2], 0
        mov byte [suppress_3], 0
        mov dword [fname1], 0
        mov dword [fname2], 0

.parse:
        cmp byte [esi], 0
        je .check_files
        cmp byte [esi], ' '
        jne .not_sp
        inc esi
        jmp .parse
.not_sp:
        cmp byte [esi], '-'
        jne .get_file
        inc esi
.flag_loop:
        cmp byte [esi], '1'
        je .set1
        cmp byte [esi], '2'
        je .set2
        cmp byte [esi], '3'
        je .set3
        jmp .parse
.set1:
        mov byte [suppress_1], 1
        inc esi
        jmp .flag_loop
.set2:
        mov byte [suppress_2], 1
        inc esi
        jmp .flag_loop
.set3:
        mov byte [suppress_3], 1
        inc esi
        jmp .flag_loop
.get_file:
        cmp dword [fname1], 0
        jne .get_f2
        mov [fname1], esi
        jmp .skip_word
.get_f2:
        mov [fname2], esi
.skip_word:
        cmp byte [esi], 0
        je .check_files
        cmp byte [esi], ' '
        je .term_word
        inc esi
        jmp .skip_word
.term_word:
        mov byte [esi], 0
        inc esi
        jmp .parse

.check_files:
        cmp dword [fname1], 0
        je show_usage
        cmp dword [fname2], 0
        je show_usage

        ; Read file 1
        mov eax, SYS_FREAD
        mov ebx, [fname1]
        mov ecx, file1_buf
        int 0x80
        cmp eax, 0
        jl file_error
        mov [file1_size], eax

        ; Read file 2
        mov eax, SYS_FREAD
        mov ebx, [fname2]
        mov ecx, file2_buf
        int 0x80
        cmp eax, 0
        jl file_error
        mov [file2_size], eax

        ; Null-terminate
        mov eax, [file1_size]
        mov byte [file1_buf + eax], 0
        mov eax, [file2_size]
        mov byte [file2_buf + eax], 0

        ; Compare lines
        mov esi, file1_buf      ; current pos in file1
        mov edi, file2_buf      ; current pos in file2

.compare:
        ; Check if either file exhausted
        cmp byte [esi], 0
        je .drain_f2
        cmp byte [edi], 0
        je .drain_f1

        ; Extract line from file1 into line1_buf
        push rdi
        mov edx, line1_buf
        call .extract_line      ; esi advanced past newline
        mov [f1_next], esi
        pop rdi

        ; Extract line from file2 into line2_buf
        push rsi
        mov esi, edi
        mov edx, line2_buf
        call .extract_line      ; esi advanced
        mov [f2_next], esi
        pop rsi

        ; Compare line1_buf vs line2_buf
        push rsi
        push rdi
        mov esi, line1_buf
        mov edi, line2_buf
        call .strcmp
        pop rdi
        pop rsi
        cmp eax, 0
        je .equal
        jl .less
        jg .greater

.less:
        ; line1 < line2 → unique to file1 (column 1)
        cmp byte [suppress_1], 1
        je .skip_less
        mov eax, SYS_PRINT
        mov ebx, line1_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
.skip_less:
        mov esi, [f1_next]      ; advance file1 only
        jmp .compare

.greater:
        ; line2 < line1 → unique to file2 (column 2)
        cmp byte [suppress_2], 1
        je .skip_greater
        cmp byte [suppress_1], 1
        je .col2_no_tab
        mov eax, SYS_PRINT
        mov ebx, tab_str
        int 0x80
.col2_no_tab:
        mov eax, SYS_PRINT
        mov ebx, line2_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
.skip_greater:
        mov edi, [f2_next]      ; advance file2 only
        jmp .compare

.equal:
        ; Common line (column 3)
        cmp byte [suppress_3], 1
        je .skip_equal
        ; Print with appropriate tabs
        cmp byte [suppress_1], 1
        je .eq_tab1
        mov eax, SYS_PRINT
        mov ebx, tab_str
        int 0x80
.eq_tab1:
        cmp byte [suppress_2], 1
        je .eq_tab2
        mov eax, SYS_PRINT
        mov ebx, tab_str
        int 0x80
.eq_tab2:
        mov eax, SYS_PRINT
        mov ebx, line1_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
.skip_equal:
        mov esi, [f1_next]
        mov edi, [f2_next]
        jmp .compare

.drain_f1:
        ; Remaining file1 lines
        cmp byte [esi], 0
        je .done
        push rdi
        mov edx, line1_buf
        call .extract_line
        pop rdi
        cmp byte [suppress_1], 1
        je .drain_f1
        mov eax, SYS_PRINT
        mov ebx, line1_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        jmp .drain_f1

.drain_f2:
        ; Remaining file2 lines
        cmp byte [edi], 0
        je .done
        push rsi
        mov esi, edi
        mov edx, line2_buf
        call .extract_line
        mov edi, esi
        pop rsi
        cmp byte [suppress_2], 1
        je .drain_f2
        cmp byte [suppress_1], 1
        je .d2_no_tab
        mov eax, SYS_PRINT
        mov ebx, tab_str
        int 0x80
.d2_no_tab:
        mov eax, SYS_PRINT
        mov ebx, line2_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        jmp .drain_f2

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; Extract line from [esi] into [edx], advance esi past newline
; Null-terminates the line
.extract_line:
        push rdi
        mov edi, edx
        xor ecx, ecx
.el_loop:
        mov al, [esi]
        cmp al, 0
        je .el_done
        cmp al, 0x0A
        je .el_nl
        stosb
        inc esi
        inc ecx
        cmp ecx, MAX_LINE - 1
        jl .el_loop
        jmp .el_done
.el_nl:
        inc esi                 ; skip newline
.el_done:
        mov byte [edi], 0
        pop rdi
        ret

; Compare null-terminated strings at ESI and EDI
; Returns: EAX < 0 if esi < edi, 0 if equal, > 0 if esi > edi
.strcmp:
        push rsi
        push rdi
.sc_loop:
        mov al, [esi]
        mov bl, [edi]
        cmp al, bl
        jne .sc_diff
        or al, al
        jz .sc_eq
        inc esi
        inc edi
        jmp .sc_loop
.sc_diff:
        movzx eax, al
        movzx ebx, bl
        sub eax, ebx
        pop rdi
        pop rsi
        ret
.sc_eq:
        xor eax, eax
        pop rdi
        pop rsi
        ret

show_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

file_error:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

msg_usage:      db "Usage: comm [-123] <file1> <file2>", 0x0A
                db "  -1  suppress lines unique to file1", 0x0A
                db "  -2  suppress lines unique to file2", 0x0A
                db "  -3  suppress lines common to both", 0x0A, 0
msg_err:        db "Error: cannot read file", 0x0A, 0
newline:        db 0x0A, 0
tab_str:        db 0x09, 0

args_buf:       times 512 db 0
fname1:         dd 0
fname2:         dd 0
f1_next:        dd 0
f2_next:        dd 0
suppress_1:     db 0
suppress_2:     db 0
suppress_3:     db 0
file1_size:     dd 0
file2_size:     dd 0
line1_buf:      times MAX_LINE db 0
line2_buf:      times MAX_LINE db 0
file1_buf:      times (MAX_FILE + 1) db 0
file2_buf:      times (MAX_FILE + 1) db 0
