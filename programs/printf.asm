; =============================================================================
; printf.asm - Formatted output
;
; Usage: printf FORMAT [ARGUMENT...]
;
; Interprets FORMAT string with:
;   %s    - Insert string argument
;   %d    - Insert signed decimal integer
;   %u    - Insert unsigned decimal integer
;   %x    - Insert hexadecimal (lowercase)
;   %X    - Insert hexadecimal (uppercase)
;   %c    - Insert single character (first char of argument)
;   %%    - Literal percent sign
;
; Backslash escapes:
;   \n    - Newline
;   \t    - Tab
;   \\    - Literal backslash
;   \0    - Null byte (terminates output)
;   \a    - Bell (0x07)
;   \r    - Carriage return
; =============================================================================

%include "syscalls.inc"

start:
        ; Get command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, args_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; First argument is the format string
        mov [fmt_ptr], rsi

        ; Find end of format string (space-separated unless quoted)
        cmp byte [esi], '"'
        je .quoted_fmt
        cmp byte [esi], 0x27   ; single quote
        je .quoted_fmt_sq

        ; Unquoted: find next space
.find_fmt_end:
        cmp byte [esi], 0
        je .got_fmt
        cmp byte [esi], ' '
        je .term_fmt
        inc esi
        jmp .find_fmt_end

.quoted_fmt:
        inc esi                 ; skip opening quote
        mov [fmt_ptr], rsi
.qf_loop:
        cmp byte [esi], 0
        je .got_fmt
        cmp byte [esi], '"'
        je .term_fmt
        inc esi
        jmp .qf_loop

.quoted_fmt_sq:
        inc esi
        mov [fmt_ptr], rsi
.qfs_loop:
        cmp byte [esi], 0
        je .got_fmt
        cmp byte [esi], 0x27
        je .term_fmt
        inc esi
        jmp .qfs_loop

.term_fmt:
        mov byte [esi], 0
        inc esi

.got_fmt:
        call skip_spaces
        mov [args_ptr], rsi     ; Remaining arguments start here

        ; Process format string
        mov rsi, [fmt_ptr]

.fmt_loop:
        lodsb
        test al, al
        jz .done

        cmp al, '%'
        je .format_spec

        cmp al, '\'
        je .escape

        ; Regular character — print it
        call putchar
        jmp .fmt_loop

; ----- Backslash escapes -----
.escape:
        lodsb
        test al, al
        jz .done
        cmp al, 'n'
        je .esc_nl
        cmp al, 't'
        je .esc_tab
        cmp al, '\'
        je .esc_bs
        cmp al, '0'
        je .done               ; \0 terminates output
        cmp al, 'a'
        je .esc_bell
        cmp al, 'r'
        je .esc_cr
        ; Unknown escape: print backslash + char
        push rax
        mov al, '\'
        call putchar
        pop rax
        call putchar
        jmp .fmt_loop

.esc_nl:
        mov al, 0x0A
        call putchar
        jmp .fmt_loop
.esc_tab:
        mov al, 0x09
        call putchar
        jmp .fmt_loop
.esc_bs:
        mov al, '\'
        call putchar
        jmp .fmt_loop
.esc_bell:
        mov al, 0x07
        call putchar
        jmp .fmt_loop
.esc_cr:
        mov al, 0x0D
        call putchar
        jmp .fmt_loop

; ----- Format specifiers -----
.format_spec:
        lodsb
        test al, al
        jz .done

        cmp al, '%'
        je .fmt_pct
        cmp al, 's'
        je .fmt_str
        cmp al, 'd'
        je .fmt_dec
        cmp al, 'u'
        je .fmt_udec
        cmp al, 'x'
        je .fmt_hex
        cmp al, 'X'
        je .fmt_hex_upper
        cmp al, 'c'
        je .fmt_char

        ; Unknown: print literal %X
        push rax
        mov al, '%'
        call putchar
        pop rax
        call putchar
        jmp .fmt_loop

.fmt_pct:
        mov al, '%'
        call putchar
        jmp .fmt_loop

.fmt_str:
        push rsi
        call next_arg
        test esi, esi
        jz .fmt_str_done
        ; Print arg string
.fmt_str_lp:
        lodsb
        test al, al
        jz .fmt_str_done
        call putchar
        jmp .fmt_str_lp
.fmt_str_done:
        pop rsi
        jmp .fmt_loop

.fmt_char:
        push rsi
        call next_arg
        test esi, esi
        jz .fmt_char_none
        mov al, [esi]
        call putchar
.fmt_char_none:
        pop rsi
        jmp .fmt_loop

.fmt_dec:
        push rsi
        call next_arg
        test esi, esi
        jz .fmt_num_done
        call parse_int
        call print_signed
.fmt_num_done:
        pop rsi
        jmp .fmt_loop

.fmt_udec:
        push rsi
        call next_arg
        test esi, esi
        jz .fmt_unum_done
        call parse_int
        call print_unsigned
.fmt_unum_done:
        pop rsi
        jmp .fmt_loop

