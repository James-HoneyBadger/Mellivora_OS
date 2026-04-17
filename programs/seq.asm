; seq.asm - Print sequence of numbers [HBU]
; Usage: seq LAST
;        seq FIRST LAST
;        seq FIRST INCREMENT LAST
;        seq -s SEP ARGS...    (use SEP as separator instead of newline)
%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle show_usage

        ; Defaults
        mov dword [first], 1
        mov dword [increment], 1
        mov dword [last], 1
        mov dword [separator], 0x0A     ; newline
        mov dword [num_count], 0

        mov esi, args_buf

        ; Check for -s flag
        cmp byte [esi], '-'
        jne .parse_numbers
        cmp byte [esi+1], 's'
        jne .parse_numbers
        add esi, 2
        call skip_sp
        cmp byte [esi], 0
        je show_usage
        movzx eax, byte [esi]
        mov [separator], eax
        inc esi
        call skip_sp

.parse_numbers:
        ; Parse up to 3 numbers
        call skip_sp
        cmp byte [esi], 0
        je show_usage

        ; Parse first number
        call parse_int
        mov [nums], eax
        inc dword [num_count]

        call skip_sp
        cmp byte [esi], 0
        je .have_args

        ; Parse second number
        call parse_int
        mov [nums+4], eax
        inc dword [num_count]

        call skip_sp
        cmp byte [esi], 0
        je .have_args

        ; Parse third number
        call parse_int
        mov [nums+8], eax
        inc dword [num_count]

.have_args:
        ; Assign based on count:
        ; 1 arg:  seq LAST         -> first=1, incr=1, last=arg1
        ; 2 args: seq FIRST LAST   -> first=arg1, incr=1, last=arg2
        ; 3 args: seq FIRST INCR LAST -> first=arg1, incr=arg2, last=arg3
        cmp dword [num_count], 1
        je .one_arg
        cmp dword [num_count], 2
        je .two_args
        ; Three args
        mov eax, [nums]
        mov [first], eax
        mov eax, [nums+4]
        mov [increment], eax
        mov eax, [nums+8]
        mov [last], eax
        jmp .run

.one_arg:
        mov eax, [nums]
        mov [last], eax
        jmp .run

.two_args:
        mov eax, [nums]
        mov [first], eax
        mov eax, [nums+4]
        mov [last], eax

.run:
        ; Validate increment != 0
        cmp dword [increment], 0
        je show_usage

        mov eax, [first]
        mov [current], eax
        mov byte [printed_first], 0

.print_loop:
        ; Check direction
        mov eax, [increment]
        test eax, eax
        js .check_down

        ; Incrementing: current <= last
        mov eax, [current]
        cmp eax, [last]
        jg .done
        jmp .do_print

.check_down:
        ; Decrementing: current >= last
        mov eax, [current]
        cmp eax, [last]
        jl .done

.do_print:
        ; Print separator before 2nd+ numbers
        cmp byte [printed_first], 0
        je .no_sep
        mov eax, SYS_PUTCHAR
        mov ebx, [separator]
        int 0x80
.no_sep:
        mov byte [printed_first], 1

        mov eax, [current]
        ; Handle negative numbers
        test eax, eax
        jns .print_pos
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        pop rax
        neg eax
.print_pos:
        call print_number

        mov eax, [current]
        add eax, [increment]
        mov [current], eax
        jmp .print_loop

.done:
        ; Final newline if separator wasn't newline
        cmp dword [separator], 0x0A
        je .exit
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; skip_sp: skip spaces/tabs at ESI
;---------------------------------------
skip_sp:
        cmp byte [esi], ' '
        je .ss
        cmp byte [esi], 9
        je .ss
        ret
.ss:
        inc esi
        jmp skip_sp

;---------------------------------------
; parse_int: parse signed integer from ESI -> EAX
;---------------------------------------
parse_int:
        xor eax, eax
        xor edx, edx
        cmp byte [esi], '-'
        jne .pi_pos
        mov edx, 1             ; negative flag
        inc esi
.pi_pos:
        xor ecx, ecx
.pi_loop:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jb .pi_done
        cmp bl, '9'
        ja .pi_done
        imul eax, 10
        sub bl, '0'
        add eax, ebx
        inc esi
        inc ecx
        jmp .pi_loop
.pi_done:
        cmp ecx, 0
        je show_usage           ; no digits
        cmp edx, 0
        je .pi_ret
        neg eax
.pi_ret:
        ret

;---------------------------------------
; print_number: print unsigned decimal EAX
;---------------------------------------
print_number:
        push rsi
        mov ecx, 0
        mov rsi, rsp
        sub rsp, 16
        mov ebx, 10

        cmp eax, 0
        jne .div_loop
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        pop rax
        jmp .pn_done

.div_loop:
        xor edx, edx
        div ebx
        add dl, '0'
        push rdx
        inc ecx
        cmp eax, 0
        jne .div_loop

.print_digits:
        cmp ecx, 0
        je .pn_done
        pop rbx
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jmp .print_digits

.pn_done:
        mov rsp, rsi
        pop rsi
        ret

show_usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

section .data
usage_str:  db "Usage: seq [-s SEP] [FIRST [INCR]] LAST", 0x0A
            db "Print a sequence of numbers.", 0x0A
            db "  seq 10          -> 1 2 3 ... 10", 0x0A
            db "  seq 5 10        -> 5 6 7 8 9 10", 0x0A
            db "  seq 0 2 10      -> 0 2 4 6 8 10", 0x0A
            db "  seq -s , 1 5    -> 1,2,3,4,5", 0x0A, 0

section .bss
args_buf:       resb 512
first:          resd 1
increment:      resd 1
last:           resd 1
current:        resd 1
separator:      resd 1
num_count:      resd 1
nums:           resd 3
printed_first:  resb 1
