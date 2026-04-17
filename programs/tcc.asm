; tcc.asm - Tiny C Compiler for Mellivora OS
; Compiles a minimal subset of C to flat 64-bit x86-64 binary
;
; Supported C subset:
;   - int and char variables (global and local)
;   - int main() { ... }
;   - int functions with up to 4 params
;   - if/else, while, for, do-while, break, continue
;   - switch/case statements
;   - return statement
;   - +, -, *, /, %, ==, !=, <, >, <=, >=, &&, ||, !
;   - Bitwise: &, |, ^, ~, <<, >>
;   - Ternary: a ? b : c
;   - putchar(), getchar(), exit(), puts(), strlen(),
;     atoi(), malloc(), free(), abs() builtins
;   - printf() with %d, %c, %s, %x and string literals
;   - Integer constants (decimal, 0xHex, 0Octal), char constants
;   - Assignment (=, +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=)
;   - Pointers: int *p, *p, &x
;   - // and /* */ comments
;
; Usage: tcc source.c output
;   Reads source.c, compiles to output (flat binary at 0x200000)
;
%include "syscalls.inc"

; Compiler constants
MAX_SRC         equ 16384       ; Max source size
MAX_OUT         equ 16384       ; Max output size
MAX_SYMS        equ 64          ; Max symbols
MAX_FIXUPS      equ 128         ; Max forward reference fixups
MAX_STRINGS     equ 32          ; Max string literals
SYM_NAME_LEN    equ 32
STRING_MAX_LEN  equ 80
BASE_ADDR       equ 0x200000    ; Load address of compiled binary

; Token types
TOK_EOF         equ 0
TOK_NUM         equ 1
TOK_ID          equ 2
TOK_STR         equ 3
TOK_CHAR        equ 4
TOK_PLUS        equ 10
TOK_MINUS       equ 11
TOK_STAR        equ 12
TOK_SLASH       equ 13
TOK_PERCENT     equ 14
TOK_ASSIGN      equ 15
TOK_EQ          equ 16
TOK_NE          equ 17
TOK_LT          equ 18
TOK_GT          equ 19
TOK_LE          equ 20
TOK_GE          equ 21
TOK_AND         equ 22
TOK_OR          equ 23
TOK_NOT         equ 24
TOK_SEMI        equ 30
TOK_COMMA       equ 31
TOK_LPAREN      equ 32
TOK_RPAREN      equ 33
TOK_LBRACE      equ 34
TOK_RBRACE      equ 35
TOK_IF          equ 40
TOK_ELSE        equ 41
TOK_WHILE       equ 42
TOK_FOR         equ 43
TOK_RETURN      equ 44
TOK_INT         equ 45
TOK_VOID        equ 46
TOK_PLUSEQ      equ 50
TOK_MINUSEQ     equ 51
TOK_STAREQ      equ 52
TOK_SLASHEQ     equ 53
TOK_INC         equ 54
TOK_DEC         equ 55
TOK_LBRACKET    equ 56          ; [
TOK_RBRACKET    equ 57          ; ]
TOK_AMPERSAND   equ 58          ; & (single, for address-of or bitwise)
TOK_PIPE        equ 59          ; | (single, for bitwise)
TOK_CARET       equ 60          ; ^
TOK_TILDE       equ 61          ; ~
TOK_SHL         equ 62          ; <<
TOK_SHR         equ 63          ; >>
TOK_PERCENTEQ   equ 64          ; %=
TOK_AMPEQ       equ 65          ; &=
TOK_PIPEEQ      equ 66          ; |=
TOK_CARETEQ     equ 67          ; ^=
TOK_SHLEQ       equ 68          ; <<=
TOK_SHREQ       equ 69          ; >>=
TOK_QUESTION    equ 70          ; ?
TOK_COLON       equ 71          ; :
TOK_BREAK       equ 72
TOK_CONTINUE    equ 73
TOK_DO          equ 74
TOK_SWITCH      equ 75
TOK_CASE        equ 76
TOK_DEFAULT     equ 77
TOK_CHAR_TYPE   equ 78          ; char keyword

; Symbol types
SYM_VAR         equ 1
SYM_FUNC        equ 2
SYM_LOCAL       equ 3           ; local variable, addr = signed EBP offset
SYM_PARAM       equ 4           ; function parameter, addr = positive EBP offset
SYM_ARRAY       equ 5           ; global array, addr = base address

start:
        ; Parse command line arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Parse source and dest filenames
        mov esi, args_buf
        mov edi, src_filename
        call parse_arg
        call skip_arg_spaces
        cmp byte [esi], 0
        je .usage
        mov edi, dst_filename
        call parse_arg

        ; Read source file
        mov eax, SYS_FREAD
        mov ebx, src_filename
        mov ecx, src_buffer
        int 0x80
        cmp eax, 0
        jle .file_err
        mov [src_size], eax
        mov byte [src_buffer + eax], 0  ; null-terminate

        ; Initialize compiler
        mov dword [src_pos], 0
        mov dword [out_pos], 0
        mov dword [sym_count], 0
        mov dword [fixup_count], 0
        mov dword [string_count], 0
        mov dword [local_offset], 0
        mov byte  [in_function], 0

        ; Emit program header: jump past data to main
        ; We'll fixup the main address later
        call emit_jmp_placeholder
        mov [main_fixup], eax   ; Save fixup position

        ; Print compiling message
        mov eax, SYS_PRINT
        mov ebx, msg_compiling
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, src_filename
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_arrow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, dst_filename
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; Compile
        call next_token
        call compile_program

        cmp byte [compile_error], 0
        jne .comp_err

        ; Fixup main address
        mov eax, [main_addr]
        cmp eax, 0
        je .no_main

        mov ebx, [main_fixup]
        mov ecx, eax
        sub ecx, ebx
        sub ecx, 4              ; relative jump offset
        mov [out_buffer + ebx], ecx

        ; Emit string data at end
        call emit_string_data

        ; Write output binary
        mov eax, SYS_FWRITE
        mov ebx, dst_filename
        mov ecx, out_buffer
        mov edx, [out_pos]
        mov esi, FTYPE_EXEC
        int 0x80
        cmp eax, 0
        jl .write_err

        ; Success message
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_success
        int 0x80
        mov eax, [out_pos]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_bytes
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.file_err:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_file_err
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.comp_err:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_comp_err
        int 0x80

        ; Print line number
        mov eax, SYS_PRINT
        mov ebx, msg_at_line
        int 0x80
        mov eax, [line_num]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.no_main:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_no_main
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.write_err:
        mov eax, SYS_PRINT
        mov ebx, msg_write_err
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; ARGUMENT PARSING
;=======================================================================
parse_arg:
        ; Copy word from ESI to EDI
.pa_loop:
        lodsb
        cmp al, ' '
        je .pa_done
        cmp al, 0
        je .pa_end
        stosb
        jmp .pa_loop
.pa_end:
        dec esi
.pa_done:
        mov byte [edi], 0
        ret

skip_arg_spaces:
        cmp byte [esi], ' '
        jne .sas_ret
        inc esi
        jmp skip_arg_spaces
.sas_ret:
        ret

;=======================================================================
; LEXER / TOKENIZER
;=======================================================================
next_token:
        pushad

.nt_restart:
        mov esi, src_buffer
        add esi, [src_pos]

        ; Skip whitespace
.nt_skip_ws:
        movzx eax, byte [esi]
        cmp al, ' '
        je .nt_ws
        cmp al, 9              ; tab
        je .nt_ws
        cmp al, 0x0D
        je .nt_ws
        cmp al, 0x0A
        je .nt_newline
        jmp .nt_check

.nt_ws:
        inc esi
        inc dword [src_pos]
        jmp .nt_skip_ws

.nt_newline:
        inc esi
        inc dword [src_pos]
        inc dword [line_num]
        jmp .nt_skip_ws

.nt_check:
        cmp al, 0
        je .nt_eof

        ; Comment: //
        cmp al, '/'
        jne .nt_not_comment
        cmp byte [esi + 1], '/'
        jne .nt_not_line_comment
        ; Skip to end of line
.nt_lc:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], 0x0A
        je .nt_restart
        cmp byte [esi], 0
        je .nt_eof
        jmp .nt_lc

.nt_not_line_comment:
        cmp byte [esi + 1], '*'
        jne .nt_not_comment
        ; Block comment
        add esi, 2
        add dword [src_pos], 2
.nt_bc:
        cmp byte [esi], 0
        je .nt_eof
        cmp byte [esi], 0x0A
        jne .nt_bc_no_nl
        inc dword [line_num]
.nt_bc_no_nl:
        cmp byte [esi], '*'
        jne .nt_bc_next
        cmp byte [esi + 1], '/'
        jne .nt_bc_next
        add esi, 2
        add dword [src_pos], 2
        jmp .nt_restart
.nt_bc_next:
        inc esi
        inc dword [src_pos]
        jmp .nt_bc

.nt_not_comment:
        ; Number (decimal, hex 0x..., octal 0...)
        cmp al, '0'
        jl .nt_not_num
        cmp al, '9'
        jg .nt_not_num
        ; Check for hex/octal prefix
        cmp al, '0'
        jne .nt_decimal
        ; Peek next char
        cmp byte [esi + 1], 'x'
        je .nt_hex
        cmp byte [esi + 1], 'X'
        je .nt_hex
        ; Check if octal (0 followed by digit)
        movzx eax, byte [esi + 1]
        cmp al, '0'
        jl .nt_decimal
        cmp al, '7'
        jg .nt_decimal
        ; Octal number
        inc esi
        inc dword [src_pos]
        xor edx, edx
.nt_octal:
        movzx eax, byte [esi]
        cmp al, '0'
        jl .nt_num_done
        cmp al, '7'
        jg .nt_num_done
        shl edx, 3
        sub al, '0'
        add edx, eax
        inc esi
        inc dword [src_pos]
        jmp .nt_octal
.nt_hex:
        ; Skip 0x prefix
        add esi, 2
        add dword [src_pos], 2
        xor edx, edx
.nt_hex_loop:
        movzx eax, byte [esi]
        cmp al, '0'
        jl .nt_num_done
        cmp al, '9'
        jle .nt_hex_digit
        cmp al, 'A'
        jl .nt_hex_lc
        cmp al, 'F'
        jle .nt_hex_uc
.nt_hex_lc:
        cmp al, 'a'
        jl .nt_num_done
        cmp al, 'f'
        jg .nt_num_done
        sub al, 'a' - 10
        jmp .nt_hex_add
.nt_hex_uc:
        sub al, 'A' - 10
        jmp .nt_hex_add
.nt_hex_digit:
        sub al, '0'
.nt_hex_add:
        shl edx, 4
        add edx, eax
        inc esi
        inc dword [src_pos]
        jmp .nt_hex_loop
.nt_decimal:
        ; Parse decimal number
        xor edx, edx
.nt_num:
        movzx eax, byte [esi]
        cmp al, '0'
        jl .nt_num_done
        cmp al, '9'
        jg .nt_num_done
        imul edx, 10
        sub al, '0'
        add edx, eax
        inc esi
        inc dword [src_pos]
        jmp .nt_num
.nt_num_done:
        mov [tok_type], dword TOK_NUM
        mov [tok_value], edx
        popad
        ret

.nt_not_num:
        ; Identifier or keyword
        cmp al, '_'
        je .nt_ident
        cmp al, 'a'
        jl .nt_not_ident
        cmp al, 'z'
        jle .nt_ident
.nt_not_ident_upper:
        cmp al, 'A'
        jl .nt_not_ident
        cmp al, 'Z'
        jle .nt_ident

.nt_not_ident:
        ; Character constant 'x'
        cmp al, 0x27            ; single quote
        jne .nt_not_char
        inc esi
        inc dword [src_pos]
        movzx edx, byte [esi]
        cmp dl, 0x5C            ; backslash
        jne .nt_char_ok
        ; Escape sequence
        inc esi
        inc dword [src_pos]
        movzx edx, byte [esi]
        cmp dl, 'n'
        jne .nt_esc_not_n
        mov edx, 10
        jmp .nt_char_ok
.nt_esc_not_n:
        cmp dl, 't'
        jne .nt_esc_not_t
        mov edx, 9
        jmp .nt_char_ok
.nt_esc_not_t:
        cmp dl, 'r'
        jne .nt_esc_not_r
        mov edx, 13
        jmp .nt_char_ok
.nt_esc_not_r:
        cmp dl, '0'
        jne .nt_esc_not_0
        mov edx, 0
        jmp .nt_char_ok
.nt_esc_not_0:
        cmp dl, 0x5C            ; backslash
        jne .nt_esc_not_bs
        mov edx, 0x5C
        jmp .nt_char_ok
.nt_esc_not_bs:
        cmp dl, 0x27            ; single quote
        jne .nt_esc_not_sq
        mov edx, 0x27
        jmp .nt_char_ok
