; =============================================================================
; test.asm - Evaluate conditional expressions
;
; Usage: test EXPRESSION
;    or: [ EXPRESSION ]
;
; Exits with status 0 (true) or 1 (false).
; Works with shell && and || operators for scripting.
;
; File tests:
;   -e FILE   True if FILE exists
;   -f FILE   True if FILE exists and is a regular file
;   -d FILE   True if FILE exists and is a directory
;   -s FILE   True if FILE exists and has size > 0
;   -L FILE   True if FILE is a symbolic link
;
; String tests:
;   -z STRING       True if STRING is empty
;   -n STRING       True if STRING is non-empty
;   STR1 = STR2    True if strings are equal
;   STR1 != STR2   True if strings are not equal
;
; Integer comparisons:
;   N1 -eq N2   Equal
;   N1 -ne N2   Not equal
;   N1 -lt N2   Less than
;   N1 -gt N2   Greater than
;   N1 -le N2   Less than or equal
;   N1 -ge N2   Greater than or equal
;
; Logical:
;   ! EXPRESSION    Negate
; =============================================================================

%include "syscalls.inc"

start:
        ; Get command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        test eax, eax
        jz .false               ; No args = false

        mov esi, args_buf
        call skip_spaces

        cmp byte [esi], 0
        je .false               ; Empty = false

        ; Check for negation prefix: ! expr
        cmp byte [esi], '!'
        jne .no_negate
        cmp byte [esi + 1], ' '
        jne .no_negate
        mov byte [negate], 1
        add esi, 2
        call skip_spaces
.no_negate:

        ; Check for unary operators: -e, -f, -d, -s, -z, -n, -L
        cmp byte [esi], '-'
        jne .check_binary

        movzx eax, byte [esi + 1]

        ; Make sure it's a single-char flag followed by space
        cmp byte [esi + 2], ' '
        jne .check_binary

        add esi, 3
        call skip_spaces

        cmp al, 'e'
        je .test_exists
        cmp al, 'f'
        je .test_file
        cmp al, 'd'
        je .test_dir
        cmp al, 's'
        je .test_size
        cmp al, 'L'
        je .test_link
        cmp al, 'z'
        je .test_zero
        cmp al, 'n'
        je .test_nonzero
        jmp .false

; ----- File tests -----
.test_exists:
        call file_stat
        cmp eax, -1
        jne .true
        jmp .false

.test_file:
        call file_find_type
        cmp al, 1               ; FTYPE_FILE
        je .true
        jmp .false

.test_dir:
        call file_find_type
        cmp al, 2               ; FTYPE_DIR
        je .true
        jmp .false

.test_link:
        call file_find_type
        cmp al, 5               ; FTYPE_LINK
        je .true
        jmp .false

.test_size:
        call file_stat
        cmp eax, -1
        je .false
        cmp eax, 0
        jg .true
        jmp .false

; ----- String tests -----
.test_zero:
        ; -z STRING: true if empty
        cmp byte [esi], 0
        je .true
        jmp .false

.test_nonzero:
        ; -n STRING: true if non-empty
        cmp byte [esi], 0
        jne .true
        jmp .false

; ----- Binary operators -----
.check_binary:
        ; Parse: ARG1 OP ARG2
        ; Save start of ARG1
        mov [arg1], rsi

        ; Find end of ARG1 (next space)
        call find_space
        cmp byte [esi], 0
        je .single_string       ; Only one token = -n test

        mov byte [esi], 0       ; Terminate ARG1
        inc esi
        call skip_spaces

        ; ESI points to operator
        mov [op_ptr], rsi

        ; Find end of operator
        call find_space
        cmp byte [esi], 0
        je .false               ; Missing ARG2

        mov byte [esi], 0       ; Terminate operator
        inc esi
        call skip_spaces
        mov [arg2], rsi

        ; Now dispatch on operator
        mov rsi, [op_ptr]

        ; Check "=" (string equal)
        cmp byte [esi], '='
        jne .chk_ne
        cmp byte [esi + 1], 0
        jne .chk_ne
        jmp .str_equal

.chk_ne:
        ; Check "!=" (string not equal)
        cmp byte [esi], '!'
        jne .chk_int_ops
        cmp byte [esi + 1], '='
        jne .chk_int_ops
        cmp byte [esi + 2], 0
        jne .chk_int_ops
        jmp .str_not_equal

.chk_int_ops:
        ; Check integer comparison operators: -eq -ne -lt -gt -le -ge
        cmp byte [esi], '-'
        jne .false

        movzx eax, byte [esi + 1]
        movzx ebx, byte [esi + 2]

        ; -eq
        cmp al, 'e'
        jne .not_eq
        cmp bl, 'q'
        jne .false
        jmp .int_eq
.not_eq:
        ; -ne
        cmp al, 'n'
        jne .not_ne
        cmp bl, 'e'
        jne .false
        jmp .int_ne
.not_ne:
        ; -lt
        cmp al, 'l'
        jne .not_lt
        cmp bl, 't'
        jne .chk_le
        jmp .int_lt
.chk_le:
        cmp bl, 'e'
        jne .false
        jmp .int_le
.not_lt:
        ; -gt
        cmp al, 'g'
        jne .not_gt
        cmp bl, 't'
        jne .chk_ge
        jmp .int_gt
