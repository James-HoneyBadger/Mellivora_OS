; ==========================================================================
; awk - Simple pattern-action text processor for Mellivora OS
;
; Usage: awk <pattern> <action> [file]
;        awk /regex/ {print}  file.txt       Print matching lines
;        awk ""  {print $1}   file.txt       Print first field of every line
;        awk ""  {print NR}   file.txt       Print line numbers
;        Command | awk ...                   Read from stdin pipe
;
; Patterns:
;   /text/     - Print lines containing "text" (substring match)
;   ""         - Match all lines
;
; Actions:
;   {print}    - Print the whole line
;   {print $N} - Print field N (1-based, space/tab delimited)
;   {print NR} - Print line number
;   {print NF} - Print number of fields
;   {count}    - Count matching lines (prints total at end)
;
; Max line length: 1024 chars. Max file: 32 KB.
; ==========================================================================
%include "syscalls.inc"

MAX_FILE    equ 32768
MAX_LINE    equ 1024
MAX_FIELDS  equ 64

; Action types
ACT_PRINT       equ 0          ; print whole line
ACT_PRINT_FIELD equ 1          ; print $N
ACT_PRINT_NR    equ 2          ; print line number
ACT_PRINT_NF    equ 3          ; print field count
ACT_COUNT       equ 4          ; count matches

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz show_usage

        mov esi, arg_buf

        ; Parse pattern (first arg)
        ; If starts with /, extract regex between /.../ 
        cmp byte [esi], '/'
        je .parse_regex
        ; Empty string or quoted empty = match all
        cmp byte [esi], '"'
        je .empty_pattern
        cmp byte [esi], ' '
        je .empty_pattern
        ; Treat as literal pattern
        mov edi, pattern_buf
        call copy_token
        mov byte [has_pattern], 1
        jmp .parse_action

.parse_regex:
        inc esi                 ; skip opening /
        mov edi, pattern_buf
.pr_loop:
        lodsb
        test al, al
        jz show_usage
        cmp al, '/'
        je .pr_done
        stosb
        jmp .pr_loop
.pr_done:
        mov byte [edi], 0
        mov byte [has_pattern], 1
        jmp .skip_to_action

.empty_pattern:
        cmp byte [esi], '"'
        jne .ep_space
        ; Skip ""
        inc esi
        cmp byte [esi], '"'
        jne .ep_space
        inc esi
        jmp .skip_to_action
.ep_space:
        mov byte [has_pattern], 0

.skip_to_action:
        call skip_sp

.parse_action:
        call skip_sp
        ; Action should start with {
        cmp byte [esi], '{'
        jne show_usage
        inc esi

        ; Parse action keyword
        call skip_sp
        ; Check for "print"
        cmp dword [esi], 'prin'
        jne .check_count
        cmp byte [esi + 4], 't'
        jne .check_count

        add esi, 5
        call skip_sp

        ; Check what follows print
        cmp byte [esi], '}'
        je .act_print_line
        cmp byte [esi], '$'
        je .act_print_field
        cmp byte [esi], 'N'
        jne .act_print_line

        ; NR or NF?
        cmp byte [esi + 1], 'R'
        je .act_nr
        cmp byte [esi + 1], 'F'
        je .act_nf
        jmp .act_print_line

.act_nr:
        mov byte [action], ACT_PRINT_NR
        add esi, 2
        jmp .skip_close

.act_nf:
        mov byte [action], ACT_PRINT_NF
        add esi, 2
        jmp .skip_close

.act_print_line:
        mov byte [action], ACT_PRINT
        jmp .skip_close

.act_print_field:
        inc esi                 ; skip $
        ; Parse field number
        xor eax, eax
.apf_digit:
        cmp byte [esi], '0'
        jb .apf_done
        cmp byte [esi], '9'
        ja .apf_done
        imul eax, 10
        movzx ebx, byte [esi]
        sub ebx, '0'
        add eax, ebx
        inc esi
        jmp .apf_digit
.apf_done:
        mov [field_num], eax
        mov byte [action], ACT_PRINT_FIELD
        jmp .skip_close

.check_count:
        cmp dword [esi], 'coun'
        jne show_usage
        cmp byte [esi + 4], 't'
        jne show_usage
        mov byte [action], ACT_COUNT
        add esi, 5

.skip_close:
        ; Find and skip closing }
