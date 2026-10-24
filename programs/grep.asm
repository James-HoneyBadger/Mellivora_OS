; grep.asm - Text pattern search utility [HBU]
; Usage: grep [-i] [-n] [-v] [-c] <pattern> [filename]
; Searches for lines containing the pattern. Flags follow POSIX grep:
;   -i ignore case  -n line numbers  -v invert  -c count only
;
%include "syscalls.inc"

MAX_LINE_LEN    equ 256

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Options: -i ignore case, -n line numbers, -v invert, -c count only
        mov esi, args_buf
.flags:
        call skip_spaces
        cmp byte [esi], '-'
        jne .flags_done
        cmp byte [esi + 1], 0
        je .flags_done
        inc esi
.flags_char:
        mov al, [esi]
        cmp al, 0
        je .flags_done
        cmp al, ' '
        je .flags_next
        cmp al, 'i'
        jne .fl_n
        mov dword [opt_i], 1
        jmp .fl_step
.fl_n:
        cmp al, 'n'
        jne .fl_v
        mov dword [opt_n], 1
        jmp .fl_step
.fl_v:
        cmp al, 'v'
        jne .fl_c
        mov dword [opt_v], 1
        jmp .fl_step
.fl_c:
        cmp al, 'c'
        jne .fl_step
        mov dword [opt_c], 1
.fl_step:
        inc esi
        jmp .flags_char
.flags_next:
        inc esi
        jmp .flags
.flags_done:
        ; Parse: pattern [filename]
        mov edi, pattern
        call parse_arg
        call skip_spaces
        cmp byte [esi], 0
        je .try_stdin           ; no filename: try stdin
        mov edi, filename
        call parse_arg

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buffer
        int 0x80
        cmp eax, 0
        jl .file_err
        mov [file_size], eax
        jmp .search_start

.try_stdin:
        mov eax, SYS_STDIN_READ
        mov ebx, file_buffer
        int 0x80
        cmp eax, 0
        jl .usage
        mov [file_size], eax

.search_start:
        ; Search line by line
        mov esi, file_buffer
        mov dword [line_num], 0
        mov dword [match_count], 0

.search_loop:
        mov eax, esi
        sub eax, file_buffer
        cmp eax, [file_size]
        jge .done

        inc dword [line_num]

        ; Extract current line into line_buf
        mov edi, line_buf
        xor ecx, ecx
.copy_line:
        movzx eax, byte [esi]
        cmp al, 0x0A
        je .line_end
        cmp al, 0
        je .line_end
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, MAX_LINE_LEN - 1
        jl .copy_line
        ; Skip rest of long line
.skip_rest:
        movzx eax, byte [esi]
        cmp al, 0x0A
        je .line_end
        cmp al, 0
        je .line_end
        inc esi
        jmp .skip_rest

.line_end:
        mov byte [edi + ecx], 0
        cmp byte [esi], 0x0A
        jne .no_skip_nl
        inc esi
.no_skip_nl:
        mov [line_len], ecx

        ; Search for pattern in this line
        cmp ecx, 0
        je .search_loop
        call search_pattern
        cmp eax, 0
        je .search_loop

        ; -v inverts the test.
        cmp dword [opt_v], 0
        je .matched
        xor eax, 1
.matched:
        cmp eax, 0
        je .search_loop
        inc dword [match_count]

        ; -c prints only the total, after the last line.
        cmp dword [opt_c], 0
        jne .search_loop

        cmp dword [opt_n], 0
        je .no_line_no
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; green
        int 0x80
        mov eax, [line_num]
        call print_dec
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; dark gray
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
.no_line_no:
        cmp dword [opt_v], 0
        jne .plain_line
        call print_highlighted_line
        jmp .line_printed
.plain_line:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line_buf
        int 0x80
.line_printed:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        jmp .search_loop

.done:
        ; -c prints only the count.
        cmp dword [opt_c], 0
        je .exit_status
        mov eax, [match_count]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