.nt_esc_not_sq:
        cmp dl, 'x'
        jne .nt_char_ok         ; unknown escape, use literal
        ; Hex escape \xNN
        inc esi
        inc dword [src_pos]
        xor edx, edx
        ; First hex digit
        movzx eax, byte [esi]
        call hex_digit_val
        cmp eax, -1
        je .nt_char_ok
        mov edx, eax
        inc esi
        inc dword [src_pos]
        ; Second hex digit (optional)
        movzx eax, byte [esi]
        call hex_digit_val
        cmp eax, -1
        je .nt_char_hex_done
        shl edx, 4
        or edx, eax
        inc esi
        inc dword [src_pos]
.nt_char_hex_done:
        dec esi
        dec dword [src_pos]
.nt_char_ok:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], 0x27
        jne .nt_char_err
        inc esi
        inc dword [src_pos]
        mov [tok_type], dword TOK_CHAR
        mov [tok_value], edx
        popad
        ret
.nt_char_err:
        mov byte [compile_error], 1
        popad
        ret

.nt_not_char:
        ; String literal
        cmp al, '"'
        jne .nt_not_string
        inc esi
        inc dword [src_pos]
        mov edi, tok_string
        xor ecx, ecx
.nt_str:
        movzx eax, byte [esi]
        cmp al, '"'
        je .nt_str_done
        cmp al, 0
        je .nt_str_done
        cmp al, 0x5C            ; backslash
        jne .nt_str_normal
        ; Escape
        inc esi
        inc dword [src_pos]
        movzx eax, byte [esi]
        cmp al, 'n'
        jne .nt_str_esc_not_n
        mov al, 10
        jmp .nt_str_normal
.nt_str_esc_not_n:
        cmp al, 't'
        jne .nt_str_esc_not_t
        mov al, 9
        jmp .nt_str_normal
.nt_str_esc_not_t:
        cmp al, 'r'
        jne .nt_str_esc_not_r
        mov al, 13
        jmp .nt_str_normal
.nt_str_esc_not_r:
        cmp al, '0'
        jne .nt_str_esc_not_0
        mov al, 0
        jmp .nt_str_normal
.nt_str_esc_not_0:
        cmp al, 0x5C            ; backslash
        jne .nt_str_esc_not_bs
        mov al, 0x5C
        jmp .nt_str_normal
.nt_str_esc_not_bs:
        cmp al, '"'
        jne .nt_str_esc_not_dq
        mov al, '"'
        jmp .nt_str_normal
.nt_str_esc_not_dq:
        cmp al, 'x'
        jne .nt_str_normal       ; unknown escape, use literal
        ; Hex escape \xNN
        inc esi
        inc dword [src_pos]
        push ecx
        push edi
        movzx eax, byte [esi]
        call hex_digit_val
        cmp eax, -1
        je .nt_str_hex_restore
        mov ebx, eax
        inc esi
        inc dword [src_pos]
        movzx eax, byte [esi]
        call hex_digit_val
        cmp eax, -1
        je .nt_str_hex_one
        shl ebx, 4
        or ebx, eax
        inc esi
        inc dword [src_pos]
.nt_str_hex_one:
        mov eax, ebx
        pop edi
        pop ecx
        jmp .nt_str_store
.nt_str_hex_restore:
        pop edi
        pop ecx
        mov al, 'x'             ; restore 'x' if no valid hex
        jmp .nt_str_normal
.nt_str_normal:
.nt_str_store:
        mov [edi + ecx], al
        inc ecx
        inc esi
        inc dword [src_pos]
        jmp .nt_str
.nt_str_done:
        mov byte [edi + ecx], 0
        cmp byte [esi], '"'
        jne .nt_str_end2
        inc esi
        inc dword [src_pos]
.nt_str_end2:
        mov [tok_type], dword TOK_STR
        popad
        ret

.nt_not_string:
        ; Operators and punctuation
        cmp al, '+'
        je .nt_plus
        cmp al, '-'
        je .nt_minus
        cmp al, '*'
        je .nt_star_op
        cmp al, '/'
        je .nt_slash
        cmp al, '%'
        je .nt_percent
        cmp al, '='
        je .nt_assign
        cmp al, '!'
        je .nt_bang
        cmp al, '<'
        je .nt_lt
        cmp al, '>'
        je .nt_gt
        cmp al, '&'
        je .nt_amp
        cmp al, '|'
        je .nt_pipe
        cmp al, '^'
        je .nt_caret
        cmp al, '~'
        je .nt_tilde
        cmp al, '?'
        je .nt_question
        cmp al, ':'
        je .nt_colon
        cmp al, ';'
        je .nt_single_tok
        cmp al, ','
        je .nt_single_tok
        cmp al, '('
        je .nt_single_tok
        cmp al, ')'
        je .nt_single_tok
        cmp al, '{'
        je .nt_single_tok
        cmp al, '}'
        je .nt_single_tok
        cmp al, '['
        je .nt_single_tok
        cmp al, ']'
        je .nt_single_tok

        ; Unknown character - skip
        inc esi
        inc dword [src_pos]
        jmp .nt_restart

.nt_plus:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        je .nt_pluseq
        cmp byte [esi], '+'
        je .nt_inc
        mov dword [tok_type], TOK_PLUS
        popad
        ret
.nt_pluseq:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_PLUSEQ
        popad
        ret
.nt_inc:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_INC
        popad
        ret

.nt_minus:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        je .nt_minuseq
        cmp byte [esi], '-'
        je .nt_dec
        mov dword [tok_type], TOK_MINUS
        popad
        ret
.nt_minuseq:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_MINUSEQ
        popad
        ret
.nt_dec:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_DEC
        popad
        ret

.nt_star_op:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        je .nt_stareq
        mov dword [tok_type], TOK_STAR
        popad
        ret
.nt_stareq:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_STAREQ
        popad
        ret

.nt_slash:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        je .nt_slasheq
        mov dword [tok_type], TOK_SLASH
        popad
        ret
.nt_slasheq:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_SLASHEQ
        popad
        ret

.nt_assign:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        jne .nt_assign_single
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_EQ
        popad
        ret
.nt_assign_single:
        mov dword [tok_type], TOK_ASSIGN
        popad
        ret

.nt_bang:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        jne .nt_not_op
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_NE
        popad
        ret
.nt_not_op:
        mov dword [tok_type], TOK_NOT
        popad
        ret

.nt_lt:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        je .nt_le
        cmp byte [esi], '<'
        je .nt_shl
        mov dword [tok_type], TOK_LT
        popad
        ret
.nt_le:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_LE
        popad
        ret
.nt_shl:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        jne .nt_shl_only
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_SHLEQ
        popad
        ret
.nt_shl_only:
        mov dword [tok_type], TOK_SHL
        popad
        ret
.nt_lt_only:
        mov dword [tok_type], TOK_LT
        popad
        ret

.nt_gt:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        je .nt_ge
        cmp byte [esi], '>'
        je .nt_shr
        mov dword [tok_type], TOK_GT
        popad
        ret
.nt_ge:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_GE
        popad
        ret
.nt_shr:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        jne .nt_shr_only
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_SHREQ
        popad
        ret
.nt_shr_only:
        mov dword [tok_type], TOK_SHR
        popad
        ret

.nt_amp:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '&'
        je .nt_logical_and
        cmp byte [esi], '='
        je .nt_ampeq
        mov dword [tok_type], TOK_AMPERSAND  ; single & (address-of or bitwise AND)
        popad
        ret
.nt_logical_and:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_AND        ; && (logical AND)
        popad
        ret
.nt_ampeq:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_AMPEQ      ; &=
        popad
        ret

.nt_pipe:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '|'
        je .nt_logical_or
        cmp byte [esi], '='
        je .nt_pipeeq
        mov dword [tok_type], TOK_PIPE       ; single | (bitwise OR)
        popad
        ret
.nt_logical_or:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_OR         ; || (logical OR)
        popad
        ret
.nt_pipeeq:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_PIPEEQ     ; |=
        popad
        ret

.nt_caret:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        jne .nt_caret_only
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_CARETEQ    ; ^=
        popad
        ret
.nt_caret_only:
        mov dword [tok_type], TOK_CARET      ; ^
        popad
        ret

.nt_tilde:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_TILDE      ; ~
        popad
        ret

.nt_question:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_QUESTION   ; ?
        popad
        ret

.nt_colon:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_COLON      ; :
        popad
        ret

.nt_percent:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '='
        jne .nt_percent_only
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_PERCENTEQ  ; %=
        popad
        ret
.nt_percent_only:
        mov dword [tok_type], TOK_PERCENT    ; %
        popad
        ret

.nt_single_tok:
        ; Map character to token type
        inc esi
        inc dword [src_pos]
        cmp al, ';'
        je .nt_is_semi
        cmp al, ','
        je .nt_is_comma
        cmp al, '('
        je .nt_is_lparen
        cmp al, ')'
        je .nt_is_rparen
        cmp al, '{'
        je .nt_is_lbrace
        cmp al, '['
        je .nt_is_lbracket
        cmp al, ']'
        je .nt_is_rbracket
        mov dword [tok_type], TOK_RBRACE
        popad
        ret
.nt_is_lbracket:
        mov dword [tok_type], TOK_LBRACKET
        popad
        ret
.nt_is_rbracket:
        mov dword [tok_type], TOK_RBRACKET
        popad
        ret
.nt_is_semi:
        mov dword [tok_type], TOK_SEMI
        popad
        ret
.nt_is_comma:
        mov dword [tok_type], TOK_COMMA
        popad
        ret
.nt_is_lparen:
        mov dword [tok_type], TOK_LPAREN
        popad
        ret
.nt_is_rparen:
        mov dword [tok_type], TOK_RPAREN
        popad
        ret
.nt_is_lbrace:
        mov dword [tok_type], TOK_LBRACE
        popad
        ret

.nt_ident:
        ; Parse identifier
        mov edi, tok_ident
        xor ecx, ecx
.nt_id_loop:
        movzx eax, byte [esi]
        cmp al, '_'
        je .nt_id_char
        cmp al, '0'
        jl .nt_id_done
        cmp al, '9'
        jle .nt_id_char
        cmp al, 'A'
        jl .nt_id_done
        cmp al, 'Z'
        jle .nt_id_char
        cmp al, 'a'
        jl .nt_id_done
        cmp al, 'z'
        jg .nt_id_done
.nt_id_char:
        mov [edi + ecx], al
        inc ecx
        inc esi
        inc dword [src_pos]
        cmp ecx, SYM_NAME_LEN - 1
        jl .nt_id_loop
.nt_id_done:
        mov byte [edi + ecx], 0

        ; Check keywords
        push esi
        mov esi, tok_ident
        mov edi, kw_c_if
        call str_eq
        jc .nt_kw_if
        mov edi, kw_c_else
        call str_eq
        jc .nt_kw_else
        mov edi, kw_c_while
        call str_eq
        jc .nt_kw_while
        mov edi, kw_c_for
        call str_eq
        jc .nt_kw_for
        mov edi, kw_c_return
        call str_eq
        jc .nt_kw_return
        mov edi, kw_c_int
        call str_eq
        jc .nt_kw_int
        mov edi, kw_c_void
        call str_eq
        jc .nt_kw_void
        mov edi, kw_c_break
        call str_eq
        jc .nt_kw_break
        mov edi, kw_c_continue
        call str_eq
        jc .nt_kw_continue
        mov edi, kw_c_do
        call str_eq
        jc .nt_kw_do
        mov edi, kw_c_switch
        call str_eq
        jc .nt_kw_switch
        mov edi, kw_c_case
        call str_eq
        jc .nt_kw_case
        mov edi, kw_c_default
        call str_eq
        jc .nt_kw_default
        mov edi, kw_c_char
        call str_eq
        jc .nt_kw_char
        pop esi
        mov dword [tok_type], TOK_ID
        popad
        ret

.nt_kw_if:
        pop esi
        mov dword [tok_type], TOK_IF
        popad
        ret
.nt_kw_else:
        pop esi
        mov dword [tok_type], TOK_ELSE
        popad
        ret
.nt_kw_while:
        pop esi
        mov dword [tok_type], TOK_WHILE
        popad
        ret
.nt_kw_for:
        pop esi
        mov dword [tok_type], TOK_FOR
        popad
        ret
.nt_kw_return:
        pop esi
        mov dword [tok_type], TOK_RETURN
        popad
        ret
.nt_kw_int:
        pop esi
        mov dword [tok_type], TOK_INT
        popad
        ret
.nt_kw_void:
        pop esi
        mov dword [tok_type], TOK_VOID
        popad
        ret
.nt_kw_break:
        pop esi
        mov dword [tok_type], TOK_BREAK
        popad
        ret
.nt_kw_continue:
        pop esi
        mov dword [tok_type], TOK_CONTINUE
        popad
        ret
.nt_kw_do:
        pop esi
        mov dword [tok_type], TOK_DO
        popad
        ret
