; wc.asm - Word/Line/Character count utility [HBU]
; Usage: wc <filename>
; Displays line count, word count, and character count
;
%include "syscalls.inc"

start:
        ; Get filename from command line args
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Read the file
        mov eax, SYS_FREAD
        mov ebx, args_buf
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
        ; Print: "  lines  words  chars  filename"
        mov eax, SYS_SETCOLOR
        push ebx
        push ecx
        push edx
        mov ebx, 0x0B          ; cyan
        int 0x80
        pop edx
        pop ecx
        pop ebx

        ; Lines
        mov eax, ebx
        push ecx
        push edx
        call print_dec
        pop edx
        pop ecx

        mov eax, SYS_PRINT
        mov ebx, sep
        int 0x80

        ; Words
        mov eax, ecx
        push edx
        call print_dec
        pop edx

        mov eax, SYS_PRINT
        mov ebx, sep
        int 0x80

        ; Characters
        mov eax, edx
        call print_dec

        mov eax, SYS_PRINT
        mov ebx, sep
        int 0x80

        ; Filename
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, args_buf
        int 0x80

        ; Labels
        mov eax, SYS_PRINT
        mov ebx, msg_labels
        int 0x80

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

; Data
sep:            db "  ", 0
msg_labels:     db 0x0A, " (lines  words  chars)", 0x0A, 0
msg_usage:      db "Usage: wc <filename>", 0x0A, 0
msg_file_err:   db "Error: Cannot open file", 0x0A, 0
args_buf:       times 256 db 0
file_size:      dd 0
file_buffer:    times 16384 db 0
