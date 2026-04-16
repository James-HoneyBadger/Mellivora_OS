; calc.asm - Interactive integer calculator for Mellivora OS
; Supports: +, -, *, /, % (modulo)
; Usage:  Type expressions like "42 + 7", "100 / 3", "255 % 16"
;         Type "quit" or press ESC to exit
%include "syscalls.inc"

start:
        ; Print welcome
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F           ; white on blue
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_help
        int 0x80

.prompt:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; green
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_prompt
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Read a line of input
        mov edi, input_buf
        xor ecx, ecx
.read:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 27              ; ESC
        je .exit
        cmp al, 0x0D            ; Enter
        je .process
        cmp al, 0x0A
        je .process
        cmp al, 0x08            ; Backspace
        je .bs
        cmp ecx, 78
        jge .read
        stosb
        inc ecx
        ; Echo
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .read
.bs:
        test ecx, ecx
        jz .read
        dec edi
        dec ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        jmp .read

.process:
        mov byte [edi], 0       ; null-terminate
        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        test ecx, ecx
        jz .prompt

        ; Check for "quit"
        cmp dword [input_buf], 'quit'
        je .maybe_quit

        ; Parse: number1 op number2
        mov esi, input_buf
        call skip_ws

        ; Check for negative first number
        xor ebp, ebp            ; sign flag for first number
        cmp byte [esi], '-'
        jne .parse_num1
        mov ebp, 1
        inc esi

.parse_num1:
        call parse_number
        jc .error
        test ebp, ebp
        jz .num1_pos
        neg eax
.num1_pos:
        mov [num1], eax

        ; Skip whitespace
        call skip_ws

        ; Read operator
        lodsb
        cmp al, '+'
        je .op_ok
        cmp al, '-'
        je .op_ok
        cmp al, '*'
        je .op_ok
        cmp al, '/'
        je .op_ok
        cmp al, '%'
        je .op_ok
        jmp .error
.op_ok:
        mov [operator], al
        call skip_ws

        ; Check for negative second number
        xor ebp, ebp
        cmp byte [esi], '-'
        jne .parse_num2
        mov ebp, 1
        inc esi

.parse_num2:
        call parse_number
        jc .error
        test ebp, ebp
        jz .num2_pos
        neg eax
.num2_pos:
        mov [num2], eax

        ; Perform operation
        mov eax, [num1]
        mov ebx, [num2]
        mov cl, [operator]

        cmp cl, '+'
        je .do_add
        cmp cl, '-'
        je .do_sub
        cmp cl, '*'
        je .do_mul
        cmp cl, '/'
        je .do_div
        cmp cl, '%'
        je .do_mod
        jmp .error

.do_add:
        add eax, ebx
        jmp .show_result
.do_sub:
        sub eax, ebx
        jmp .show_result
.do_mul:
        imul eax, ebx
        jmp .show_result
.do_div:
        test ebx, ebx
        jz .div_zero
        cdq                     ; sign-extend EAX into EDX:EAX
        idiv ebx
        jmp .show_result
.do_mod:
        test ebx, ebx
        jz .div_zero
        cdq
        idiv ebx
        mov eax, edx            ; remainder
        jmp .show_result

.show_result:
        mov [result], eax
        ; Print "= "
        push rax
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; cyan
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_equals
        int 0x80
        pop rax

        ; Check if negative
        test eax, eax
        jns .positive
        ; Print minus sign
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        pop rax
        neg eax
.positive:
        call print_dec

        ; Also print hex representation
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; dark gray
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_hex_prefix
        int 0x80

        mov eax, [result]
        call print_hex

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        jmp .prompt

.div_zero:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; red
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_divzero
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        jmp .prompt

.error:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; red
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_error
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        jmp .prompt

.maybe_quit:
        cmp byte [input_buf + 4], 0
        jne .error              ; Not exactly "quit"
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=== Parse unsigned decimal number from ESI ===
; Returns: EAX = number, CF set on error
; Advances ESI past the digits
parse_number:
        xor eax, eax
        xor ecx, ecx           ; digit count
        mov ebx, 10
.pn_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jl .pn_done
        cmp dl, '9'
        jg .pn_done
        inc ecx
        imul eax, ebx           ; eax *= 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pn_loop
.pn_done:
        test ecx, ecx
        jz .pn_err
        clc
        ret
.pn_err:
        stc
        ret

;=== Skip whitespace ===
skip_ws:
        cmp byte [esi], ' '
        jne .sw_done
        inc esi
        jmp skip_ws
.sw_done:
        ret

;=== Print hex value in EAX ===
print_hex:
        PUSHALL
        mov ecx, 8
        mov ebx, eax
.ph_loop:
        rol ebx, 4
        mov eax, ebx
        and eax, 0x0F
        cmp eax, 10
        jl .ph_digit
        add eax, 'A' - 10
        jmp .ph_put
.ph_digit:
        add eax, '0'
.ph_put:
        push rbx
        push rcx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        pop rbx
        dec ecx
        jnz .ph_loop
        POPALL
        ret

;=== Data ===
msg_title:      db "  Mellivora Calculator  ", 0x0A, 0
msg_help:       db "  Enter: number op number  (op: + - * / %)", 0x0A
                db "  Type 'quit' or press ESC to exit", 0x0A, 0x0A, 0
msg_prompt:     db "calc> ", 0
msg_equals:     db "= ", 0
msg_hex_prefix: db "  (0x", 0
msg_divzero:    db "Error: division by zero", 0x0A, 0
msg_error:      db "Error: use format: num1 op num2  (e.g. 42 + 7)", 0x0A, 0

;=== BSS ===
input_buf:      resb 80
num1:           resd 1
num2:           resd 1
operator:       resb 1
result:         resd 1
