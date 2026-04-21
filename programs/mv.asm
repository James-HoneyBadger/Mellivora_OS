; mv.asm - Move (rename) a file
; Usage: mv <source> <destination>

%include "syscalls.inc"

MAX_SIZE        equ 131072      ; 128 KB max file size

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces

        mov edi, src_name
        call copy_token
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        mov edi, dst_name
        call copy_token

        cmp byte [src_name], 0
        je .usage
        cmp byte [dst_name], 0
        je .usage

        ; Read source file
        mov eax, SYS_FREAD
        mov ebx, src_name
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jl .src_err
        mov [file_size], eax

        ; Write destination file
        mov eax, SYS_FWRITE
        mov ebx, dst_name
        mov ecx, file_buf
        mov edx, [file_size]
        mov esi, FTYPE_TEXT
        int 0x80
        cmp eax, -1
        je .dst_err

        ; Delete source file
        mov eax, SYS_DELETE
        mov ebx, src_name
        int 0x80
        cmp eax, -1
        je .del_err

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .exit

.src_err:
        mov eax, SYS_PRINT
        mov ebx, msg_src_err
        int 0x80
        jmp .exit

.dst_err:
        mov eax, SYS_PRINT
        mov ebx, msg_dst_err
        int 0x80
        jmp .exit

.del_err:
        mov eax, SYS_PRINT
        mov ebx, msg_del_err
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

msg_usage:      db "Usage: mv <source> <destination>", 10, 0
msg_src_err:    db "mv: cannot read source file", 10, 0
msg_dst_err:    db "mv: cannot write destination", 10, 0
msg_del_err:    db "mv: warning: could not delete source", 10, 0

src_name:       times 256 db 0
dst_name:       times 256 db 0
file_size:      dd 0
arg_buf:        times 512 db 0
file_buf:       times MAX_SIZE db 0
