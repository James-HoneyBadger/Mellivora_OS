; =============================================================================
; truncate.asm - Shrink or set the size of a file
;
; Usage: truncate -s SIZE FILE
;
; Truncates FILE to SIZE bytes. SIZE can be:
;   N     - Set size to exactly N bytes
;   0     - Empty the file (keep 1 block allocated)
;
; Only shrinking is supported (new size must be <= current size).
; =============================================================================

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, args_buf

        ; Skip spaces
.skip1:
        cmp byte [esi], ' '
        jne .parse
        inc esi
        jmp .skip1

.parse:
        ; Expect -s flag
        cmp byte [esi], '-'
        jne .usage
        cmp byte [esi + 1], 's'
        jne .usage
        add esi, 2
        ; Skip spaces
.skip2:
        cmp byte [esi], ' '
        jne .get_size
        inc esi
        jmp .skip2

.get_size:
        cmp byte [esi], 0
        je .usage

        ; Parse size
        call parse_num
        mov [new_size], eax

        ; Skip spaces to filename
.skip3:
        cmp byte [esi], ' '
        jne .get_file
        inc esi
        jmp .skip3

.get_file:
        cmp byte [esi], 0
        je .usage

        ; Copy filename
        mov edi, filename_buf
.copy_fn:
        lodsb
        cmp al, ' '
        je .fn_term
        stosb
        test al, al
        jnz .copy_fn
        jmp .do_truncate
.fn_term:
        mov byte [edi], 0

.do_truncate:
        ; First check current size
        mov eax, SYS_STAT
        mov ebx, filename_buf
        int 0x80
        cmp eax, -1
        je .not_found

        ; Print old size
        push rax
        mov eax, SYS_PRINT
        mov ebx, msg_old
        int 0x80
        pop rax
        push rax
        call print_dec
        pop rax

        ; Check new size <= old size
        mov ebx, [new_size]
        cmp ebx, eax
        ja .too_large

        ; Do the truncation
        mov eax, SYS_TRUNCATE
        mov ebx, filename_buf
        mov ecx, [new_size]
        int 0x80
        test eax, eax
        jnz .error

        ; Print new size
        mov eax, SYS_PRINT
        mov ebx, msg_new
        int 0x80
        mov eax, [new_size]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.not_found:
        mov eax, SYS_PRINT
        mov ebx, err_notfound
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.too_large:
        mov eax, SYS_PRINT
        mov ebx, err_grow
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.error:
        mov eax, SYS_PRINT
        mov ebx, err_fail
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_msg
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; =========================================================================
; Helper: parse_num - Parse unsigned decimal from ESI
; Output: EAX = value, ESI advanced
; =========================================================================
parse_num:
        xor eax, eax
.pn_loop:
        movzx edx, byte [esi]
        sub dl, '0'
        cmp dl, 9
        ja .pn_done
        imul eax, 10
        add eax, edx
        inc esi
        jmp .pn_loop
.pn_done:
        ret

section .data
usage_msg:    db "Usage: truncate -s SIZE FILE", 0x0A, 0
err_notfound: db "truncate: file not found", 0x0A, 0
err_grow:     db "truncate: cannot grow file (new size > current size)", 0x0A, 0
err_fail:     db "truncate: operation failed", 0x0A, 0
msg_old:      db "Old size: ", 0
msg_new:      db " -> New size: ", 0

section .bss
args_buf:     resb 512
filename_buf: resb 256
new_size:     resd 1