.chk_ge:
        cmp bl, 'e'
        jne .false
        jmp .int_ge
.not_gt:
        jmp .false

; ----- String comparisons -----
.str_equal:
        mov rsi, [arg1]
        mov rdi, [arg2]
        call str_cmp
        test eax, eax
        jz .true
        jmp .false

.str_not_equal:
        mov rsi, [arg1]
        mov rdi, [arg2]
        call str_cmp
        test eax, eax
        jnz .true
        jmp .false

; ----- Integer comparisons -----
.int_eq:
        call parse_both_ints
        cmp eax, ebx
        je .true
        jmp .false

.int_ne:
        call parse_both_ints
        cmp eax, ebx
        jne .true
        jmp .false

.int_lt:
        call parse_both_ints
        cmp eax, ebx
        jl .true
        jmp .false

.int_gt:
        call parse_both_ints
        cmp eax, ebx
        jg .true
        jmp .false

.int_le:
        call parse_both_ints
        cmp eax, ebx
        jle .true
        jmp .false

.int_ge:
        call parse_both_ints
        cmp eax, ebx
        jge .true
        jmp .false

; ----- Single string (implicit -n) -----
.single_string:
        ; A bare string is true if non-empty
        mov rsi, [arg1]
        cmp byte [esi], 0
        jne .true
        jmp .false

; ----- Result -----
.true:
        xor ebx, ebx           ; exit code 0 = true
        cmp byte [negate], 1
        jne .exit
        mov ebx, 1             ; negated: true -> false
        jmp .exit

.false:
        mov ebx, 1             ; exit code 1 = false
        cmp byte [negate], 1
        jne .exit
        xor ebx, ebx           ; negated: false -> true

.exit:
        mov eax, SYS_EXIT
        int 0x80

; =========================================================================
; Helper: file_stat - Get file size via SYS_STAT
; Input: ESI = filename
; Output: EAX = file size (-1 if not found)
; =========================================================================
file_stat:
        push rbx
        mov eax, SYS_STAT
        mov ebx, esi
        int 0x80
        pop rbx
        ret

; =========================================================================
; Helper: file_find_type - Find file type via SYS_READDIR scan
; Input: ESI = filename to find
; Output: AL = file type (0 if not found)
; =========================================================================
file_find_type:
        push rbx
        push rcx
        push rdx
        push rsi
        mov [.fft_name], esi
        xor edx, edx           ; index

.fft_loop:
        mov eax, SYS_READDIR
        mov ebx, readdir_buf
        mov ecx, edx
        int 0x80

        cmp eax, -1
        je .fft_not_found       ; End of directory

        test eax, eax
        jz .fft_next            ; Free slot, skip

        ; Compare name
        mov esi, readdir_buf
        mov edi, [.fft_name]
        call str_cmp
        test eax, eax
        jz .fft_found

.fft_next:
        inc edx
        cmp edx, 4096          ; Safety limit
        jb .fft_loop

.fft_not_found:
        xor eax, eax
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

.fft_found:
        ; EAX still holds the type from SYS_READDIR
        mov eax, SYS_READDIR
        mov ebx, readdir_buf
        mov ecx, edx
        int 0x80
        ; EAX = type
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

.fft_name:  dd 0

; =========================================================================
; Helper: parse_both_ints - Parse arg1 and arg2 as signed integers
; Output: EAX = int(arg1), EBX = int(arg2)
; =========================================================================
parse_both_ints:
        mov rsi, [arg1]
        call parse_int
        push rax
        mov rsi, [arg2]
        call parse_int
        mov ebx, eax
        pop rax
        ret

; =========================================================================
; Helper: parse_int - Parse signed decimal integer from string
; Input: ESI = string
; Output: EAX = integer value
; =========================================================================
parse_int:
        xor eax, eax
        xor ecx, ecx           ; sign flag
        cmp byte [esi], '-'
        jne .pi_loop
        inc ecx
        inc esi
.pi_loop:
        movzx edx, byte [esi]
        sub dl, '0'
        cmp dl, 9
        ja .pi_done
        imul eax, 10
        add eax, edx
        inc esi
        jmp .pi_loop
.pi_done:
        test ecx, ecx
        jz .pi_pos
        neg eax
.pi_pos:
        ret

; =========================================================================
; Helper: str_cmp - Compare two null-terminated strings
; Input: ESI, EDI
; Output: EAX = 0 if equal, nonzero if different
; =========================================================================
str_cmp:
        push rsi
        push rdi
.sc_loop:
        lodsb
        mov ah, [edi]
        inc edi
        cmp al, ah
        jne .sc_diff
        test al, al
        jnz .sc_loop
        xor eax, eax
        pop rdi
        pop rsi
        ret
.sc_diff:
        mov eax, 1
        pop rdi
        pop rsi
        ret

; =========================================================================
; Helper: skip_spaces - Advance ESI past spaces
; =========================================================================
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

; =========================================================================
; Helper: find_space - Advance ESI to next space or null
; =========================================================================
find_space:
        cmp byte [esi], 0
        je .fs_done
        cmp byte [esi], ' '
        je .fs_done
        inc esi
        jmp find_space
.fs_done:
        ret

section .data
negate:     db 0

section .bss
args_buf:   resb 512
arg1:       resq 1
arg2:       resq 1
op_ptr:     resq 1
readdir_buf: resb 260
