; cowsay.asm - Cowsay: display message in a speech bubble from a cow
; Usage: cowsay <message>
;        cowsay               (reads from keyboard)

%include "syscalls.inc"

MAX_MSG equ 200

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov esi, arg_buf
        call skip_spaces

        cmp byte [esi], 0
        je .interactive

        ; Copy args as message
        mov edi, msg_buf
        xor ecx, ecx
.copy_arg:
        lodsb
        cmp al, 0
        je .have_msg
        cmp ecx, MAX_MSG - 1
        jge .have_msg
        stosb
        inc ecx
        jmp .copy_arg

.interactive:
        ; Read from keyboard
        mov eax, SYS_PRINT
        mov ebx, prompt_str
        int 0x80
        mov edi, msg_buf
        xor ecx, ecx
.read:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 0x0D
        je .have_msg
        cmp al, 0x0A
        je .have_msg
        cmp al, 0x08
        je .bs
        cmp ecx, MAX_MSG - 1
        jge .read
        stosb
        inc ecx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .read
.bs:
        cmp ecx, 0
        je .read
        dec edi
        dec ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        jmp .read

.have_msg:
        mov byte [edi], 0
        mov [msg_len], ecx

        ; Print newline
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Print top border: " _____...___"
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov ecx, [msg_len]
        add ecx, 2             ; padding
.top_border:
        mov eax, SYS_PUTCHAR
        mov ebx, '_'
        int 0x80
        dec ecx
        jnz .top_border
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Print message line: "< message >" or "| message |"
        mov eax, SYS_PUTCHAR
        mov ebx, '<'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_buf
        int 0x80

        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '>'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Print bottom border: " -----...---"
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov ecx, [msg_len]
        add ecx, 2
.bot_border:
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        dec ecx
        jnz .bot_border
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Print the cow
        mov eax, SYS_PRINT
        mov ebx, cow_art
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;--------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

;=======================================
; Data
;=======================================
prompt_str: db "Enter message: ", 0

cow_art:
        db "        \   ^__^", 10
        db "         \  (oo)\_______", 10
        db "            (__)\       )\/\", 10
        db "                ||----w |", 10
        db "                ||     ||", 10, 0

msg_buf:    times MAX_MSG + 1 db 0
msg_len:    dd 0
arg_buf:    times 256 db 0
