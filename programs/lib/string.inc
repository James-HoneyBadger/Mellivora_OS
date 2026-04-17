;=======================================================================
; string.inc - Mellivora String API
;
; High-performance string manipulation routines for application and
; systems development. All functions preserve registers unless they
; return a value. Uses the standard Mellivora calling convention:
;   - Arguments in EAX, EBX, ECX, EDX, ESI, EDI as documented
;   - Return values in EAX (and sometimes ECX)
;   - All other registers preserved via PUSHALL/POPALL
;
; Functions:
;   str_len         - Get string length
;   str_copy        - Copy string
;   str_ncopy       - Copy up to N chars
;   str_cat         - Concatenate strings
;   str_cmp         - Compare strings (case-sensitive)
;   str_icmp        - Compare strings (case-insensitive)
;   str_ncmp        - Compare first N chars
;   str_chr         - Find character in string
;   str_rchr        - Find last occurrence of character
;   str_str         - Find substring
;   str_starts_with - Check if string starts with prefix
;   str_ends_with   - Check if string ends with suffix
;   str_upper       - Convert to uppercase in-place
;   str_lower       - Convert to lowercase in-place
;   str_trim        - Trim leading/trailing whitespace
;   str_ltrim       - Trim leading whitespace
;   str_rtrim       - Trim trailing whitespace
;   str_reverse     - Reverse string in-place
;   str_count_char  - Count occurrences of character
;   str_replace_char - Replace all occurrences of a char
;   str_token       - Tokenize string (strtok-like)
;   str_split_line  - Extract next line from buffer
;   str_to_upper_c  - Convert single char to uppercase
;   str_to_lower_c  - Convert single char to lowercase
;   str_is_alpha    - Check if char is alphabetic
;   str_is_digit    - Check if char is a digit
;   str_is_alnum    - Check if char is alphanumeric
;   str_is_space    - Check if char is whitespace
;   mem_copy        - Copy memory block
;   mem_set         - Fill memory block
;   mem_cmp         - Compare memory blocks
;   mem_zero        - Zero memory block
;=======================================================================

;-----------------------------------------------------------------------
; str_len - Get length of null-terminated string
; Input:  ESI = pointer to string
; Output: EAX = length (not counting null terminator)
;-----------------------------------------------------------------------
str_len:
        push rsi
        push rcx
        xor eax, eax
.sl_loop:
        cmp byte [esi], 0
        je .sl_done
        inc eax
        inc esi
        jmp .sl_loop
.sl_done:
        pop rcx
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_copy - Copy null-terminated string
; Input:  ESI = source, EDI = destination
; Output: EAX = bytes copied (including null)
;-----------------------------------------------------------------------
str_copy:
        push rsi
        push rdi
        push rcx
        xor eax, eax
.sc_loop:
        mov cl, [esi]
        mov [edi], cl
        inc eax
        cmp cl, 0
        je .sc_done
        inc esi
        inc edi
        jmp .sc_loop
.sc_done:
        pop rcx
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_ncopy - Copy up to N chars, always null-terminates
; Input:  ESI = source, EDI = destination, ECX = max chars (incl. null)
; Output: EAX = bytes written (including null)
;-----------------------------------------------------------------------
str_ncopy:
        push rsi
        push rdi
        push rcx
        push rdx
        xor eax, eax
        cmp ecx, 0
        je .snc_done
        dec ecx                 ; reserve space for null
.snc_loop:
        cmp ecx, 0
        je .snc_term
        mov dl, [esi]
        cmp dl, 0
        je .snc_term
        mov [edi], dl
        inc esi
        inc edi
        inc eax
        dec ecx
        jmp .snc_loop
.snc_term:
        mov byte [edi], 0
        inc eax
.snc_done:
        pop rdx
        pop rcx
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_cat - Concatenate src onto end of dest
; Input:  ESI = source, EDI = destination (must have space)
; Output: EAX = total length of resulting string
;-----------------------------------------------------------------------
str_cat:
        push rsi
        push rdi
        push rcx
        ; Find end of dest
        xor eax, eax
.scat_find:
        cmp byte [edi], 0
        je .scat_copy
        inc edi
        inc eax
        jmp .scat_find
.scat_copy:
        mov cl, [esi]
        mov [edi], cl
        cmp cl, 0
        je .scat_done
        inc esi
        inc edi
        inc eax
        jmp .scat_copy
