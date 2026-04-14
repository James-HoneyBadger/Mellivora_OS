; fold.asm - Wrap lines to a given width
; Usage: fold [-w WIDTH] [FILE]
; Default width: 80. Reads stdin if no file given.

%include "syscalls.inc"

DEFAULT_WIDTH equ 80

start:
        mov dword [width], DEFAULT_WIDTH

        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        jle .try_stdin

        ; Parse arguments
        mov esi, arg_buf

.parse_args:
        cmp byte [esi], 0
        je .try_stdin

        cmp byte [esi], '-'
        jne .got_filename

        ; Check for -w
        cmp byte [esi + 1], 'w'
        jne .got_filename
        add esi, 2

        ; Skip spaces
.skip_w_sp:
        cmp byte [esi], ' '
        jne .parse_width
        inc esi
        jmp .skip_w_sp

.parse_width:
        xor eax, eax
        xor ecx, ecx
.pw_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .pw_done
        cmp dl, '9'
        ja .pw_done
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pw_loop
.pw_done:
        cmp eax, 1
        jl .try_stdin           ; Ignore invalid width
        mov [width], eax

        ; Skip spaces to filename
.skip_fn_sp:
        cmp byte [esi], ' '
        jne .check_fn
        inc esi
        jmp .skip_fn_sp

.check_fn:
        cmp byte [esi], 0
        je .try_stdin

.got_filename:
        ; Read file
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .file_err
        mov [file_size], eax
        jmp .do_fold

.try_stdin:
        mov eax, SYS_STDIN_READ
        mov ebx, file_buf
        int 0x80
        cmp eax, 0
        jl .usage
        mov [file_size], eax

.do_fold:
        ; Fold the text in file_buf
        mov esi, file_buf
        mov ecx, [file_size]
        xor edx, edx           ; current column

.fold_loop:
        cmp ecx, 0
        jle .done

        movzx eax, byte [esi]

        ; Newline resets column
        cmp al, 0x0A
        je .emit_newline

        ; Tab counts as spaces to next tab stop
        cmp al, 0x09
        je .emit_tab

        ; Check if we need to wrap
        cmp edx, [width]
        jl .emit_char

        ; Wrap: insert newline
        push eax
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop ecx
        pop eax
        xor edx, edx

.emit_char:
        push ecx
        push edx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop edx
        pop ecx
        inc edx
        inc esi
        dec ecx
        jmp .fold_loop

.emit_newline:
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop ecx
        xor edx, edx
        inc esi
        dec ecx
        jmp .fold_loop

.emit_tab:
        push ecx
        push edx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x09
        int 0x80
        pop edx
        pop ecx
        ; Advance to next tab stop (8)
        add edx, 8
        and edx, ~7
        inc esi
        dec ecx
        jmp .fold_loop

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_not_found
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; Data
msg_usage:      db "Usage: fold [-w WIDTH] [FILE]", 0x0A, 0
msg_not_found:  db "fold: file not found", 0x0A, 0
width:          dd DEFAULT_WIDTH
file_size:      dd 0
arg_buf:        times 256 db 0
file_buf:       times 65536 db 0
