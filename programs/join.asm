; join.asm - Join lines of two files on a common field [HBU]
; Usage: join [-1 N] [-2 N] [-t CHAR] <file1> <file2>
; Joins lines from file1 and file2 that have matching join fields.
; Default: join field is field 1, separator is space/tab.
;
%include "syscalls.inc"

MAX_FILE        equ 32768
MAX_LINE        equ 256
MAX_FIELDS      equ 16

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle show_usage

        ; Defaults
        mov dword [join_field1], 1      ; 1-based field number
        mov dword [join_field2], 1
        mov byte [separator], ' '
        mov dword [fname1], 0
        mov dword [fname2], 0

        ; Parse arguments
        mov esi, args_buf
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
        cmp byte [esi], '1'
        je .set_f1
        cmp byte [esi], '2'
        je .set_f2
        cmp byte [esi], 't'
        je .set_sep
        jmp show_usage

.set_f1:
        inc esi
        call .skip_to_arg
        call .parse_num
        mov [join_field1], eax
        jmp .parse
.set_f2:
        inc esi
        call .skip_to_arg
        call .parse_num
        mov [join_field2], eax
        jmp .parse
.set_sep:
        inc esi
        call .skip_to_arg
        mov al, [esi]
        mov [separator], al
        inc esi
        jmp .parse

.skip_to_arg:
        cmp byte [esi], ' '
        jne .sta_done
        inc esi
        jmp .skip_to_arg
.sta_done:
        ret

.parse_num:
        xor eax, eax
.pn_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .pn_done
        cmp dl, '9'
        ja .pn_done
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pn_loop
.pn_done:
        cmp eax, 0
        je show_usage           ; field 0 not valid
        ret

.get_file:
        cmp dword [fname1], 0
        jne .get_f2_name
        mov [fname1], esi
        jmp .skip_word
.get_f2_name:
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

        ; Load file 1
        mov eax, SYS_FREAD
        mov ebx, [fname1]
        mov ecx, file1_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, -1
        je .file1_err
        mov [file1_len], eax

        ; Load file 2
        mov eax, SYS_FREAD
        mov ebx, [fname2]
        mov ecx, file2_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, -1
        je .file2_err
        mov [file2_len], eax

        ; For each line in file1, find matching lines in file2
        mov esi, file1_buf
        mov eax, [file1_len]
        add eax, file1_buf
        mov [file1_end], rax
.line1_loop:
        cmp rsi, [file1_end]
        jge .done
        ; Extract line from file1
        mov edi, line1_buf
        call extract_line
        mov [f1_next], rsi      ; save next line ptr
        ; Get join field from line1
        mov esi, line1_buf
        mov eax, [join_field1]
        call get_field
        jc .skip_line1          ; no such field
        ; Save join key
        mov esi, eax
        mov edi, key1_buf
        call copy_field

        ; Scan all lines in file2
        mov esi, file2_buf
        mov eax, [file2_len]
        add eax, file2_buf
        mov [file2_end_ptr], rax
.line2_loop:
        cmp rsi, [file2_end_ptr]
        jge .skip_line1
        mov edi, line2_buf
        call extract_line
        mov [f2_next], rsi
        ; Get join field from line2
        mov esi, line2_buf
        mov eax, [join_field2]
        call get_field
        jc .next_line2
        ; Compare keys
        mov esi, eax
        mov edi, key2_buf
        call copy_field
        mov esi, key1_buf
        mov edi, key2_buf
        call strcmp
        jnz .next_line2
        ; Match found - print: key field1_rest field2_rest
        call print_joined
.next_line2:
        mov rsi, [f2_next]
        jmp .line2_loop
.skip_line1:
        mov rsi, [f1_next]
        jmp .line1_loop

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.file1_err:
        mov eax, SYS_PRINT
        mov ebx, err_open1
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
.file2_err:
        mov eax, SYS_PRINT
        mov ebx, err_open2
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;---------------------------------------
; extract_line: copy one line from [ESI] to [EDI]
; Advances ESI past the newline. Null-terminates line in EDI.
;---------------------------------------
extract_line:
        push rcx
        mov ecx, MAX_LINE - 1
.el_loop:
        mov al, [esi]
        cmp al, 0
        je .el_end
        cmp al, 0x0A
        je .el_nl
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jnz .el_loop
        jmp .el_end
.el_nl:
        inc esi                 ; skip newline
.el_end:
        mov byte [edi], 0
        pop rcx
        ret

;---------------------------------------
; get_field: get Nth field (1-based) from line at ESI
; EAX = field number (1-based)
; Returns: EAX = pointer to start of field, CF clear
;          CF set if field not found
;---------------------------------------
get_field:
        push rbx
        push rcx
        mov ecx, eax            ; field count
        dec ecx                 ; skip (N-1) fields
.gf_skip:
        cmp ecx, 0
        je .gf_found
        ; Skip current field
.gf_scan:
        cmp byte [esi], 0
        je .gf_fail
        mov al, [esi]
        cmp al, [separator]
        je .gf_sep
        ; Also treat tab as separator when sep is space
        cmp byte [separator], ' '
        jne .gf_not_tab
        cmp al, 9              ; TAB
        je .gf_sep
.gf_not_tab:
        inc esi
        jmp .gf_scan
.gf_sep:
        inc esi                 ; skip separator
        ; Skip consecutive separators
        mov al, [esi]
        cmp al, [separator]
        je .gf_sep
        cmp byte [separator], ' '
        jne .gf_dec
        cmp al, 9
        je .gf_sep