.scat_done:
        pop rcx
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_cmp - Case-sensitive string compare
; Input:  ESI = string1, EDI = string2
; Output: EAX = 0 if equal, <0 if s1<s2, >0 if s1>s2
;         ZF set if equal
;-----------------------------------------------------------------------
str_cmp:
        push rsi
        push rdi
.scmp_loop:
        movzx eax, byte [esi]
        movzx ecx, byte [edi]
        sub eax, ecx
        jne .scmp_done
        cmp byte [esi], 0
        je .scmp_done
        inc esi
        inc edi
        jmp .scmp_loop
.scmp_done:
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_icmp - Case-insensitive string compare
; Input:  ESI = string1, EDI = string2
; Output: EAX = 0 if equal, <0 if s1<s2, >0 if s1>s2
;         ZF set if equal
;-----------------------------------------------------------------------
str_icmp:
        push rsi
        push rdi
        push rbx
        push rdx
.sicmp_loop:
        movzx eax, byte [esi]
        movzx ebx, byte [edi]
        ; Lowercase both
        cmp al, 'A'
        jb .sicmp_noup1
        cmp al, 'Z'
        ja .sicmp_noup1
        add al, 32
.sicmp_noup1:
        cmp bl, 'A'
        jb .sicmp_noup2
        cmp bl, 'Z'
        ja .sicmp_noup2
        add bl, 32
.sicmp_noup2:
        sub eax, ebx
        jne .sicmp_done
        cmp byte [esi], 0
        je .sicmp_done
        inc esi
        inc edi
        jmp .sicmp_loop
.sicmp_done:
        pop rdx
        pop rbx
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_ncmp - Compare first N characters (case-sensitive)
; Input:  ESI = string1, EDI = string2, ECX = max chars
; Output: EAX = 0 if equal, <0 if s1<s2, >0 if s1>s2
;         ZF set if equal
;-----------------------------------------------------------------------
str_ncmp:
        push rsi
        push rdi
        push rcx
        xor eax, eax
        cmp ecx, 0
        je .sncmp_done
.sncmp_loop:
        movzx eax, byte [esi]
        movzx edx, byte [edi]
        sub eax, edx
        jne .sncmp_done
        cmp byte [esi], 0
        je .sncmp_done
        inc esi
        inc edi
        dec ecx
        jnz .sncmp_loop
        xor eax, eax
.sncmp_done:
        pop rcx
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_chr - Find first occurrence of character in string
; Input:  ESI = string, AL = character to find
; Output: EAX = pointer to character, or 0 if not found
;-----------------------------------------------------------------------
str_chr:
        push rsi
        push rcx
        mov cl, al
.schr_loop:
        mov al, [esi]
        cmp al, cl
        je .schr_found
        cmp al, 0
        je .schr_nf
        inc esi
        jmp .schr_loop
.schr_found:
        mov eax, esi
        pop rcx
        pop rsi
        ret
.schr_nf:
        xor eax, eax
        pop rcx
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_rchr - Find last occurrence of character in string
; Input:  ESI = string, AL = character to find
; Output: EAX = pointer to character, or 0 if not found
;-----------------------------------------------------------------------
str_rchr:
        push rsi
        push rcx
        push rdx
        mov cl, al
        xor edx, edx           ; last found = 0
.srchr_loop:
        mov al, [esi]
        cmp al, 0
        je .srchr_done
        cmp al, cl
        jne .srchr_next
        mov edx, esi
.srchr_next:
        inc esi
        jmp .srchr_loop
.srchr_done:
        mov eax, edx
        pop rdx
        pop rcx
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_str - Find substring in string
; Input:  ESI = haystack, EDI = needle
; Output: EAX = pointer to first match, or 0 if not found
;-----------------------------------------------------------------------
str_str:
        push rsi
        push rdi
        push rbx
        push rcx
        push rdx
        ; Get needle length
        mov ebx, edi
        xor ecx, ecx
.sstr_nlen:
        cmp byte [ebx + ecx], 0
        je .sstr_search
        inc ecx
        jmp .sstr_nlen
.sstr_search:
        cmp ecx, 0
        je .sstr_found         ; empty needle = match
.sstr_outer:
        cmp byte [esi], 0
        je .sstr_nf
        ; Compare ecx chars
        xor edx, edx
.sstr_inner:
        cmp edx, ecx
        je .sstr_found
        mov al, [esi + edx]
        cmp al, 0
        je .sstr_nf
        cmp al, [edi + edx]
        jne .sstr_advance
        inc edx
        jmp .sstr_inner
.sstr_advance:
        inc esi
        jmp .sstr_outer
