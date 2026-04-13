; factor.asm - Print prime factorization of a number
; Usage: factor <number>

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        cmp eax, 0
        je .usage

        ; Parse number from args
        mov esi, argbuf
        call parse_int
        cmp eax, 0
        je .usage
        cmp eax, 1
        je .one
        mov [number], eax

        ; Print "N: "
        mov eax, [number]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_colon
        int 0x80

        mov eax, [number]
        mov ecx, 2             ; first trial divisor

.factor_loop:
        cmp eax, 1
        jle .done_nl

        ; Check if ecx*ecx > eax
        push eax
        mov eax, ecx
        imul eax, ecx
        cmp eax, [esp]
        pop eax
        jg .last_factor

        ; Try dividing
        push ecx
        xor edx, edx
        div ecx
        pop ecx
        cmp edx, 0
        jne .not_divisible

        ; ecx divides eax. Print factor.
        push eax
        mov eax, ecx
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop eax
        jmp .factor_loop       ; try same divisor again

.not_divisible:
        ; Restore eax = eax*ecx + edx (undo the division)
        push ecx
        imul eax, ecx
        add eax, edx
        pop ecx
        ; Next divisor
        cmp ecx, 2
        jne .odd_inc
        inc ecx
        jmp .factor_loop
.odd_inc:
        add ecx, 2
        jmp .factor_loop

.last_factor:
        ; eax is the remaining prime factor
        call print_dec

.done_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.one:
        mov eax, SYS_PRINT
        mov ebx, msg_one
        int 0x80
        jmp .done

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .done

;---------------------------------------
parse_int:
        ; ESI = string, returns EAX = value
        pushad
        xor eax, eax
        xor ecx, ecx
.pi_loop:
        movzx edx, byte [esi + ecx]
        cmp dl, 0
        je .pi_done
        cmp dl, ' '
        je .pi_done
        cmp dl, '0'
        jl .pi_done
        cmp dl, '9'
        jg .pi_done
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc ecx
        jmp .pi_loop
.pi_done:
        mov [esp + 28], eax
        popad
        ret

;=======================================
msg_usage:      db "Usage: factor <number>", 10, 0
msg_colon:      db ": ", 0
msg_one:        db "1: 1", 10, 0

argbuf:         times 256 db 0
number:         dd 0
