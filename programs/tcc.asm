; tcc.asm - Tiny C Compiler for Mellivora OS
; Compiles a minimal subset of C to flat 32-bit x86 binary
;
; Supported C subset:
;   - int variables (global)
;   - int main() { ... }
;   - int functions with up to 4 params
;   - if/else, while, for
;   - return statement
;   - +, -, *, /, %, ==, !=, <, >, <=, >=, &&, ||, !
;   - putchar(), getchar(), exit() builtins
;   - printf() with %d and string literals
;   - Integer constants, char constants ('x')
;   - Assignment (=, +=, -=, *=, /=)
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

; Symbol types
SYM_VAR         equ 1
SYM_FUNC        equ 2

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
        ; Number
        cmp al, '0'
        jl .nt_not_num
        cmp al, '9'
        jg .nt_not_num
        ; Parse number
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
        cmp dl, '\'
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
        jne .nt_char_ok
        mov edx, 9
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
        cmp al, '\'
        jne .nt_str_normal
        ; Escape
        inc esi
        inc dword [src_pos]
        movzx eax, byte [esi]
        cmp al, 'n'
        jne .nt_str_esc2
        mov al, 10
        jmp .nt_str_normal
.nt_str_esc2:
        cmp al, 't'
        jne .nt_str_normal
        mov al, 9
.nt_str_normal:
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
        je .nt_single_tok
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
        jne .nt_lt_only
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_LE
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
        jne .nt_gt_only
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_GE
        popad
        ret
.nt_gt_only:
        mov dword [tok_type], TOK_GT
        popad
        ret

.nt_amp:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '&'
        jne .nt_restart
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_AND
        popad
        ret

.nt_pipe:
        inc esi
        inc dword [src_pos]
        cmp byte [esi], '|'
        jne .nt_restart
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_OR
        popad
        ret

.nt_single_tok:
        ; Map character to token type
        inc esi
        inc dword [src_pos]
        cmp al, '%'
        je .nt_is_percent
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
        mov dword [tok_type], TOK_RBRACE
        popad
        ret
.nt_is_percent:
        mov dword [tok_type], TOK_PERCENT
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

        ; Global variable
        call add_global_var
        ; Expect ;
        cmp dword [tok_type], TOK_SEMI
        jne .cp_err
        call next_token
        jmp .cp_loop

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

        ; Skip parameters (consume until ')')
        call next_token          ; skip '('
.cf_params:
        cmp dword [tok_type], TOK_RPAREN
        je .cf_params_done
        cmp dword [tok_type], TOK_EOF
        je .cf_err
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

        ; Function epilogue (in case no return)
        ; xor eax, eax; pop ebp; ret
        mov al, 0x31            ; xor eax, eax
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x5D            ; pop ebp
        call emit_byte
        mov al, 0xC3            ; ret
        call emit_byte

        mov byte [in_function], 0
        popad
        ret

.cf_err:
        mov byte [compile_error], 1
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
        cmp dword [tok_type], TOK_RETURN
        je .cs_return
        cmp dword [tok_type], TOK_INT
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
; COMPILE RETURN
;=======================================================================
compile_return:
        pushad
        call next_token          ; skip 'return'

        cmp dword [tok_type], TOK_SEMI
        je .cr_void

        call compile_expression

.cr_void:
        ; pop ebp; ret
        mov al, 0x5D
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
        sub dword [local_offset], 4
        mov eax, [local_offset]

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
        call next_token

        popad
        ret

.cld_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; COMPILE EXPRESSION (returns result in EAX)
;=======================================================================
compile_expression:
        pushad

        ; Check for identifier (could be assignment or function call)
        cmp dword [tok_type], TOK_ID
        jne .ce_not_id

        ; Save identifier
        mov esi, tok_ident
        mov edi, expr_name
        call str_copy_local

        call next_token

        ; Check for assignment
        cmp dword [tok_type], TOK_ASSIGN
        je .ce_assign

        ; Check for function call
        cmp dword [tok_type], TOK_LPAREN
        je .ce_call

        ; Just a variable reference: load value into eax
        ; For simplicity, use global address
        mov esi, expr_name
        call find_symbol
        cmp eax, 0
        je .ce_var_not_found
        ; mov eax, [addr]
        mov al, 0xA1
        call emit_byte
        mov eax, [symbol_addr]
        call emit_dword
        jmp .ce_binop

.ce_var_not_found:
        ; Emit mov eax, 0 as fallback
        mov al, 0xB8
        call emit_byte
        mov eax, 0
        call emit_dword
        jmp .ce_binop

.ce_assign:
        ; Resolve target variable BEFORE compiling RHS, because
        ; the recursive compile_expression will clobber expr_name
        mov esi, expr_name
        call find_symbol
        push dword [symbol_addr]

        call next_token
        call compile_expression

        ; Store: mov [addr], eax
        mov al, 0xA3
        call emit_byte
        pop eax
        call emit_dword
        popad
        ret

