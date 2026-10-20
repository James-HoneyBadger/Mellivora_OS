; basicc.asm - BASIC Compiler for Mellivora OS
; Compiles a BASIC source file to a flat x86-32 binary executable.
;
; Supported language subset:
;   - Numeric variables A-Z (32-bit integer)
;   - Arithmetic: +, -, *, /, MOD
;   - Bitwise/logical: AND, OR, XOR, NOT
;   - Comparisons: =, <>, <, >, <=, >=
;   - PRINT [items] [; | ,]    (string literals and numeric exprs)
;   - INPUT [prompt;] var
;   - LET var = expr  or  var = expr
;   - IF expr THEN lineno [ELSE lineno]
;   - IF expr THEN stmt [ELSE stmt]
;   - GOTO lineno
;   - GOSUB lineno / RETURN
;   - FOR var = start TO end [STEP step] / NEXT [var]
;   - WHILE expr / WEND
;   - END, STOP, CLS, BEEP, SLEEP expr
;   - REM (comment)
;   - Colon multi-statement lines
;
; Usage:  basicc source.bas output.bin
;   Compiles source.bas to a flat x86 binary at PROGRAM_BASE.
;
; Output binary layout:
;   [0x000] JMP to code_start          (5 bytes)
;   [0x005] zero padding               (to 0x100)
;   [0x100] vars A-Z                   (26*4 = 104 bytes)
;   [0x168] for_end_vals[16]           (64 bytes)
;   [0x1A8] for_step_vals[16]          (64 bytes)
;   [0x1E8] rand_seed = 12345          (4 bytes)
;   [0x1EC] padding                    (4 bytes)
;   [0x1F0] input_buf                  (256 bytes)
;   [0x2F0] print_buf                  (32 bytes)
;   [0x310] "? \0"                     (3 bytes)
;   [0x313] padding                    (to 0x400)
;   [0x400] rt_input_int subroutine    (~180 bytes)
;   [0x500] rt_print_int subroutine    (~85 bytes)
;   [0x560] padding                    (to 0x600)
;   [0x600] compiled BASIC code
;   [end]   string literal data
;
%include "syscalls.inc"

; -----------------------------------------------------------------------
; Compiler limits
; -----------------------------------------------------------------------
MAX_SRC         equ 24576       ; max source file size (24 KB)
MAX_OUT         equ 65536       ; max output binary size (64 KB)
MAX_LINENOS     equ 1024        ; max labeled line numbers in program
MAX_FIXUPS      equ 512         ; max forward GOTO/GOSUB fixups
MAX_WHILE_FIX   equ 64         ; max nested WHILE loops
MAX_STRS        equ 128         ; max string literals
STRING_MAX      equ 128         ; max string literal length (bytes incl NUL)
FOR_STACK_MAX   equ 16          ; max nested FOR loops (compile-time)

; -----------------------------------------------------------------------
; Output binary fixed layout constants
; -----------------------------------------------------------------------
BASE_ADDR       equ 0x300000    ; = kernel.asm PROGRAM_BASE, where the shell loads
                                ; flat binaries. Emitted absolute addresses (vars A-Z,
                                ; runtime subroutines, string literals) are based here.

; Data section offsets (from BASE_ADDR)
VARS_OFF        equ 0x100       ; vars A..Z (104 bytes)
FOR_END_OFF     equ 0x168       ; for_end_vals[16] (64 bytes)
FOR_STEP_OFF    equ 0x1A8       ; for_step_vals[16] (64 bytes)
RAND_OFF        equ 0x1E8       ; rand_seed (4 bytes)
INPUTBUF_OFF    equ 0x1F0       ; input_buf (256 bytes)
PRINTBUF_OFF    equ 0x2F0       ; print_buf (32 bytes)
PROMPT_OFF      equ 0x310       ; "? \0" (3 bytes)

; Runtime subroutine offsets
RT_INPUT_OFF    equ 0x400       ; rt_input_int
RT_PRINT_OFF    equ 0x500       ; rt_print_int

; Code start offset (where compiled BASIC code begins)
CODE_START_OFF  equ 0x600
JMP_DELTA       equ CODE_START_OFF - 5   ; = 0x5FB

; Absolute runtime addresses (used when emitting CALL/reference)
VARS_ADDR       equ BASE_ADDR + VARS_OFF
FOR_END_ADDR    equ BASE_ADDR + FOR_END_OFF
FOR_STEP_ADDR   equ BASE_ADDR + FOR_STEP_OFF
INPUTBUF_ADDR   equ BASE_ADDR + INPUTBUF_OFF
PRINTBUF_ADDR   equ BASE_ADDR + PRINTBUF_OFF
PROMPT_ADDR     equ BASE_ADDR + PROMPT_OFF
RT_INPUT_ADDR   equ BASE_ADDR + RT_INPUT_OFF
RT_PRINT_ADDR   equ BASE_ADDR + RT_PRINT_OFF

; -----------------------------------------------------------------------
; Token types
; -----------------------------------------------------------------------
TOK_EOF         equ 0
TOK_NUM         equ 1
TOK_VAR         equ 2       ; single letter A-Z variable; tok_var = 0-25
TOK_STR         equ 3       ; string literal in tok_string
TOK_NL          equ 4       ; end of line / blank
TOK_PLUS        equ 10
TOK_MINUS       equ 11
TOK_STAR        equ 12
TOK_SLASH       equ 13
TOK_EQ          equ 14      ; = (assignment or comparison)
TOK_NE          equ 15      ; <>
TOK_LT          equ 16      ; <
TOK_GT          equ 17      ; >
TOK_LE          equ 18      ; <=
TOK_GE          equ 19      ; >=
TOK_LPAREN      equ 20      ; (
TOK_RPAREN      equ 21      ; )
TOK_SEMI        equ 22      ; ;
TOK_COMMA       equ 23      ; ,
TOK_COLON       equ 24      ; :

; Keyword tokens (50+)
KW_PRINT        equ 50
KW_INPUT        equ 51
KW_LET          equ 52
KW_IF           equ 53
KW_THEN         equ 54
KW_ELSE         equ 55
KW_GOTO         equ 56
KW_GOSUB        equ 57
KW_RETURN       equ 58
KW_FOR          equ 59
KW_TO           equ 60
KW_STEP         equ 61
KW_NEXT         equ 62
KW_WHILE        equ 63
KW_WEND         equ 64
KW_END          equ 65
KW_STOP         equ 66
KW_REM          equ 67
KW_CLS          equ 68
KW_BEEP         equ 69
KW_SLEEP        equ 70
KW_MOD          equ 71
KW_AND          equ 72
KW_OR           equ 73
KW_NOT          equ 74
KW_XOR          equ 75

; -----------------------------------------------------------------------
; Entry point
; -----------------------------------------------------------------------
start:
        ; Get command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Parse source filename
        mov esi, args_buf
        mov edi, src_filename
        call parse_arg
        call skip_arg_spaces
        cmp byte [esi], 0
        je .usage

        ; Parse output filename
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

        ; Initialize compiler state
        mov dword [src_pos],   0
        mov dword [out_pos],   0
        mov dword [src_line],  1
        mov dword [lineno_count], 0
        mov dword [fixup_count],  0
        mov dword [string_count], 0
        mov dword [for_sp],    0
        mov dword [while_sp],  0
        mov byte  [compile_error], 0

        ; Print status
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

        ; Emit fixed header (data section + runtime subroutines)
        call emit_header

        ; Compile the BASIC program
        call compile_program

        cmp byte [compile_error], 0
        jne .comp_err

        ; Emit implicit SYS_EXIT at end of code
        call emit_sys_exit

        ; Append string literal data and patch addresses
        call emit_string_data

        ; Resolve all forward GOTO/GOSUB fixups
        call resolve_fixups

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
        mov eax, SYS_PRINT
        mov ebx, msg_at_line
        int 0x80
        mov eax, [src_line]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.write_err:
        mov eax, SYS_PRINT
        mov ebx, msg_write_err
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

; -----------------------------------------------------------------------
; ARGUMENT PARSING
; -----------------------------------------------------------------------
parse_arg:
        ; Copy one whitespace-delimited word from [esi] to [edi]
.pa_loop:
        lodsb
        cmp al, ' '
        je .pa_done
        cmp al, 9           ; tab
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
        je .sas_skip
        cmp byte [esi], 9
        jne .sas_ret
.sas_skip:
        inc esi
        jmp skip_arg_spaces
.sas_ret:
        ret

; -----------------------------------------------------------------------
; LEXER
; -----------------------------------------------------------------------
; Reads next token from src_buffer[src_pos].
; Sets: tok_type, tok_value (for TOK_NUM), tok_var (for TOK_VAR),
;       tok_string (for TOK_STR).

next_token:
        pushad

.nt_restart:
        mov esi, src_buffer
        add esi, [src_pos]

        ; Skip spaces and tabs (but not newlines)
.nt_ws:
        mov al, [esi]
        cmp al, ' '
        je .nt_skip1
        cmp al, 9
        jne .nt_not_ws
.nt_skip1:
        inc esi
        inc dword [src_pos]
        jmp .nt_ws

.nt_not_ws:
        ; Check for end of source
        cmp al, 0
        je .nt_eof

        ; Newline: return TOK_NL
        cmp al, 0x0A
        je .nt_newline
        cmp al, 0x0D
        je .nt_cr

        ; Colon: return TOK_COLON
        cmp al, ':'
        je .nt_single_tok

        ; Semicolon
        cmp al, ';'
        je .nt_single_tok

        ; Comma
        cmp al, ','
        je .nt_single_tok

        ; Parentheses
        cmp al, '('
        je .nt_single_tok
        cmp al, ')'
        je .nt_single_tok

        ; Arithmetic operators
        cmp al, '+'
        je .nt_single_tok
        cmp al, '*'
        je .nt_single_tok
        cmp al, '/'
        je .nt_single_tok

        ; '-' (could be unary minus, handled by expression parser)
        cmp al, '-'
        je .nt_single_tok

        ; '=' assignment/comparison
        cmp al, '='
        je .nt_single_tok

        ; '<' (could be <> or <=)
        cmp al, '<'
        je .nt_lt_op

        ; '>' (could be >=)
        cmp al, '>'
        je .nt_gt_op

        ; Number literal
        cmp al, '0'
        jl .nt_not_num
        cmp al, '9'
        jg .nt_not_num
        jmp .nt_number

