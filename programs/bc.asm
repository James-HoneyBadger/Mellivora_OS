; bc.asm - Basic calculator REPL
; Supports: integers, +, -, *, /, %, ( ), and `quit`/`exit`
; Uses recursive descent parsing

%include "syscalls.inc"

start:
        mov eax, SYS_PRINT
        mov ebx, msg_banner
        int 0x80

.loop:
        mov eax, SYS_PRINT
        mov ebx, msg_prompt
        int 0x80
        mov eax, SYS_STDIN_READ
        mov ebx, line_buf
        mov ecx, LINE_BUF_SIZE - 1
        int 0x80
        cmp eax, -1
        je .quit
        test eax, eax
        jz .loop

        ; Null-terminate
        mov byte [line_buf + eax], 0

        ; Check for quit/exit
        mov esi, line_buf
        call skip_ws
        cmp dword [esi], 'quit'
        je .quit
        cmp dword [esi], 'exit'
        je .quit

        ; Parse and evaluate
        mov esi, line_buf
        call expr
        ; Check for errors
        cmp dword [parse_err], 0
        jne .parse_error

        ; Print result
        mov eax, SYS_PRINT
        mov ebx, msg_eq
        int 0x80
        mov eax, [result]
        ; Handle negative
        test eax, eax
        jns .pos_result
        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        pop eax
        neg eax
.pos_result:
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov dword [parse_err], 0
        jmp .loop

.parse_error:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        mov dword [parse_err], 0
        jmp .loop

.quit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; expr → term (('+' | '-') term)*
expr:
        call term
        cmp dword [parse_err], 0
        jne .e_ret
        mov [result], eax
.e_loop:
        call skip_ws
        cmp byte [esi], '+'
        je .e_add
        cmp byte [esi], '-'
        je .e_sub
        ret
.e_add:
        inc esi
        push dword [result]
        call term
        cmp dword [parse_err], 0
        jne .e_ret
        pop ecx
        add eax, ecx
        mov [result], eax
        jmp .e_loop
.e_sub:
        inc esi
        push dword [result]
        call term
        cmp dword [parse_err], 0
        jne .e_ret
        pop ecx
        sub ecx, eax
        mov eax, ecx
        mov [result], eax
        jmp .e_loop
.e_ret:
        ret

; term → factor (('*' | '/' | '%') factor)*
term:
        call factor
        cmp dword [parse_err], 0
        jne .t_ret
.t_loop:
        call skip_ws
        cmp byte [esi], '*'
        je .t_mul
        cmp byte [esi], '/'
        je .t_div
        cmp byte [esi], '%'
        je .t_mod
        ret
.t_mul:
        inc esi
        push eax
        call factor
        cmp dword [parse_err], 0
        jne .t_ret
        pop ecx
        imul eax, ecx
        jmp .t_loop
.t_div:
        inc esi
        push eax
        call factor
        cmp dword [parse_err], 0
        jne .t_ret
        pop ecx
        ; ecx / eax
        test eax, eax
        jz .t_divzero
        push eax
        mov eax, ecx
        pop ecx
        cdq
        idiv ecx
        jmp .t_loop
.t_mod:
        inc esi
        push eax
        call factor
        cmp dword [parse_err], 0
        jne .t_ret
        pop ecx
        test eax, eax
        jz .t_divzero
        push eax
        mov eax, ecx
        pop ecx
        cdq
        idiv ecx
        mov eax, edx
        jmp .t_loop
.t_divzero:
        pop eax     ; clean stack
        mov dword [parse_err], 1
        mov eax, SYS_PRINT
        mov ebx, msg_div0
        int 0x80
        xor eax, eax
        ret
.t_ret:
        ret

; factor → '-' factor | '(' expr ')' | number
factor:
        call skip_ws
        cmp byte [esi], '-'
        je .f_neg
        cmp byte [esi], '('
        je .f_paren
        ; Try to parse number
        movzx eax, byte [esi]
        cmp eax, '0'
        jb .f_err
        cmp eax, '9'
        ja .f_err
        call parse_number
        ret
.f_neg:
        inc esi
        call factor
        cmp dword [parse_err], 0
        jne .f_ret
        neg eax
        ret
.f_paren:
        inc esi
        call expr
        cmp dword [parse_err], 0
        jne .f_ret
        call skip_ws
        cmp byte [esi], ')'
        jne .f_err
        inc esi
        ret
.f_err:
        mov dword [parse_err], 1
        xor eax, eax
.f_ret:
        ret

; parse_number → EAX = integer, advances ESI
parse_number:
        xor eax, eax
.pn_loop:
        movzx ecx, byte [esi]
        cmp ecx, '0'
        jb .pn_done
        cmp ecx, '9'
        ja .pn_done
        sub ecx, '0'
        imul eax, 10
        add eax, ecx
        inc esi
        jmp .pn_loop
.pn_done:
        ret

skip_ws:
        cmp byte [esi], ' '
        je .sw
        cmp byte [esi], 9
        je .sw
        ret
.sw:    inc esi
        jmp skip_ws


LINE_BUF_SIZE   equ 256

msg_banner:     db "bc - basic calculator (type `quit` to exit)", 10, 0
msg_prompt:     db "> ", 0
msg_eq:         db "= ", 0
msg_err:        db "Error: invalid expression", 10, 0
msg_div0:       db "Error: division by zero", 10, 0

result:         dd 0
parse_err:      dd 0
line_buf:       times LINE_BUF_SIZE db 0
