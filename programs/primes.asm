; primes.asm - Prime number sieve for Mellivora OS
%include "syscalls.inc"

MAX_NUM         equ 1000       ; Find primes up to 1000

start:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0D           ; Light magenta
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_header
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Initialize sieve: set all to 1 (prime)
        mov edi, sieve
        mov ecx, MAX_NUM
        mov al, 1
        rep stosb

        ; Mark 0 and 1 as not prime
        mov byte [sieve], 0
        mov byte [sieve + 1], 0

        ; Sieve of Eratosthenes
        mov ecx, 2              ; Start from 2

.sieve_loop:
        mov eax, ecx
        imul eax, eax           ; i*i
        cmp eax, MAX_NUM
        jge .sieve_done

        cmp byte [sieve + ecx], 0
        je .next_i

        ; Mark multiples of ECX as composite
        mov eax, ecx
        imul eax, eax           ; Start from i*i

.mark_loop:
        cmp eax, MAX_NUM
        jge .next_i
        mov byte [sieve + eax], 0
        add eax, ecx
        jmp .mark_loop

.next_i:
        inc ecx
        jmp .sieve_loop

.sieve_done:
        ; Print all primes
        mov ecx, 2
        xor ebx, ebx           ; Count of primes
        xor edx, edx           ; Column counter

.print_loop:
        cmp ecx, MAX_NUM
        jge .print_done

        cmp byte [sieve + ecx], 0
        je .not_prime

        inc ebx                 ; Count primes
        inc edx                 ; Column

        push rcx
        push rbx
        push rdx

        mov eax, ecx
        call print_dec_padded

        ; Add space
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        pop rdx
        pop rbx
        pop rcx

        ; Newline every 10 primes
        cmp edx, 10
        jl .not_prime
        xor edx, edx
        push rcx
        push rbx
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rdx
        pop rbx
        pop rcx

.not_prime:
        inc ecx
        jmp .print_loop

.print_done:
        ; Print count
        mov eax, SYS_PUTCHAR
        push rbx
        mov ebx, 0x0A
        int 0x80
        int 0x80
        pop rbx

        mov eax, SYS_SETCOLOR
        push rbx
        mov ebx, 0x0A           ; Green
        int 0x80
        pop rbx

        mov eax, SYS_PRINT
        push rbx
        mov ebx, msg_found
        int 0x80
        pop rbx

        mov eax, ebx
        call print_dec

        push rbx
        mov eax, SYS_PRINT
        mov ebx, msg_primes
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        pop rbx

        mov eax, SYS_EXIT
        int 0x80

; Print EAX as right-aligned decimal (padded to 5 chars)
print_dec_padded:
        PUSHALL
        ; First count digits
        push rax
        xor ecx, ecx
        mov ebx, 10
        test eax, eax
        jz .one_digit
.count_digits:
        xor edx, edx
        div ebx
        inc ecx
        test eax, eax
        jnz .count_digits
        jmp .pad
.one_digit:
        mov ecx, 1
.pad:
        ; Pad with spaces (5 - digit_count)
        mov edx, 5
        sub edx, ecx
        jle .no_pad
.pad_loop:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec edx
        jnz .pad_loop
.no_pad:
        pop rax
        call print_dec
        POPALL
        ret

msg_header:     db "=== Prime Sieve (2 to 1000) ===", 0x0A, 0x0A, 0
msg_found:      db "Found ", 0
msg_primes:     db " prime numbers below 1000.", 0x0A, 0

; Sieve array (placed after code)
sieve:          times MAX_NUM db 0