.gf_dec:
        dec ecx
        jmp .gf_skip
.gf_found:
        mov eax, esi
        pop rcx
        pop rbx
        clc
        ret
.gf_fail:
        pop rcx
        pop rbx
        stc
        ret

;---------------------------------------
; copy_field: copy one field from [ESI] to [EDI]
; Stops at separator or null. Null-terminates.
;---------------------------------------
copy_field:
        push rax
.cf_loop:
        mov al, [esi]
        cmp al, 0
        je .cf_done
        cmp al, [separator]
        je .cf_done
        cmp byte [separator], ' '
        jne .cf_not_tab
        cmp al, 9
        je .cf_done
.cf_not_tab:
        mov [edi], al
        inc esi
        inc edi
        jmp .cf_loop
.cf_done:
        mov byte [edi], 0
        pop rax
        ret

;---------------------------------------
; strcmp: compare [ESI] and [EDI] (null-terminated)
; ZF=1 if equal, ZF=0 if not
;---------------------------------------
strcmp:
        push rax
        push rbx
.sc_loop:
        mov al, [esi]
        mov bl, [edi]
        cmp al, bl
        jne .sc_ne
        cmp al, 0
        je .sc_eq
        inc esi
        inc edi
        jmp .sc_loop
.sc_eq:
        pop rbx
        pop rax
        xor eax, eax           ; ZF=1
        ret
.sc_ne:
        pop rbx
        pop rax
        or eax, 1              ; ZF=0 (EAX nonzero)
        ret

;---------------------------------------
; print_joined: print key + non-key fields from line1 + non-key fields from line2
;---------------------------------------
print_joined:
        PUSHALL
        ; Print join key
        mov eax, SYS_PRINT
        mov ebx, key1_buf
        int 0x80

        ; Print non-key fields from line1
        mov esi, line1_buf
        mov ecx, 1             ; field counter
.pj_f1:
        cmp byte [esi], 0
        je .pj_f2_start
        cmp ecx, [join_field1]
        je .pj_f1_skip
        ; Print separator then field
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [separator]
        int 0x80
        call .pj_print_field
        inc ecx
        jmp .pj_f1
.pj_f1_skip:
        ; Skip this field
        call .pj_skip_field
        inc ecx
        jmp .pj_f1

.pj_f2_start:
        ; Print non-key fields from line2
        mov esi, line2_buf
        mov ecx, 1
.pj_f2:
        cmp byte [esi], 0
        je .pj_nl
        cmp ecx, [join_field2]
        je .pj_f2_skip
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [separator]
        int 0x80
        call .pj_print_field
        inc ecx
        jmp .pj_f2
.pj_f2_skip:
        call .pj_skip_field
        inc ecx
        jmp .pj_f2

.pj_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        POPALL
        ret

.pj_print_field:
        ; Print chars from [ESI] until separator or null
.ppf_loop:
        mov al, [esi]
        cmp al, 0
        je .ppf_done
        cmp al, [separator]
        je .ppf_sep
        cmp byte [separator], ' '
        jne .ppf_not_tab
        cmp al, 9
        je .ppf_sep
.ppf_not_tab:
        push rax
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rax
        inc esi
        jmp .ppf_loop
.ppf_sep:
        inc esi                 ; skip separator
        ; Skip consecutive separators
.ppf_skip_seps:
        mov al, [esi]
        cmp al, [separator]
        je .ppf_ss
        cmp byte [separator], ' '
        jne .ppf_done
        cmp al, 9
        je .ppf_ss
        jmp .ppf_done
.ppf_ss:
        inc esi
        jmp .ppf_skip_seps
.ppf_done:
        ret

.pj_skip_field:
        ; Skip chars from [ESI] until separator or null
.psf_loop:
        mov al, [esi]
        cmp al, 0
        je .psf_done
        cmp al, [separator]
        je .psf_sep
        cmp byte [separator], ' '
        jne .psf_not_tab
        cmp al, 9
        je .psf_sep
.psf_not_tab:
        inc esi
        jmp .psf_loop
.psf_sep:
        inc esi
        ; Skip consecutive separators
.psf_skip_seps:
        mov al, [esi]
        cmp al, [separator]
        je .psf_ss
        cmp byte [separator], ' '
        jne .psf_done
        cmp al, 9
        je .psf_ss
        jmp .psf_done
.psf_ss:
        inc esi
        jmp .psf_skip_seps
.psf_done:
        ret

show_usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

section .data
usage_str:      db "Usage: join [-1 N] [-2 N] [-t CHAR] file1 file2", 0x0A
                db "Join lines of two files on a common field.", 0x0A
                db "  -1 N   join on field N of file1 (default: 1)", 0x0A
                db "  -2 N   join on field N of file2 (default: 1)", 0x0A
                db "  -t C   use character C as field separator", 0x0A, 0
err_open1:      db "join: cannot open file1", 0x0A, 0
err_open2:      db "join: cannot open file2", 0x0A, 0

section .bss
args_buf:       resb 512
fname1:         resd 1
fname2:         resd 1
join_field1:    resd 1
join_field2:    resd 1
separator:      resb 1
file1_buf:      resb MAX_FILE
file2_buf:      resb MAX_FILE
file1_len:      resd 1
file2_len:      resd 1
file1_end:      resq 1
file2_end_ptr:  resq 1
f1_next:        resq 1
f2_next:        resq 1
line1_buf:      resb MAX_LINE
line2_buf:      resb MAX_LINE
key1_buf:       resb MAX_LINE
key2_buf:       resb MAX_LINE