.nt_not_num:
        ; String literal
        cmp al, '"'
        je .nt_string

        ; REM comment: rest of line is skipped
        ; Identifier: letter, letter$, or keyword
        cmp al, 'A'
        jl .nt_try_lower
        cmp al, 'Z'
        jle .nt_ident
.nt_try_lower:
        cmp al, 'a'
        jl .nt_unknown
        cmp al, 'z'
        jg .nt_unknown
        sub al, 32              ; to upper
        jmp .nt_ident

.nt_unknown:
        ; Skip unknown character
        inc esi
        inc dword [src_pos]
        jmp .nt_restart

; --- Single-character tokens ---
.nt_single_tok:
        inc esi
        inc dword [src_pos]
        movzx eax, al
        ; Map character to token type
        cmp al, ':'
        je .nt_colon_t
        cmp al, ';'
        je .nt_semi_t
        cmp al, ','
        je .nt_comma_t
        cmp al, '('
        je .nt_lp_t
        cmp al, ')'
        je .nt_rp_t
        cmp al, '+'
        je .nt_plus_t
        cmp al, '-'
        je .nt_minus_t
        cmp al, '*'
        je .nt_star_t
        cmp al, '/'
        je .nt_slash_t
        ; '='
        mov dword [tok_type], TOK_EQ
        popad
        ret
.nt_colon_t:
        mov dword [tok_type], TOK_COLON
        popad
        ret
.nt_semi_t:
        mov dword [tok_type], TOK_SEMI
        popad
        ret
.nt_comma_t:
        mov dword [tok_type], TOK_COMMA
        popad
        ret
.nt_lp_t:
        mov dword [tok_type], TOK_LPAREN
        popad
        ret
.nt_rp_t:
        mov dword [tok_type], TOK_RPAREN
        popad
        ret
.nt_plus_t:
        mov dword [tok_type], TOK_PLUS
        popad
        ret
.nt_minus_t:
        mov dword [tok_type], TOK_MINUS
        popad
        ret
.nt_star_t:
        mov dword [tok_type], TOK_STAR
        popad
        ret
.nt_slash_t:
        mov dword [tok_type], TOK_SLASH
        popad
        ret

; --- '<' operator ---
.nt_lt_op:
        inc esi
        inc dword [src_pos]
        mov al, [esi]
        cmp al, '='
        je .nt_le_t
        cmp al, '>'
        je .nt_ne_t
        mov dword [tok_type], TOK_LT
        popad
        ret
.nt_le_t:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_LE
        popad
        ret
.nt_ne_t:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_NE
        popad
        ret

; --- '>' operator ---
.nt_gt_op:
        inc esi
        inc dword [src_pos]
        mov al, [esi]
        cmp al, '='
        je .nt_ge_t
        mov dword [tok_type], TOK_GT
        popad
        ret
.nt_ge_t:
        inc esi
        inc dword [src_pos]
        mov dword [tok_type], TOK_GE
        popad
        ret

; --- EOF ---
.nt_eof:
        mov dword [tok_type], TOK_EOF
        popad
        ret

; --- CR (treat as newline) ---
.nt_cr:
        inc esi
        inc dword [src_pos]
        ; If followed by LF, skip that too
        cmp byte [esi], 0x0A
        jne .nt_newline2
        inc esi
        inc dword [src_pos]
.nt_newline2:
        inc dword [src_line]
        mov dword [tok_type], TOK_NL
        popad
        ret

; --- Newline ---
.nt_newline:
        inc esi
        inc dword [src_pos]
        inc dword [src_line]
        mov dword [tok_type], TOK_NL
        popad
        ret

; --- Number literal ---
.nt_number:
        xor edx, edx
.nt_num_loop:
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
        jmp .nt_num_loop
.nt_num_done:
        mov dword [tok_type], TOK_NUM
        mov [tok_value], edx
        popad
        ret

; --- String literal ---
.nt_string:
        inc esi                 ; skip opening '"'
        inc dword [src_pos]
        mov edi, tok_string
        xor ecx, ecx
.nt_str_loop:
        mov al, [esi]
        cmp al, '"'
        je .nt_str_done
        cmp al, 0
        je .nt_str_done
        cmp al, 0x0A
        je .nt_str_done
        cmp ecx, STRING_MAX - 2
        jge .nt_str_skip
        mov [edi + ecx], al
        inc ecx
.nt_str_skip:
        inc esi
        inc dword [src_pos]
        jmp .nt_str_loop
.nt_str_done:
        mov byte [edi + ecx], 0
        cmp byte [esi], '"'
        jne .nt_str_end
        inc esi
        inc dword [src_pos]
.nt_str_end:
        mov dword [tok_type], TOK_STR
        popad
        ret

; --- Identifier / keyword ---
.nt_ident:
        ; Scan word (already uppercased first char in AL)
        ; Save start position for potential '$' suffix check
        mov edi, tok_ident
        xor ecx, ecx
        ; First char already in AL (uppercase)
.nt_id_store:
        mov [edi + ecx], al
        inc ecx
.nt_id_next:
        inc esi
        inc dword [src_pos]
        mov al, [esi]
        ; Uppercase
        cmp al, 'a'
        jl .nt_id_check
        cmp al, 'z'
        jg .nt_id_check
        sub al, 32
.nt_id_check:
        cmp al, 'A'
        jl .nt_id_sym
        cmp al, 'Z'
        jle .nt_id_store
        cmp al, '0'
        jl .nt_id_sym
        cmp al, '9'
        jle .nt_id_store
        cmp al, '$'             ; allow A$ etc for future; treat as end for now
        je .nt_id_sym
        cmp al, '_'
        jne .nt_id_sym
        jmp .nt_id_store
.nt_id_sym:
        mov byte [edi + ecx], 0
        ; Now match against keyword table
        call match_keywords
        cmp eax, 0
        jne .nt_id_kw
        ; Not a keyword: if single letter A-Z, it's a variable
        cmp ecx, 1
        jne .nt_id_syntax_skip
        movzx eax, byte [edi]
        cmp al, 'A'
        jl .nt_id_syntax_skip
        cmp al, 'Z'
        jg .nt_id_syntax_skip
        sub al, 'A'
        mov [tok_var], eax
        mov dword [tok_type], TOK_VAR
        popad
        ret
.nt_id_syntax_skip:
        ; Unknown multi-letter identifier; skip (could be unsupported feature)
        jmp .nt_restart
.nt_id_kw:
        ; If keyword is KW_REM, skip to end of line
        cmp eax, KW_REM
        je .nt_rem_skip
        mov [tok_type], eax
        popad
        ret
.nt_rem_skip:
        ; Skip to end of line
.nt_rem_loop:
        mov al, [esi]
        cmp al, 0
        je .nt_eof
        cmp al, 0x0A
        je .nt_rem_nl
        cmp al, 0x0D
        je .nt_rem_nl
        inc esi
        inc dword [src_pos]
        jmp .nt_rem_loop
.nt_rem_nl:
        mov dword [tok_type], TOK_NL
        popad
        ret

; -----------------------------------------------------------------------
; KEYWORD MATCHING
; Returns keyword token value in EAX, or 0 if not found.
; Input: tok_ident = null-terminated identifier (already uppercased)
; -----------------------------------------------------------------------
match_keywords:
        push esi
        push edi
        push ebx

        lea esi, [tok_ident]

        lea edi, [kw_print]
        call str_eq
        jc .mk_ret_print

        lea edi, [kw_input]
        call str_eq
        jc .mk_ret_input

        lea edi, [kw_let]
        call str_eq
        jc .mk_ret_let

        lea edi, [kw_if]
        call str_eq
        jc .mk_ret_if

        lea edi, [kw_then]
        call str_eq
        jc .mk_ret_then

        lea edi, [kw_else]
        call str_eq
        jc .mk_ret_else

        lea edi, [kw_goto]
        call str_eq
        jc .mk_ret_goto

        lea edi, [kw_gosub]
        call str_eq
        jc .mk_ret_gosub

        lea edi, [kw_return]
        call str_eq
        jc .mk_ret_return

        lea edi, [kw_for]
        call str_eq
        jc .mk_ret_for

        lea edi, [kw_to]
        call str_eq
        jc .mk_ret_to

        lea edi, [kw_step]
        call str_eq
        jc .mk_ret_step

        lea edi, [kw_next]
        call str_eq
        jc .mk_ret_next

        lea edi, [kw_while]
        call str_eq
        jc .mk_ret_while

        lea edi, [kw_wend]
        call str_eq
        jc .mk_ret_wend

        lea edi, [kw_end]
        call str_eq
        jc .mk_ret_end

        lea edi, [kw_stop]
        call str_eq
        jc .mk_ret_stop

        lea edi, [kw_rem]
        call str_eq
        jc .mk_ret_rem

        lea edi, [kw_cls]
        call str_eq
        jc .mk_ret_cls

        lea edi, [kw_beep]
        call str_eq
        jc .mk_ret_beep

        lea edi, [kw_sleep]
        call str_eq
        jc .mk_ret_sleep

        lea edi, [kw_mod]
        call str_eq
        jc .mk_ret_mod

        lea edi, [kw_and]
        call str_eq
        jc .mk_ret_and

        lea edi, [kw_or]
        call str_eq
        jc .mk_ret_or

        lea edi, [kw_not]
        call str_eq
        jc .mk_ret_not

        lea edi, [kw_xor]
        call str_eq
        jc .mk_ret_xor

        xor eax, eax
        jmp .mk_done

.mk_ret_print:  mov eax, KW_PRINT  ; fall through for ret
        jmp .mk_done