.sstr_found:
        mov eax, esi
        pop rdx
        pop rcx
        pop rbx
        pop rdi
        pop rsi
        ret
.sstr_nf:
        xor eax, eax
        pop rdx
        pop rcx
        pop rbx
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_starts_with - Check if string starts with prefix
; Input:  ESI = string, EDI = prefix
; Output: EAX = 1 if yes, 0 if no
;-----------------------------------------------------------------------
str_starts_with:
        push rsi
        push rdi
.ssw_loop:
        cmp byte [edi], 0
        je .ssw_yes
        mov al, [esi]
        cmp al, 0
        je .ssw_no
        cmp al, [edi]
        jne .ssw_no
        inc esi
        inc edi
        jmp .ssw_loop
.ssw_yes:
        mov eax, 1
        pop rdi
        pop rsi
        ret
.ssw_no:
        xor eax, eax
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_ends_with - Check if string ends with suffix
; Input:  ESI = string, EDI = suffix
; Output: EAX = 1 if yes, 0 if no
;-----------------------------------------------------------------------
str_ends_with:
        push rsi
        push rdi
        push rbx
        push rcx
        ; Get string length
        push rsi
        call str_len
        mov ebx, eax           ; string len
        pop rsi
        ; Get suffix length
        push rsi
        mov esi, edi
        call str_len
        mov ecx, eax           ; suffix len
        pop rsi
        ; suffix longer than string?
        cmp ecx, ebx
        ja .sew_no
        ; Compare from offset
        add esi, ebx
        sub esi, ecx
.sew_cmp:
        cmp ecx, 0
        je .sew_yes
        mov al, [esi]
        cmp al, [edi]
        jne .sew_no
        inc esi
        inc edi
        dec ecx
        jmp .sew_cmp
.sew_yes:
        mov eax, 1
        pop rcx
        pop rbx
        pop rdi
        pop rsi
        ret
.sew_no:
        xor eax, eax
        pop rcx
        pop rbx
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_upper - Convert string to uppercase in-place
; Input:  ESI = string
; Output: none
;-----------------------------------------------------------------------
str_upper:
        push rsi
        push rax
.sup_loop:
        mov al, [esi]
        cmp al, 0
        je .sup_done
        cmp al, 'a'
        jb .sup_next
        cmp al, 'z'
        ja .sup_next
        sub al, 32
        mov [esi], al
.sup_next:
        inc esi
        jmp .sup_loop
.sup_done:
        pop rax
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_lower - Convert string to lowercase in-place
; Input:  ESI = string
; Output: none
;-----------------------------------------------------------------------
str_lower:
        push rsi
        push rax
.slo_loop:
        mov al, [esi]
        cmp al, 0
        je .slo_done
        cmp al, 'A'
        jb .slo_next
        cmp al, 'Z'
        ja .slo_next
        add al, 32
        mov [esi], al
.slo_next:
        inc esi
        jmp .slo_loop
.slo_done:
        pop rax
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_trim - Trim leading and trailing whitespace in-place
; Input:  ESI = string
; Output: ESI = adjusted start (may be same pointer)
;-----------------------------------------------------------------------
str_trim:
        call str_ltrim
        call str_rtrim
        ret

;-----------------------------------------------------------------------
; str_ltrim - Trim leading whitespace (shifts string in memory)
; Input:  ESI = string
; Output: none (modifies in-place by shifting content left)
;-----------------------------------------------------------------------
str_ltrim:
        push rax
        push rdi
        push rcx
        ; Find first non-space
        mov edi, esi
.slt_skip:
        mov al, [edi]
        cmp al, ' '
        je .slt_ws
        cmp al, 9              ; tab
        je .slt_ws
        cmp al, 10             ; LF
        je .slt_ws
        cmp al, 13             ; CR
        je .slt_ws
        jmp .slt_shift
.slt_ws:
        inc edi
        jmp .slt_skip
.slt_shift:
        cmp edi, esi
        je .slt_done           ; no leading whitespace
        ; Shift content left
        push rsi
.slt_copy:
        mov al, [edi]
        mov [esi], al
        cmp al, 0
        je .slt_shifted
        inc edi
        inc esi
        jmp .slt_copy
.slt_shifted:
        pop rsi
.slt_done:
        pop rcx
        pop rdi
        pop rax
        ret

;-----------------------------------------------------------------------
; str_rtrim - Trim trailing whitespace in-place
; Input:  ESI = string
; Output: none
;-----------------------------------------------------------------------
str_rtrim:
        push rax
        push rdi
        ; Find end
        mov edi, esi