.ce_call:
        ; Function call
        call next_token          ; skip '('
        mov esi, expr_name
        call compile_func_call
        jmp .ce_binop

.ce_not_id:
        call compile_atom

.ce_binop:
        ; Check for binary operator
        cmp dword [tok_type], TOK_PLUS
        je .ce_add
        cmp dword [tok_type], TOK_MINUS
        je .ce_sub
        cmp dword [tok_type], TOK_STAR
        je .ce_mul
        cmp dword [tok_type], TOK_SLASH
        je .ce_div_op
        cmp dword [tok_type], TOK_EQ
        je .ce_eq
        cmp dword [tok_type], TOK_NE
        je .ce_ne
        cmp dword [tok_type], TOK_LT
        je .ce_lt
        cmp dword [tok_type], TOK_GT
        je .ce_gt
        cmp dword [tok_type], TOK_LE
        je .ce_le
        cmp dword [tok_type], TOK_GE
        je .ce_ge
        ; No more operators
        popad
        ret

.ce_add:
        call next_token
        ; push eax
        mov al, 0x50
        call emit_byte
        call compile_expression
        ; pop ebx; add eax, ebx (but we want left+right, so: pop ebx; add eax, ebx)
        mov al, 0x5B            ; pop ebx
        call emit_byte
        mov al, 0x01            ; add eax, ebx
        call emit_byte
        mov al, 0xD8
        call emit_byte
        popad
        ret

.ce_sub:
        call next_token
        ; push eax (left side)
        mov al, 0x50
        call emit_byte
        call compile_expression
        ; Result in eax = right. pop ebx = left. Want left - right.
        ; mov ecx, eax; pop eax; sub eax, ecx
        mov al, 0x89            ; mov ecx, eax
        call emit_byte
        mov al, 0xC1
        call emit_byte
        mov al, 0x58            ; pop eax
        call emit_byte
        mov al, 0x29            ; sub eax, ecx
        call emit_byte
        mov al, 0xC8
        call emit_byte
        popad
        ret

.ce_mul:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expression
        ; pop ebx; imul eax, ebx
        mov al, 0x5B
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0xAF
        call emit_byte
        mov al, 0xC3
        call emit_byte
        popad
        ret

.ce_div_op:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expression
        ; mov ecx, eax; pop eax; cdq; idiv ecx
        mov al, 0x89
        call emit_byte
        mov al, 0xC1
        call emit_byte
        mov al, 0x58            ; pop eax
        call emit_byte
        mov al, 0x99            ; cdq
        call emit_byte
        mov al, 0xF7            ; idiv ecx
        call emit_byte
        mov al, 0xF9
        call emit_byte
        popad
        ret

.ce_eq:
        call .ce_emit_cmp
        ; sete al; movzx eax, al
        mov al, 0x0F
        call emit_byte
        mov al, 0x94
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call .ce_emit_movzx
        popad
        ret

.ce_ne:
        call .ce_emit_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x95
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call .ce_emit_movzx
        popad
        ret

.ce_lt:
        call .ce_emit_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x9C
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call .ce_emit_movzx
        popad
        ret

.ce_gt:
        call .ce_emit_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x9F
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call .ce_emit_movzx
        popad
        ret

.ce_le:
        call .ce_emit_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x9E
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call .ce_emit_movzx
        popad
        ret

.ce_ge:
        call .ce_emit_cmp
        mov al, 0x0F
        call emit_byte
        mov al, 0x9D
        call emit_byte
        mov al, 0xC0
        call emit_byte
        call .ce_emit_movzx
        popad
        ret

; Helper: emit push eax, compile RHS, pop ebx, cmp ebx, eax
.ce_emit_cmp:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expression
        mov al, 0x89            ; mov ecx, eax
        call emit_byte
        mov al, 0xC1
        call emit_byte
        mov al, 0x58            ; pop eax
        call emit_byte
        mov al, 0x39            ; cmp eax, ecx
        call emit_byte
        mov al, 0xC8
        call emit_byte
        ret

; Helper: emit movzx eax, al
.ce_emit_movzx:
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ret

;=======================================================================
; COMPILE ATOM (number, char, parenthesized expr)
;=======================================================================
compile_atom:
        pushad

        cmp dword [tok_type], TOK_NUM
        je .ca_num
        cmp dword [tok_type], TOK_CHAR
        je .ca_num
        cmp dword [tok_type], TOK_LPAREN
        je .ca_paren
        cmp dword [tok_type], TOK_STR
        je .ca_string

        ; Unknown - emit 0
        mov al, 0xB8            ; mov eax, imm32
        call emit_byte
        mov eax, 0
        call emit_dword
        popad
        ret