.mk_ret_input:  mov eax, KW_INPUT  ; fall through
        jmp .mk_done
.mk_ret_let:    mov eax, KW_LET
        jmp .mk_done
.mk_ret_if:     mov eax, KW_IF
        jmp .mk_done
.mk_ret_then:   mov eax, KW_THEN
        jmp .mk_done
.mk_ret_else:   mov eax, KW_ELSE
        jmp .mk_done
.mk_ret_goto:   mov eax, KW_GOTO
        jmp .mk_done
.mk_ret_gosub:  mov eax, KW_GOSUB
        jmp .mk_done
.mk_ret_return: mov eax, KW_RETURN
        jmp .mk_done
.mk_ret_for:    mov eax, KW_FOR
        jmp .mk_done
.mk_ret_to:     mov eax, KW_TO
        jmp .mk_done
.mk_ret_step:   mov eax, KW_STEP
        jmp .mk_done
.mk_ret_next:   mov eax, KW_NEXT
        jmp .mk_done
.mk_ret_while:  mov eax, KW_WHILE
        jmp .mk_done
.mk_ret_wend:   mov eax, KW_WEND
        jmp .mk_done
.mk_ret_end:    mov eax, KW_END
        jmp .mk_done
.mk_ret_stop:   mov eax, KW_STOP
        jmp .mk_done
.mk_ret_rem:    mov eax, KW_REM
        jmp .mk_done
.mk_ret_cls:    mov eax, KW_CLS
        jmp .mk_done
.mk_ret_beep:   mov eax, KW_BEEP
        jmp .mk_done
.mk_ret_sleep:  mov eax, KW_SLEEP
        jmp .mk_done
.mk_ret_mod:    mov eax, KW_MOD
        jmp .mk_done
.mk_ret_and:    mov eax, KW_AND
        jmp .mk_done
.mk_ret_or:     mov eax, KW_OR
        jmp .mk_done
.mk_ret_not:    mov eax, KW_NOT
        jmp .mk_done
.mk_ret_xor:    mov eax, KW_XOR

.mk_done:
        pop ebx
        pop edi
        pop esi
        ret

; str_eq: compare [esi] (tok_ident) with [edi] (keyword)
; Sets CF if equal (case-insensitive already handled since tok_ident uppercased)
str_eq:
        push eax
        push esi
        push edi
.se_loop:
        mov al, [esi]
        cmp al, [edi]
        jne .se_ne
        cmp al, 0
        je .se_eq
        inc esi
        inc edi
        jmp .se_loop
.se_eq:
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

; -----------------------------------------------------------------------
; RECORD / LOOKUP LINE NUMBER
; -----------------------------------------------------------------------
record_lineno:
        ; Record (linenum, out_pos) in lineno_table
        ; Input: EAX = linenum
        push ebx
        push ecx
        mov ecx, [lineno_count]
        cmp ecx, MAX_LINENOS
        jge .rl_full
        imul ebx, ecx, 8
        mov [lineno_table + ebx],     eax   ; line number
        mov ecx, [out_pos]
        mov [lineno_table + ebx + 4], ecx   ; out_pos at this line
        inc dword [lineno_count]
.rl_full:
        pop ecx
        pop ebx
        ret

find_lineno_outpos:
        ; Find out_pos for linenum EAX in lineno_table
        ; Returns EAX = out_pos, or -1 if not found
        push ebx
        push ecx
        mov ecx, [lineno_count]
        xor ebx, ebx
.fl_loop:
        cmp ebx, ecx
        jge .fl_nf
        imul edx, ebx, 8
        cmp eax, [lineno_table + edx]
        je .fl_found
        inc ebx
        jmp .fl_loop
.fl_found:
        imul edx, ebx, 8
        mov eax, [lineno_table + edx + 4]
        pop ecx
        pop ebx
        ret
.fl_nf:
        mov eax, -1
        pop ecx
        pop ebx
        ret

; -----------------------------------------------------------------------
; EMIT HELPERS
; -----------------------------------------------------------------------
emit_byte:
        ; Emits AL into out_buffer
        push edi
        mov edi, [out_pos]
        cmp edi, MAX_OUT - 1
        jge .eb_overflow
        mov [out_buffer + edi], al
        inc dword [out_pos]
.eb_overflow:
        pop edi
        ret

emit_dword:
        ; Emits EAX (4 bytes) into out_buffer
        push edi
        mov edi, [out_pos]
        mov [out_buffer + edi], eax
        add dword [out_pos], 4
        pop edi
        ret

emit_zeros:
        ; Emits ECX zero bytes
        push eax
        push ecx
        xor eax, eax
.ez_loop:
        cmp ecx, 0
        jle .ez_done
        mov al, 0
        call emit_byte
        dec ecx
        jmp .ez_loop
.ez_done:
        pop ecx
        pop eax
        ret

; -----------------------------------------------------------------------
; EMIT HEADER (0x600 bytes of prefix: data section + runtime routines)
; -----------------------------------------------------------------------
emit_header:
        pushad

        ; ---- Byte 0-4: JMP rel32 to CODE_START ----
        mov al, 0xE9                ; JMP rel32
        call emit_byte
        mov eax, JMP_DELTA          ; = 0x5FB = 1531
        call emit_dword
        ; out_pos = 5

        ; ---- Bytes 5 - 0xFF: zeros ----
        mov ecx, 0x100 - 5
        call emit_zeros
        ; out_pos = 0x100

        ; ---- Bytes 0x100-0x167: vars A-Z (104 bytes of zeros) ----
        mov ecx, 26 * 4
        call emit_zeros
        ; out_pos = 0x168

        ; ---- Bytes 0x168-0x1A7: for_end_vals[16] ----
        mov ecx, 16 * 4
        call emit_zeros
        ; out_pos = 0x1A8

        ; ---- Bytes 0x1A8-0x1E7: for_step_vals[16] ----
        mov ecx, 16 * 4
        call emit_zeros
        ; out_pos = 0x1E8

        ; ---- Bytes 0x1E8-0x1EB: rand_seed = 12345 ----
        mov eax, 12345
        call emit_dword
        ; out_pos = 0x1EC

        ; ---- Bytes 0x1EC-0x1EF: padding ----
        mov ecx, 4
        call emit_zeros
        ; out_pos = 0x1F0

        ; ---- Bytes 0x1F0-0x2EF: input_buf (256 zeros) ----
        mov ecx, 256
        call emit_zeros
        ; out_pos = 0x2F0

        ; ---- Bytes 0x2F0-0x30F: print_buf (32 zeros) ----
        mov ecx, 32
        call emit_zeros
        ; out_pos = 0x310

        ; ---- Bytes 0x310-0x312: "? \0" ----
        mov al, '?'
        call emit_byte
        mov al, ' '
        call emit_byte
        mov al, 0
        call emit_byte
        ; out_pos = 0x313

        ; ---- Bytes 0x313-0x3FF: zeros (padding to 0x400) ----
        mov ecx, 0x400 - 0x313
        call emit_zeros
        ; out_pos = 0x400

        ; ---- Bytes 0x400-0x4FF: rt_input_int subroutine ----
        call emit_rt_input_int
        ; Pad to 0x500
        mov ecx, 0x500
        sub ecx, [out_pos]
        cmp ecx, 0
        jle .eh_no_pad1
        call emit_zeros
.eh_no_pad1:
        ; out_pos = 0x500

        ; ---- Bytes 0x500-0x55F: rt_print_int subroutine ----
        call emit_rt_print_int
        ; Pad to 0x600
        mov ecx, 0x600
        sub ecx, [out_pos]
        cmp ecx, 0
        jle .eh_no_pad2
        call emit_zeros
.eh_no_pad2:
        ; out_pos = 0x600 = CODE_START_OFF

        popad
        ret

