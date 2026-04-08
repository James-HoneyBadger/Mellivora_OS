;=======================================================================
; math.inc - Mellivora Math/Number API
;
; Integer arithmetic, number parsing, number formatting, random number
; generation, and fixed-point math. All functions preserve registers
; unless they return a value.
;
; Functions:
;   math_parse_int    - Parse decimal string to integer
;   math_parse_hex    - Parse hex string to integer
;   math_parse_signed - Parse signed decimal string
;   math_int_to_str   - Convert integer to decimal string
;   math_hex_to_str   - Convert integer to hex string
;   math_bin_to_str   - Convert integer to binary string
;   math_abs          - Absolute value
;   math_min          - Minimum of two values
;   math_max          - Maximum of two values
;   math_clamp        - Clamp value to range
;   math_sign         - Get sign (-1, 0, 1)
;   math_random       - Generate pseudo-random number
;   math_random_range - Random number in range [min, max]
;   math_seed_random  - Seed the random generator
;   math_div_round    - Divide with rounding
;   math_mul_safe     - Multiply with overflow detection
;   math_power        - Integer exponentiation
;   math_gcd          - Greatest common divisor
;   math_sqrt         - Integer square root
;   math_log2         - Integer log base 2
;   math_digits       - Count decimal digits in number
;=======================================================================

;-----------------------------------------------------------------------
; math_parse_int - Parse decimal string to unsigned integer
; Input:  ESI = string pointer
; Output: EAX = parsed value
;         ECX = number of digits parsed (0 = invalid)
;-----------------------------------------------------------------------
math_parse_int:
        push ebx
        push edx
        push esi
        xor eax, eax
        xor ecx, ecx
        mov ebx, 10

.mpi_skip:
        cmp byte [esi], ' '
        je .mpi_skip_next
        cmp byte [esi], 9
        je .mpi_skip_next
        jmp .mpi_loop
.mpi_skip_next:
        inc esi
        jmp .mpi_skip

.mpi_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .mpi_done
        cmp dl, '9'
        ja .mpi_done
        imul eax, ebx
        sub dl, '0'
        add eax, edx
        inc ecx
        inc esi
        jmp .mpi_loop

.mpi_done:
        pop esi
        pop edx
        pop ebx
        ret

;-----------------------------------------------------------------------
; math_parse_signed - Parse signed decimal string
; Input:  ESI = string pointer (may start with '-' or '+')
; Output: EAX = parsed value (signed)
;         ECX = number of chars parsed
;-----------------------------------------------------------------------
math_parse_signed:
        push ebx
        push edx
        push esi
        xor edx, edx           ; sign flag

        ; Skip whitespace
.mps_skip:
        cmp byte [esi], ' '
        je .mps_skip_next
        cmp byte [esi], 9
        je .mps_skip_next
        jmp .mps_check_sign
.mps_skip_next:
        inc esi
        jmp .mps_skip

.mps_check_sign:
        cmp byte [esi], '-'
        jne .mps_plus
        mov edx, 1
        inc esi
        jmp .mps_digits
.mps_plus:
        cmp byte [esi], '+'
        jne .mps_digits
        inc esi

.mps_digits:
        xor eax, eax
        xor ecx, ecx
        mov ebx, 10
.mps_loop:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jb .mps_done
        cmp bl, '9'
        ja .mps_done
        mov ebx, 10
        imul eax, ebx
        movzx ebx, byte [esi]
        sub bl, '0'
        add eax, ebx
        inc ecx
        inc esi
        jmp .mps_loop

.mps_done:
        cmp edx, 0
        je .mps_positive
        neg eax
        inc ecx                 ; count sign char
.mps_positive:
        pop esi
        pop edx
        pop ebx
        ret