.nt_kw_switch:
        pop esi
        mov dword [tok_type], TOK_SWITCH
        popad
        ret
.nt_kw_case:
        pop esi
        mov dword [tok_type], TOK_CASE
        popad
        ret
.nt_kw_default:
        pop esi
        mov dword [tok_type], TOK_DEFAULT
        popad
        ret
.nt_kw_char:
        pop esi
        mov dword [tok_type], TOK_CHAR_TYPE
        popad
        ret

.nt_eof:
        mov [tok_type], dword TOK_EOF
        popad
        ret

;=======================================================================
; STRING COMPARE: ESI vs EDI, CF=1 if equal
;=======================================================================
str_eq:
        push eax
        push esi
        push edi
.se_loop:
        lodsb
        mov ah, [edi]
        inc edi
        cmp al, ah
        jne .se_ne
        cmp al, 0
        jne .se_loop
        pop edi
        pop esi
        pop eax
        stc
        ret
.se_ne:
        pop edi
        pop esi
        pop eax
        clc
        ret

;=======================================================================
; COMPILER: Top-level program
;=======================================================================
compile_program:
        pushad
        mov dword [main_addr], 0

.cp_loop:
        cmp dword [tok_type], TOK_EOF
        je .cp_done
        cmp byte [compile_error], 0
        jne .cp_done

        ; Global: int type declaration
        cmp dword [tok_type], TOK_INT
        je .cp_int_decl
        cmp dword [tok_type], TOK_VOID
        je .cp_void_decl
        ; Unexpected token
        mov byte [compile_error], 1
        jmp .cp_done

.cp_int_decl:
.cp_void_decl:
        call next_token
        ; Expect identifier
        cmp dword [tok_type], TOK_ID
        jne .cp_err

        ; Save name
        mov esi, tok_ident
        mov edi, temp_name
        call str_copy_local

        call next_token

        ; Function or variable?
        cmp dword [tok_type], TOK_LPAREN
        je .cp_function

        ; Global array: int name[NUM];
        cmp dword [tok_type], TOK_LBRACKET
        je .cp_array

        ; Global variable
        call add_global_var
        ; Expect ;
        cmp dword [tok_type], TOK_SEMI
        jne .cp_err
        call next_token
        jmp .cp_loop

.cp_array:
        call next_token          ; skip '['
        cmp dword [tok_type], TOK_NUM
        jne .cp_err
        mov eax, [tok_value]     ; element count
        push eax
        call next_token
        cmp dword [tok_type], TOK_RBRACKET
        jne .cp_array_err_pop
        call next_token          ; skip ']'
        pop eax
        call add_global_array
        cmp dword [tok_type], TOK_SEMI
        jne .cp_err
        call next_token
        jmp .cp_loop

.cp_array_err_pop:
        pop eax
        jmp .cp_err

.cp_function:
        call compile_function
        jmp .cp_loop

.cp_err:
        mov byte [compile_error], 1
.cp_done:
        popad
        ret

;=======================================================================
; COMPILE FUNCTION
;=======================================================================
compile_function:
        pushad
        mov byte [in_function], 1
        mov dword [local_offset], 0
        ; Save global sym boundary so locals/params are removed on exit
        mov eax, [sym_count]
        mov [global_sym_end], eax

        ; Record function address
        mov eax, [out_pos]
        mov esi, temp_name
        call add_symbol_func

        ; Check if it's main
        mov esi, temp_name
        mov edi, kw_c_main
        call str_eq
        jnc .cf_not_main
        mov eax, [out_pos]
        mov [main_addr], eax
.cf_not_main:

        ; Function prologue: push ebp; mov ebp, esp
        mov al, 0x55            ; push ebp
        call emit_byte
        mov al, 0x89
        call emit_byte
        mov al, 0xE5            ; mov ebp, esp
        call emit_byte

        ; Parse parameters: int p1, int p2, ...
        call next_token          ; skip '('
        mov dword [param_offset], 8   ; first param at [ebp+8] (4 ret + 4 saved ebp)
.cf_params:
        cmp dword [tok_type], TOK_RPAREN
        je .cf_params_done
        cmp dword [tok_type], TOK_EOF
        je .cf_err
        cmp dword [tok_type], TOK_INT
        jne .cf_err
        call next_token                 ; skip 'int'
        cmp dword [tok_type], TOK_ID
        jne .cf_err
        call add_param_sym
        add dword [param_offset], 4     ; 32-bit: 4 bytes per param
        call next_token
        cmp dword [tok_type], TOK_COMMA
        je .cf_param_comma
        cmp dword [tok_type], TOK_RPAREN
        je .cf_params_done
        jmp .cf_err
.cf_param_comma:
        call next_token
        jmp .cf_params
.cf_params_done:
        call next_token          ; skip ')'

        ; Expect '{'
        cmp dword [tok_type], TOK_LBRACE
        jne .cf_err
        call next_token

        ; Compile body
        call compile_block

        ; Function epilogue (in case no explicit return): xor eax,eax; leave; ret
        ; Note: xor eax,eax zero-extends to RAX in 64-bit mode
        mov al, 0x31
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0xC9            ; leave (= mov rsp,rbp + pop rbp in 64-bit)
        call emit_byte
        mov al, 0xC3            ; ret
        call emit_byte

        ; Remove local/param symbols from scope
        mov eax, [global_sym_end]
        mov [sym_count], eax
        mov byte [in_function], 0
        popad
        ret

.cf_err:
        mov byte [compile_error], 1
        mov eax, [global_sym_end]
        mov [sym_count], eax
        mov byte [in_function], 0
        popad
        ret

;=======================================================================
; COMPILE BLOCK (statements between { })
;=======================================================================
compile_block:
        pushad

.cb_loop:
        cmp dword [tok_type], TOK_RBRACE
        je .cb_done
        cmp dword [tok_type], TOK_EOF
        je .cb_done
        cmp byte [compile_error], 0
        jne .cb_done

        call compile_statement
        jmp .cb_loop

.cb_done:
        cmp dword [tok_type], TOK_RBRACE
        jne .cb_ret
        call next_token          ; consume '}'
.cb_ret:
        popad
        ret

;=======================================================================
; COMPILE STATEMENT
;=======================================================================
compile_statement:
        pushad

        cmp dword [tok_type], TOK_IF
        je .cs_if
        cmp dword [tok_type], TOK_WHILE
        je .cs_while
        cmp dword [tok_type], TOK_FOR
        je .cs_for
        cmp dword [tok_type], TOK_DO
        je .cs_do
        cmp dword [tok_type], TOK_SWITCH
        je .cs_switch
        cmp dword [tok_type], TOK_BREAK
        je .cs_break
        cmp dword [tok_type], TOK_CONTINUE
        je .cs_continue
        cmp dword [tok_type], TOK_RETURN
        je .cs_return
        cmp dword [tok_type], TOK_INT
        je .cs_local_var
        cmp dword [tok_type], TOK_CHAR_TYPE
        je .cs_local_var
        cmp dword [tok_type], TOK_LBRACE
        je .cs_block
        cmp dword [tok_type], TOK_SEMI
        je .cs_empty

        ; Expression statement (assignment, function call, etc.)
        call compile_expression
        ; Expect ;
        cmp dword [tok_type], TOK_SEMI
        jne .cs_err
        call next_token
        jmp .cs_done

.cs_empty:
        call next_token
        jmp .cs_done

.cs_block:
        call next_token          ; skip '{'
        call compile_block
        jmp .cs_done

.cs_if:
        call compile_if
        jmp .cs_done

.cs_while:
        call compile_while
        jmp .cs_done

.cs_for:
        call compile_for
        jmp .cs_done

.cs_do:
        call compile_do_while
        jmp .cs_done

.cs_switch:
        call compile_switch
        jmp .cs_done

.cs_break:
        call compile_break
        jmp .cs_done

.cs_continue:
        call compile_continue
        jmp .cs_done

.cs_return:
        call compile_return
        jmp .cs_done

.cs_local_var:
        call compile_local_decl
        jmp .cs_done

.cs_err:
        mov byte [compile_error], 1
.cs_done:
        popad
        ret

;=======================================================================
; COMPILE IF
;=======================================================================
compile_if:
        pushad
        call next_token          ; skip 'if'

        ; Expect '('
        cmp dword [tok_type], TOK_LPAREN
        jne .ci_err
        call next_token

        call compile_expression

        cmp dword [tok_type], TOK_RPAREN
        jne .ci_err
        call next_token

        ; Test eax: cmp eax, 0; je skip
        mov al, 0x85            ; test eax, eax
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ; je rel32
        mov al, 0x0F
        call emit_byte
        mov al, 0x84
        call emit_byte
        mov eax, [out_pos]
        push eax                ; save fixup position
        mov eax, 0
        call emit_dword          ; placeholder

        ; Compile then-body
        call compile_statement

        ; Check for else
        cmp dword [tok_type], TOK_ELSE
        jne .ci_no_else

        ; Jump past else: jmp rel32
        mov al, 0xE9
        call emit_byte
        mov eax, [out_pos]
        push eax                ; else fixup
        mov eax, 0
        call emit_dword

        ; Fixup the if-false jump to here
        pop eax                 ; else fixup pos
        pop ebx                 ; if-false fixup pos
        push eax                ; re-push else fixup
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax

        call next_token          ; skip 'else'
        call compile_statement

        ; Fixup else-end jump
        pop ebx
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax
        jmp .ci_done

.ci_no_else:
        ; Fixup if-false jump to here
        pop ebx
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax

.ci_done:
        popad
        ret

.ci_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE WHILE
;=======================================================================
compile_while:
        pushad
        call next_token          ; skip 'while'

        mov eax, [out_pos]
        push eax                ; loop start

        cmp dword [tok_type], TOK_LPAREN
        jne .cw_err
        call next_token

        call compile_expression

        cmp dword [tok_type], TOK_RPAREN
        jne .cw_err
        call next_token

        ; test eax, eax; je exit
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x84
        call emit_byte
        mov eax, [out_pos]
        push eax                ; exit fixup
        mov eax, 0
        call emit_dword

        call compile_statement

        ; Jump back to start
        pop ebx                 ; exit fixup
        pop ecx                 ; loop start
        push ebx
        ; jmp rel32 back to start
        mov al, 0xE9
        call emit_byte
        mov eax, ecx
        sub eax, [out_pos]
        sub eax, 4
        call emit_dword

        ; Fixup exit
        pop ebx
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax

        popad
        ret

.cw_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE FOR
; Supports increment clause forms: i++, i--, i += N, i -= N
;=======================================================================
compile_for:
        pushad
        call next_token          ; skip 'for'

        cmp dword [tok_type], TOK_LPAREN
        jne .cf2_err
        call next_token

        ; init clause (optional)
        cmp dword [tok_type], TOK_SEMI
        je .cf2_init_done
        call compile_expression
.cf2_init_done:
        cmp dword [tok_type], TOK_SEMI
        jne .cf2_err
        call next_token

        ; loop start at condition
        mov eax, [out_pos]
        push eax                 ; loop start

        ; condition clause (optional => true)
        cmp dword [tok_type], TOK_SEMI
        jne .cf2_cond_expr
        mov al, 0xB8             ; mov eax, 1
        call emit_byte
        mov eax, 1
        call emit_dword
        jmp .cf2_cond_done
.cf2_cond_expr:
        call compile_expression
.cf2_cond_done:

        cmp dword [tok_type], TOK_SEMI
        jne .cf2_err_pop1
        call next_token

        ; Parse increment clause (optional)
        mov dword [for_inc_kind], 0
        cmp dword [tok_type], TOK_RPAREN
        je .cf2_header_done
        cmp dword [tok_type], TOK_ID
        jne .cf2_err_pop1

        mov esi, tok_ident
        call find_symbol
        cmp eax, 0
        je .cf2_err_pop1
        mov eax, [symbol_type]
        mov [for_var_type], eax
        mov eax, [symbol_addr]
        mov [for_var_addr], eax

        call next_token
        cmp dword [tok_type], TOK_INC
        je .cf2_inc_pp
        cmp dword [tok_type], TOK_DEC
        je .cf2_inc_mm
        cmp dword [tok_type], TOK_PLUSEQ
        je .cf2_inc_peq
        cmp dword [tok_type], TOK_MINUSEQ
        je .cf2_inc_meq
        jmp .cf2_err_pop1

.cf2_inc_pp:
        mov dword [for_inc_kind], 1
        mov dword [for_inc_value], 1
        call next_token
        jmp .cf2_header_done

.cf2_inc_mm:
        mov dword [for_inc_kind], 2
        mov dword [for_inc_value], 1
        call next_token
        jmp .cf2_header_done

.cf2_inc_peq:
        call next_token
        cmp dword [tok_type], TOK_NUM
        jne .cf2_err_pop1
        mov dword [for_inc_kind], 3
        mov eax, [tok_value]
        mov [for_inc_value], eax
        call next_token
        jmp .cf2_header_done

