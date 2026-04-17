; =============================================================================
; rmdir.asm - Remove empty directories
;
; Usage: rmdir <directory> [directory2 ...]
;
; Removes one or more empty directories using SYS_RMDIR.
; Fails with an error message for each directory that cannot be removed
; (not found, not a directory, or not empty).
; =============================================================================

%include "syscalls.inc"

start:
        ; Get command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, args_buf
        mov byte [exit_code], 0

.next_arg:
        ; Skip leading spaces
        cmp byte [esi], ' '
        jne .check_end
        inc esi
        jmp .next_arg

.check_end:
        cmp byte [esi], 0
        je .done

        ; ESI points to start of directory name
        mov [cur_name], esi

        ; Find end of this argument
.find_end:
        cmp byte [esi], 0
        je .got_arg
        cmp byte [esi], ' '
        je .terminate
        inc esi
        jmp .find_end

.terminate:
        mov byte [esi], 0
        inc esi

.got_arg:
        push rsi

        ; Try to remove the directory
        mov eax, SYS_RMDIR
        mov ebx, [cur_name]
        int 0x80
        test eax, eax
        jnz .fail

        pop rsi
        jmp .next_arg

.fail:
        ; Print error
        mov eax, SYS_PRINT
        mov ebx, err_prefix
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, [cur_name]
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_suffix
        int 0x80
        mov byte [exit_code], 1
        pop rsi
        jmp .next_arg

.done:
        movzx eax, byte [exit_code]
        mov ebx, eax
        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_msg
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

section .data
usage_msg:  db "Usage: rmdir <directory> [directory2 ...]", 0x0A, 0
err_prefix: db "rmdir: failed to remove '", 0
err_suffix: db "': Not found, not a directory, or not empty", 0x0A, 0

section .bss
args_buf:   resb 512
cur_name:   resd 1
exit_code:  resb 1