.exit_status:
        ; Like grep: 0 if something matched, 1 otherwise.
        mov ebx, 1
        cmp dword [match_count], 0
        je .exit_now
        xor ebx, ebx
.exit_now:
        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.file_err:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_file_err
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; Search for pattern in line_buf (case-insensitive)
; Returns EAX=1 if found, 0 if not
; Sets [match_pos] to position of first match
;=======================================================================
search_pattern:
        pushad
        mov esi, line_buf
        xor ebx, ebx           ; position in line

.sp_try:
        movzx eax, byte [esi + ebx]
        cmp al, 0
        je .sp_not_found

        ; Try to match pattern at this position
        mov edi, pattern
        mov ecx, ebx
.sp_match:
        movzx eax, byte [edi]
        cmp al, 0
        je .sp_found            ; end of pattern = full match
        movzx edx, byte [esi + ecx]
        cmp dl, 0
        je .sp_not_found

        cmp dword [opt_i], 0
        jne .sp_ci
        cmp al, dl
        jne .sp_next
        jmp .sp_advance
.sp_ci:
        call to_lower_al
        xchg eax, edx
        call to_lower_al
        cmp eax, edx
        jne .sp_next
.sp_advance:
        inc edi
        inc ecx
        jmp .sp_match

.sp_next:
        inc ebx
        jmp .sp_try

.sp_found:
        mov [match_pos], ebx
        mov [esp + 28], dword 1 ; EAX in pushad frame
        popad
        ret

.sp_not_found:
        mov [esp + 28], dword 0
        popad
        ret

;=======================================================================
; Convert AL to lowercase
;=======================================================================
to_lower_al:
        cmp al, 'A'
        jl .tla_done
        cmp al, 'Z'
        jg .tla_done
        add al, 32
.tla_done:
        ret

;=======================================================================
; Print line with pattern highlighted
;=======================================================================
print_highlighted_line:
        pushad
        mov esi, line_buf
        xor ecx, ecx           ; position

.phl_loop:
        movzx eax, byte [esi + ecx]
        cmp al, 0
        je .phl_done

        ; Check if we're at match position
        cmp ecx, [match_pos]
        jne .phl_normal

        ; Highlight the pattern
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; yellow highlight
        int 0x80

        ; Print pattern length chars
        push ecx
        mov edi, pattern
        xor edx, edx
.phl_highlight:
        cmp byte [edi + edx], 0
        je .phl_hl_done
        movzx ebx, byte [esi + ecx]
        cmp bl, 0
        je .phl_hl_done
        mov eax, SYS_PUTCHAR
        int 0x80
        inc ecx
        inc edx
        jmp .phl_highlight
.phl_hl_done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        add esp, 4             ; discard saved ecx
        jmp .phl_loop

.phl_normal:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        movzx ebx, byte [esi + ecx]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc ecx
        jmp .phl_loop

.phl_done:
        popad
        ret

;=======================================================================
; Parse argument from ESI to EDI
;=======================================================================
parse_arg:
.pa_loop:
        lodsb
        cmp al, ' '
        je .pa_done
        cmp al, 0
        je .pa_end
        stosb
        jmp .pa_loop
.pa_end:
        dec esi
.pa_done:
        mov byte [edi], 0
        ret

skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

; Data
msg_usage:      db "Usage: grep [-i] [-n] [-v] [-c] <pattern> [filename]", 0x0A, 0
msg_file_err:   db "Error: Cannot open file", 0x0A, 0

args_buf:       times 256 db 0
pattern:        times 64 db 0
filename:       times 64 db 0
file_size:      dd 0
line_num:       dd 0
line_len:       dd 0
match_count:    dd 0
opt_i:          dd 0
opt_n:          dd 0
opt_v:          dd 0
opt_c:          dd 0
match_pos:      dd 0
line_buf:       times MAX_LINE_LEN db 0
file_buffer:    times 32768 db 0