; -----------------------------------------------------------------------
; rt_input_int: emits the input-integer subroutine at current out_pos.
; Reads a line with echo/backspace, parses as signed integer, returns in EAX.
; Called via CALL from compiled INPUT statements.
;
; Byte-exact encoding (180 bytes) for the subroutine at RT_INPUT_ADDR:
;  prints "? ", reads chars with echo into INPUTBUF_ADDR,
;  then parses to integer.
; -----------------------------------------------------------------------
emit_rt_input_int:
        pushad
        ; mov eax, 3 (SYS_PRINT)
        mov al, 0xB8 ; call emit_byte
        call emit_byte
        mov eax, SYS_PRINT
        call emit_dword
        ; mov ebx, PROMPT_ADDR
        mov al, 0xBB
        call emit_byte
        mov eax, PROMPT_ADDR
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; mov edi, INPUTBUF_ADDR
        mov al, 0xBF
        call emit_byte
        mov eax, INPUTBUF_ADDR
        call emit_dword
        ; xor ecx, ecx
        mov al, 0x31
        call emit_byte
        mov al, 0xC9
        call emit_byte
        ; --- .inp_loop: (offset 19 from routine start) ---
        ; mov eax, 2 (SYS_GETCHAR)
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_GETCHAR
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; cmp al, 0x0D
        mov al, 0x3C
        call emit_byte
        mov al, 0x0D
        call emit_byte
        ; je .inp_done_ln (+80)
        mov al, 0x74
        call emit_byte
        mov al, 80
        call emit_byte
        ; cmp al, 0x0A
        mov al, 0x3C
        call emit_byte
        mov al, 0x0A
        call emit_byte
        ; je .inp_done_ln (+76)
        mov al, 0x74
        call emit_byte
        mov al, 76
        call emit_byte
        ; cmp al, 0x08
        mov al, 0x3C
        call emit_byte
        mov al, 0x08
        call emit_byte
        ; je .inp_bs (+27)
        mov al, 0x74
        call emit_byte
        mov al, 27
        call emit_byte
        ; cmp al, 0x7F
        mov al, 0x3C
        call emit_byte
        mov al, 0x7F
        call emit_byte
        ; je .inp_bs (+23)
        mov al, 0x74
        call emit_byte
        mov al, 23
        call emit_byte
        ; cmp ecx, 255 (83 F9 FF)
        mov al, 0x83
        call emit_byte
        mov al, 0xF9
        call emit_byte
        mov al, 255
        call emit_byte
        ; jge .inp_loop (-28 = 0xE4)
        mov al, 0x7D
        call emit_byte
        mov al, 0xE4
        call emit_byte
        ; mov [edi+ecx], al (88 04 0F)
        mov al, 0x88
        call emit_byte
        mov al, 0x04
        call emit_byte
        mov al, 0x0F
        call emit_byte
        ; inc ecx (41)
        mov al, 0x41
        call emit_byte
        ; push ecx (51)
        mov al, 0x51
        call emit_byte
        ; movzx ebx, al (0F B6 D8)
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xD8
        call emit_byte
        ; mov eax, 1 (SYS_PUTCHAR)
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; pop ecx (59)
        mov al, 0x59
        call emit_byte
        ; jmp .inp_loop (-46 = 0xD2)
        mov al, 0xEB
        call emit_byte
        mov al, 0xD2
        call emit_byte
        ; --- .inp_bs: (offset 65) ---
        ; test ecx, ecx (85 C9)
        mov al, 0x85
        call emit_byte
        mov al, 0xC9
        call emit_byte
        ; jz .inp_loop (-50 = 0xCE)
        mov al, 0x74
        call emit_byte
        mov al, 0xCE
        call emit_byte
        ; dec ecx (49)
        mov al, 0x49
        call emit_byte
        ; push ecx (51)
        mov al, 0x51
        call emit_byte
        ; mov eax, 1
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        ; mov ebx, 8
        mov al, 0xBB
        call emit_byte
        mov eax, 8
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; mov eax, 1
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        ; mov ebx, 0x20
        mov al, 0xBB
        call emit_byte
        mov eax, 0x20
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; mov eax, 1
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        ; mov ebx, 8
        mov al, 0xBB
        call emit_byte
        mov eax, 8
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; pop ecx (59)
        mov al, 0x59
        call emit_byte
        ; jmp .inp_loop (-91 = 0xA5)
        mov al, 0xEB
        call emit_byte
        mov al, 0xA5
        call emit_byte
        ; --- .inp_done_ln: (offset 110) ---
        ; mov byte [edi+ecx], 0  (C6 04 0F 00)
        mov al, 0xC6
        call emit_byte
        mov al, 0x04
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x00
        call emit_byte
        ; mov eax, 1
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        ; mov ebx, 0x0A
        mov al, 0xBB
        call emit_byte
        mov eax, 0x0A
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; mov esi, INPUTBUF_ADDR (BE addr)
        mov al, 0xBE
        call emit_byte
        mov eax, INPUTBUF_ADDR
        call emit_dword
        ; xor eax, eax (31 C0)
        mov al, 0x31
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ; xor edx, edx (31 D2)
        mov al, 0x31
        call emit_byte
        mov al, 0xD2
        call emit_byte
        ; cmp byte [esi], '-' (80 3E 2D)
        mov al, 0x80
        call emit_byte
        mov al, 0x3E
        call emit_byte
        mov al, 0x2D
        call emit_byte
        ; jne +4 (75 04)
        mov al, 0x75
        call emit_byte
        mov al, 0x04
        call emit_byte
        ; inc esi (46)
        mov al, 0x46
        call emit_byte
        ; mov edx, 1 (BA 01 00 00 00)
        mov al, 0xBA
        call emit_byte
        mov eax, 1
        call emit_dword
        ; --- .inp_digit_loop: (offset 146) ---
        ; movzx ecx, byte [esi] (0F B6 0E)
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0x0E
        call emit_byte
        ; cmp cl, 0x30 (80 F9 30)
        mov al, 0x80
        call emit_byte
        mov al, 0xF9
        call emit_byte
        mov al, 0x30
        call emit_byte
        ; jl +19 (7C 13)
        mov al, 0x7C
        call emit_byte
        mov al, 0x13
        call emit_byte
        ; cmp cl, 0x39 (80 F9 39)
        mov al, 0x80
        call emit_byte
        mov al, 0xF9
        call emit_byte
        mov al, 0x39
        call emit_byte
        ; jg +14 (7F 0E)
        mov al, 0x7F
        call emit_byte
        mov al, 0x0E
        call emit_byte
        ; imul eax, eax, 10 (6B C0 0A)
        mov al, 0x6B
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 10
        call emit_byte
        ; sub cl, 0x30 (80 E9 30)
        mov al, 0x80
        call emit_byte
        mov al, 0xE9
        call emit_byte
        mov al, 0x30
        call emit_byte
        ; movzx ecx, cl (0F B6 C9)
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC9
        call emit_byte
        ; add eax, ecx (03 C1)
        mov al, 0x03
        call emit_byte
        mov al, 0xC1
        call emit_byte
        ; inc esi (46)
        mov al, 0x46
        call emit_byte
        ; jmp .inp_digit_loop (-27 = 0xE5)
        mov al, 0xEB
        call emit_byte
        mov al, 0xE5
        call emit_byte
        ; --- .inp_parse_done: (offset 173) ---
        ; test edx, edx (85 D2)
        mov al, 0x85
        call emit_byte
        mov al, 0xD2
        call emit_byte
        ; jz +2 (74 02)
        mov al, 0x74
        call emit_byte
        mov al, 0x02
        call emit_byte
        ; neg eax (F7 D8)
        mov al, 0xF7
        call emit_byte
        mov al, 0xD8
        call emit_byte
        ; ret (C3)
        mov al, 0xC3
        call emit_byte

        popad
        ret

; -----------------------------------------------------------------------
; rt_print_int: emits the print-integer subroutine (85 bytes).
; Prints EAX as signed decimal via SYS_PUTCHAR.
; Uses PRINTBUF_ADDR+31 as scratch buffer.
; -----------------------------------------------------------------------
emit_rt_print_int:
        pushad

        ; test eax, eax (85 C0)
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ; jnz +14 (75 0E)
        mov al, 0x75
        call emit_byte
        mov al, 0x0E
        call emit_byte
        ; --- zero path ---
        ; mov eax, 1 (SYS_PUTCHAR)
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        ; mov ebx, '0'
        mov al, 0xBB
        call emit_byte
        mov eax, 0x30
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; jmp +66 (EB 42)
        mov al, 0xEB
        call emit_byte
        mov al, 0x42
        call emit_byte
        ; --- .nonzero: ---
        ; test eax, eax (85 C0)
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ; jns +16 (79 10)
        mov al, 0x79
        call emit_byte
        mov al, 0x10
        call emit_byte
        ; --- negative path ---
        ; push eax (50)
        mov al, 0x50
        call emit_byte
        ; mov eax, 1
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        ; mov ebx, '-'
        mov al, 0xBB
        call emit_byte
        mov eax, 0x2D
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; pop eax (58)
        mov al, 0x58
        call emit_byte
        ; neg eax (F7 D8)
        mov al, 0xF7
        call emit_byte
        mov al, 0xD8
        call emit_byte
        ; --- .positive: ---
        ; mov edi, PRINTBUF_ADDR+31  (BF addr)
        mov al, 0xBF
        call emit_byte
        mov eax, PRINTBUF_ADDR + 31
        call emit_dword
        ; mov byte [edi], 0  (C6 07 00)
        mov al, 0xC6
        call emit_byte
        mov al, 0x07
        call emit_byte
        mov al, 0x00
        call emit_byte
        ; mov ecx, 10  (B9 0A 00 00 00)
        mov al, 0xB9
        call emit_byte
        mov eax, 10
        call emit_dword
        ; --- .div_loop: ---
        ; xor edx, edx  (31 D2)
        mov al, 0x31
        call emit_byte
        mov al, 0xD2
        call emit_byte
        ; div ecx  (F7 F1)
        mov al, 0xF7
        call emit_byte
        mov al, 0xF1
        call emit_byte
        ; add dl, '0'  (80 C2 30)
        mov al, 0x80
        call emit_byte
        mov al, 0xC2
        call emit_byte
        mov al, 0x30
        call emit_byte
        ; dec edi  (4F)
        mov al, 0x4F
        call emit_byte
        ; mov [edi], dl  (88 17)
        mov al, 0x88
        call emit_byte
        mov al, 0x17
        call emit_byte
        ; test eax, eax  (85 C0)
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ; jnz .div_loop  (75 F2)
        mov al, 0x75
        call emit_byte
        mov al, 0xF2
        call emit_byte
        ; --- .print_loop: ---
        ; movzx ebx, byte [edi]  (0F B6 1F)
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0x1F
        call emit_byte
        ; test bl, bl  (84 DB)
        mov al, 0x84
        call emit_byte
        mov al, 0xDB
        call emit_byte
        ; jz .done  (74 0C)
        mov al, 0x74
        call emit_byte
        mov al, 0x0C
        call emit_byte
        ; push edi  (57)
        mov al, 0x57
        call emit_byte
        ; mov eax, 1
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ; pop edi  (5F)
        mov al, 0x5F
        call emit_byte
        ; inc edi  (47)
        mov al, 0x47
        call emit_byte
        ; jmp .print_loop  (EB ED)
        mov al, 0xEB
        call emit_byte
        mov al, 0xED
        call emit_byte
        ; --- .done: ---
        ; ret  (C3)
        mov al, 0xC3
        call emit_byte

        popad
        ret

; -----------------------------------------------------------------------
; MAIN COMPILATION LOOP
; -----------------------------------------------------------------------
compile_program:
        pushad
        call next_token

.cp_loop:
        cmp dword [tok_type], TOK_EOF
        je .cp_done

        ; Skip blank lines
        cmp dword [tok_type], TOK_NL
        je .cp_next_tok

        ; Line number
        cmp dword [tok_type], TOK_NUM
        jne .cp_stmt

        mov eax, [tok_value]
        call record_lineno          ; record lineno → out_pos
        call next_token             ; consume the line number

        ; Check for blank numbered line (NL or EOF after line number)
        cmp dword [tok_type], TOK_NL
        je .cp_next_tok
        cmp dword [tok_type], TOK_EOF
        je .cp_done

