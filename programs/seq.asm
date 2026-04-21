; seq.asm - Print sequence of numbers [HBU]
; Usage: seq LAST
;        seq FIRST LAST
;        seq FIRST INCR LAST
%include "syscalls.inc"

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .done

        mov esi, args_buf

        ; Parse first number
        call skip_ws
        call parse_num          ; EAX = first parsed number
        mov [seq_a], eax

        ; Check for second number
        call skip_ws
        movzx eax, byte [esi]
        cmp al, 0
        je .one_arg             ; only one arg: seq N

        ; Parse second number
        call parse_num
        mov [seq_b], eax

        ; Check for third number
        call skip_ws
        movzx eax, byte [esi]
        cmp al, 0
        je .two_args            ; two args: seq FIRST LAST

        ; Parse third number
        call parse_num
        mov [seq_c], eax
        ; Three args: seq FIRST INCR LAST
        mov eax, [seq_a]
        mov [seq_first], eax
        mov eax, [seq_b]
        mov [seq_step], eax
        mov eax, [seq_c]
        mov [seq_last], eax
        jmp .print_loop

.one_arg:
        mov dword [seq_first], 1
        mov dword [seq_step], 1
        mov eax, [seq_a]
        mov [seq_last], eax
        jmp .print_loop

.two_args:
        mov eax, [seq_a]
        mov [seq_first], eax
        mov dword [seq_step], 1
        mov eax, [seq_b]
        mov [seq_last], eax

.print_loop:
        mov eax, [seq_step]
        cmp eax, 0
        je .done                ; zero step would loop forever

        mov eax, [seq_first]
        mov [current], eax

.loop:
        mov eax, [current]
        mov ecx, [seq_step]
        ; Check bounds: if step > 0, loop while current <= last
        ;               if step < 0, loop while current >= last
        cmp ecx, 0
        jl .check_neg
        cmp eax, [seq_last]
        jg .done
        jmp .do_print
.check_neg:
        cmp eax, [seq_last]
        jl .done

.do_print:
        ; Print current number
        call print_number

        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Advance by step
        mov eax, [current]
        add eax, [seq_step]
        mov [current], eax
        jmp .loop

.done:
        mov eax, SYS_EXIT
        int 0x80

; Skip whitespace at [esi]
skip_ws:
        cmp byte [esi], ' '
        je .sp
        cmp byte [esi], 9
        je .sp
        ret
.sp:
        inc esi
        jmp skip_ws

; Parse decimal integer at [esi] into EAX, advancing ESI
parse_num:
        xor eax, eax
.pn_loop:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jb .pn_done
        cmp cl, '9'
        ja .pn_done
        sub cl, '0'
        imul eax, 10
        add eax, ecx
        inc esi
        jmp .pn_loop
.pn_done:
        ret

; Print decimal number in EAX
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
        jmp .pn2_done

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
        je .pn2_done
        pop ebx
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jmp .print_digits

.pn2_done:
        mov esp, esi
        pop esi
        ret

section .bss
args_buf:       resb 256
seq_a:          resd 1
seq_b:          resd 1
seq_c:          resd 1
seq_first:      resd 1
seq_step:       resd 1
seq_last:       resd 1
current:        resd 1