.fmt_hex:
        push rsi
        call next_arg
        test esi, esi
        jz .fmt_hex_done
        call parse_int
        mov byte [hex_upper], 0
        call print_hex
.fmt_hex_done:
        pop rsi
        jmp .fmt_loop

.fmt_hex_upper:
        push rsi
        call next_arg
        test esi, esi
        jz .fmt_hexu_done
        call parse_int
        mov byte [hex_upper], 1
        call print_hex
.fmt_hexu_done:
        pop rsi
        jmp .fmt_loop

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_msg
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; =========================================================================
; Helper: putchar - Print one character
; Input: AL = character
; =========================================================================
putchar:
        push rax
        push rbx
        movzx eax, al
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rbx
        pop rax
        ret

; =========================================================================
; Helper: next_arg - Get next whitespace-delimited argument
; Updates [args_ptr]. Returns ESI = ptr to arg (null-terminated), or ESI=0.
; =========================================================================
next_arg:
        mov rsi, [args_ptr]
        call skip_spaces
        cmp byte [esi], 0
        je .na_empty

        ; Handle quoted argument
        cmp byte [esi], '"'
        je .na_quoted
        cmp byte [esi], 0x27
        je .na_quoted_sq

        ; Unquoted: find end
        mov edi, esi
.na_find:
        cmp byte [esi], 0
        je .na_done
        cmp byte [esi], ' '
        je .na_term
        inc esi
        jmp .na_find
.na_term:
        mov byte [esi], 0
        inc esi
.na_done:
        mov [args_ptr], rsi
        mov esi, edi
        ret

.na_quoted:
        inc esi                 ; skip opening "
        mov edi, esi
.na_q_loop:
        cmp byte [esi], 0
        je .na_done
        cmp byte [esi], '"'
        je .na_q_end
        inc esi
        jmp .na_q_loop
.na_q_end:
        mov byte [esi], 0
        inc esi
        jmp .na_done

.na_quoted_sq:
        inc esi
        mov edi, esi
.na_qs_loop:
        cmp byte [esi], 0
        je .na_done
        cmp byte [esi], 0x27
        je .na_qs_end
        inc esi
        jmp .na_qs_loop
.na_qs_end:
        mov byte [esi], 0
        inc esi
        jmp .na_done

.na_empty:
        xor esi, esi
        ret

; =========================================================================
; Helper: parse_int - Parse signed decimal string to integer
; Input: ESI = string
; Output: EAX = value
; =========================================================================
parse_int:
        xor eax, eax
        xor ecx, ecx           ; sign
        cmp byte [esi], '-'
        jne .pi_loop
        inc ecx
        inc esi
.pi_loop:
        movzx edx, byte [esi]
        sub dl, '0'
        cmp dl, 9
        ja .pi_end
        imul eax, 10
        add eax, edx
        inc esi
        jmp .pi_loop
.pi_end:
        test ecx, ecx
        jz .pi_ret
        neg eax
.pi_ret:
        ret

; =========================================================================
; Helper: print_signed - Print EAX as signed decimal
; =========================================================================
print_signed:
        test eax, eax
        jns .ps_pos
        push rax
        mov al, '-'
        call putchar
        pop rax
        neg eax
.ps_pos:
        call print_unsigned
        ret

; =========================================================================
; Helper: print_unsigned - Print EAX as unsigned decimal
; =========================================================================
print_unsigned:
        push rbx
        push rcx
        push rdx
        mov ecx, 0             ; digit count
        mov ebx, 10
.pu_div:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        test eax, eax
        jnz .pu_div
.pu_print:
        pop rax
        add al, '0'
        call putchar
        loop .pu_print
        pop rdx
        pop rcx
        pop rbx
        ret

; =========================================================================
; Helper: print_hex - Print EAX as hex
; =========================================================================
print_hex:
        push rbx
        push rcx
        push rdx
        mov ecx, 0
        test eax, eax
        jnz .ph_loop
        ; Special case: zero
        mov al, '0'
        call putchar
        pop rdx
        pop rcx
        pop rbx
        ret
.ph_loop:
        test eax, eax
        jz .ph_print
        mov edx, eax
        and edx, 0x0F
        push rdx
        shr eax, 4
        inc ecx
        jmp .ph_loop
.ph_print:
        pop rax
        cmp al, 10
        jb .ph_digit
        sub al, 10
        cmp byte [hex_upper], 1
        je .ph_upper
        add al, 'a'
        jmp .ph_out
.ph_upper:
        add al, 'A'
        jmp .ph_out
.ph_digit:
        add al, '0'
.ph_out:
        call putchar
        loop .ph_print
        pop rdx
        pop rcx
        pop rbx
        ret

; =========================================================================
; Helper: skip_spaces
; =========================================================================
skip_spaces:
        cmp byte [esi], ' '
        jne .ret
        inc esi
        jmp skip_spaces
.ret:
        ret

section .data
usage_msg:  db "Usage: printf FORMAT [ARGUMENT...]", 0x0A, 0
hex_upper:  db 0

section .bss
args_buf:   resb 512
fmt_ptr:    resq 1
args_ptr:   resq 1