;-----------------------------------------------------------------------
; math_parse_hex - Parse hex string to integer
; Input:  ESI = string pointer (optional "0x" prefix)
; Output: EAX = parsed value
;         ECX = number of hex digits parsed
;-----------------------------------------------------------------------
math_parse_hex:
        push ebx
        push edx
        push esi
        xor eax, eax
        xor ecx, ecx

        ; Skip optional "0x" or "0X" prefix
        cmp byte [esi], '0'
        jne .mph_loop
        cmp byte [esi+1], 'x'
        je .mph_skip_prefix
        cmp byte [esi+1], 'X'
        je .mph_skip_prefix
        jmp .mph_loop
.mph_skip_prefix:
        add esi, 2

.mph_loop:
        movzx edx, byte [esi]

        cmp dl, '0'
        jb .mph_done
        cmp dl, '9'
        jbe .mph_digit

        cmp dl, 'a'
        jb .mph_upper
        cmp dl, 'f'
        jbe .mph_hex_lower

.mph_upper:
        cmp dl, 'A'
        jb .mph_done
        cmp dl, 'F'
        ja .mph_done
        sub dl, 'A'
        add dl, 10
        jmp .mph_add

.mph_hex_lower:
        sub dl, 'a'
        add dl, 10
        jmp .mph_add

.mph_digit:
        sub dl, '0'

.mph_add:
        shl eax, 4
        movzx edx, dl
        or eax, edx
        inc ecx
        inc esi
        jmp .mph_loop

.mph_done:
        pop esi
        pop edx
        pop ebx
        ret

;-----------------------------------------------------------------------
; math_int_to_str - Convert unsigned integer to decimal string
; Input:  EAX = value, EDI = output buffer (12 bytes min)
; Output: ECX = string length
;-----------------------------------------------------------------------
math_int_to_str:
        push eax
        push ebx
        push edx
        push edi
        push esi

        mov esi, edi            ; save start
        mov ebx, 10
        xor ecx, ecx

        test eax, eax
        jnz .mits_push
        ; Zero case
        mov byte [edi], '0'
        mov byte [edi+1], 0
        mov ecx, 1
        jmp .mits_end

.mits_push:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        test eax, eax
        jnz .mits_push

        mov edx, ecx           ; save count
.mits_pop:
        pop eax
        add al, '0'
        mov [edi], al
        inc edi
        dec edx
        jnz .mits_pop
        mov byte [edi], 0
        ; ECX already holds length

.mits_end:
        pop esi
        pop edi
        pop edx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; math_hex_to_str - Convert integer to hex string
; Input:  EAX = value, EDI = output buffer (11 bytes min)
;         ECX = min digits (0 = auto, typically 2, 4, or 8)
; Output: none (string written to EDI)
;-----------------------------------------------------------------------
math_hex_to_str:
        pushad
        mov esi, eax            ; save value
        mov edx, ecx            ; min digits

        ; Count hex digits needed
        mov ebx, eax
        xor ecx, ecx
        test ebx, ebx
        jnz .mhts_count
        mov ecx, 1
        jmp .mhts_pad
.mhts_count:
        test ebx, ebx
        jz .mhts_pad
        shr ebx, 4
        inc ecx
        jmp .mhts_count

.mhts_pad:
        cmp ecx, edx
        jge .mhts_write
        mov ecx, edx

.mhts_write:
        ; Write digits from MSB to LSB
        mov edx, ecx
        add edi, ecx
        mov byte [edi], 0      ; null terminate
        dec edi
        mov eax, esi

.mhts_loop:
        mov ebx, eax
        and ebx, 0x0F
        cmp bl, 10
        jb .mhts_dec
        add bl, 'A' - 10
        jmp .mhts_store
.mhts_dec:
        add bl, '0'
.mhts_store:
        mov [edi], bl
        shr eax, 4
        dec edi
        dec edx
        jnz .mhts_loop
        popad
        ret

;-----------------------------------------------------------------------
; math_bin_to_str - Convert integer to binary string
; Input:  EAX = value, EDI = output buffer (33 bytes min)
;         ECX = number of bits to show (1-32)
; Output: none (string written to EDI)
;-----------------------------------------------------------------------
math_bin_to_str:
        pushad
        mov edx, ecx           ; bit count
        add edi, ecx
        mov byte [edi], 0
        dec edi

