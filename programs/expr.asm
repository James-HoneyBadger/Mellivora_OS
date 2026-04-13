; expr.asm - Evaluate arithmetic expressions
; Usage: expr 3 + 5
;        expr 10 \* 3
;        expr 100 / 7
;        expr 17 % 5
; Supports: + - * / % (integer arithmetic)

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov esi, arg_buf
        call skip_spaces

        cmp byte [esi], 0
        je usage

        ; Parse first operand
        call parse_number
        mov [operand_a], eax

        call skip_spaces
        cmp byte [esi], 0
        je .just_number

        ; Get operator
        movzx eax, byte [esi]
        mov [operator], al
        inc esi
        call skip_spaces

        ; Parse second operand
        call parse_number
        mov [operand_b], eax

        ; Evaluate
        mov eax, [operand_a]
        mov ecx, [operand_b]
        movzx edx, byte [operator]

        cmp dl, '+'
        je .add
        cmp dl, '-'
        je .sub
        cmp dl, '*'
        je .mul
        cmp dl, 'x'
        je .mul
        cmp dl, '/'
        je .div
        cmp dl, '%'
        je .mod
        jmp usage

.add:
        add eax, ecx
        jmp .print_result
.sub:
        sub eax, ecx
        jmp .print_result
.mul:
        imul eax, ecx
        jmp .print_result
.div:
        cmp ecx, 0
        je div_zero
        cdq
        idiv ecx
        jmp .print_result
.mod:
        cmp ecx, 0
        je div_zero
        cdq
        idiv ecx
        mov eax, edx
        jmp .print_result

.just_number:
        mov eax, [operand_a]

.print_result:
        ; Check if negative
        test eax, eax
        jns .positive
        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        pop eax
        neg eax
.positive:
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

div_zero:
        mov eax, SYS_PRINT
        mov ebx, divz_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;--------------------------------------
; parse_number: Parse signed integer from [ESI], return in EAX
;--------------------------------------
parse_number:
        xor eax, eax
        xor ecx, ecx           ; sign flag
        cmp byte [esi], '-'
        jne .pn_digit
        inc ecx
        inc esi
.pn_digit:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .pn_done
        cmp dl, '9'
        ja .pn_done
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pn_digit
.pn_done:
        cmp ecx, 0
        je .pn_pos
        neg eax
.pn_pos:
        ret

;--------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

;=======================================
; Data
;=======================================
usage_str:  db "Usage: expr NUM OP NUM", 10
            db "  Operators: + - * / %", 10
            db "  Example: expr 3 + 5", 10, 0
divz_str:   db "Error: division by zero", 10, 0

operand_a:  dd 0
operand_b:  dd 0
operator:   db 0
arg_buf:    times 256 db 0