.cf2_inc_meq:
        call next_token
        cmp dword [tok_type], TOK_NUM
        jne .cf2_err_pop1
        mov dword [for_inc_kind], 4
        mov eax, [tok_value]
        mov [for_inc_value], eax
        call next_token

.cf2_header_done:
        cmp dword [tok_type], TOK_RPAREN
        jne .cf2_err_pop1
        call next_token

        ; test eax,eax ; je exit
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x84
        call emit_byte
        mov eax, [out_pos]
        push eax                 ; exit fixup
        mov eax, 0
        call emit_dword

        call compile_statement

        ; Emit increment clause
        cmp dword [for_inc_kind], 0
        je .cf2_no_inc

        cmp dword [for_var_type], SYM_LOCAL
        je .cf2_load_rbp
        cmp dword [for_var_type], SYM_PARAM
        je .cf2_load_rbp
        ; Global load: mov rbx, addr; mov rax, [rbx]
        call emit_rex_w
        mov al, 0xBB            ; mov rbx, imm32
        call emit_byte
        mov eax, [for_var_addr]
        call emit_dword
        call emit_rex_w
        mov al, 0x8B            ; mov rax, [rbx]
        call emit_byte
        mov al, 0x03
        call emit_byte
        jmp .cf2_loaded

.cf2_load_rbp:
        call emit_rex_w
        mov al, 0x8B            ; mov rax, [rbp+off8]
        call emit_byte
        mov al, 0x45
        call emit_byte
        mov eax, [for_var_addr]
        call emit_byte

.cf2_loaded:
        cmp dword [for_inc_kind], 1
        je .cf2_add_one
        cmp dword [for_inc_kind], 2
        je .cf2_sub_one
        cmp dword [for_inc_kind], 3
        je .cf2_add_imm
        cmp dword [for_inc_kind], 4
        je .cf2_sub_imm
        jmp .cf2_no_inc

.cf2_add_one:
        call emit_rex_w
        mov al, 0x83            ; add rax, 1
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 1
        call emit_byte
        jmp .cf2_store

.cf2_sub_one:
        call emit_rex_w
        mov al, 0x83            ; sub rax, 1
        call emit_byte
        mov al, 0xE8
        call emit_byte
        mov al, 1
        call emit_byte
        jmp .cf2_store

.cf2_add_imm:
        call emit_rex_w
        mov al, 0x05            ; add rax, imm32
        call emit_byte
        mov eax, [for_inc_value]
        call emit_dword
        jmp .cf2_store

.cf2_sub_imm:
        call emit_rex_w
        mov al, 0x2D            ; sub rax, imm32
        call emit_byte
        mov eax, [for_inc_value]
        call emit_dword

.cf2_store:
        cmp dword [for_var_type], SYM_LOCAL
        je .cf2_store_rbp
        cmp dword [for_var_type], SYM_PARAM
        je .cf2_store_rbp
        ; Global store: mov rbx, addr; mov [rbx], rax
        call emit_rex_w
        mov al, 0xBB            ; mov rbx, imm32
        call emit_byte
        mov eax, [for_var_addr]
        call emit_dword
        call emit_rex_w
        mov al, 0x89            ; mov [rbx], rax
        call emit_byte
        mov al, 0x03
        call emit_byte
        jmp .cf2_no_inc

.cf2_store_rbp:
        call emit_rex_w
        mov al, 0x89            ; mov [rbp+off8], rax
        call emit_byte
        mov al, 0x45
        call emit_byte
        mov eax, [for_var_addr]
        call emit_byte

.cf2_no_inc:
        ; jmp loop start
        pop ebx                  ; exit fixup
        pop ecx                  ; loop start
        push ebx
        mov al, 0xE9
        call emit_byte
        mov eax, ecx
        sub eax, [out_pos]
        sub eax, 4
        call emit_dword

        ; fixup exit
        pop ebx
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax

        popad
        ret

.cf2_err_pop1:
        pop eax
.cf2_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE DO-WHILE
; do { body } while (cond);
;=======================================================================
compile_do_while:
        pushad
        call next_token          ; skip 'do'

        ; Save current loop context
        mov eax, [loop_start]
        push eax
        mov eax, [loop_exit]
        push eax

        mov eax, [out_pos]
        mov [loop_start], eax    ; loop start for continue

        ; Placeholder for break exit - will be set after condition
        mov dword [loop_exit], 0

        call compile_statement   ; compile body

        ; Expect 'while'
        cmp dword [tok_type], TOK_WHILE
        jne .cdw_err
        call next_token

        ; Expect '('
        cmp dword [tok_type], TOK_LPAREN
        jne .cdw_err
        call next_token

        call compile_expression

        cmp dword [tok_type], TOK_RPAREN
        jne .cdw_err
        call next_token

        ; Expect ';'
        cmp dword [tok_type], TOK_SEMI
        jne .cdw_err
        call next_token

        ; test eax, eax; jnz loop_start
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x85             ; jnz
        call emit_byte
        mov eax, [loop_start]
        sub eax, [out_pos]
        sub eax, 4
        call emit_dword

        ; Record exit point for any break fixups
        mov eax, [out_pos]
        mov ecx, [break_fixup_count]
.cdw_fix_breaks:
        cmp ecx, 0
        je .cdw_fix_done
        dec ecx
        mov ebx, [break_fixups + ecx * 4]
        mov edx, eax
        sub edx, ebx
        sub edx, 4
        mov [out_buffer + ebx], edx
        jmp .cdw_fix_breaks
.cdw_fix_done:
        mov dword [break_fixup_count], 0

        ; Restore loop context
        pop eax
        mov [loop_exit], eax
        pop eax
        mov [loop_start], eax

        popad
        ret

.cdw_err:
        pop eax
        mov [loop_exit], eax
        pop eax
        mov [loop_start], eax
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE BREAK
;=======================================================================
compile_break:
        pushad
        call next_token          ; skip 'break'

        cmp dword [tok_type], TOK_SEMI
        jne .cb_err
        call next_token

        ; Emit jump placeholder for break
        mov al, 0xE9             ; jmp rel32
        call emit_byte
        mov eax, [out_pos]
        ; Record fixup position
        mov ecx, [break_fixup_count]
        mov [break_fixups + ecx * 4], eax
        inc dword [break_fixup_count]
        mov eax, 0
        call emit_dword

        popad
        ret

.cb_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE CONTINUE
;=======================================================================
compile_continue:
        pushad
        call next_token          ; skip 'continue'

        cmp dword [tok_type], TOK_SEMI
        jne .cc_err
        call next_token

        ; Jump to loop_start
        mov eax, [loop_start]
        cmp eax, 0
        je .cc_err               ; continue outside loop

        mov al, 0xE9             ; jmp rel32
        call emit_byte
        mov eax, [loop_start]
        sub eax, [out_pos]
        sub eax, 4
        call emit_dword

        popad
        ret

.cc_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE SWITCH
; switch (expr) { case N: ... default: ... }
;=======================================================================
compile_switch:
        pushad
        call next_token          ; skip 'switch'

        ; Save current loop context (for break)
        mov eax, [loop_exit]
        push eax
        mov eax, [break_fixup_count]
        push eax
        mov dword [break_fixup_count], 0

        cmp dword [tok_type], TOK_LPAREN
        jne .csw_err
        call next_token

        call compile_expression  ; switch value in RAX

        cmp dword [tok_type], TOK_RPAREN
        jne .csw_err
        call next_token

        ; Save switch value: push rax
        mov al, 0x50
        call emit_byte

        cmp dword [tok_type], TOK_LBRACE
        jne .csw_err
        call next_token

        ; Parse cases
        mov dword [switch_case_count], 0

.csw_cases:
        cmp dword [tok_type], TOK_RBRACE
        je .csw_end
        cmp dword [tok_type], TOK_EOF
        je .csw_err
        cmp byte [compile_error], 0
        jne .csw_err

        cmp dword [tok_type], TOK_CASE
        je .csw_case
        cmp dword [tok_type], TOK_DEFAULT
        je .csw_default

        ; Statement in case body
        call compile_statement
        jmp .csw_cases

.csw_case:
        call next_token          ; skip 'case'
        cmp dword [tok_type], TOK_NUM
        jne .csw_err
        mov edx, [tok_value]     ; case value
        call next_token
        cmp dword [tok_type], TOK_COLON
        jne .csw_err
        call next_token

        ; Emit: cmp [rsp], edx; jne next_case
        ; Load switch value: mov rax, [rsp]
        call emit_rex_w
        mov al, 0x8B
        call emit_byte
        mov al, 0x04             ; mov rax, [rsp]
        call emit_byte
        mov al, 0x24
        call emit_byte
        ; cmp rax, imm32
        call emit_rex_w
        mov al, 0x3D             ; cmp rax, imm32
        call emit_byte
        mov eax, edx
        call emit_dword
        ; je case_body (skip the jmp)
        mov al, 0x74             ; je rel8
        call emit_byte
        mov al, 5                ; skip jmp rel32
        call emit_byte
        ; jmp next_case (placeholder)
        mov al, 0xE9
        call emit_byte
        mov eax, [out_pos]
        mov ecx, [switch_case_count]
        mov [switch_case_fixups + ecx * 4], eax
        inc dword [switch_case_count]
        mov eax, 0
        call emit_dword

        jmp .csw_cases

.csw_default:
        call next_token          ; skip 'default'
        cmp dword [tok_type], TOK_COLON
        jne .csw_err
        call next_token
        ; Patch previous case jumps to skip to here
        mov ecx, [switch_case_count]
.csw_patch_cases:
        cmp ecx, 0
        je .csw_patch_done
        dec ecx
        mov ebx, [switch_case_fixups + ecx * 4]
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax
        jmp .csw_patch_cases
.csw_patch_done:
        mov dword [switch_case_count], 0
        jmp .csw_cases

.csw_end:
        call next_token          ; skip '}'

        ; Patch remaining case jumps to exit
        mov ecx, [switch_case_count]
.csw_patch_exit:
        cmp ecx, 0
        je .csw_patch_exit_done
        dec ecx
        mov ebx, [switch_case_fixups + ecx * 4]
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax
        jmp .csw_patch_exit
.csw_patch_exit_done:

        ; Pop switch value: add rsp, 8
        call emit_rex_w
        mov al, 0x83
        call emit_byte
        mov al, 0xC4
        call emit_byte
        mov al, 8
        call emit_byte

        ; Patch break jumps
        mov ecx, [break_fixup_count]
.csw_fix_breaks:
        cmp ecx, 0
        je .csw_breaks_done
        dec ecx
        mov ebx, [break_fixups + ecx * 4]
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax
        jmp .csw_fix_breaks
.csw_breaks_done:

        ; Restore context
        pop eax
        mov [break_fixup_count], eax
        pop eax
        mov [loop_exit], eax

        popad
        ret

.csw_err:
        pop eax
        mov [break_fixup_count], eax
        pop eax
        mov [loop_exit], eax
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE RETURN
;=======================================================================
compile_return:
        pushad
        call next_token          ; skip 'return'

        cmp dword [tok_type], TOK_SEMI
        je .cr_void

        call compile_expression

.cr_void:
        ; leave; ret
        mov al, 0xC9
        call emit_byte
        mov al, 0xC3
        call emit_byte

        cmp dword [tok_type], TOK_SEMI
        jne .cr_done
        call next_token
.cr_done:
        popad
        ret

;=======================================================================
; COMPILE LOCAL VAR DECLARATION
;=======================================================================
compile_local_decl:
        pushad
        call next_token          ; skip 'int'

        cmp dword [tok_type], TOK_ID
        jne .cld_err

        ; Add as local variable (using stack offset from ebp)
        sub dword [local_offset], 4     ; 32-bit: 4 bytes per local
        call add_local_sym      ; register in symbol table using tok_ident

        ; sub esp, 4
        mov al, 0x83
        call emit_byte
        mov al, 0xEC
        call emit_byte
        mov al, 4
        call emit_byte

        call next_token

        ; Check for initializer
        cmp dword [tok_type], TOK_ASSIGN
        jne .cld_no_init

        call next_token
        call compile_expression

        ; mov [ebp + offset], eax
        mov al, 0x89
        call emit_byte
        mov al, 0x45
        call emit_byte
        mov eax, [local_offset]
        call emit_byte

.cld_no_init:
        cmp dword [tok_type], TOK_SEMI
        jne .cld_err
        call next_token          ; consume variable identifier

        popad
        ret

