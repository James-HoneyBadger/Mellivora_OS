; nl.asm - Number lines (like Unix nl)
; Usage: nl <filename>

%include "syscalls.inc"

BUF_SIZE        equ 32768

start:
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        cmp eax, 0
        je .usage

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, argbuf
        mov ecx, filebuf
        int 0x80
        cmp eax, -1
        je .err
        cmp eax, 0
        je .done
        mov [file_len], eax

        ; Process file line by line
        mov dword [line_num], 1
        xor esi, esi            ; position in filebuf

.line_loop:
        cmp esi, [file_len]
        jge .done

        ; Print line number (right-justified, 6 chars)
        mov eax, [line_num]
        call print_num_padded
        mov eax, SYS_PUTCHAR
        mov ebx, 9             ; tab
        int 0x80

        ; Print line content until newline or EOF
.char_loop:
        cmp esi, [file_len]
        jge .end_line
        movzx ebx, byte [filebuf + esi]
        inc esi
        cmp bl, 10
        je .end_line
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .char_loop

.end_line:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc dword [line_num]
        jmp .line_loop

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .done

.err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        jmp .done

;---------------------------------------
print_num_padded:
        ; Print EAX right-justified in 6-char field
        PUSHALL
        ; Count digits
        mov ecx, 0
        mov ebx, eax
        mov eax, ebx
.pnp_count:
        xor edx, edx
        mov ebx, 10
        div ebx
        inc ecx
        cmp eax, 0
        jne .pnp_count

        ; Print leading spaces
        mov edx, 6
        sub edx, ecx
        jle .pnp_num
.pnp_space:
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rdx
        dec edx
        jnz .pnp_space

.pnp_num:
        ; Re-get original number from stack
        mov eax, [rsp + 112]    ; original EAX from PUSHALL
        call print_dec
        POPALL
        ret

;=======================================
msg_usage:      db "Usage: nl <filename>", 10, 0
msg_err:        db "Error: cannot read file", 10, 0

argbuf:         times 256 db 0
file_len:       dd 0
line_num:       dd 1
filebuf:        times BUF_SIZE db 0
