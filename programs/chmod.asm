; chmod.asm - Change file permissions
; Usage: chmod <octal-mode> <filename>
; Example: chmod 755 myfile.bin

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces

        ; Parse octal permission value
        xor ecx, ecx
.parse_oct:
        mov al, [esi]
        cmp al, '0'
        jb .parse_done
        cmp al, '7'
        ja .parse_done
        sub al, '0'
        imul ecx, 8
        add ecx, eax
        inc esi
        jmp .parse_oct
.parse_done:
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Copy filename
        mov edi, fname
        call copy_token

        cmp byte [fname], 0
        je .usage

        ; Apply permissions
        mov eax, SYS_CHMOD
        mov ebx, fname
        ; ecx already has the permission value
        int 0x80
        cmp eax, -1
        je .err

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .exit

.err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80

.exit:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

copy_token:
        xor ecx, ecx
.ct:    mov al, [esi]
        cmp al, ' '
        je .done
        cmp al, 9
        je .done
        cmp al, 0
        je .done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .ct
.done:  mov byte [edi + ecx], 0
        ret

msg_usage:      db "Usage: chmod <octal-mode> <filename>", 10
                db "Example: chmod 755 myfile.bin", 10, 0
msg_err:        db "chmod: operation failed", 10, 0

fname:          times 256 db 0
arg_buf:        times 512 db 0
