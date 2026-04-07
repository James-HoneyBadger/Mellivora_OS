; guess.asm - Number guessing game for Mellivora OS
%include "syscalls.inc"

start:
        ; Get a pseudo-random number from timer
        mov eax, SYS_GETTIME
        int 0x80

        ; Generate number 1-100 using modular arithmetic
        xor edx, edx
        mov ebx, 100
        div ebx
        inc edx                 ; 1-100
        mov [secret], edx

        mov dword [guesses], 0

        ; Welcome
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_welcome
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

.game_loop:
        inc dword [guesses]

        ; Prompt
        mov eax, SYS_PRINT
        mov ebx, msg_prompt
        int 0x80

        ; Read a number (up to 3 digits)
        call read_number
        mov [guess], eax

        ; Compare
        mov eax, [guess]
        cmp eax, [secret]
        je .correct
        jl .too_low

        ; Too high
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; Red
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_high
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        jmp .game_loop

.too_low:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x09           ; Blue
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_low
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        jmp .game_loop

.correct:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; Green
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_correct
        int 0x80

        ; Print guess count
        mov eax, [guesses]
        call print_dec

        mov eax, SYS_PRINT
        mov ebx, msg_tries
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        mov eax, SYS_EXIT
        int 0x80

; Read a decimal number from keyboard (Enter to confirm)
; Returns: EAX = number
read_number:
        xor esi, esi            ; Accumulator
        xor ecx, ecx            ; Digit count

.loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 0x0D            ; Enter
        je .done
        cmp al, 0x0A
        je .done
        cmp al, 0x08            ; Backspace
        je .backspace

        cmp al, '0'
        jl .loop
        cmp al, '9'
        jg .loop

        ; Echo the digit
        push eax
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop eax

        ; Accumulate: esi = esi * 10 + digit
        sub al, '0'
        movzx eax, al
        imul esi, 10
        add esi, eax
        inc ecx
        cmp ecx, 3              ; Max 3 digits
        jl .loop

.done:
        ; Newline
        push esi
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop esi

        mov eax, esi
        ret

.backspace:
        or ecx, ecx
        jz .loop
        dec ecx
        ; Undo last digit: esi /= 10
        xor edx, edx
        mov eax, esi
        mov ebx, 10
        div ebx
        mov esi, eax
        ; Echo backspace + space + backspace to erase character visually
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        jmp .loop

secret:         dd 0
guess:          dd 0
guesses:        dd 0

msg_welcome:    db "=== Number Guessing Game ===", 0x0A
                db "I'm thinking of a number between 1 and 100.", 0x0A, 0x0A, 0
msg_prompt:     db "Your guess: ", 0
msg_high:       db "  Too high! Try lower.", 0x0A, 0
msg_low:        db "  Too low! Try higher.", 0x0A, 0
msg_correct:    db 0x0A, "  Correct! You got it in ", 0
msg_tries:      db " guesses!", 0x0A, 0