.cp_stmt:
        call compile_statement
        cmp byte [compile_error], 0
        jne .cp_done

        ; After statement: handle ':' (multi-stmt) or consume NL
.cp_after_stmt:
        cmp dword [tok_type], TOK_COLON
        je .cp_colon
        cmp dword [tok_type], TOK_NL
        je .cp_next_tok
        cmp dword [tok_type], TOK_EOF
        je .cp_done
        jmp .cp_next_tok    ; skip unexpected tokens

.cp_colon:
        call next_token
        jmp .cp_stmt

.cp_next_tok:
        call next_token
        jmp .cp_loop

.cp_done:
        popad
        ret

; -----------------------------------------------------------------------
; COMPILE_STATEMENT: dispatches to per-keyword compilers
; -----------------------------------------------------------------------
compile_statement:
        ; Note: does NOT call next_token on entry; current tok is the keyword
        cmp dword [tok_type], KW_PRINT
        je .do_print
        cmp dword [tok_type], KW_INPUT
        je .do_input
        cmp dword [tok_type], KW_LET
        je .do_let
        cmp dword [tok_type], KW_IF
        je .do_if
        cmp dword [tok_type], KW_GOTO
        je .do_goto
        cmp dword [tok_type], KW_GOSUB
        je .do_gosub
        cmp dword [tok_type], KW_RETURN
        je .do_return
        cmp dword [tok_type], KW_FOR
        je .do_for
        cmp dword [tok_type], KW_NEXT
        je .do_next
        cmp dword [tok_type], KW_WHILE
        je .do_while
        cmp dword [tok_type], KW_WEND
        je .do_wend
        cmp dword [tok_type], KW_END
        je .do_end
        cmp dword [tok_type], KW_STOP
        je .do_end
        cmp dword [tok_type], KW_CLS
        je .do_cls
        cmp dword [tok_type], KW_BEEP
        je .do_beep
        cmp dword [tok_type], KW_SLEEP
        je .do_sleep
        cmp dword [tok_type], TOK_VAR
        je .do_assign
        ; Unknown - skip this token
        call next_token
        ret

.do_print:
        call next_token
        call compile_print
        ret
.do_input:
        call next_token
        call compile_input
        ret
.do_let:
        call next_token
        ; fall through to assign
.do_assign:
        call compile_assign
        ret
.do_if:
        call next_token
        call compile_if
        ret
.do_goto:
        call next_token
        call compile_goto
        ret
.do_gosub:
        call next_token
        call compile_gosub
        ret
.do_return:
        call next_token
        call compile_return
        ret
.do_for:
        call next_token
        call compile_for
        ret
.do_next:
        call next_token
        call compile_next
        ret
.do_while:
        call next_token
        call compile_while
        ret
.do_wend:
        call next_token
        call compile_wend
        ret
.do_end:
        call next_token
        call emit_sys_exit
        ret
.do_cls:
        call next_token
        call compile_cls
        ret
.do_beep:
        call next_token
        call compile_beep
        ret
.do_sleep:
        call next_token
        call compile_sleep
        ret

; -----------------------------------------------------------------------
; PRINT: PRINT [item [;|,] item ...]
; -----------------------------------------------------------------------
compile_print:
        push ebx
        ; Track whether we need to print a newline at end
        ; Default: print newline after all items (unless trailing ;)
        mov byte [print_need_nl], 1

.cp_item:
        cmp dword [tok_type], TOK_NL
        je .cp_end_print
        cmp dword [tok_type], TOK_EOF
        je .cp_end_print
        cmp dword [tok_type], TOK_COLON
        je .cp_end_print

        cmp dword [tok_type], TOK_STR
        je .cp_string_item

        ; Separator check first
        cmp dword [tok_type], TOK_SEMI
        je .cp_semi
        cmp dword [tok_type], TOK_COMMA
        je .cp_comma

        ; Numeric expression item
        call compile_expr_or
        ; EAX = result → emit CALL rt_print_int
        ; E8 rel32
        mov al, 0xE8
        call emit_byte
        ; rel32 = RT_PRINT_ADDR - (BASE_ADDR + out_pos + 4)
        mov eax, RT_PRINT_ADDR
        sub eax, BASE_ADDR
        sub eax, [out_pos]
        sub eax, 4
        call emit_dword
        mov byte [print_need_nl], 1
        jmp .cp_item

.cp_string_item:
        ; Emit print string literal inline
        call compile_print_str_lit
        mov byte [print_need_nl], 1
        call next_token
        jmp .cp_item

.cp_semi:
        ; No separator, no newline between items
        mov byte [print_need_nl], 0
        call next_token
        jmp .cp_item

.cp_comma:
        ; Print a tab character as separator
        ; mov eax, 1; mov ebx, 9; int 0x80
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        mov al, 0xBB
        call emit_byte
        mov eax, 9          ; TAB
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        mov byte [print_need_nl], 0
        call next_token
        jmp .cp_item

.cp_end_print:
        ; Print newline if needed
        cmp byte [print_need_nl], 0
        je .cp_print_done
        ; mov eax, SYS_PUTCHAR; mov ebx, 0x0A; int 0x80
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PUTCHAR
        call emit_dword
        mov al, 0xBB
        call emit_byte
        mov eax, 0x0A
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
.cp_print_done:
        pop ebx
        ret

; Emit code to print string literal in tok_string via SYS_PRINT
compile_print_str_lit:
        push eax
        ; Store string and record fixup
        call store_string           ; returns index in EAX
        ; Emit: mov eax, SYS_PRINT
        push eax                    ; save string index
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_PRINT
        call emit_dword
        ; Emit: mov ebx, string_addr_placeholder
        mov al, 0xBB
        call emit_byte
        ; Record fixup: out_pos is where we need to patch
        mov eax, [out_pos]
        pop ebx                     ; string index
        mov [string_fixups + ebx * 4], eax
        mov eax, 0                  ; placeholder addr
        call emit_dword
        ; Emit: int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        pop eax
        ret

; -----------------------------------------------------------------------
; INPUT: INPUT ["prompt";] var
; -----------------------------------------------------------------------
compile_input:
        ; Optional string prompt
        cmp dword [tok_type], TOK_STR
        jne .ci2_no_prompt
        call compile_print_str_lit
        call next_token
        cmp dword [tok_type], TOK_SEMI
        je .ci2_sep
        cmp dword [tok_type], TOK_COMMA
        jne .ci2_get_var
.ci2_sep:
        call next_token
.ci2_no_prompt:
.ci2_get_var:
        cmp dword [tok_type], TOK_VAR
        jne .ci2_done
        mov eax, [tok_var]
        mov [ci2_var_idx], eax

        ; Emit CALL rt_input_int → result in EAX
        mov al, 0xE8
        call emit_byte
        mov eax, RT_INPUT_ADDR
        sub eax, BASE_ADDR
        sub eax, [out_pos]
        sub eax, 4
        call emit_dword

        ; Emit: A3 [var_addr]  (mov [VARS_ADDR + idx*4], eax)
        mov al, 0xA3
        call emit_byte
        mov eax, [ci2_var_idx]
        imul eax, 4
        add eax, VARS_ADDR
        call emit_dword

        call next_token
.ci2_done:
        ret

; -----------------------------------------------------------------------
; LET / ASSIGN: var = expr
; -----------------------------------------------------------------------
compile_assign:
        ; Current token should be TOK_VAR
        cmp dword [tok_type], TOK_VAR
        jne .ca_done
        mov eax, [tok_var]
        mov [ca_var_idx], eax

        call next_token
        ; Expect '='
        cmp dword [tok_type], TOK_EQ
        jne .ca_done
        call next_token

        ; Compile expression → EAX
        call compile_expr_or

        ; Emit: A3 [var_addr]  (mov [addr], eax)
        mov al, 0xA3
        call emit_byte
        mov eax, [ca_var_idx]
        imul eax, 4
        add eax, VARS_ADDR
        call emit_dword

.ca_done:
        ret

; -----------------------------------------------------------------------
; IF: IF expr THEN [lineno | stmt] [ELSE [lineno | stmt]]
; -----------------------------------------------------------------------
compile_if:
        ; Compile condition expression → EAX
        call compile_expr_or

        ; Emit: test eax, eax  (85 C0)
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte

        ; Emit: JZ rel32 placeholder  (0F 84 [4 bytes])
        mov al, 0x0F
        call emit_byte
        mov al, 0x84
        call emit_byte
        mov eax, [out_pos]
        mov [if_jz_fixup], eax    ; position of rel32 field
        xor eax, eax
        call emit_dword

        ; Expect THEN
        cmp dword [tok_type], KW_THEN
        jne .cif_error
        call next_token

        ; THEN clause: line number or statement?
        cmp dword [tok_type], TOK_NUM
        je .cif_then_lineno

        ; THEN statement
        call compile_statement
        jmp .cif_check_else

.cif_then_lineno:
        ; Emit JMP to lineno (might need fixup)
        mov eax, [tok_value]
        call emit_jmp_to_lineno
        call next_token
        jmp .cif_check_else

.cif_check_else:
        ; Patch JZ to here (or just past JMP if else follows)
        cmp dword [tok_type], KW_ELSE
        je .cif_has_else

        ; No ELSE: patch JZ to current out_pos
        call patch_jz_fixup
        ret

.cif_has_else:
        ; Emit unconditional JMP over ELSE part
        mov al, 0xE9
        call emit_byte
        mov eax, [out_pos]
        mov [if_jmp_fixup], eax
        xor eax, eax
        call emit_dword

        ; Patch JZ to here (= start of else clause)
        call patch_jz_fixup

        call next_token         ; consume ELSE

        cmp dword [tok_type], TOK_NUM
        je .cif_else_lineno

        ; ELSE statement
        call compile_statement
        jmp .cif_patch_jmp

.cif_else_lineno:
        mov eax, [tok_value]
        call emit_jmp_to_lineno
        call next_token