.mbts_loop:
        cmp edx, 0
        jle .mbts_done
        test eax, 1
        jz .mbts_zero
        mov byte [edi], '1'
        jmp .mbts_next
.mbts_zero:
        mov byte [edi], '0'
.mbts_next:
        shr eax, 1
        dec edi
        dec edx
        jmp .mbts_loop

.mbts_done:
        popad
        ret

;-----------------------------------------------------------------------
; math_abs - Absolute value
; Input:  EAX = signed integer
; Output: EAX = absolute value
;-----------------------------------------------------------------------
math_abs:
        test eax, eax
        jns .mabs_done
        neg eax
.mabs_done:
        ret

;-----------------------------------------------------------------------
; math_min - Minimum of two values (unsigned)
; Input:  EAX, EBX
; Output: EAX = min(EAX, EBX)
;-----------------------------------------------------------------------
math_min:
        cmp eax, ebx
        jbe .mmin_done
        mov eax, ebx
.mmin_done:
        ret

;-----------------------------------------------------------------------
; math_max - Maximum of two values (unsigned)
; Input:  EAX, EBX
; Output: EAX = max(EAX, EBX)
;-----------------------------------------------------------------------
math_max:
        cmp eax, ebx
        jae .mmax_done
        mov eax, ebx
.mmax_done:
        ret

;-----------------------------------------------------------------------
; math_clamp - Clamp value to range [min, max] (unsigned)
; Input:  EAX = value, EBX = min, ECX = max
; Output: EAX = clamped value
;-----------------------------------------------------------------------
math_clamp:
        cmp eax, ebx
        jae .mc_above_min
        mov eax, ebx
        ret
.mc_above_min:
        cmp eax, ecx
        jbe .mc_done
        mov eax, ecx
.mc_done:
        ret

;-----------------------------------------------------------------------
; math_sign - Get sign of signed integer
; Input:  EAX = signed value
; Output: EAX = -1, 0, or 1
;-----------------------------------------------------------------------
math_sign:
        test eax, eax
        jz .ms_done
        js .ms_neg
        mov eax, 1
        ret
.ms_neg:
        mov eax, -1
.ms_done:
        ret

;-----------------------------------------------------------------------
; math_random - Generate pseudo-random 32-bit number (LCG)
; Output: EAX = random value
;-----------------------------------------------------------------------
math_random:
        push ebx
        push edx
        mov eax, [_math_seed]
        mov ebx, 1103515245
        imul eax, ebx
        add eax, 12345
        mov [_math_seed], eax
        ; Improve distribution: use bits 16-30
        shr eax, 16
        and eax, 0x7FFF
        pop edx
        pop ebx
        ret

;-----------------------------------------------------------------------
; math_random_range - Random number in range [min, max]
; Input:  EAX = min, EBX = max
; Output: EAX = random value in [min, max]
;-----------------------------------------------------------------------
math_random_range:
        push ecx
        push edx
        push esi
        mov esi, eax            ; save min
        sub ebx, eax
        inc ebx                 ; range = max - min + 1

        call math_random        ; EAX = random 0..32767

        ; EAX = min + (random % range)
        xor edx, edx
        div ebx                 ; EDX = EAX % range
        mov eax, edx
        add eax, esi
        pop esi
        pop edx
        pop ecx
        ret

;-----------------------------------------------------------------------
; math_seed_random - Seed the random number generator
; Input:  EAX = seed value (use io_get_time for entropy)
; Output: none
;-----------------------------------------------------------------------
math_seed_random:
        mov [_math_seed], eax
        ret

;-----------------------------------------------------------------------
; math_div_round - Divide with rounding (unsigned)
; Input:  EAX = dividend, EBX = divisor
; Output: EAX = rounded quotient
;-----------------------------------------------------------------------
math_div_round:
        push edx
        xor edx, edx
        div ebx
        ; Check if remainder >= divisor/2
        shl edx, 1
        cmp edx, ebx
        jb .mdr_done
        inc eax
