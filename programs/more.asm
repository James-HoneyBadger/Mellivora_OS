; more.asm — Paging text viewer for Mellivora OS
; Usage: more [FILE]   or   ... | more
; Reads from FILE if given, otherwise from stdin (pipe).
; Displays 23 lines per page.  Press SPACE for next page, Q to quit.

%include "syscalls.inc"

PAGE_LINES  equ 23             ; lines per screen before pausing

start:
        ; --- Get optional filename arg ---
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        mov esi, args_buf
        call skip_spaces
        cmp byte [esi], 0
        je .read_stdin

        ; Copy filename (first word) to fname_buf
        mov edi, fname_buf
.copy_name:
        mov al, [esi]
        cmp al, ' '
        je .name_done
        cmp al, 0
        je .name_done
        mov [edi], al
        inc esi
        inc edi
        jmp .copy_name
.name_done:
        mov byte [edi], 0

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, fname_buf
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .file_err
        mov [file_size], eax
        jmp .page_start

.read_stdin:
        mov eax, SYS_STDIN_READ
        mov ebx, file_buf
        int 0x80
        cmp eax, 0
        jl .no_input
        jz .no_input
        mov [file_size], eax

.page_start:
        mov esi, file_buf
        mov dword [line_count], 0

.print_char:
        ; Check if we've consumed all data
        mov eax, esi
        sub eax, file_buf
        cmp eax, [file_size]
        jge .done

        movzx ebx, byte [esi]
        inc esi

        ; Output character
        mov eax, SYS_PUTCHAR
        int 0x80

        ; Track newlines to implement paging
        cmp bl, 0x0A
        jne .print_char
        inc dword [line_count]
        mov eax, [line_count]
        cmp eax, PAGE_LINES
        jl  .print_char

        ; --- Page boundary: show prompt and wait ---
        mov dword [line_count], 0
        mov eax, SYS_PRINT
        mov ebx, msg_more
        int 0x80

.wait_key:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz  .wait_key           ; no key yet

        ; Erase the "-- More --" prompt (overwrite with spaces + CR)
        mov eax, SYS_PRINT
        mov ebx, msg_clear
        int 0x80

        ; q or Q → quit
        cmp al, 'q'
        je .done
        cmp al, 'Q'
        je .done
        ; SPACE or ENTER → next page (already reset line_count)
        jmp .print_char

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.no_input:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; --- Helpers ---
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

; --- Data ---
fname_buf:   times 256 db 0
args_buf:    times 512 db 0
file_buf:    times 65536 db 0
file_size:   dd 0
line_count:  dd 0

msg_more:  db 0x0D, "-- More -- (SPACE=next page  Q=quit) ", 0
msg_clear: db 0x0D, "                                      ", 0x0D, 0
msg_err:   db "more: cannot read file", 0x0A, 0