.cif_patch_jmp:
        ; Patch unconditional JMP to current out_pos
        mov eax, [out_pos]
        sub eax, [if_jmp_fixup]
        sub eax, 4
        mov ebx, [if_jmp_fixup]
        mov [out_buffer + ebx], eax
        ret

.cif_error:
        mov byte [compile_error], 1
        ret

patch_jz_fixup:
        ; Patch out_buffer[if_jz_fixup] = out_pos - if_jz_fixup - 4
        mov eax, [out_pos]
        sub eax, [if_jz_fixup]
        sub eax, 4
        mov ebx, [if_jz_fixup]
        mov [out_buffer + ebx], eax
        ret

; -----------------------------------------------------------------------
; GOTO lineno
; -----------------------------------------------------------------------
compile_goto:
        cmp dword [tok_type], TOK_NUM
        jne .cg_done
        mov eax, [tok_value]
        call emit_jmp_to_lineno
        call next_token
.cg_done:
        ret

; -----------------------------------------------------------------------
; GOSUB lineno
; -----------------------------------------------------------------------
compile_gosub:
        cmp dword [tok_type], TOK_NUM
        jne .cgs_done
        mov eax, [tok_value]
        call emit_call_to_lineno
        call next_token
.cgs_done:
        ret

; -----------------------------------------------------------------------
; RETURN
; -----------------------------------------------------------------------
compile_return:
        ; Emit RET (C3)
        mov al, 0xC3
        call emit_byte
        ret

; -----------------------------------------------------------------------
; Emit JMP to lineno (GOTO helper).
; If lineno already seen: emit direct JMP rel32.
; If not seen yet: emit JMP with 0 and record fixup.
; -----------------------------------------------------------------------
emit_jmp_to_lineno:
        ; Input: EAX = target line number
        push ebx
        push ecx
        push edx
        mov ecx, eax            ; save linenum

        call find_lineno_outpos
        ; EAX = target out_pos, or -1 if not found yet

        ; Emit E9 opcode
        push eax                ; save target
        mov al, 0xE9
        call emit_byte

        pop eax                 ; target out_pos or -1
        cmp eax, -1
        je .ejl_fixup

        ; Known address: compute rel32
        sub eax, [out_pos]
        sub eax, 4
        call emit_dword
        jmp .ejl_done

.ejl_fixup:
        ; Record forward fixup
        mov edx, [fixup_count]
        cmp edx, MAX_FIXUPS
        jge .ejl_done
        imul ebx, edx, 12
        mov [fixup_table + ebx],     ecx   ; target linenum
        mov eax, [out_pos]
        mov [fixup_table + ebx + 4], eax   ; out_pos of rel32 field
        mov dword [fixup_table + ebx + 8], 0  ; type 0 = JMP
        inc dword [fixup_count]
        xor eax, eax
        call emit_dword         ; placeholder

.ejl_done:
        pop edx
        pop ecx
        pop ebx
        ret

; -----------------------------------------------------------------------
; Emit CALL to lineno (GOSUB helper).
; -----------------------------------------------------------------------
emit_call_to_lineno:
        push ebx
        push ecx
        push edx
        mov ecx, eax            ; save linenum

        call find_lineno_outpos

        push eax
        mov al, 0xE8            ; CALL rel32
        call emit_byte

        pop eax
        cmp eax, -1
        je .ecl_fixup

        sub eax, [out_pos]
        sub eax, 4
        call emit_dword
        jmp .ecl_done

.ecl_fixup:
        mov edx, [fixup_count]
        cmp edx, MAX_FIXUPS
        jge .ecl_done
        imul ebx, edx, 12
        mov [fixup_table + ebx],     ecx
        mov eax, [out_pos]
        mov [fixup_table + ebx + 4], eax
        mov dword [fixup_table + ebx + 8], 1  ; type 1 = CALL
        inc dword [fixup_count]
        xor eax, eax
        call emit_dword

.ecl_done:
        pop edx
        pop ecx
        pop ebx
        ret

; -----------------------------------------------------------------------
; FOR / NEXT
; -----------------------------------------------------------------------
compile_for:
        ; FOR var = start TO end [STEP step]
        cmp dword [tok_type], TOK_VAR
        jne .cf_error
        mov eax, [tok_var]
        mov [for_cur_var], eax
        call next_token

        ; Expect '='
        cmp dword [tok_type], TOK_EQ
        jne .cf_error
        call next_token

        ; Compile start → EAX
        call compile_expr_or

        ; Emit: A3 [var_addr]  (store start to var)
        mov al, 0xA3
        call emit_byte
        mov eax, [for_cur_var]
        imul eax, 4
        add eax, VARS_ADDR
        call emit_dword

        ; Expect TO
        cmp dword [tok_type], KW_TO
        jne .cf_error
        call next_token

        ; Get current for_sp as level
        mov eax, [for_sp]
        cmp eax, FOR_STACK_MAX
        jge .cf_error
        mov [for_cur_sp], eax

        ; Compile end → EAX
        call compile_expr_or

        ; Emit: A3 [FOR_END_ADDR + sp*4]  (store end value)
        mov al, 0xA3
        call emit_byte
        mov eax, [for_cur_sp]
        imul eax, 4
        add eax, FOR_END_ADDR
        call emit_dword

        ; Check for STEP
        cmp dword [tok_type], KW_STEP
        je .cf_has_step

        ; Default step = 1: Emit mov [FOR_STEP_ADDR + sp*4], 1
        ; C7 05 [addr] 01 00 00 00
        mov al, 0xC7
        call emit_byte
        mov al, 0x05
        call emit_byte
        mov eax, [for_cur_sp]
        imul eax, 4
        add eax, FOR_STEP_ADDR
        call emit_dword
        mov eax, 1
        call emit_dword
        jmp .cf_loop_top

.cf_has_step:
        call next_token
        ; Compile step expression → EAX
        call compile_expr_or
        ; Emit: A3 [FOR_STEP_ADDR + sp*4]
        mov al, 0xA3
        call emit_byte
        mov eax, [for_cur_sp]
        imul eax, 4
        add eax, FOR_STEP_ADDR
        call emit_dword

.cf_loop_top:
        ; Record loop top address in for_loop_top_stack[sp]
        mov eax, [for_cur_sp]
        mov ebx, [out_pos]
        mov [for_loop_top_stack + eax * 4], ebx

        ; Emit condition check (45 bytes):
        ; Load var → EAX
        ; A1 [var_addr] (5 bytes)
        mov al, 0xA1
        call emit_byte
        mov eax, [for_cur_var]
        imul eax, 4
        add eax, VARS_ADDR
        call emit_dword

        ; Load step → ECX
        ; 8B 0D [FOR_STEP_ADDR + sp*4] (6 bytes)
        mov al, 0x8B
        call emit_byte
        mov al, 0x0D
        call emit_byte
        mov eax, [for_cur_sp]
        imul eax, 4
        add eax, FOR_STEP_ADDR
        call emit_dword

        ; test ecx, ecx (85 C9) (2 bytes)
        mov al, 0x85
        call emit_byte
        mov al, 0xC9
        call emit_byte

        ; jns +16 → to pos_step block (79 10) (2 bytes)
        mov al, 0x79
        call emit_byte
        mov al, 0x10
        call emit_byte

        ; --- negative step block (16 bytes) ---
        ; Load end → EDX: 8B 15 [FOR_END_ADDR + sp*4] (6 bytes)
        mov al, 0x8B
        call emit_byte
        mov al, 0x15
        call emit_byte
        mov eax, [for_cur_sp]
        imul eax, 4
        add eax, FOR_END_ADDR
        call emit_dword

        ; cmp eax, edx (3B C2) (2 bytes)
        mov al, 0x3B
        call emit_byte
        mov al, 0xC2
        call emit_byte

        ; jl .exit rel32  (0F 8C [4 bytes])  (6 bytes)
        ; Record jl fixup position
        mov al, 0x0F
        call emit_byte
        mov al, 0x8C
        call emit_byte
        mov eax, [for_cur_sp]
        mov ebx, [out_pos]
        mov [for_jl_fixup_stack + eax * 4], ebx
        xor eax, eax
        call emit_dword

        ; jmp past pos_step block (EB 0E) (2 bytes)
        mov al, 0xEB
        call emit_byte
        mov al, 0x0E
        call emit_byte

        ; --- positive step block (14 bytes) ---
        ; Load end → EDX: 8B 15 [FOR_END_ADDR + sp*4] (6 bytes)
        mov al, 0x8B
        call emit_byte
        mov al, 0x15
        call emit_byte
        mov eax, [for_cur_sp]
        imul eax, 4
        add eax, FOR_END_ADDR
        call emit_dword

        ; cmp eax, edx (3B C2) (2 bytes)
        mov al, 0x3B
        call emit_byte
        mov al, 0xC2
        call emit_byte

        ; jg .exit rel32  (0F 8F [4 bytes])  (6 bytes)
        mov al, 0x0F
        call emit_byte
        mov al, 0x8F
        call emit_byte
        mov eax, [for_cur_sp]
        mov ebx, [out_pos]
        mov [for_jg_fixup_stack + eax * 4], ebx
        xor eax, eax
        call emit_dword
        ; .body starts here (39 bytes total from loop top)

        ; Save var index in stack
        mov eax, [for_cur_sp]
        mov ebx, [for_cur_var]
        mov [for_var_stack + eax * 4], ebx

        ; Push for_sp
        inc dword [for_sp]
        ret

.cf_error:
        mov byte [compile_error], 1
        ret

compile_next:
        ; NEXT [var]
        mov eax, [for_sp]
        cmp eax, 0
        je .cn_error

        dec eax
        mov [for_cur_sp], eax       ; level = for_sp - 1

        ; Optional variable check (skip it, trust BASIC programmer)
        cmp dword [tok_type], TOK_VAR
        jne .cn_no_var
        call next_token