.cld_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE EXPRESSION (returns result in EAX)
;
; Precedence-climbing parser (C operator precedence, left-to-right):
;   compile_expression  -> assignment (=) or fall through to or_expr
;   compile_or_expr     -> ||        (left-assoc)
;   compile_and_expr    -> &&        (left-assoc)
;   compile_eq_expr     -> == !=     (left-assoc)
;   compile_rel_expr    -> < > <= >= (left-assoc)
;   compile_add_expr    -> + -       (left-assoc)
;   compile_mul_expr    -> * / %     (left-assoc)
;   compile_unary       -> ! - (prefix)
;   compile_primary     -> number, char, string, ( ), id, call, array
;=======================================================================
compile_expression:
        pushad

        ; Check for identifier (could be assignment or function call)
        cmp dword [tok_type], TOK_ID
        jne .ce_not_assign

        ; Save identifier
        mov esi, tok_ident
        mov edi, expr_name
        call str_copy_local

        ; Save scanner position before lookahead so non-assignment
        ; identifiers (like function calls) can be reparsed correctly.
        mov eax, [src_pos]
        mov [expr_probe_pos], eax

        call next_token

        ; Check for assignment
        cmp dword [tok_type], TOK_ASSIGN
        je .ce_assign

        ; Check for array subscript followed by assignment: arr[i] = val
        cmp dword [tok_type], TOK_LBRACKET
        je .ce_check_arr_assign

        ; Not an assignment - undo the token consumption so or_expr
        ; can parse the identifier properly.  Push back by resetting
        ; src_pos to the start of the identifier.
        ; We don't have a true "unget" so we copy expr_name back into
        ; tok_ident, set tok_type = TOK_ID and let primary pick it up.
        mov eax, [expr_probe_pos]
        mov [src_pos], eax
        mov esi, expr_name
        mov edi, tok_ident
        call str_copy_local
        mov dword [tok_type], TOK_ID
        jmp .ce_not_assign

.ce_assign:
        ; Resolve target variable BEFORE compiling RHS, because
        ; the recursive compile_expression will clobber expr_name
        mov esi, expr_name
        call find_symbol
        push dword [symbol_type]   ; save type
        push dword [symbol_addr]   ; save addr/offset

        call next_token
        call compile_expression

        pop eax                    ; addr / RBP offset
        pop ebx                    ; type
        cmp ebx, SYM_LOCAL
        je .ce_store_rbp
        cmp ebx, SYM_PARAM
        je .ce_store_rbp
        ; Global: mov rbx, addr; mov [rbx], rax (64-bit)
        push eax                   ; preserve abs addr
        call emit_rex_w
        mov al, 0xBB               ; mov rbx, imm32 (sign-extended in 64-bit)
        call emit_byte
        pop eax
        call emit_dword
        ; mov [rbx], rax (REX.W + 89 03)
        call emit_rex_w
        mov al, 0x89
        call emit_byte
        mov al, 0x03
        call emit_byte
        popad
        ret
.ce_store_rbp:
        ; Local/param: mov [rbp + off8], rax (REX.W + 89 45 off8)
        push eax                   ; save RBP offset
        call emit_rex_w
        mov al, 0x89
        call emit_byte
        mov al, 0x45
        call emit_byte
        pop eax
        call emit_byte             ; emit offset byte
        popad
        ret

.ce_check_arr_assign:
        ; arr[i] - might be arr[i] = val (store) or arr[i] (load in expr)
        ; We must handle both here since we already consumed the identifier.
        mov esi, expr_name
        call find_symbol
        cmp eax, 0
        je .ce_arr_err
        push dword [symbol_addr]   ; push array base address
        call next_token            ; skip '['
        call compile_expression    ; compile index -> RAX at runtime
        cmp dword [tok_type], TOK_RBRACKET
        jne .ce_arr_err2
        call next_token            ; skip ']'
        ; Check for store: arr[i] = val
        cmp dword [tok_type], TOK_ASSIGN
        je .ce_arr_store
        ; Array load: EAX = arr[index]
        ; imul eax,eax,4
        call emit_rex_w
        mov al, 0x6B
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 4                  ; 4 bytes per int element
        call emit_byte
        ; add rax, base (REX.W + 05 imm32)
        call emit_rex_w
        mov al, 0x05
        call emit_byte
        pop eax                    ; base address
        call emit_dword
        ; mov rax, [rax] (REX.W + 8B 00)
        call emit_rex_w
        mov al, 0x8B
        call emit_byte
        mov al, 0x00
        call emit_byte
        ; Continue parsing binary operators at correct precedence
        jmp .ce_arr_load_binop
.ce_arr_store:
        ; arr[i] = val: compute element address, store
        ; imul eax,eax,4
        call emit_rex_w
        mov al, 0x6B
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 4                  ; 4 bytes per int element
        call emit_byte
        ; add rax, base (REX.W + 05 imm32)
        call emit_rex_w
        mov al, 0x05
        call emit_byte
        pop eax                    ; base address
        call emit_dword
        ; push rax (element address) (50)
        mov al, 0x50
        call emit_byte
        ; compile RHS
        call next_token            ; skip '='
        call compile_expression    ; result in RAX
        ; pop rcx; mov [rcx], rax (REX.W + 59 89 01)
        mov al, 0x59               ; pop rcx
        call emit_byte
        call emit_rex_w
        mov al, 0x89               ; mov [rcx], rax
        call emit_byte
        mov al, 0x01
        call emit_byte
        popad
        ret
.ce_arr_err2:
        pop eax                    ; clean up base addr
.ce_arr_err:
        mov byte [compile_error], 1
        popad
        ret

.ce_arr_load_binop:
        ; After array load, we have the value in EAX at codegen level.
        ; Continue with binary operators from the add level (most common
        ; after array access), but actually we should just return and let
        ; the caller's precedence loop handle it.
        popad
        ret

.ce_not_assign:
        ; Parse expression with proper precedence
        call compile_or_expr
        popad
        ret

;-----------------------------------------------------------------------
; Precedence level 1: || (logical OR)
;-----------------------------------------------------------------------
compile_or_expr:
        pushad
        call compile_and_expr
.or_loop:
        cmp dword [tok_type], TOK_OR
        jne .or_check_ternary
        call next_token
        ; push rax (left)
        mov al, 0x50
        call emit_byte
        call compile_and_expr
        ; pop rbx; or rax, rbx (64-bit)
        mov al, 0x5B             ; pop rbx
        call emit_byte
        call emit_rex_w
        mov al, 0x09             ; or rax, rbx
        call emit_byte
        mov al, 0xD8
        call emit_byte
        jmp .or_loop

.or_check_ternary:
        ; Check for ternary operator: expr ? true_expr : false_expr
        cmp dword [tok_type], TOK_QUESTION
        jne .or_done
        call next_token          ; skip '?'
        ; test rax, rax; jz false_branch
        call emit_rex_w
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x84             ; jz rel32
        call emit_byte
        mov eax, [out_pos]
        push eax                 ; save false branch fixup
        mov eax, 0
        call emit_dword
        ; Compile true expression
        call compile_or_expr
        ; jmp end
        mov al, 0xE9
        call emit_byte
        mov eax, [out_pos]
        push eax                 ; save end fixup
        mov eax, 0
        call emit_dword
        ; Fixup false branch
        pop edx                  ; end fixup
        pop ebx                  ; false branch fixup
        push edx                 ; re-push end fixup
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax
        ; Expect ':'
        cmp dword [tok_type], TOK_COLON
        jne .or_done             ; error, but let it slide
        call next_token
        ; Compile false expression
        call compile_or_expr
        ; Fixup end
        pop ebx
        mov eax, [out_pos]
        sub eax, ebx
        sub eax, 4
        mov [out_buffer + ebx], eax

.or_done:
        popad
        ret

;-----------------------------------------------------------------------
; Precedence level 2: && (logical AND)
;-----------------------------------------------------------------------
compile_and_expr:
        pushad
        call compile_bitor_expr
.and_loop:
        cmp dword [tok_type], TOK_AND
        jne .and_done
        call next_token
        ; push rax (left)
        mov al, 0x50
        call emit_byte
        call compile_bitor_expr
        ; Logical AND: convert both to boolean, then AND
        ; test rax, rax; setne al; movzx eax, al
        call emit_rex_w
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x95             ; setne al
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ; movzx ecx, al
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC8
        call emit_byte
        ; pop rax; test rax, rax; setne al; movzx eax, al; and eax, ecx
        mov al, 0x58
        call emit_byte
        call emit_rex_w
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x95
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x21             ; and eax, ecx
        call emit_byte
        mov al, 0xC8
        call emit_byte
        jmp .and_loop
.and_done:
        popad
        ret

;-----------------------------------------------------------------------
; Precedence level 3: | (bitwise OR)
;-----------------------------------------------------------------------
compile_bitor_expr:
        pushad
        call compile_bitxor_expr
.bitor_loop:
        cmp dword [tok_type], TOK_PIPE
        jne .bitor_done
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_bitxor_expr
        mov al, 0x5B             ; pop rbx
        call emit_byte
        call emit_rex_w
        mov al, 0x09             ; or rax, rbx
        call emit_byte
        mov al, 0xD8
        call emit_byte
        jmp .bitor_loop
.bitor_done:
        popad
        ret

;-----------------------------------------------------------------------
; Precedence level 4: ^ (bitwise XOR)
;-----------------------------------------------------------------------
compile_bitxor_expr:
        pushad
        call compile_bitand_expr
.bitxor_loop:
        cmp dword [tok_type], TOK_CARET
        jne .bitxor_done
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_bitand_expr
        mov al, 0x5B             ; pop rbx
        call emit_byte
        call emit_rex_w
        mov al, 0x31             ; xor rax, rbx
        call emit_byte
        mov al, 0xD8
        call emit_byte
        jmp .bitxor_loop
.bitxor_done:
        popad
        ret

;-----------------------------------------------------------------------
; Precedence level 5: & (bitwise AND)
;-----------------------------------------------------------------------
compile_bitand_expr:
        pushad
        call compile_eq_expr
.bitand_loop:
        cmp dword [tok_type], TOK_AMPERSAND
        jne .bitand_done
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_eq_expr
        mov al, 0x5B             ; pop rbx
        call emit_byte
        call emit_rex_w
        mov al, 0x21             ; and rax, rbx
        call emit_byte
        mov al, 0xD8
        call emit_byte
        jmp .bitand_loop
.bitand_done:
        popad
        ret

;-----------------------------------------------------------------------
; Precedence level 6: == !=
;-----------------------------------------------------------------------
compile_eq_expr:
        pushad
        call compile_rel_expr
.eq_loop:
        cmp dword [tok_type], TOK_EQ
        je .eq_op
        cmp dword [tok_type], TOK_NE
        je .ne_op
        jmp .eq_done

.eq_op:
        call cmp_emit_push_rhs_cmp
        ; sete al; movzx eax, al
        mov al, 0x0F
        call emit_byte
        mov al, 0x94
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call cmp_emit_movzx
        jmp .eq_loop

.ne_op:
        call cmp_emit_push_rhs_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x95
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call cmp_emit_movzx
        jmp .eq_loop

.eq_done:
        popad
        ret

;-----------------------------------------------------------------------
; Precedence level 7: < > <= >=
;-----------------------------------------------------------------------
compile_rel_expr:
        pushad
        call compile_shift_expr
.rel_loop:
        cmp dword [tok_type], TOK_LT
        je .lt_op
        cmp dword [tok_type], TOK_GT
        je .gt_op
        cmp dword [tok_type], TOK_LE
        je .le_op
        cmp dword [tok_type], TOK_GE
        je .ge_op
        jmp .rel_done

.lt_op:
        call cmp_emit_push_rhs_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x9C
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call cmp_emit_movzx
        jmp .rel_loop

.gt_op:
        call cmp_emit_push_rhs_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x9F
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call cmp_emit_movzx
        jmp .rel_loop

.le_op:
        call cmp_emit_push_rhs_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x9E
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call cmp_emit_movzx
        jmp .rel_loop

.ge_op:
        call cmp_emit_push_rhs_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x9D
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call cmp_emit_movzx
        jmp .rel_loop

.rel_done:
        popad
        ret

; Helper: emit push rax, call next_level, pop + cmp (64-bit)
; Used by eq_expr and rel_expr
cmp_emit_push_rhs_cmp:
        call next_token
        mov al, 0x50             ; push rax
        call emit_byte
        call compile_shift_expr
        call emit_rex_w          ; REX.W for mov rcx, rax
        mov al, 0x89
        call emit_byte
        mov al, 0xC1
        call emit_byte
        mov al, 0x58             ; pop rax
        call emit_byte
        call emit_rex_w          ; REX.W for cmp rax, rcx
        mov al, 0x39
        call emit_byte
        mov al, 0xC8
        call emit_byte
        ret

; Helper: emit movzx rax, al (64-bit)
cmp_emit_movzx:
        call emit_rex_w
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ret

;-----------------------------------------------------------------------
; Precedence level 8: << >> (shift)
;-----------------------------------------------------------------------
compile_shift_expr:
        pushad
        call compile_add_expr
.shift_loop:
        cmp dword [tok_type], TOK_SHL
        je .shl_op
        cmp dword [tok_type], TOK_SHR
        je .shr_op
        jmp .shift_done

