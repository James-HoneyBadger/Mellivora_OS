; pager.asm - A simple file pager (like 'more') for Mellivora OS
; Usage: pager FILENAME
%include "syscalls.inc"

PAGE_LINES equ 23

start:
        ; Get command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        je .no_args

        ; Skip leading spaces
        mov esi, arg_buf
.skip_sp:
        cmp byte [esi], ' '
        jne .got_name
        inc esi
        jmp .skip_sp

.got_name:
        cmp byte [esi], 0
        je .no_args

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .not_found
        mov [file_size], eax

        ; Null-terminate
        mov edi, file_buf
        add edi, eax
        mov byte [edi], 0

        ; Page through
        mov esi, file_buf
        mov dword [line_count], 0

.page_loop:
        cmp byte [esi], 0
        je .eof

.print_line:
        mov al, [esi]
        cmp al, 0
        je .eof
        cmp al, 0x0A
        je .newline
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .print_line

.newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        inc esi
        inc dword [line_count]
        mov eax, [line_count]
        cmp eax, PAGE_LINES
        jl .page_loop

        ; Show prompt
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70           ; Inverse
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_more
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Wait for key
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        cmp al, 27
        je .quit

        ; Clear prompt
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0D           ; CR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_blank
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0D
        int 0x80

        cmp al, ' '
        je .next_page
        ; Any other key = one more line
        mov dword [line_count], PAGE_LINES - 1
        jmp .page_loop

.next_page:
        mov dword [line_count], 0
        jmp .page_loop

.eof:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
.quit:
        mov eax, SYS_EXIT
        int 0x80

.no_args:
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

;---------------------------------------
; Data
;---------------------------------------
msg_usage:     db "Usage: pager FILENAME", 0x0A, 0
msg_not_found: db "File not found.", 0x0A, 0
msg_more:      db " -- MORE -- (Space=page, Enter=line, q=quit) ", 0
msg_blank:     db "                                                ", 0

;---------------------------------------
; BSS
;---------------------------------------
arg_buf:   times 256 db 0
file_buf:  times 65536 db 0
file_size: dd 0
line_count: dd 0