.sc_loop:
        cmp byte [esi], 0
        je .sc_done
        cmp byte [esi], '}'
        je .sc_found
        inc esi
        jmp .sc_loop
.sc_found:
        inc esi
.sc_done:
        call skip_sp

        ; Remaining arg = filename (or empty for stdin)
        cmp byte [esi], 0
        je .use_stdin

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, file_buf
        int 0x80
        test eax, eax
        jz file_err
        mov [file_len], eax
        mov byte [file_buf + eax], 0
        jmp process

.use_stdin:
        ; Try reading from piped stdin
        mov eax, SYS_STDIN_READ
        mov ebx, file_buf
        int 0x80
        cmp eax, -1
        je show_usage
        test eax, eax
        jz show_usage
        mov [file_len], eax
        mov byte [file_buf + eax], 0

process:
        mov dword [line_nr], 0
        mov dword [match_count], 0
        mov esi, file_buf

.next_line:
        cmp byte [esi], 0
        je .done

        ; Extract line into line_buf
        mov edi, line_buf
        xor ecx, ecx
.copy_line:
        lodsb
        test al, al
        jz .line_end
        cmp al, 0x0A
        je .line_end
        cmp al, 0x0D
        je .skip_cr
        stosb
        inc ecx
        cmp ecx, MAX_LINE - 1
        jge .line_end
        jmp .copy_line
.skip_cr:
        jmp .copy_line
.line_end:
        mov byte [edi], 0
        mov [line_len], ecx

        inc dword [line_nr]

        ; Check pattern match
        cmp byte [has_pattern], 0
        je .matched              ; no pattern = match all

        ; Substring search: does line_buf contain pattern_buf?
        call substr_match
        test eax, eax
        jz .next_line           ; no match

.matched:
        inc dword [match_count]

        ; Execute action
        movzx eax, byte [action]
        cmp eax, ACT_PRINT
        je .do_print
        cmp eax, ACT_PRINT_FIELD
        je .do_field
        cmp eax, ACT_PRINT_NR
        je .do_nr
        cmp eax, ACT_PRINT_NF
        je .do_nf
        cmp eax, ACT_COUNT
        je .next_line           ; count mode: just count, print at end
        jmp .next_line

.do_print:
        push rsi
        mov eax, SYS_PRINT
        mov ebx, line_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rsi
        jmp .next_line

.do_nr:
        push rsi
        mov eax, [line_nr]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rsi
        jmp .next_line

.do_nf:
        push rsi
        call count_fields
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rsi
        jmp .next_line

.do_field:
        push rsi
        call extract_field
        test eax, eax
        jz .df_empty
        mov eax, SYS_PRINT
        mov ebx, field_buf
        int 0x80
.df_empty:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rsi
        jmp .next_line

.done:
        ; If count mode, print the total
        cmp byte [action], ACT_COUNT
        jne .exit_ok
        mov eax, [match_count]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

.exit_ok:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_file_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

show_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -------------------------------------------------------------------
; substr_match - Check if pattern_buf is substring of line_buf
; Returns: EAX=1 if found, 0 if not
; -------------------------------------------------------------------
substr_match:
        push rsi
        push rdi
        push rcx
        push rdx

        mov esi, line_buf
.sm_outer:
        cmp byte [esi], 0
        je .sm_no

        ; Try matching pattern starting at ESI
        mov edi, pattern_buf
        mov edx, esi
.sm_inner:
        cmp byte [edi], 0
        je .sm_yes              ; pattern exhausted = match
        cmp byte [edx], 0
        je .sm_no               ; line exhausted = no match
        mov al, [edx]
        cmp al, [edi]
        jne .sm_next
        inc edx
        inc edi
        jmp .sm_inner