.shl_op:
        call next_token
        mov al, 0x50             ; push rax (left)
        call emit_byte
        call compile_add_expr
        ; mov rcx, rax; pop rax; shl rax, cl
        call emit_rex_w
        mov al, 0x89
        call emit_byte
        mov al, 0xC1             ; mov rcx, rax
        call emit_byte
        mov al, 0x58             ; pop rax
        call emit_byte
        call emit_rex_w
        mov al, 0xD3             ; shl rax, cl
        call emit_byte
        mov al, 0xE0
        call emit_byte
        jmp .shift_loop

.shr_op:
        call next_token
        mov al, 0x50             ; push rax (left)
        call emit_byte
        call compile_add_expr
        ; mov rcx, rax; pop rax; sar rax, cl (arithmetic shift)
        call emit_rex_w
        mov al, 0x89
        call emit_byte
        mov al, 0xC1             ; mov rcx, rax
        call emit_byte
        mov al, 0x58             ; pop rax
        call emit_byte
        call emit_rex_w
        mov al, 0xD3             ; sar rax, cl
        call emit_byte
        mov al, 0xF8
        call emit_byte
        jmp .shift_loop

.shift_done:
        popad
        ret

;-----------------------------------------------------------------------
; Precedence level 9: + -  (left-to-right)
;-----------------------------------------------------------------------
compile_add_expr:
        pushad
        call compile_mul_expr
.add_loop:
        cmp dword [tok_type], TOK_PLUS
        je .add_op
        cmp dword [tok_type], TOK_MINUS
        je .sub_op
        jmp .add_done

.add_op:
        call next_token
        ; push rax
        mov al, 0x50
        call emit_byte
        call compile_mul_expr
        ; pop rbx; add rax, rbx (64-bit)
        mov al, 0x5B             ; pop rbx
        call emit_byte
        call emit_rex_w          ; REX.W for 64-bit add
        mov al, 0x01             ; add rax, rbx
        call emit_byte
        mov al, 0xD8
        call emit_byte
        jmp .add_loop

.sub_op:
        call next_token
        ; push rax (left side)
        mov al, 0x50
        call emit_byte
        call compile_mul_expr
        ; Result in rax = right. pop rbx = left. Want left - right.
        ; mov rcx, rax; pop rax; sub rax, rcx (64-bit)
        call emit_rex_w          ; REX.W for mov rcx, rax
        mov al, 0x89
        call emit_byte
        mov al, 0xC1
        call emit_byte
        mov al, 0x58             ; pop rax
        call emit_byte
        call emit_rex_w          ; REX.W for sub rax, rcx
        mov al, 0x29
        call emit_byte
        mov al, 0xC8
        call emit_byte
        jmp .add_loop

.add_done:
        popad
        ret

;-----------------------------------------------------------------------
; Precedence level 6: * / %  (left-to-right)
;-----------------------------------------------------------------------
compile_mul_expr:
        pushad
        call compile_unary
.mul_loop:
        cmp dword [tok_type], TOK_STAR
        je .mul_op
        cmp dword [tok_type], TOK_SLASH
        je .div_op
        cmp dword [tok_type], TOK_PERCENT
        je .mod_op
        jmp .mul_done

.mul_op:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_unary
        ; pop rbx; imul rax, rbx (64-bit)
        mov al, 0x5B
        call emit_byte
        call emit_rex_w          ; REX.W for 64-bit imul
        mov al, 0x0F
        call emit_byte
        mov al, 0xAF
        call emit_byte
        mov al, 0xC3
        call emit_byte
        jmp .mul_loop

.div_op:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_unary
        ; mov rcx, rax; pop rax; cqo; idiv rcx (64-bit)
        call emit_rex_w
        mov al, 0x89
        call emit_byte
        mov al, 0xC1
        call emit_byte
        mov al, 0x58             ; pop rax
        call emit_byte
        call emit_rex_w          ; REX.W for cqo (64-bit sign extend)
        mov al, 0x99             ; cqo
        call emit_byte
        call emit_rex_w          ; REX.W for 64-bit idiv
        mov al, 0xF7             ; idiv rcx
        call emit_byte
        mov al, 0xF9
        call emit_byte
        jmp .mul_loop

.mod_op:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_unary
        ; mov rcx, rax; pop rax; cqo; idiv rcx; mov rax, rdx (remainder) (64-bit)
        call emit_rex_w
        mov al, 0x89
        call emit_byte
        mov al, 0xC1
        call emit_byte
        mov al, 0x58             ; pop rax
        call emit_byte
        call emit_rex_w          ; REX.W for cqo
        mov al, 0x99             ; cqo
        call emit_byte
        call emit_rex_w          ; REX.W for 64-bit idiv
        mov al, 0xF7             ; idiv rcx
        call emit_byte
        mov al, 0xF9
        call emit_byte
        ; mov rax, rdx (REX.W + 89 D0)
        call emit_rex_w
        mov al, 0x89
        call emit_byte
        mov al, 0xD0
        call emit_byte
        jmp .mul_loop

.mul_done:
        popad
        ret

;-----------------------------------------------------------------------
; Unary: !, ~, and unary minus
;-----------------------------------------------------------------------
compile_unary:
        pushad

        cmp dword [tok_type], TOK_NOT
        je .un_not
        cmp dword [tok_type], TOK_MINUS
        je .un_neg
        cmp dword [tok_type], TOK_TILDE
        je .un_bitnot

        ; Not a unary operator - fall through to primary
        call compile_primary
        popad
        ret

.un_not:
        call next_token
        call compile_unary
        ; test rax, rax; sete al; movzx rax, al (64-bit)
        call emit_rex_w
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x94             ; sete al
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call emit_rex_w          ; REX.W for movzx rax, al
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0
        call emit_byte
        popad
        ret

.un_neg:
        call next_token
        call compile_unary
        ; neg rax (REX.W + F7 D8)
        call emit_rex_w
        mov al, 0xF7
        call emit_byte
        mov al, 0xD8
        call emit_byte
        popad
        ret

.un_bitnot:
        call next_token
        call compile_unary
        ; not rax (REX.W + F7 D0)
        call emit_rex_w
        mov al, 0xF7
        call emit_byte
        mov al, 0xD0
        call emit_byte
        popad
        ret

;=======================================================================
; COMPILE PRIMARY (number, char, string, paren, identifier, call, array)
;=======================================================================
compile_primary:
        pushad

        cmp dword [tok_type], TOK_NUM
        je .cp_num
        cmp dword [tok_type], TOK_CHAR
        je .cp_num
        cmp dword [tok_type], TOK_LPAREN
        je .cp_paren
        cmp dword [tok_type], TOK_STR
        je .cp_string
        cmp dword [tok_type], TOK_ID
        je .cp_id

        ; Unknown - emit 0 (mov eax, 0 zero-extends to RAX)
        mov al, 0xB8            ; mov eax, imm32
        call emit_byte
        mov eax, 0
        call emit_dword
        popad
        ret

.cp_num:
        ; mov eax, imm32
        mov al, 0xB8
        call emit_byte
        mov eax, [tok_value]
        call emit_dword
        call next_token
        popad
        ret

.cp_paren:
        call next_token
        call compile_expression
        cmp dword [tok_type], TOK_RPAREN
        jne .cp_err
        call next_token
        popad
        ret

.cp_string:
        ; Store string, emit mov eax, string_addr
        call store_string        ; returns index in EAX
        push eax
        mov al, 0xB8
        call emit_byte
        mov eax, [out_pos]       ; record position of the imm32 for fixup
        pop ebx                  ; string index
        mov [string_fixups + ebx * 4], eax
        mov eax, 0               ; placeholder (will be patched)
        call emit_dword
        call next_token
        popad
        ret

.cp_id:
        ; Save identifier and advance
        mov esi, tok_ident
        mov edi, expr_name
        call str_copy_local
        call next_token

        ; Function call?
        cmp dword [tok_type], TOK_LPAREN
        je .cp_call

        ; Array subscript?
        cmp dword [tok_type], TOK_LBRACKET
        je .cp_array_load

        ; Plain variable load
        mov esi, expr_name
        call find_symbol
        cmp eax, 0
        je .cp_var_not_found
        cmp dword [symbol_type], SYM_LOCAL
        je .cp_load_rbp
        cmp dword [symbol_type], SYM_PARAM
        je .cp_load_rbp
        ; Global: mov rbx, addr; mov rax, [rbx] (64-bit)
        call emit_rex_w
        mov al, 0xBB               ; mov rbx, imm32 (sign-extended)
        call emit_byte
        mov eax, [symbol_addr]
        call emit_dword
        ; mov rax, [rbx] (REX.W + 8B 03)
        call emit_rex_w
        mov al, 0x8B
        call emit_byte
        mov al, 0x03
        call emit_byte
        popad
        ret
.cp_load_rbp:
        ; Local/param: mov rax, [rbp + off8] (REX.W + 8B 45 off8)
        call emit_rex_w
        mov al, 0x8B
        call emit_byte
        mov al, 0x45
        call emit_byte
        mov eax, [symbol_addr]
        call emit_byte           ; low byte = signed RBP offset
        popad
        ret

.cp_var_not_found:
        ; Emit mov eax, 0 as fallback (zero-extends to RAX)
        mov al, 0xB8
        call emit_byte
        mov eax, 0
        call emit_dword
        popad
        ret

.cp_call:
        ; Function call
        call next_token          ; skip '('
        mov esi, expr_name
        call compile_func_call
        popad
        ret

.cp_array_load:
        ; arr[i] — read-only array access in expression context
        mov esi, expr_name
        call find_symbol
        cmp eax, 0
        je .cp_arr_err
        push dword [symbol_addr]   ; push array base address
        call next_token            ; skip '['
        call compile_expression    ; compile index -> RAX at runtime
        cmp dword [tok_type], TOK_RBRACKET
        jne .cp_arr_err2
        call next_token            ; skip ']'
        ; Array load: EAX = arr[index]
        ; imul eax,eax,4
        call emit_rex_w
        mov al, 0x6B
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 4                  ; 4 bytes per int element
        call emit_byte
        ; add rax, base (REX.W + 05 imm32)
        call emit_rex_w
        mov al, 0x05
        call emit_byte
        pop eax                    ; base address
        call emit_dword
        ; mov rax, [rax] (REX.W + 8B 00)
        call emit_rex_w
        mov al, 0x8B
        call emit_byte
        mov al, 0x00
        call emit_byte
        popad
        ret
.cp_arr_err2:
        pop eax
.cp_arr_err:
        mov byte [compile_error], 1
        popad
        ret

.cp_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE FUNCTION CALL
; ESI = function name, tok at first arg (past '(')
;=======================================================================
compile_func_call:
        pushad

        ; Check for built-in functions
        mov edi, kw_c_putchar
        call str_eq
        jc .cfc_putchar
        mov edi, kw_c_getchar
        call str_eq
        jc .cfc_getchar
        mov edi, kw_c_exit_fn
        call str_eq
        jc .cfc_exit
        mov edi, kw_c_printf
        call str_eq
        jc .cfc_printf
        mov edi, kw_c_puts
        call str_eq
        jc .cfc_puts
        mov edi, kw_c_strlen
        call str_eq
        jc .cfc_strlen
        mov edi, kw_c_malloc
        call str_eq
        jc .cfc_malloc
        mov edi, kw_c_free
        call str_eq
        jc .cfc_free
        mov edi, kw_c_abs
        call str_eq
        jc .cfc_abs
        mov edi, kw_c_atoi
        call str_eq
        jc .cfc_atoi

        ; User function call: look up in symbol table and emit call
        push esi                 ; save function name pointer
        call find_symbol
        pop esi
        cmp eax, 0
        je .cfc_skip_unk
        cmp dword [symbol_type], SYM_FUNC
        jne .cfc_skip_unk
        ; Found - save target address
        mov edx, [symbol_addr]
        xor ecx, ecx             ; arg count
.cfc_uarg:
        cmp dword [tok_type], TOK_RPAREN
        je .cfc_ucall
        cmp dword [tok_type], TOK_EOF
        je .cfc_err
        push ecx
        push edx
        call compile_expression  ; compile arg, result in RAX at runtime
        pop edx
        pop ecx
        mov al, 0x50             ; push rax (64-bit in long mode)
        call emit_byte
        inc ecx
        cmp dword [tok_type], TOK_COMMA
        jne .cfc_uarg
        call next_token          ; skip ','
        jmp .cfc_uarg
.cfc_ucall:
        call next_token          ; skip ')'
        ; Emit: call rel32
        push ecx
        push edx
        mov al, 0xE8
        call emit_byte
        pop edx                  ; func abs addr
        pop ecx                  ; arg count
        mov eax, edx
        sub eax, BASE_ADDR
        sub eax, [out_pos]
        sub eax, 4
        call emit_dword
        ; Caller cleanup: add esp, ecx*4 (32-bit: 4 bytes per arg)
        cmp ecx, 0
        je .cfc_udone
        mov al, 0x83
        call emit_byte
        mov al, 0xC4
        call emit_byte
        mov eax, ecx
        shl eax, 2               ; multiply by 4 (32-bit stack slots)
        call emit_byte