.cn_no_var:

        ; Emit: add [var_addr], step
        ; A1 [FOR_STEP_ADDR + sp*4]  (load step)
        mov al, 0xA1
        call emit_byte
        mov eax, [for_cur_sp]
        imul eax, 4
        add eax, FOR_STEP_ADDR
        call emit_dword

        ; 01 05 [var_addr]  (add [var_addr], eax)
        mov al, 0x01
        call emit_byte
        mov al, 0x05
        call emit_byte
        mov eax, [for_cur_sp]
        mov ebx, [for_var_stack + eax * 4]
        imul ebx, 4
        add ebx, VARS_ADDR
        mov eax, ebx
        call emit_dword

        ; Emit JMP back to loop top
        mov al, 0xE9
        call emit_byte
        mov eax, [for_cur_sp]
        mov ebx, [for_loop_top_stack + eax * 4]
        ; rel32 = loop_top_out_pos - (out_pos + 4)
        sub ebx, [out_pos]
        sub ebx, 4
        mov eax, ebx
        call emit_dword

        ; Patch jl and jg exit fixups to current out_pos
        mov eax, [for_cur_sp]

        ; Patch jl fixup
        mov ebx, [for_jl_fixup_stack + eax * 4]
        mov ecx, [out_pos]
        sub ecx, ebx
        sub ecx, 4
        mov [out_buffer + ebx], ecx

        ; Patch jg fixup
        mov ebx, [for_jg_fixup_stack + eax * 4]
        mov ecx, [out_pos]
        sub ecx, ebx
        sub ecx, 4
        mov [out_buffer + ebx], ecx

        dec dword [for_sp]
        ret

.cn_error:
        mov byte [compile_error], 1
        ret

; -----------------------------------------------------------------------
; WHILE / WEND
; -----------------------------------------------------------------------
compile_while:
        ; Save loop top and condition
        mov eax, [while_sp]
        cmp eax, MAX_WHILE_FIX
        jge .cw_err

        ; Record loop top
        mov ebx, [out_pos]
        imul eax, 8
        mov [while_loop_top + eax], ebx

        ; Compile condition → EAX
        mov eax, [while_sp]
        push eax
        call compile_expr_or

        ; Emit test + JZ placeholder
        mov al, 0x85
        call emit_byte
        mov al, 0xC0
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x84
        call emit_byte

        pop eax
        imul eax, 8
        mov ebx, [out_pos]
        mov [while_loop_top + eax + 4], ebx    ; store exit fixup pos

        xor eax, eax
        call emit_dword
        inc dword [while_sp]
        ret
.cw_err:
        mov byte [compile_error], 1
        ret

compile_wend:
        mov eax, [while_sp]
        cmp eax, 0
        je .cwend_err
        dec eax
        imul ebx, eax, 8

        ; Emit JMP back to loop top
        mov al, 0xE9
        call emit_byte
        mov ecx, [while_loop_top + ebx]
        sub ecx, [out_pos]
        sub ecx, 4
        mov eax, ecx
        call emit_dword

        ; Patch JZ exit fixup to current out_pos
        mov ecx, [while_loop_top + ebx + 4]
        mov edx, [out_pos]
        sub edx, ecx
        sub edx, 4
        mov [out_buffer + ecx], edx

        dec dword [while_sp]
        ret
.cwend_err:
        mov byte [compile_error], 1
        ret

; -----------------------------------------------------------------------
; CLS, BEEP, SLEEP
; -----------------------------------------------------------------------
compile_cls:
        ; Emit: mov eax, SYS_CLEAR; int 0x80
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_CLEAR
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ret

compile_beep:
        ; Emit: mov eax, SYS_BEEP; int 0x80
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_BEEP
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ret

compile_sleep:
        ; SLEEP expr → mov eax, SYS_SLEEP; mov ebx, N; int 0x80
        call compile_expr_or
        ; EAX = sleep duration; emit:
        ; push eax; mov ebx, eax; mov eax, SYS_SLEEP; int 0x80
        ; Actually: emit mov ebx, <computed val> using ebx=imm won't work
        ; since we just computed into EAX at runtime; use mov ebx,eax
        ; push eax (50)
        mov al, 0x50
        call emit_byte
        ; pop ebx (5B)
        mov al, 0x5B
        call emit_byte
        ; mov eax, SYS_SLEEP
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_SLEEP
        call emit_dword
        ; int 0x80
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ret

; -----------------------------------------------------------------------
; SYS_EXIT emitter
; -----------------------------------------------------------------------
emit_sys_exit:
        ; Emit: mov eax, SYS_EXIT; int 0x80
        mov al, 0xB8
        call emit_byte
        mov eax, SYS_EXIT
        call emit_dword
        mov al, 0xCD
        call emit_byte
        mov al, 0x80
        call emit_byte
        ret

; -----------------------------------------------------------------------
; EXPRESSION COMPILER
; All routines leave result in EAX (in the emitted code).
; They emit x86 machine code into out_buffer.
;
; Precedence (low to high):
;   OR, XOR
;   AND
;   NOT (unary)
;   Comparison: =, <>, <, >, <=, >=
;   Additive: +, -
;   Multiplicative: *, /, MOD
;   Unary: -, primary
; -----------------------------------------------------------------------

compile_expr_or:
        call compile_expr_and
.ceo_loop:
        cmp dword [tok_type], KW_OR
        je .ceo_or
        cmp dword [tok_type], KW_XOR
        je .ceo_xor
        ret
.ceo_or:
        call next_token
        ; push eax (50)
        mov al, 0x50
        call emit_byte
        call compile_expr_and
        ; pop ecx (59)
        mov al, 0x59
        call emit_byte
        ; or eax, ecx (0B C1)
        mov al, 0x0B
        call emit_byte
        mov al, 0xC1
        call emit_byte
        jmp .ceo_loop
.ceo_xor:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expr_and
        mov al, 0x59
        call emit_byte
        ; xor eax, ecx (33 C1)
        mov al, 0x33
        call emit_byte
        mov al, 0xC1
        call emit_byte
        jmp .ceo_loop

compile_expr_and:
        call compile_expr_not
.cea_loop:
        cmp dword [tok_type], KW_AND
        jne .cea_ret
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expr_not
        mov al, 0x59
        call emit_byte
        ; and eax, ecx (23 C1)
        mov al, 0x23
        call emit_byte
        mov al, 0xC1
        call emit_byte
        jmp .cea_loop
.cea_ret:
        ret

compile_expr_not:
        cmp dword [tok_type], KW_NOT
        jne compile_expr_cmp
        call next_token
        call compile_expr_cmp
        ; not eax (F7 D0)
        mov al, 0xF7
        call emit_byte
        mov al, 0xD0
        call emit_byte
        ret

compile_expr_cmp:
        call compile_expr_add
        ; Check for comparison operator
        cmp dword [tok_type], TOK_EQ
        je .cec_eq
        cmp dword [tok_type], TOK_NE
        je .cec_ne
        cmp dword [tok_type], TOK_LT
        je .cec_lt
        cmp dword [tok_type], TOK_GT
        je .cec_gt
        cmp dword [tok_type], TOK_LE
        je .cec_le
        cmp dword [tok_type], TOK_GE
        je .cec_ge
        ret

.cec_eq:
        call next_token
        ; push eax (50)
        mov al, 0x50
        call emit_byte
        call compile_expr_add
        ; pop ecx (59); cmp ecx, eax; sete al; movzx eax, al
        mov al, 0x59
        call emit_byte
        mov al, 0x3B
        call emit_byte
        mov al, 0xC8    ; cmp ecx, eax
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x94
        call emit_byte
        mov al, 0xC0    ; sete al
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0    ; movzx eax, al
        call emit_byte
        ret

.cec_ne:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expr_add
        mov al, 0x59
        call emit_byte
        mov al, 0x3B
        call emit_byte
        mov al, 0xC8
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x95
        call emit_byte
        mov al, 0xC0    ; setne al
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ret

.cec_lt:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expr_add
        mov al, 0x59
        call emit_byte
        mov al, 0x3B
        call emit_byte
        mov al, 0xC8
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x9C
        call emit_byte
        mov al, 0xC0    ; setl al
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ret

.cec_gt:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expr_add
        mov al, 0x59
        call emit_byte
        mov al, 0x3B
        call emit_byte
        mov al, 0xC8
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x9F
        call emit_byte
        mov al, 0xC0    ; setg al
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ret

.cec_le:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expr_add
        mov al, 0x59
        call emit_byte
        mov al, 0x3B
        call emit_byte
        mov al, 0xC8
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x9E
        call emit_byte
        mov al, 0xC0    ; setle al
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ret

.cec_ge:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_expr_add
        mov al, 0x59
        call emit_byte
        mov al, 0x3B
        call emit_byte
        mov al, 0xC8
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0x9D
        call emit_byte
        mov al, 0xC0    ; setge al
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0xB6
        call emit_byte
        mov al, 0xC0
        call emit_byte
        ret

compile_expr_add:
        call compile_expr_mul
.ceadd_loop:
        cmp dword [tok_type], TOK_PLUS
        je .ceadd_add
        cmp dword [tok_type], TOK_MINUS
        je .ceadd_sub
        ret
.ceadd_add:
        call next_token
        ; push eax (save left)
        mov al, 0x50
        call emit_byte
        call compile_expr_mul
        ; pop ecx (get left); add eax, ecx
        mov al, 0x59
        call emit_byte
        mov al, 0x01
        call emit_byte
        mov al, 0xC8    ; add eax, ecx
        call emit_byte
        jmp .ceadd_loop
.ceadd_sub:
        call next_token
        ; push eax (save left)
        mov al, 0x50
        call emit_byte
        call compile_expr_mul
        ; EAX = right; pop ecx = left; result = left - right = ecx - eax
        mov al, 0x59
        call emit_byte
        ; sub ecx, eax; mov eax, ecx
        mov al, 0x29
        call emit_byte
        mov al, 0xC1    ; sub ecx, eax
        call emit_byte
        mov al, 0x89
        call emit_byte
        mov al, 0xC8    ; mov eax, ecx
        call emit_byte
        jmp .ceadd_loop

