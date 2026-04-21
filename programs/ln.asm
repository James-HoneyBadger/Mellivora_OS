; ln.asm - Create a symbolic link
; Usage: ln [-s] <target> <linkname>
; Note: -s flag is accepted but all HBFS links are symbolic

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces

        ; Check for -s flag
        cmp byte [esi], '-'
        jne .no_flag
        cmp byte [esi+1], 's'
        jne .no_flag
        add esi, 2
        call skip_spaces
.no_flag:

        ; Copy target
        mov edi, target_name
        call copy_token
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Copy linkname
        mov edi, link_name
        call copy_token

        cmp byte [target_name], 0
        je .usage
        cmp byte [link_name], 0
        je .usage

        ; Create symlink
        mov eax, SYS_SYMLINK
        mov ebx, link_name
        mov ecx, target_name
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

msg_usage:      db "Usage: ln [-s] <target> <linkname>", 10, 0
msg_err:        db "ln: failed to create link", 10, 0

target_name:    times 256 db 0
link_name:      times 256 db 0
arg_buf:        times 512 db 0