.cfc_udone:
        popad
        ret

.cfc_skip_unk:
        ; Function not in symtable yet - skip args
.cfc_skip2:
        cmp dword [tok_type], TOK_RPAREN
        je .cfc_skip2_done
        cmp dword [tok_type], TOK_EOF
        je .cfc_err
        call next_token
        jmp .cfc_skip2
.cfc_skip2_done:
        call next_token
        popad
        ret

.cfc_putchar:
        ; Compile argument
        call compile_expression
        ; Emit: mov ebx, eax; mov eax, SYS_PUTCHAR; int 0x80
        mov al, 0x89            ; mov ebx, eax
        call emit_byte
        mov al, 0xC3
        call emit_byte
        mov al, 0xB8            ; mov eax, imm32
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        mov al, 0xCD            ; int 0x80
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; skip ')'
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_getchar:
        ; No args expected
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        ; Emit: mov eax, SYS_GETCHAR; int 0x80
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_GETCHAR
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        popad
        ret

.cfc_exit:
        ; Compile argument (exit code)
        call compile_expression
        ; mov ebx, eax; mov eax, SYS_EXIT; int 0x80
        mov al, 0x89
        call emit_byte
        mov al, 0xC3
        call emit_byte
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_EXIT
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_printf:
        ; Handle printf("string") and printf("fmt", arg)
        ; For now: if string literal, print it
        cmp dword [tok_type], TOK_STR
        jne .cfc_printf_expr
        ; Store string, emit: mov ebx, str_addr; mov eax, SYS_PRINT; int 0x80
        call store_string       ; returns string index in EAX
        push eax
        mov al, 0xBB            ; mov ebx, imm32
        call emit_byte
        mov eax, [out_pos]      ; record fixup position
        pop ebx                 ; string index
        mov [string_fixups + ebx * 4], eax
        mov eax, 0              ; placeholder (patched later)
        call emit_dword
        mov al, 0xB8            ; mov eax, imm32
        call emit_byte
        mov eax, SYS_PRINT
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        call next_token          ; skip past string
        ; Check for , arg
        cmp dword [tok_type], TOK_COMMA
        jne .cfc_printf_done
        call next_token
        call compile_expression
        ; Print number: call print_dec equivalent
        ; push eax; use our decimal print loop
        call emit_print_dec_inline
.cfc_printf_done:
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_printf_expr:
        call compile_expression
        call emit_print_dec_inline
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_puts:
        ; puts(str) - print string and newline
        call compile_expression
        ; mov ebx, eax (string addr)
        mov al, 0x89
        call emit_byte
        mov al, 0xC3
        call emit_byte
        ; mov eax, SYS_PRINT; int 0x80
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PRINT
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; putchar('\n'): mov ebx, 10; mov eax, SYS_PUTCHAR; int 0x80
        mov al, 0xBB
        call emit_byte
        mov eax, 10
        call emit_dword
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_strlen:
        ; strlen(str) -> length in rax
        call compile_expression
        ; Inline strlen: mov rdi, rax; xor rcx, rcx; dec rcx; xor al, al; repne scasb; not rcx; dec rcx; mov rax, rcx
        call emit_rex_w
        mov al, 0x89             ; mov rdi, rax
        call emit_byte
        mov al, 0xC7
        call emit_byte
        call emit_rex_w
        mov al, 0x31             ; xor rcx, rcx
        call emit_byte
        mov al, 0xC9
        call emit_byte
        call emit_rex_w
        mov al, 0xFF             ; dec rcx
        call emit_byte
        mov al, 0xC9
        call emit_byte
        mov al, 0x30             ; xor al, al
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0xF2             ; repne scasb
        call emit_byte
        mov al, 0xAE
        call emit_byte
        call emit_rex_w
        mov al, 0xF7             ; not rcx
        call emit_byte
        mov al, 0xD1
        call emit_byte
        call emit_rex_w
        mov al, 0xFF             ; dec rcx
        call emit_byte
        mov al, 0xC9
        call emit_byte
        call emit_rex_w
        mov al, 0x89             ; mov rax, rcx
        call emit_byte
        mov al, 0xC8
        call emit_byte
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_malloc:
        ; malloc(size) -> pointer in rax
        call compile_expression
        ; mov ebx, eax (size); mov eax, SYS_MALLOC; int 0x80
        mov al, 0x89
        call emit_byte
        mov al, 0xC3
        call emit_byte
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_MALLOC
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_free:
        ; free(ptr)
        call compile_expression
        ; mov ebx, eax (ptr); mov eax, SYS_FREE; int 0x80
        mov al, 0x89
        call emit_byte
        mov al, 0xC3
        call emit_byte
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_FREE
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_abs:
        ; abs(n) -> absolute value in rax
        call compile_expression
        ; Inline abs: test rax, rax; jns .pos; neg rax; .pos:
        call emit_rex_w
        mov al, 0x85             ; test rax, rax
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x79             ; jns +3 (skip neg)
        call emit_byte
        mov al, 3
        call emit_byte
        call emit_rex_w
        mov al, 0xF7             ; neg rax
        call emit_byte
        mov al, 0xD8
        call emit_byte
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_atoi:
        ; atoi(str) -> integer in rax
        call compile_expression
        ; Inline atoi: mov rsi, rax; xor rax, rax; xor rcx, rcx; .loop: movzx rdx, byte [rsi]; test dl, dl; jz .done; sub dl, '0'; js .done; cmp dl, 9; ja .done; imul rax, 10; add rax, rdx; inc rsi; jmp .loop; .done:
        ; mov rsi, rax
        call emit_rex_w
        mov al, 0x89
        call emit_byte
        mov al, 0xC6
        call emit_byte
        ; xor rax, rax
        call emit_rex_w
        mov al, 0x31
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ; .loop (offset 0): movzx rdx, byte [rsi] (REX.W 0F B6 16)
        call emit_rex_w
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0x16
        call emit_byte
        ; test dl, dl
        mov al, 0x84
        call emit_byte
        mov al, 0xD2
        call emit_byte
        ; jz .done (+23 bytes from here)
        mov al, 0x74
        call emit_byte
        mov al, 23
        call emit_byte
        ; sub dl, '0'
        mov al, 0x80
        call emit_byte
        mov al, 0xEA
        call emit_byte
        mov al, '0'
        call emit_byte
        ; js .done (+17)
        mov al, 0x78
        call emit_byte
        mov al, 17
        call emit_byte
        ; cmp dl, 9
        mov al, 0x80
        call emit_byte
        mov al, 0xFA
        call emit_byte
        mov al, 9
        call emit_byte
        ; ja .done (+11)
        mov al, 0x77
        call emit_byte
        mov al, 11
        call emit_byte
        ; imul rax, rax, 10
        call emit_rex_w
        mov al, 0x6B
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 10
        call emit_byte
        ; add rax, rdx
        call emit_rex_w
        mov al, 0x01
        call emit_byte
        mov al, 0xD0
        call emit_byte
        ; inc rsi
        call emit_rex_w
        mov al, 0xFF
        call emit_byte
        mov al, 0xC6
        call emit_byte
        ; jmp .loop (-35 bytes)
        mov al, 0xEB
        call emit_byte
        mov al, 256-35
        call emit_byte
        ; .done: (result in rax)
        cmp dword [tok_type], TOK_RPAREN
        jne .cfc_err
        call next_token
        popad
        ret

.cfc_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; Emit inline decimal print (prints EAX as decimal number)
;=======================================================================
; HELPER: add_local_sym - register local var in symbol table
; Uses tok_ident (name) and [local_offset] (EBP offset)
;=======================================================================
add_local_sym:
        pushad
        mov eax, [sym_count]
        cmp eax, MAX_SYMS
        jge .als_full
        imul ebx, eax, SYM_NAME_LEN + 8
        mov esi, tok_ident
        lea edi, [sym_table + ebx]
        mov ecx, SYM_NAME_LEN - 1
.als_copy:
        lodsb
        stosb
        cmp al, 0
        je .als_done
        dec ecx
        jnz .als_copy
        mov byte [edi], 0
.als_done:
        lea edi, [sym_table + ebx + SYM_NAME_LEN]
        mov dword [edi], SYM_LOCAL
        mov eax, [local_offset]  ; signed EBP offset (e.g. -4)
        mov [edi + 4], eax
        inc dword [sym_count]
.als_full:
        popad
        ret

;=======================================================================
; HELPER: add_param_sym - register function param in symbol table
; Uses tok_ident (name) and [param_offset] (positive EBP offset)
;=======================================================================
add_param_sym:
        pushad
        mov eax, [sym_count]
        cmp eax, MAX_SYMS
        jge .aps_full
        imul ebx, eax, SYM_NAME_LEN + 8
        mov esi, tok_ident
        lea edi, [sym_table + ebx]
        mov ecx, SYM_NAME_LEN - 1
.aps_copy:
        lodsb
        stosb
        cmp al, 0
        je .aps_done
        dec ecx
        jnz .aps_copy
        mov byte [edi], 0
.aps_done:
        lea edi, [sym_table + ebx + SYM_NAME_LEN]
        mov dword [edi], SYM_PARAM
        mov eax, [param_offset]  ; positive EBP offset (8, 12, 16, ...)
        mov [edi + 4], eax
        inc dword [sym_count]
.aps_full:
        popad
        ret

;=======================================================================
; Emit inline decimal print (prints RAX as decimal number) - 64-bit
;=======================================================================
emit_print_dec_inline:
        pushad
        ; Emit inline decimal print routine for 64-bit

        ; xor rcx, rcx; mov rbx, 10 (64-bit)
        call emit_rex_w
        mov al, 0x31
        call emit_byte
        mov al, 0xC9            ; xor rcx, rcx
        call emit_byte
        call emit_rex_w
        mov al, 0xBB            ; mov rbx, 10
        call emit_byte
        mov eax, 10
        call emit_dword

        ; .loop: xor rdx, rdx; div rbx; push rdx; inc rcx; test rax, rax; jnz .loop
        mov eax, [out_pos]
        push eax                ; save loop start
        call emit_rex_w
        mov al, 0x31            ; xor rdx, rdx
        call emit_byte
        mov al, 0xD2
        call emit_byte
        call emit_rex_w
        mov al, 0xF7            ; div rbx
        call emit_byte
        mov al, 0xF3
        call emit_byte
        mov al, 0x52            ; push rdx
        call emit_byte
        ; inc rcx (REX.W + FF C1) - 64-bit inc
        call emit_rex_w
        mov al, 0xFF
        call emit_byte
        mov al, 0xC1
        call emit_byte
        call emit_rex_w
        mov al, 0x85            ; test rax, rax
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ; jnz loop_start
        mov al, 0x75
        call emit_byte
        pop ebx
        mov eax, ebx
        sub eax, [out_pos]
        dec eax
        call emit_byte

        ; .pop: pop rbx; add ebx, '0'; mov eax, SYS_PUTCHAR; int 0x80; dec rcx; jnz .pop
        mov eax, [out_pos]
        push eax                ; pop loop start
        mov al, 0x5B            ; pop rbx
        call emit_byte
        mov al, 0x83            ; add ebx, '0' (32-bit, zero-extends)
        call emit_byte
        mov al, 0xC3
        call emit_byte
        mov al, 0x30
        call emit_byte
        mov al, 0xB8            ; mov eax, SYS_PUTCHAR
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        mov al, 0xCD            ; int 0x80
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; dec rcx (REX.W + FF C9) - 64-bit dec
        call emit_rex_w
        mov al, 0xFF
        call emit_byte
        mov al, 0xC9
        call emit_byte
        ; jnz pop_start
        mov al, 0x75
        call emit_byte
        pop ebx
        mov eax, ebx
        sub eax, [out_pos]
        dec eax
        call emit_byte

        popad
        ret

;=======================================================================
; EMIT HELPERS
;=======================================================================
emit_byte:
        push edi
        mov edi, [out_pos]
        mov [out_buffer + edi], al
        inc dword [out_pos]
        pop edi
        ret

emit_dword:
        push edi
        mov edi, [out_pos]
        mov [out_buffer + edi], eax
        add dword [out_pos], 4
        pop edi
        ret

emit_qword:
        ; Emit 8 bytes: low dword in EAX, high dword in EDX
        push edi
        mov edi, [out_pos]
        mov [out_buffer + edi], eax
        mov [out_buffer + edi + 4], edx
        add dword [out_pos], 8
        pop edi
        ret

emit_rex_w:
        ; 32-bit target: old x86-64 prefixes must not be emitted
        ret