.mdr_done:
        pop edx
        ret

;-----------------------------------------------------------------------
; math_mul_safe - Multiply with overflow detection
; Input:  EAX, EBX
; Output: EAX = product
;         Carry flag set on overflow
;-----------------------------------------------------------------------
math_mul_safe:
        push edx
        mul ebx
        test edx, edx
        jnz .mms_overflow
        pop edx
        clc
        ret
.mms_overflow:
        pop edx
        stc
        ret

;-----------------------------------------------------------------------
; math_power - Integer exponentiation (base^exp)
; Input:  EAX = base, ECX = exponent
; Output: EAX = result
;-----------------------------------------------------------------------
math_power:
        push ebx
        push ecx
        push edx
        mov ebx, eax            ; base
        mov eax, 1              ; result

        test ecx, ecx
        jz .mpow_done

.mpow_loop:
        test ecx, 1
        jz .mpow_square
        mul ebx                 ; result *= base

.mpow_square:
        push eax
        mov eax, ebx
        mul ebx
        mov ebx, eax            ; base *= base
        pop eax
        shr ecx, 1
        jnz .mpow_loop

.mpow_done:
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; math_gcd - Greatest common divisor (Euclidean algorithm)
; Input:  EAX = a, EBX = b
; Output: EAX = gcd(a, b)
;-----------------------------------------------------------------------
math_gcd:
        push edx
.mgcd_loop:
        test ebx, ebx
        jz .mgcd_done
        xor edx, edx
        div ebx
        mov eax, ebx
        mov ebx, edx
        jmp .mgcd_loop
.mgcd_done:
        pop edx
        ret

;-----------------------------------------------------------------------
; math_sqrt - Integer square root (binary search)
; Input:  EAX = value
; Output: EAX = floor(sqrt(value))
;-----------------------------------------------------------------------
math_sqrt:
        push ebx
        push ecx
        push edx

        test eax, eax
        jz .msqrt_done
        cmp eax, 1
        je .msqrt_done

        mov ecx, eax            ; save value
        ; Start with initial guess
        mov ebx, eax
        shr ebx, 1              ; guess = n/2

.msqrt_loop:
        ; new_guess = (guess + n/guess) / 2
        mov eax, ecx
        xor edx, edx
        div ebx                 ; EAX = n/guess
        add eax, ebx            ; EAX = guess + n/guess
        shr eax, 1              ; EAX = (guess + n/guess) / 2

        cmp eax, ebx
        jge .msqrt_converged
        mov ebx, eax
        jmp .msqrt_loop

.msqrt_converged:
        mov eax, ebx            ; return guess
.msqrt_done:
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; math_log2 - Integer log base 2 (position of highest set bit)
; Input:  EAX = value (must be > 0)
; Output: EAX = floor(log2(value)), or -1 if input is 0
;-----------------------------------------------------------------------
math_log2:
        test eax, eax
        jz .ml2_zero
        push ecx
        bsr ecx, eax            ; bit scan reverse
        mov eax, ecx
        pop ecx
        ret
.ml2_zero:
        mov eax, -1
        ret

;-----------------------------------------------------------------------
; math_digits - Count decimal digits in unsigned number
; Input:  EAX = value
; Output: EAX = number of digits
;-----------------------------------------------------------------------
math_digits:
        push ebx
        push edx
        push ecx
        mov ecx, 1
        mov ebx, 10
.md_loop:
        xor edx, edx
        div ebx
        test eax, eax
        jz .md_done
        inc ecx
        jmp .md_loop
.md_done:
        mov eax, ecx
        pop ecx
        pop edx
        pop ebx
        ret

;=======================================================================
; BSS Section - Random seed
;=======================================================================
section .bss
        _math_seed: resd 1

section .text