.sm_next:
        inc esi
        jmp .sm_outer

.sm_yes:
        mov eax, 1
        jmp .sm_ret
.sm_no:
        xor eax, eax
.sm_ret:
        pop rdx
        pop rcx
        pop rdi
        pop rsi
        ret

; -------------------------------------------------------------------
; count_fields - Count space/tab-separated fields in line_buf
; Returns: EAX = field count
; -------------------------------------------------------------------
count_fields:
        push rsi
        mov esi, line_buf
        xor eax, eax            ; count
        xor ecx, ecx            ; in_field flag

.cf_loop:
        cmp byte [esi], 0
        je .cf_done
        cmp byte [esi], ' '
        je .cf_sep
        cmp byte [esi], 0x09
        je .cf_sep
        ; In a field
        test ecx, ecx
        jnz .cf_cont
        inc eax                 ; new field starts
        mov ecx, 1
.cf_cont:
        inc esi
        jmp .cf_loop
.cf_sep:
        xor ecx, ecx            ; left a field
        inc esi
        jmp .cf_loop
.cf_done:
        pop rsi
        ret

; -------------------------------------------------------------------
; extract_field - Extract field [field_num] from line_buf into field_buf
; Returns: EAX=1 if found, 0 if not
; -------------------------------------------------------------------
extract_field:
        push rsi
        push rdi
        push rcx

        mov esi, line_buf
        mov ecx, [field_num]
        test ecx, ecx
        jz .ef_fail              ; $0 not supported

        ; Skip to field N
        xor edx, edx            ; current field
        xor ebx, ebx            ; in_field

.ef_scan:
        cmp byte [esi], 0
        je .ef_fail
        cmp byte [esi], ' '
        je .ef_sep2
        cmp byte [esi], 0x09
        je .ef_sep2
        test ebx, ebx
        jnz .ef_in
        inc edx                 ; entering new field
        mov ebx, 1
        cmp edx, ecx
        je .ef_copy             ; found our field
.ef_in:
        inc esi
        jmp .ef_scan
.ef_sep2:
        xor ebx, ebx
        inc esi
        jmp .ef_scan

.ef_copy:
        ; Copy field to field_buf
        mov edi, field_buf
.ef_cc:
        lodsb
        test al, al
        jz .ef_term
        cmp al, ' '
        je .ef_term
        cmp al, 0x09
        je .ef_term
        stosb
        jmp .ef_cc
.ef_term:
        mov byte [edi], 0
        mov eax, 1
        jmp .ef_ret

.ef_fail:
        mov byte [field_buf], 0
        xor eax, eax
.ef_ret:
        pop rcx
        pop rdi
        pop rsi
        ret

; -------------------------------------------------------------------
; Helpers
; -------------------------------------------------------------------
skip_sp:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_sp
.done:  ret

copy_token:
.ct_loop:
        lodsb
        test al, al
        jz .ct_end
        cmp al, ' '
        je .ct_end
        stosb
        jmp .ct_loop
.ct_end:
        mov byte [edi], 0
        ret

; -------------------------------------------------------------------
; Strings
; -------------------------------------------------------------------
msg_usage:      db "Usage: awk <pattern> <action> [file]", 0x0A
                db "  Patterns: /text/ or empty string", 0x0A
                db "  Actions:  {print} {print $N} {print NR} {print NF} {count}", 0x0A
                db "  Example:  awk /error/ {print} log.txt", 0x0A, 0
msg_file_err:   db "awk: cannot read file", 0x0A, 0

; -------------------------------------------------------------------
; BSS
; -------------------------------------------------------------------
has_pattern:    db 0
action:         db 0
field_num:      dd 0
line_nr:        dd 0
line_len:       dd 0
file_len:       dd 0
match_count:    dd 0
arg_buf:        times 512 db 0
pattern_buf:    times 256 db 0
field_buf:      times 256 db 0
line_buf:       times MAX_LINE db 0
file_buf:       times (MAX_FILE + 1) db 0