emit_jmp_placeholder:
        push eax
        mov al, 0xE9            ; jmp rel32
        call emit_byte
        mov eax, [out_pos]
        push eax
        mov eax, 0              ; placeholder
        call emit_dword
        pop eax                 ; return fixup position
        mov [esp], eax          ; put in return value position
        pop eax
        ret

;=======================================================================
; SYMBOL TABLE
;=======================================================================

; Add global variable symbol, allocate space at end
add_global_var:
        pushad
        mov eax, [sym_count]
        cmp eax, MAX_SYMS
        jge .agv_full
        imul ebx, eax, SYM_NAME_LEN + 8  ; name + type(4) + addr(4)
        ; Copy name
        mov esi, tok_ident
        lea edi, [sym_table + ebx]
        mov ecx, SYM_NAME_LEN - 1
.agv_copy:
        lodsb
        stosb
        cmp al, 0
        je .agv_name_done
        dec ecx
        jnz .agv_copy
        mov byte [edi], 0
.agv_name_done:
        ; Set type
        lea edi, [sym_table + ebx + SYM_NAME_LEN]
        mov dword [edi], SYM_VAR
        ; Address: BASE_ADDR + out_pos (we'll emit space)
        mov eax, BASE_ADDR
        add eax, [out_pos]
        mov [edi + 4], eax
        ; Emit 4 bytes of zero (global int storage)
        push eax
        mov eax, 0
        call emit_dword
        pop eax
        inc dword [sym_count]
.agv_full:
        popad
        ret

; Add global array symbol, allocate N qwords at end (64-bit)
; In: EAX = element count, name in temp_name
add_global_array:
        pushad
        mov edx, eax
        cmp edx, 1
        jl .aga_err

        mov eax, [sym_count]
        cmp eax, MAX_SYMS
        jge .aga_done
        imul ebx, eax, SYM_NAME_LEN + 8

        ; Copy name from temp_name
        mov esi, temp_name
        lea edi, [sym_table + ebx]
        mov ecx, SYM_NAME_LEN - 1
.aga_copy:
        lodsb
        stosb
        cmp al, 0
        je .aga_name_done
        dec ecx
        jnz .aga_copy
        mov byte [edi], 0
.aga_name_done:
        ; Set type + base address
        lea edi, [sym_table + ebx + SYM_NAME_LEN]
        mov dword [edi], SYM_ARRAY
        mov eax, BASE_ADDR
        add eax, [out_pos]
        mov [edi + 4], eax

        ; Emit N dwords of zero (32-bit ints)
        mov ecx, edx
        xor eax, eax
.aga_emit:
        cmp ecx, 0
        je .aga_count_done
        call emit_dword
        dec ecx
        jmp .aga_emit

.aga_count_done:
        inc dword [sym_count]
        jmp .aga_done

.aga_err:
        mov byte [compile_error], 1
.aga_done:
        popad
        ret

; Add function symbol
; EAX = address (out_pos), ESI = name
add_symbol_func:
        pushad
        mov edx, eax
        mov eax, [sym_count]
        cmp eax, MAX_SYMS
        jge .asf_full
        imul ebx, eax, SYM_NAME_LEN + 8
        lea edi, [sym_table + ebx]
        mov ecx, SYM_NAME_LEN - 1
.asf_copy:
        lodsb
        stosb
        cmp al, 0
        je .asf_name_done
        dec ecx
        jnz .asf_copy
        mov byte [edi], 0
.asf_name_done:
        lea edi, [sym_table + ebx + SYM_NAME_LEN]
        mov dword [edi], SYM_FUNC
        mov eax, BASE_ADDR
        add eax, edx
        mov [edi + 4], eax
        inc dword [sym_count]
.asf_full:
        popad
        ret

; Find symbol by name in ESI
; Returns: EAX=1 if found, 0 if not; [symbol_addr] = address
find_symbol:
        push ebx
        push ecx
        push edx
        push edi
        mov ecx, [sym_count]
        xor ebx, ebx
.fs_loop:
        cmp ebx, ecx
        jge .fs_not_found
        imul edx, ebx, SYM_NAME_LEN + 8
        lea edi, [sym_table + edx]
        push esi
        push edi
        call str_eq_local
        pop edi
        pop esi
        jc .fs_found
        inc ebx
        jmp .fs_loop
.fs_found:
        lea edi, [sym_table + edx + SYM_NAME_LEN + 4]
        mov eax, [edi]
        mov [symbol_addr], eax
        mov eax, 1
                ; Also store type so callers can emit correct load/store
                lea edi, [sym_table + edx + SYM_NAME_LEN]
                mov ebx, [edi]
                mov [symbol_type], ebx
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret
.fs_not_found:
        xor eax, eax
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret

str_eq_local:
        ; Compare [esi] with [edi] for equality
        push eax
.sel_loop:
        mov al, [esi]
        cmp al, [edi]
        jne .sel_ne
        cmp al, 0
        je .sel_eq
        inc esi
        inc edi
        jmp .sel_loop
.sel_eq:
        pop eax
        stc
        ret
.sel_ne:
        pop eax
        clc
        ret

;=======================================================================
; STRING LITERAL STORAGE
;=======================================================================
store_string:
        ; Store tok_string in string table, return string index in EAX.
        ; Caller is responsible for recording string_fixups[index] = out_pos
        ; of the imm32 operand that should be patched.
        push ebx
        push ecx
        push edi
        push esi
        mov eax, [string_count]
        cmp eax, MAX_STRINGS
        jge .ss_full
        mov ecx, eax             ; ECX = index we are returning
        imul ebx, eax, STRING_MAX_LEN
        lea edi, [string_table + ebx]
        mov esi, tok_string
.ss_copy:
        lodsb
        stosb
        cmp al, 0
        jne .ss_copy
        inc dword [string_count]
        mov eax, ecx             ; return string index
        pop esi
        pop edi
        pop ecx
        pop ebx
        ret
.ss_full:
        xor eax, eax
        pop esi
        pop edi
        pop ecx
        pop ebx
        ret

; Emit string data at end of output and patch all fixup references
emit_string_data:
        pushad
        mov ecx, [string_count]
        cmp ecx, 0
        je .esd_done
        xor ebx, ebx             ; string index
.esd_loop:
        ; Record the runtime address of this string
        mov eax, BASE_ADDR
        add eax, [out_pos]       ; runtime addr = BASE + current out_pos
        mov [string_addrs + ebx * 4], eax

        ; Emit the string bytes (including null terminator)
        imul edx, ebx, STRING_MAX_LEN
        lea esi, [string_table + edx]
.esd_char:
        lodsb
        call emit_byte
        cmp al, 0
        jne .esd_char

        inc ebx
        cmp ebx, ecx
        jl .esd_loop

        ; Now patch all fixup locations
        xor ebx, ebx
.esd_patch:
        cmp ebx, ecx
        jge .esd_done
        mov eax, [string_fixups + ebx * 4]  ; out_pos of the imm32 placeholder
        cmp eax, 0
        je .esd_patch_next       ; no fixup recorded (shouldn't happen)
        mov edx, [string_addrs + ebx * 4]   ; patched runtime address
        mov [out_buffer + eax], edx
.esd_patch_next:
        inc ebx
        jmp .esd_patch
.esd_done:
        popad
        ret

;=======================================================================
; UTILITY
;=======================================================================
str_copy_local:
        ; Copy null-terminated string from ESI to EDI
        lodsb
        stosb
        cmp al, 0
        jne str_copy_local
        ret

; hex_digit_val: Convert hex char in AL to value (0-15), or -1 if invalid
; Returns in EAX
hex_digit_val:
        cmp al, '0'
        jb .hdv_invalid
        cmp al, '9'
        jbe .hdv_digit
        cmp al, 'a'
        jb .hdv_check_upper
        cmp al, 'f'
        jbe .hdv_lower
        jmp .hdv_invalid
.hdv_check_upper:
        cmp al, 'A'
        jb .hdv_invalid
        cmp al, 'F'
        jbe .hdv_upper
        jmp .hdv_invalid
.hdv_digit:
        sub al, '0'
        movzx eax, al
        ret
.hdv_lower:
        sub al, 'a'
        add al, 10
        movzx eax, al
        ret
.hdv_upper:
        sub al, 'A'
        add al, 10
        movzx eax, al
        ret
.hdv_invalid:
        mov eax, -1
        ret

;=======================================================================
; DATA
;=======================================================================

; C keywords
kw_c_if:        db "if", 0
kw_c_else:      db "else", 0
kw_c_while:     db "while", 0
kw_c_for:       db "for", 0
kw_c_return:    db "return", 0
kw_c_int:       db "int", 0
kw_c_void:      db "void", 0
kw_c_main:      db "main", 0
kw_c_putchar:   db "putchar", 0
kw_c_getchar:   db "getchar", 0
kw_c_exit_fn:   db "exit", 0
kw_c_printf:    db "printf", 0
kw_c_break:     db "break", 0
kw_c_continue:  db "continue", 0
kw_c_do:        db "do", 0
kw_c_switch:    db "switch", 0
kw_c_case:      db "case", 0
kw_c_default:   db "default", 0
kw_c_char:      db "char", 0
kw_c_puts:      db "puts", 0
kw_c_gets:      db "gets", 0
kw_c_strlen:    db "strlen", 0
kw_c_strcmp:    db "strcmp", 0
kw_c_strcpy:    db "strcpy", 0
kw_c_atoi:      db "atoi", 0
kw_c_malloc:    db "malloc", 0
kw_c_free:      db "free", 0
kw_c_abs:       db "abs", 0

; Messages
msg_usage:      db "Tiny C Compiler for Mellivora OS", 0x0A
                db "Usage: tcc <source.c> <output>", 0x0A, 0
msg_compiling:  db "Compiling: ", 0
msg_arrow:      db " -> ", 0
msg_success:    db "Success! Output: ", 0
msg_bytes:      db " bytes", 0x0A, 0
msg_file_err:   db "Error: Cannot read source file", 0x0A, 0
msg_comp_err:   db "Compile error", 0
msg_at_line:    db " at line ", 0
msg_no_main:    db "Error: No main() function found", 0x0A, 0
msg_write_err:  db "Error: Cannot write output file", 0x0A, 0

; Compiler state
src_pos:        dd 0
out_pos:        dd 0
line_num:       dd 1
compile_error:  db 0
in_function:    db 0
local_offset:   dd 0
main_addr:      dd 0
main_fixup:     dd 0
symbol_addr:    dd 0

; Token state
tok_type:       dd 0
tok_value:      dd 0
tok_ident:      times SYM_NAME_LEN db 0
tok_string:     times 128 db 0

; Temp buffers
temp_name:      times SYM_NAME_LEN db 0
expr_name:      times SYM_NAME_LEN db 0
args_buf:       times 256 db 0
src_filename:   times 64 db 0
dst_filename:   times 64 db 0
src_size:       dd 0

; Symbol table
sym_count:      dd 0
sym_table:      times MAX_SYMS * (SYM_NAME_LEN + 8) db 0

; Symbol type returned by find_symbol (SYM_VAR / SYM_FUNC / SYM_LOCAL / SYM_PARAM / SYM_ARRAY)
symbol_type:    dd 0
; global_sym_end: sym_count saved when entering a function scope
global_sym_end: dd 0
; param_offset: current param EBP offset (first param = [ebp+8])
param_offset:   dd 0
; for-loop state (stack-saved, but these catch nested if needed)
for_save_src:   dd 0
for_var_type:   dd 0
for_var_addr:   dd 0
for_inc_kind:   dd 0
for_inc_value:  dd 0
; lexer position saved for assignment lookahead in compile_expression
expr_probe_pos: dd 0

; Loop control for break/continue
loop_start:     dd 0            ; address of loop start (for continue)
loop_exit:      dd 0            ; address of loop exit (for break)
break_fixup_count: dd 0
break_fixups:   times 64 dd 0   ; fixup addresses for break statements
continue_fixup_count: dd 0
continue_fixups: times 64 dd 0  ; fixup addresses for continue statements

; Switch statement state
switch_expr_val: dd 0           ; value of switch expression (for case matching)
switch_case_count: dd 0
switch_default_fixup: dd 0      ; fixup for default label
switch_end_fixup: dd 0          ; fixup for switch end
switch_case_fixups: times 64 dd 0  ; fixup addresses for case jumps

; Fixups
fixup_count:    dd 0
fixup_table:    times MAX_FIXUPS * 8 db 0

; String table
string_count:   dd 0
string_table:   times MAX_STRINGS * STRING_MAX_LEN db 0
string_offsets: times MAX_STRINGS dd 0
string_fixups:  times MAX_STRINGS dd 0  ; out_pos of each string's imm32 placeholder
string_addrs:   times MAX_STRINGS dd 0  ; final runtime address of each string

; Source and output buffers
src_buffer:     times MAX_SRC + 1 db 0
out_buffer:     times MAX_OUT db 0
