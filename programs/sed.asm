; sed.asm - Simple stream editor (search and replace) [HBU]
; Usage: sed FIND REPLACE FILENAME
; Replaces first occurrence of FIND with REPLACE on each line
%include "syscalls.inc"

start:
        ; Get args
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        je .usage

        mov esi, arg_buf
        call .skip_sp

        ; Copy FIND pattern
        mov edi, find_str
        call .copy_word
        cmp byte [find_str], 0
        je .usage

        call .skip_sp

        ; Copy REPLACE string
        mov edi, replace_str
        call .copy_word

        call .skip_sp

        ; Copy FILENAME
        mov edi, filename
        call .copy_word
        cmp byte [filename], 0
        je .usage

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .not_found
        mov edi, file_buf
        add edi, eax
        mov byte [edi], 0

        ; Compute find length
        mov esi, find_str
        xor ecx, ecx
.fl_len:
        cmp byte [esi + ecx], 0
        je .fl_done
        inc ecx
        jmp .fl_len
.fl_done:
        mov [find_len], ecx

        ; Process line by line
        mov esi, file_buf

.process_line:
        cmp byte [esi], 0
        je .done

        ; Search for find_str starting at esi within current line
        mov edi, esi
.search:
        cmp byte [edi], 0
        je .no_match
        cmp byte [edi], 0x0A
        je .no_match

        ; Check if find_str matches at edi
        push esi
        push edi
        mov ebx, find_str
        mov ecx, [find_len]
.cmp_loop:
        cmp ecx, 0
        je .found_match
        mov al, [edi]
        cmp al, 0
        je .cmp_fail
        cmp al, 0x0A
        je .cmp_fail
        cmp al, [ebx]
        jne .cmp_fail
        inc edi
        inc ebx
        dec ecx
        jmp .cmp_loop

.cmp_fail:
        pop edi
        pop esi
        inc edi
        jmp .search

.found_match:
        pop edi                 ; edi = match start position
        pop esi                 ; esi = line start position

        ; Print characters from esi up to edi (before the match)
        mov ebx, esi
.print_before:
        cmp ebx, edi
        jge .print_repl
        push ebx
        movzx ebx, byte [ebx]
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ebx
        inc ebx
        jmp .print_before

.print_repl:
        ; Print replacement string
        push esi
        mov esi, replace_str
.pr_loop:
        cmp byte [esi], 0
        je .pr_done
        movzx ebx, byte [esi]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .pr_loop
.pr_done:
        pop esi

        ; Skip past the matched text in the original
        mov eax, [find_len]
        add edi, eax

        ; Print rest of line after match
.print_rest:
        cmp byte [edi], 0
        je .end_line
        cmp byte [edi], 0x0A
        je .end_line_nl
        movzx ebx, byte [edi]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc edi
        jmp .print_rest

.end_line_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        lea esi, [edi + 1]
        jmp .process_line

.end_line:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov esi, edi
        jmp .done

.no_match:
        ; Print line as-is
.nm_loop:
        cmp byte [esi], 0
        je .done
        cmp byte [esi], 0x0A
        je .nm_nl
        movzx ebx, byte [esi]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .nm_loop
.nm_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        inc esi
        jmp .process_line

.done:
        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.not_found:
        mov eax, SYS_PRINT
        mov ebx, msg_not_found
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

; Helper: skip spaces at ESI
.skip_sp:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp .skip_sp
.ss_done:
        ret

; Helper: copy word from ESI to EDI, advance ESI
.copy_word:
        cmp byte [esi], 0
        je .cw_end
        cmp byte [esi], ' '
        je .cw_end
        movsb
        jmp .copy_word
.cw_end:
        mov byte [edi], 0
        ret

;---------------------------------------
; Data
;---------------------------------------
msg_usage:     db "Usage: sed FIND REPLACE FILENAME", 0x0A, 0
msg_not_found: db "File not found.", 0x0A, 0

;---------------------------------------
; BSS
;---------------------------------------
arg_buf:     times 256 db 0
find_str:    times 128 db 0
replace_str: times 128 db 0
filename:    times 128 db 0
find_len:    dd 0
file_buf:    times 65536 db 0
file_size:   dd 0