.ca_num:
        ; mov eax, imm32
        mov al, 0xB8
        call emit_byte
        mov eax, [tok_value]
        call emit_dword
        call next_token
        popad
        ret

.ca_paren:
        call next_token
        call compile_expression
        cmp dword [tok_type], TOK_RPAREN
        jne .ca_err
        call next_token
        popad
        ret

.ca_string:
        ; Store string, emit mov eax, string_addr
        call store_string        ; returns address in EAX
        push eax
        mov al, 0xB8
        call emit_byte
        pop eax
        call emit_dword
        call next_token
        popad
        ret

.ca_err:
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

        ; User function call - skip args, emit call
        ; For simplicity, just skip to ')'
.cfc_skip:
        cmp dword [tok_type], TOK_RPAREN
        je .cfc_skip_done
        cmp dword [tok_type], TOK_EOF
        je .cfc_err
        call next_token
        jmp .cfc_skip
.cfc_skip_done:
        call next_token          ; skip ')'
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
        call store_string
        push eax
        mov al, 0xBB            ; mov ebx, imm32
        call emit_byte
        pop eax
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

.cfc_err:
        mov byte [compile_error], 1
        popad
        ret

;=======================================================================
; Emit inline decimal print (prints EAX as decimal number)
;=======================================================================
emit_print_dec_inline:
        pushad
        ; Emit a call to the print_dec routine we embed
        ; For simplicity, emit inline:
        ; push eax; (save value)
        ; Emit pushad + print loop + popad (quite long)
        ; Instead, use a simpler approach: emit syscall putchar loop

        ; Just emit: push eax as EBX, syscall to print_dec shared routine
        ; Actually, the compiled binary includes the shared print_dec from syscalls.inc
        ; So we can call it: the program starts at 0x200000, print_dec is after the jmp
        ; Let's embed a simple digit-push approach

        ; xor ecx, ecx; mov ebx, 10
        mov al, 0x31
        call emit_byte
        mov al, 0xC9            ; xor ecx, ecx
        call emit_byte
        mov al, 0xBB            ; mov ebx, 10
        call emit_byte
        mov eax, 10
        call emit_dword

        ; .loop: xor edx, edx; div ebx; push edx; inc ecx; test eax, eax; jnz .loop
        mov eax, [out_pos]
        push eax                ; save loop start
        mov al, 0x31            ; xor edx, edx
        call emit_byte
        mov al, 0xD2
        call emit_byte
        mov al, 0xF7            ; div ebx
        call emit_byte
        mov al, 0xF3
        call emit_byte
        mov al, 0x52            ; push edx
        call emit_byte
        mov al, 0x41            ; inc ecx
        call emit_byte
        mov al, 0x85            ; test eax, eax
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

        ; .pop: pop ebx; add ebx, '0'; mov eax, 1; int 0x80; dec ecx; jnz .pop
        mov eax, [out_pos]
        push eax                ; pop loop start
        mov al, 0x5B            ; pop ebx
        call emit_byte
        mov al, 0x83            ; add ebx, '0'
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
        mov al, 0x49            ; dec ecx
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
        ; Emit 4 bytes of zero (global var storage)
        push eax
        mov eax, 0
        call emit_dword
        pop eax
        inc dword [sym_count]
.agv_full:
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
        ; Store tok_string in string table, return BASE_ADDR + offset
        push ebx
        push ecx
        push edi
        push esi
        mov eax, [string_count]
        cmp eax, MAX_STRINGS
        jge .ss_full
        imul ebx, eax, STRING_MAX_LEN
        lea edi, [string_table + ebx]
        mov esi, tok_string
        xor ecx, ecx
.ss_copy:
        lodsb
        stosb
        inc ecx
        cmp al, 0
        jne .ss_copy
        ; Store offset info
        mov [string_offsets + eax * 4], ebx  ; wait, eax was clobbered
        inc dword [string_count]
        ; Return will be patched later when we emit strings
        ; For now return a placeholder
        mov eax, ebx            ; offset within string_table
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

; Emit string data and patch references
emit_string_data:
        pushad
        mov ecx, [string_count]
        cmp ecx, 0
        je .esd_done
        xor ebx, ebx
.esd_loop:
        imul edx, ebx, STRING_MAX_LEN
        lea esi, [string_table + edx]
        ; Emit bytes
.esd_char:
        lodsb
        call emit_byte
        cmp al, 0
        jne .esd_char
        inc ebx
        cmp ebx, ecx
        jl .esd_loop
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

; Fixups
fixup_count:    dd 0
fixup_table:    times MAX_FIXUPS * 8 db 0

; String table
string_count:   dd 0
string_table:   times MAX_STRINGS * STRING_MAX_LEN db 0
string_offsets: times MAX_STRINGS dd 0

; Source and output buffers
src_buffer:     times MAX_SRC + 1 db 0
out_buffer:     times MAX_OUT db 0
