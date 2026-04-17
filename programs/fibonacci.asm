; fibonacci.asm - Fibonacci sequence calculator for Mellivora OS
%include "syscalls.inc"

MAX_FIB         equ 30          ; Calculate first 30 Fibonacci numbers

start:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_header
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; Cyan
        int 0x80

        ; Calculate and display Fibonacci numbers
        xor ecx, ecx           ; Index counter
        mov esi, 0              ; fib(n-2)
        mov edi, 1              ; fib(n-1)

.fib_loop:
        ; Print index
        push rcx
        push rsi
        push rdi

        ; Print "F(nn) = "
        mov eax, SYS_PRINT
        mov ebx, msg_fprefix
        int 0x80

        mov eax, ecx
        call print_dec

        mov eax, SYS_PRINT
        mov ebx, msg_eq
        int 0x80

        ; Print current Fibonacci number
        cmp ecx, 0
        jne .not_zero
        xor eax, eax
        jmp .print_val
.not_zero:
        cmp ecx, 1
        jne .not_one
        mov eax, 1
        jmp .print_val
.not_one:
        mov eax, edi            ; Current fib value
.print_val:
        call print_dec

        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        pop rdi
        pop rsi
        pop rcx

        ; Calculate next: new = esi + edi, shift
        cmp ecx, 1
        jle .skip_calc
        mov eax, esi
        add eax, edi
        mov esi, edi
        mov edi, eax
.skip_calc:
        cmp ecx, 0
        jne .not_first
        mov esi, 0
        mov edi, 1
.not_first:
        cmp ecx, 1
        jne .past_second
        mov esi, 1
        mov edi, 1
.past_second:

        inc ecx
        cmp ecx, MAX_FIB
        jl .fib_loop

        ; Final message
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; Green
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_done
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        mov eax, SYS_EXIT
        int 0x80

msg_header:     db "=== Fibonacci Sequence (first 30 numbers) ===", 0x0A, 0
msg_fprefix:    db "  F(", 0
msg_eq:         db ") = ", 0
msg_done:       db 0x0A, "Calculation complete!", 0x0A, 0
