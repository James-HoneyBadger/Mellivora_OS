; seq.asm - Print sequence of numbers (1 to N) [HBU]
; Usage: seq N
%include "syscalls.inc"

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .done

        ; Skip leading spaces
        mov esi, args_buf
.skip_spaces:
        cmp byte [esi], ' '
        jne .check_tab
        inc esi
        jmp .skip_spaces
.check_tab:
        cmp byte [esi], 9
        jne .parse_num
        inc esi
        jmp .skip_spaces

        ; Parse limit number
.parse_num:
        xor ecx, ecx           ; limit
.parse_loop:
        movzx eax, byte [esi]
        cmp al, '0'
        jb .have_limit
        cmp al, '9'
        ja .have_limit
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_loop

.have_limit:
        mov [limit], ecx
        mov dword [current], 1

.print_loop:
        mov eax, [current]
        cmp eax, [limit]
        jg .done

        ; Print current number
        call print_number

        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        inc dword [current]
        jmp .print_loop

.done:
        mov eax, SYS_EXIT
        int 0x80

; Print decimal number in EAX (0-9999)
print_number:
        push esi
        mov ecx, 0              ; digit count
        mov esi, esp
        sub esp, 16             ; digit buffer on stack
        mov ebx, 10

        cmp eax, 0
        jne .div_loop
        ; Handle 0
        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        pop eax
        jmp .pn_done

.div_loop:
        xor edx, edx
        div ebx
        add dl, '0'
        push edx
        inc ecx
        cmp eax, 0
        jne .div_loop

.print_digits:
        cmp ecx, 0
        je .pn_done
        pop ebx
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jmp .print_digits

.pn_done:
        mov esp, esi
        pop esi
        ret

section .bss
args_buf:       resb 256
limit:          resd 1
current:        resd 1