compile_expr_mul:
        call compile_primary
.cemul_loop:
        cmp dword [tok_type], TOK_STAR
        je .cemul_mul
        cmp dword [tok_type], TOK_SLASH
        je .cemul_div
        cmp dword [tok_type], KW_MOD
        je .cemul_mod
        ret
.cemul_mul:
        call next_token
        ; push eax (save left)
        mov al, 0x50
        call emit_byte
        call compile_primary
        ; pop ecx; imul eax, ecx
        mov al, 0x59
        call emit_byte
        mov al, 0x0F
        call emit_byte
        mov al, 0xAF
        call emit_byte
        mov al, 0xC1    ; imul eax, ecx
        call emit_byte
        jmp .cemul_loop
.cemul_div:
        call next_token
        ; push eax (left), compile right → EAX
        mov al, 0x50
        call emit_byte
        call compile_primary
        ; EAX=right → mov ebx, eax; pop eax = left; cdq; idiv ebx
        mov al, 0x89
        call emit_byte
        mov al, 0xC3    ; mov ebx, eax
        call emit_byte
        mov al, 0x58    ; pop eax
        call emit_byte
        mov al, 0x99    ; cdq
        call emit_byte
        mov al, 0xF7
        call emit_byte
        mov al, 0xFB    ; idiv ebx
        call emit_byte
        jmp .cemul_loop
.cemul_mod:
        call next_token
        mov al, 0x50
        call emit_byte
        call compile_primary
        ; same as div but take EDX (remainder) as result
        mov al, 0x89
        call emit_byte
        mov al, 0xC3
        call emit_byte
        mov al, 0x58
        call emit_byte
        mov al, 0x99
        call emit_byte
        mov al, 0xF7
        call emit_byte
        mov al, 0xFB
        call emit_byte
        ; mov eax, edx (89 D0)
        mov al, 0x89
        call emit_byte
        mov al, 0xD0
        call emit_byte
        jmp .cemul_loop

compile_primary:
        ; Unary minus
        cmp dword [tok_type], TOK_MINUS
        jne .cprim_not_neg
        call next_token
        call compile_primary
        ; neg eax (F7 D8)
        mov al, 0xF7
        call emit_byte
        mov al, 0xD8
        call emit_byte
        ret

.cprim_not_neg:
        ; Parenthesized expression
        cmp dword [tok_type], TOK_LPAREN
        jne .cprim_not_paren
        call next_token
        call compile_expr_or
        ; Expect ')'
        cmp dword [tok_type], TOK_RPAREN
        jne .cprim_paren_err
        call next_token
        ret
.cprim_paren_err:
        mov byte [compile_error], 1
        ret

.cprim_not_paren:
        ; Number literal: emit MOV EAX, imm32
        cmp dword [tok_type], TOK_NUM
        jne .cprim_not_num
        ; B8 imm32
        mov al, 0xB8
        call emit_byte
        mov eax, [tok_value]
        call emit_dword
        call next_token
        ret

.cprim_not_num:
        ; Variable: emit MOV EAX, [addr]
        cmp dword [tok_type], TOK_VAR
        jne .cprim_default
        mov eax, [tok_var]
        ; A1 addr32 (MOV EAX, [imm32])
        mov al, 0xA1
        call emit_byte
        imul eax, 4
        add eax, VARS_ADDR
        call emit_dword
        call next_token
        ret

.cprim_default:
        ; Unsupported primary: emit MOV EAX, 0
        mov al, 0xB8
        call emit_byte
        xor eax, eax
        call emit_dword
        ret

; -----------------------------------------------------------------------
; STRING TABLE (same pattern as TCC)
; -----------------------------------------------------------------------
store_string:
        ; Stores tok_string in string_table, returns index in EAX
        push ebx
        push ecx
        push edi
        push esi
        mov eax, [string_count]
        cmp eax, MAX_STRS
        jge .ss_full
        mov ecx, eax
        imul ebx, eax, STRING_MAX
        lea edi, [string_table + ebx]
        lea esi, [tok_string]
.ss_copy:
        lodsb
        stosb
        cmp al, 0
        jne .ss_copy
        inc dword [string_count]
        mov eax, ecx
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

emit_string_data:
        ; Emit all string literals at end of out_buffer, patch addresses
        pushad
        mov ecx, [string_count]
        cmp ecx, 0
        je .esd_done
        xor ebx, ebx
.esd_loop:
        ; Compute runtime address of this string
        mov eax, BASE_ADDR
        add eax, [out_pos]
        mov [string_addrs + ebx * 4], eax

        ; Emit the string bytes
        imul edx, ebx, STRING_MAX
        lea esi, [string_table + edx]
.esd_chars:
        lodsb
        call emit_byte
        cmp al, 0
        jne .esd_chars

        inc ebx
        cmp ebx, ecx
        jl .esd_loop

        ; Patch all string address fixups
        xor ebx, ebx
.esd_patch:
        cmp ebx, ecx
        jge .esd_done
        mov eax, [string_fixups + ebx * 4]
        cmp eax, 0
        je .esd_patch_next
        mov edx, [string_addrs + ebx * 4]
        mov [out_buffer + eax], edx
.esd_patch_next:
        inc ebx
        jmp .esd_patch
.esd_done:
        popad
        ret

; -----------------------------------------------------------------------
; RESOLVE FORWARD GOTO/GOSUB FIXUPS
; -----------------------------------------------------------------------
resolve_fixups:
        pushad
        mov ecx, [fixup_count]
        xor ebx, ebx
.rf_loop:
        cmp ebx, ecx
        jge .rf_done
        imul edx, ebx, 12
        ; Get target linenum
        mov eax, [fixup_table + edx]
        call find_lineno_outpos
        cmp eax, -1
        je .rf_not_found
        ; eax = target out_pos
        mov esi, [fixup_table + edx + 4]   ; fixup position
        sub eax, esi
        sub eax, 4
        mov [out_buffer + esi], eax
.rf_not_found:
        inc ebx
        jmp .rf_loop
.rf_done:
        popad
        ret

; -----------------------------------------------------------------------
; MESSAGES
; -----------------------------------------------------------------------
msg_usage:      db "BASIC Compiler for Mellivora OS", 0x0A
                db "Usage: basicc source.bas output.bin", 0x0A, 0
msg_compiling:  db "Compiling: ", 0
msg_arrow:      db " -> ", 0
msg_success:    db "Done. Output size: ", 0
msg_bytes:      db " bytes", 0x0A, 0
msg_file_err:   db "Error: Cannot read source file", 0x0A, 0
msg_comp_err:   db "Compile error at line ", 0
msg_at_line:    db " ", 0
msg_write_err:  db "Error: Cannot write output file", 0x0A, 0

; -----------------------------------------------------------------------
; KEYWORDS
; -----------------------------------------------------------------------
kw_print:       db "PRINT", 0
kw_input:       db "INPUT", 0
kw_let:         db "LET", 0
kw_if:          db "IF", 0
kw_then:        db "THEN", 0
kw_else:        db "ELSE", 0
kw_goto:        db "GOTO", 0
kw_gosub:       db "GOSUB", 0
kw_return:      db "RETURN", 0
kw_for:         db "FOR", 0
kw_to:          db "TO", 0
kw_step:        db "STEP", 0
kw_next:        db "NEXT", 0
kw_while:       db "WHILE", 0
kw_wend:        db "WEND", 0
kw_end:         db "END", 0
kw_stop:        db "STOP", 0
kw_rem:         db "REM", 0
kw_cls:         db "CLS", 0
kw_beep:        db "BEEP", 0
kw_sleep:       db "SLEEP", 0
kw_mod:         db "MOD", 0
kw_and:         db "AND", 0
kw_or:          db "OR", 0
kw_not:         db "NOT", 0
kw_xor:         db "XOR", 0

; -----------------------------------------------------------------------
; COMPILER STATE
; -----------------------------------------------------------------------
src_pos:        dd 0
out_pos:        dd 0
src_line:       dd 1
src_size:       dd 0
compile_error:  db 0

; Token state
tok_type:       dd 0
tok_value:      dd 0
tok_var:        dd 0
tok_ident:      times 32 db 0
tok_string:     times STRING_MAX db 0

; Argument buffers
args_buf:       times 256 db 0
src_filename:   times 128 db 0
dst_filename:   times 128 db 0

; Line number table: (linenum, out_pos) pairs
lineno_count:   dd 0
lineno_table:   times MAX_LINENOS * 8 db 0

; Forward fixup table: (target_linenum, fixup_out_pos, type) triples
fixup_count:    dd 0
fixup_table:    times MAX_FIXUPS * 12 db 0

; String literal table
string_count:   dd 0
string_table:   times MAX_STRS * STRING_MAX db 0
string_fixups:  times MAX_STRS * 4 db 0
string_addrs:   times MAX_STRS * 4 db 0

; FOR loop compile-time stack
for_sp:             dd 0
for_cur_sp:         dd 0
for_cur_var:        dd 0
for_var_stack:      times FOR_STACK_MAX * 4 dd 0
for_loop_top_stack: times FOR_STACK_MAX * 4 dd 0
for_jl_fixup_stack: times FOR_STACK_MAX * 4 dd 0
for_jg_fixup_stack: times FOR_STACK_MAX * 4 dd 0

; WHILE loop compile-time stack
while_sp:           dd 0
; while_loop_top[i*2+0] = loop_top_out_pos
; while_loop_top[i*2+1] = exit_fixup_out_pos
while_loop_top:     times MAX_WHILE_FIX * 8 db 0

; IF fixup positions (only one level deep; nested IFs use stack via recursion)
if_jz_fixup:    dd 0
if_jmp_fixup:   dd 0

; Misc compile temporaries
print_need_nl:  db 0
ci2_var_idx:    dd 0
ca_var_idx:     dd 0

; Source and output buffers
src_buffer:     times MAX_SRC + 1 db 0
out_buffer:     times MAX_OUT db 0
