; wc.asm - Word/Line/Character count utility [HBU]
; Usage: wc [-l] [-w] [-c] <filename>
;   -l  Print line count only
;   -w  Print word count only
;   -c  Print character count only
;   (no flags: print all three)
;
%include "syscalls.inc"

start:
        ; Get args
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Parse flags
        mov esi, args_buf
        mov byte [flag_l], 0
        mov byte [flag_w], 0
        mov byte [flag_c], 0

.parse_flags:
        cmp byte [esi], ' '
        jne .check_flag
        inc esi
        jmp .parse_flags
.check_flag:
        cmp byte [esi], '-'
        jne .flags_done
        inc esi
.flag_chars:
        movzx eax, byte [esi]
        cmp al, 0
        je .flags_done
        cmp al, ' '
        je .next_flag_group
        cmp al, 'l'
        je .set_l
        cmp al, 'w'
        je .set_w
        cmp al, 'c'
        je .set_c
        ; Unknown flag char — treat as filename start
        dec esi                 ; back to '-'
        jmp .flags_done
.set_l:
        mov byte [flag_l], 1
        inc esi
        jmp .flag_chars
.set_w:
        mov byte [flag_w], 1
        inc esi
        jmp .flag_chars
.set_c:
        mov byte [flag_c], 1
        inc esi
        jmp .flag_chars
.next_flag_group:
        inc esi
        jmp .parse_flags

.flags_done:
        ; Skip spaces to filename
        cmp byte [esi], ' '
        jne .have_filename
        inc esi
        jmp .flags_done
.have_filename:
        cmp byte [esi], 0
        je .usage

        ; Copy filename to filename_buf
        mov edi, filename_buf
.copy_fn:
        lodsb
        cmp al, ' '
        je .fn_done
        stosb
        test al, al
        jnz .copy_fn
        jmp .fn_ready
.fn_done:
        mov byte [edi], 0
.fn_ready:

        ; If no flags set, enable all
        mov al, [flag_l]
        or al, [flag_w]
        or al, [flag_c]
        jnz .read_file
        mov byte [flag_l], 1
        mov byte [flag_w], 1
        mov byte [flag_c], 1

.read_file:
        ; Read the file
        mov eax, SYS_FREAD
        mov ebx, filename_buf
        mov ecx, file_buffer
        int 0x80
        cmp eax, 0
        jl .file_err
        mov [file_size], eax

        ; Count lines, words, characters
        xor ebx, ebx           ; line count
        xor ecx, ecx           ; word count
        xor edx, edx           ; char count (same as file_size)
        mov edx, eax           ; total characters
        mov esi, file_buffer
        xor edi, edi           ; in_word flag (0=no, 1=yes)

.count_loop:
        cmp eax, 0
        jle .count_done
        movzx ebp, byte [esi]

        ; Check for newline
        cmp ebp, 0x0A
        jne .not_newline
        inc ebx                ; line++
        mov edi, 0             ; end of word
        jmp .next_char

.not_newline:
        ; Check for whitespace (space, tab, CR)
        cmp ebp, ' '
        je .is_space
        cmp ebp, 9
        je .is_space
        cmp ebp, 0x0D
        je .is_space

        ; Non-whitespace character
        cmp edi, 0
        jne .next_char
        mov edi, 1             ; start of new word
        inc ecx                ; word++
        jmp .next_char

.is_space:
        mov edi, 0

.next_char:
        inc esi
        dec eax
        jmp .count_loop

.count_done:
        ; If file doesn't end with newline, count last line
        cmp edx, 0
        je .display
        mov eax, edx
        dec eax
        movzx eax, byte [file_buffer + eax]
        cmp eax, 0x0A
        je .display
        inc ebx                ; add last line

.display:
        ; Save counts
        mov [line_count], ebx
        mov [word_count], ecx
        mov [char_count], edx

        ; Set color
        push rbx
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B          ; cyan
        int 0x80
        pop rbx

        ; Print requested counts
        mov byte [printed], 0

        cmp byte [flag_l], 0
        je .skip_lines
        mov eax, [line_count]
        call print_dec
        mov byte [printed], 1
.skip_lines:

        cmp byte [flag_w], 0
        je .skip_words
        cmp byte [printed], 0
        je .no_sep_w
        mov eax, SYS_PRINT
        mov ebx, sep
        int 0x80
.no_sep_w:
        mov eax, [word_count]
        call print_dec
        mov byte [printed], 1
.skip_words:

        cmp byte [flag_c], 0
        je .skip_chars
        cmp byte [printed], 0
        je .no_sep_c
        mov eax, SYS_PRINT
        mov ebx, sep
        int 0x80
.no_sep_c:
        mov eax, [char_count]
        call print_dec
.skip_chars:

        ; Print filename
        mov eax, SYS_PRINT
        mov ebx, sep
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, filename_buf
        int 0x80

        ; Print labels if showing all
        mov al, [flag_l]
        and al, [flag_w]
        and al, [flag_c]
        jz .no_labels
        mov eax, SYS_PRINT
        mov ebx, msg_labels
        int 0x80
        jmp .exit
.no_labels:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
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

; Data
sep:            db "  ", 0
msg_labels:     db 0x0A, " (lines  words  chars)", 0x0A, 0
msg_usage:      db "Usage: wc [-l] [-w] [-c] <filename>", 0x0A, 0
msg_file_err:   db "Error: Cannot open file", 0x0A, 0

section .bss
args_buf:       resb 256
filename_buf:   resb 256
flag_l:         resb 1
flag_w:         resb 1
flag_c:         resb 1
printed:        resb 1
line_count:     resd 1
word_count:     resd 1
char_count:     resd 1
file_size:      resd 1
file_buffer:    resb 16384
