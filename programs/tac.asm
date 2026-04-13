; tac.asm - Print file in reverse line order (like Unix tac)
; Usage: tac <filename>

%include "syscalls.inc"

BUF_SIZE        equ 32768

start:
        ; Get filename argument
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
        je .err_read
        cmp eax, 0
        je .done
        mov [file_len], eax

        ; Scan backwards printing lines in reverse order
        ; Start from end of file
        mov esi, eax
        dec esi                 ; last char index

        ; Skip trailing newline
        cmp byte [filebuf + esi], 10
        jne .find_lines
        dec esi

.find_lines:
        ; esi = end of current line (exclusive of newline)
        mov [line_end], esi

.scan_back:
        cmp esi, 0
        jl .print_last
        cmp byte [filebuf + esi], 10
        je .found_newline
        dec esi
        jmp .scan_back

.found_newline:
        ; Print from esi+1 to line_end
        lea ebx, [filebuf + esi + 1]
        mov ecx, [line_end]
        sub ecx, esi
        call print_n
        ; Print newline
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        dec esi                 ; skip the newline
        mov [line_end], esi
        jmp .scan_back

.print_last:
        ; Print from 0 to line_end
        mov ebx, filebuf
        mov ecx, [line_end]
        inc ecx
        cmp ecx, 0
        jle .done
        call print_n
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .done

.err_read:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        jmp .done

;---------------------------------------
print_n:
        ; EBX = pointer, ECX = count
        pushad
        mov esi, ebx
        mov edi, ecx
        xor ecx, ecx
.pn_loop:
        cmp ecx, edi
        jge .pn_done
        movzx ebx, byte [esi + ecx]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc ecx
        jmp .pn_loop
.pn_done:
        popad
        ret

;=======================================
msg_usage:      db "Usage: tac <filename>", 10, 0
msg_err:        db "Error: cannot read file", 10, 0

argbuf:         times 256 db 0
file_len:       dd 0
line_end:       dd 0
filebuf:        times BUF_SIZE db 0
