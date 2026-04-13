; cmp.asm - Compare two files byte by byte (like Unix cmp)
; Usage: cmp <file1> <file2>

%include "syscalls.inc"

BUF_SIZE        equ 32768

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        cmp eax, 0
        je .usage

        ; Split args into file1 and file2 at first space
        mov esi, argbuf
        mov edi, file1
        xor ecx, ecx
.parse1:
        mov al, [esi + ecx]
        cmp al, 0
        je .usage               ; no second file
        cmp al, ' '
        je .got_space
        mov [edi], al
        inc edi
        inc ecx
        jmp .parse1
.got_space:
        mov byte [edi], 0
        inc ecx                 ; skip space
        ; Skip extra spaces
.skip_sp:
        cmp byte [esi + ecx], ' '
        jne .parse2
        inc ecx
        jmp .skip_sp
.parse2:
        mov edi, file2
.copy2:
        mov al, [esi + ecx]
        cmp al, 0
        je .got_both
        cmp al, ' '
        je .got_both
        mov [edi], al
        inc edi
        inc ecx
        jmp .copy2
.got_both:
        mov byte [edi], 0

        ; Check file2 not empty
        cmp byte [file2], 0
        je .usage

        ; Read file 1
        mov eax, SYS_FREAD
        mov ebx, file1
        mov ecx, buf1
        int 0x80
        cmp eax, -1
        je .err1
        mov [len1], eax

        ; Read file 2
        mov eax, SYS_FREAD
        mov ebx, file2
        mov ecx, buf2
        int 0x80
        cmp eax, -1
        je .err2
        mov [len2], eax

        ; Compare
        mov ecx, [len1]
        cmp ecx, [len2]
        jle .use_len1
        mov ecx, [len2]
.use_len1:
        ; Compare ECX bytes
        xor esi, esi
        mov dword [line_num], 1
        mov dword [byte_num], 1
.cmp_loop:
        cmp esi, ecx
        jge .check_length

        mov al, [buf1 + esi]
        cmp al, [buf2 + esi]
        jne .differ

        ; Track line number
        cmp al, 10
        jne .cmp_next
        inc dword [line_num]
        mov dword [byte_num], 0
.cmp_next:
        inc dword [byte_num]
        inc esi
        jmp .cmp_loop

.differ:
        ; Files differ at this position
        mov eax, SYS_PRINT
        mov ebx, file1
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, file2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_differ
        int 0x80
        lea eax, [esi + 1]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_line
        int 0x80
        mov eax, [line_num]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .exit_diff

.check_length:
        mov eax, [len1]
        cmp eax, [len2]
        je .identical
        ; Different lengths
        mov eax, SYS_PRINT
        mov ebx, msg_eof
        int 0x80
        mov eax, [len1]
        cmp eax, [len2]
        jl .eof1
        mov eax, SYS_PRINT
        mov ebx, file2
        int 0x80
        jmp .eof_end
.eof1:
        mov eax, SYS_PRINT
        mov ebx, file1
        int 0x80
.eof_end:
        mov eax, SYS_PRINT
        mov ebx, msg_eof2
        int 0x80
        lea eax, [ecx + 1]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .exit_diff

.identical:
        ; Files are identical (no output, like Unix cmp)
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.exit_diff:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.err1:
        mov eax, SYS_PRINT
        mov ebx, msg_err1
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.err2:
        mov eax, SYS_PRINT
        mov ebx, msg_err2
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;=======================================
msg_usage:      db "Usage: cmp <file1> <file2>", 10, 0
msg_err1:       db "cmp: cannot read first file", 10, 0
msg_err2:       db "cmp: cannot read second file", 10, 0
msg_differ:     db " differ: byte ", 0
msg_line:       db ", line ", 0
msg_eof:        db "cmp: EOF on ", 0
msg_eof2:       db " after byte ", 0

argbuf:         times 256 db 0
file1:          times 128 db 0
file2:          times 128 db 0
len1:           dd 0
len2:           dd 0
line_num:       dd 1
byte_num:       dd 1
buf1:           times BUF_SIZE db 0
buf2:           times BUF_SIZE db 0
