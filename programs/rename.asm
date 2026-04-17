; rename.asm - Rename a file using SYS_RENAME [HBU]
; Usage: rename <oldname> <newname>
;
%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle show_usage

        ; Parse two filenames
        mov esi, args_buf
        ; Skip leading spaces
.skip1:
        cmp byte [esi], ' '
        jne .got1
        inc esi
        jmp .skip1
.got1:
        cmp byte [esi], 0
        je show_usage
        mov [old_name], esi
        ; Find end of first word
.scan1:
        cmp byte [esi], 0
        je show_usage           ; need second arg
        cmp byte [esi], ' '
        je .term1
        inc esi
        jmp .scan1
.term1:
        mov byte [esi], 0
        inc esi
        ; Skip spaces
.skip2:
        cmp byte [esi], ' '
        jne .got2
        inc esi
        jmp .skip2
.got2:
        cmp byte [esi], 0
        je show_usage
        mov [new_name], esi
        ; Null-terminate at next space or end
.scan2:
        cmp byte [esi], 0
        je .do_rename
        cmp byte [esi], ' '
        je .term2
        inc esi
        jmp .scan2
.term2:
        mov byte [esi], 0

.do_rename:
        mov eax, SYS_RENAME
        mov ebx, [old_name]
        mov ecx, [new_name]
        int 0x80
        cmp eax, 0
        jne .err
        ; Success
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.err:
        mov eax, SYS_PRINT
        mov ebx, err_msg
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, [old_name]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

show_usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

section .data
usage_str:      db "Usage: rename <oldname> <newname>", 0x0A, 0
err_msg:        db "rename: cannot rename ", 0

section .bss
args_buf:       resb 512
old_name:       resd 1
new_name:       resd 1