.srt_end:
        cmp byte [edi], 0
        je .srt_trim
        inc edi
        jmp .srt_end
.srt_trim:
        cmp edi, esi
        je .srt_done
        dec edi
        mov al, [edi]
        cmp al, ' '
        je .srt_kill
        cmp al, 9
        je .srt_kill
        cmp al, 10
        je .srt_kill
        cmp al, 13
        je .srt_kill
        jmp .srt_done
.srt_kill:
        mov byte [edi], 0
        jmp .srt_trim
.srt_done:
        pop rdi
        pop rax
        ret

;-----------------------------------------------------------------------
; str_reverse - Reverse a string in-place
; Input:  ESI = string
; Output: none
;-----------------------------------------------------------------------
str_reverse:
        push rax
        push rsi
        push rdi
        ; Find end
        mov edi, esi
.srev_end:
        cmp byte [edi], 0
        je .srev_go
        inc edi
        jmp .srev_end
.srev_go:
        cmp edi, esi
        je .srev_done
        dec edi                 ; point to last char
.srev_swap:
        cmp esi, edi
        jge .srev_done
        mov al, [esi]
        mov ah, [edi]
        mov [esi], ah
        mov [edi], al
        inc esi
        dec edi
        jmp .srev_swap
.srev_done:
        pop rdi
        pop rsi
        pop rax
        ret

;-----------------------------------------------------------------------
; str_count_char - Count occurrences of a character in string
; Input:  ESI = string, AL = character
; Output: EAX = count
;-----------------------------------------------------------------------
str_count_char:
        push rsi
        push rcx
        push rdx
        mov cl, al
        xor eax, eax
.scc_loop:
        mov dl, [esi]
        cmp dl, 0
        je .scc_done
        cmp dl, cl
        jne .scc_next
        inc eax
.scc_next:
        inc esi
        jmp .scc_loop
.scc_done:
        pop rdx
        pop rcx
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_replace_char - Replace all occurrences of a char with another
; Input:  ESI = string, AL = old char, AH = new char
; Output: EAX = number of replacements
;-----------------------------------------------------------------------
str_replace_char:
        push rsi
        push rcx
        push rdx
        mov cl, al
        mov dl, ah
        xor eax, eax
.src_loop:
        cmp byte [esi], 0
        je .src_done
        cmp byte [esi], cl
        jne .src_next
        mov [esi], dl
        inc eax
.src_next:
        inc esi
        jmp .src_loop
.src_done:
        pop rdx
        pop rcx
        pop rsi
        ret

;-----------------------------------------------------------------------
; str_token - Tokenize string (strtok-like)
; First call:  ESI = string, AL = delimiter
; Next calls:  ESI = 0 (continue from last position), AL = delimiter
; Output: EAX = pointer to token, or 0 if no more tokens
;         EDI = pointer past end of token (for chaining)
; Note: Modifies string in-place (inserts nulls at delimiters)
;-----------------------------------------------------------------------
str_token:
        push rcx
        push rdx
        mov cl, al
        ; Use saved position if ESI=0
        cmp esi, 0
        jne .stok_start
        mov esi, [_str_token_pos]
        cmp esi, 0
        je .stok_none
.stok_start:
        ; Skip leading delimiters
.stok_skip:
        mov al, [esi]
        cmp al, 0
        je .stok_none
        cmp al, cl
        jne .stok_found
        inc esi
        jmp .stok_skip
.stok_found:
        mov eax, esi            ; token start
        mov edi, esi
        ; Find end of token
.stok_scan:
        mov dl, [edi]
        cmp dl, 0
        je .stok_end_str
        cmp dl, cl
        je .stok_end_tok
        inc edi
        jmp .stok_scan
.stok_end_tok:
        mov byte [edi], 0      ; null-terminate token
        inc edi
        mov [_str_token_pos], edi
        pop rdx
        pop rcx
        ret
.stok_end_str:
        mov dword [_str_token_pos], 0
        pop rdx
        pop rcx
        ret
.stok_none:
        xor eax, eax
        mov dword [_str_token_pos], 0
        pop rdx
        pop rcx
        ret

section .bss
_str_token_pos: resd 1

section .text

;-----------------------------------------------------------------------
; str_split_line - Extract next line from a buffer
; Input:  ESI = current position in buffer
;         ECX = remaining bytes
;         EDI = output line buffer
;         EDX = max line buffer size
; Output: EAX = length of line (0 if empty line, -1 if no more data)
;         ESI = updated position (past newline)
;         ECX = updated remaining bytes
;-----------------------------------------------------------------------
str_split_line:
        push rdi
        push rbx
        xor eax, eax
        cmp ecx, 0
        je .ssl_eof
.ssl_loop:
        cmp ecx, 0
        je .ssl_done
        cmp eax, edx
        jge .ssl_skip_to_nl    ; line too long, skip rest
        movzx ebx, byte [esi]
        cmp bl, 0x0A
        je .ssl_newline
        cmp bl, 0x0D
        je .ssl_cr
        mov [edi], bl
        inc edi
        inc eax
        inc esi
        dec ecx
        jmp .ssl_loop
.ssl_cr:
        ; Skip CR, check for LF
        inc esi
        dec ecx
        cmp ecx, 0
        je .ssl_done
        cmp byte [esi], 0x0A
        jne .ssl_done
        ; Fall through to handle LF
.ssl_newline:
        inc esi
        dec ecx
.ssl_done:
        mov byte [edi], 0      ; null-terminate
        pop rbx
        pop rdi
        ret
.ssl_skip_to_nl:
        cmp ecx, 0
        je .ssl_done
        cmp byte [esi], 0x0A
        je .ssl_newline
        inc esi
        dec ecx
        jmp .ssl_skip_to_nl
.ssl_eof:
        mov eax, -1
        pop rbx
        pop rdi
        ret

;-----------------------------------------------------------------------
; Character classification functions
; Input: AL = character
; Output: EAX = 1 if true, 0 if false
;-----------------------------------------------------------------------
str_to_upper_c:
        cmp al, 'a'
        jb .stuc_done
        cmp al, 'z'
        ja .stuc_done
        sub al, 32
.stuc_done:
        ret

str_to_lower_c:
        cmp al, 'A'
        jb .stlc_done
        cmp al, 'Z'
        ja .stlc_done
        add al, 32
.stlc_done:
        ret

str_is_alpha:
        cmp al, 'A'
        jb .sia_no
        cmp al, 'Z'
        jbe .sia_yes
        cmp al, 'a'
        jb .sia_no
        cmp al, 'z'
        jbe .sia_yes
.sia_no:
        xor eax, eax
        ret
.sia_yes:
        mov eax, 1
        ret

str_is_digit:
        cmp al, '0'
        jb .sid_no
        cmp al, '9'
        ja .sid_no
        mov eax, 1
        ret
.sid_no:
        xor eax, eax
        ret

str_is_alnum:
        call str_is_alpha
        cmp eax, 1
        je .sian_done
        call str_is_digit
.sian_done:
        ret

str_is_space:
        cmp al, ' '
        je .sis_yes
        cmp al, 9
        je .sis_yes
        cmp al, 10
        je .sis_yes
        cmp al, 13
        je .sis_yes
        xor eax, eax
        ret
.sis_yes:
        mov eax, 1
        ret

;-----------------------------------------------------------------------
; Memory operations
;-----------------------------------------------------------------------

;-----------------------------------------------------------------------
; mem_copy - Copy memory block (handles non-overlapping regions)
; Input:  ESI = source, EDI = destination, ECX = byte count
; Output: none
;-----------------------------------------------------------------------
mem_copy:
        push rsi
        push rdi
        push rcx
        rep movsb
        pop rcx
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; mem_set - Fill memory block with byte value
; Input:  EDI = destination, AL = fill byte, ECX = byte count
; Output: none
;-----------------------------------------------------------------------
mem_set:
        push rdi
        push rcx
        rep stosb
        pop rcx
        pop rdi
        ret

;-----------------------------------------------------------------------
; mem_cmp - Compare memory blocks
; Input:  ESI = block1, EDI = block2, ECX = byte count
; Output: EAX = 0 if equal, <0 if b1<b2, >0 if b1>b2
;         ZF set if equal
;-----------------------------------------------------------------------
mem_cmp:
        push rsi
        push rdi
        push rcx
        xor eax, eax
.mc_loop:
        cmp ecx, 0
        je .mc_done
        movzx eax, byte [esi]
        movzx edx, byte [edi]
        sub eax, edx
        jne .mc_done
        inc esi
        inc edi
        dec ecx
        jmp .mc_loop
.mc_done:
        pop rcx
        pop rdi
        pop rsi
        ret

;-----------------------------------------------------------------------
; mem_zero - Zero a memory block
; Input:  EDI = destination, ECX = byte count
; Output: none
;-----------------------------------------------------------------------
mem_zero:
        push rax
        xor al, al
        call mem_set
        pop rax
        ret
