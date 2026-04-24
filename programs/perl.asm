; perl.asm - Perl Interpreter for Mellivora OS
; A subset of Perl 5 with interactive REPL and script execution.
;
; Supports:
;   Scalars: $var = expr;  Integers and strings
;   print "string\n";  print $var;  say "...";
;   Arithmetic: + - * / % **   String: . (concat) x (repeat)
;   Comparison: == != < > <= >=  eq ne lt gt le ge
;   Logical: && || ! and or not
;   Control: if/elsif/else, unless, while, until, for(;;), foreach
;   Subs: sub name { ... }  return expr;
;   Built-ins: chomp, length, substr, push, pop, shift, unshift,
;              split, join, reverse, sort, abs, int, chr, ord,
;              uc, lc, index, rindex, die, exit, defined
;   Arrays: @arr = (1,2,3);  $arr[i];  scalar @arr;
;   Special: $_, @ARGV, STDIN input via <STDIN> or <>
;   File I/O: open, close, read/write via print HANDLE
;   use strict; use warnings; (accepted, no-op for compat)
;   Comments: # to end of line
;   String escapes: \n \t \\ \"
;
; Usage: perl              - interactive REPL
;        perl script.pl    - execute script
;        perl -e 'code'    - execute one-liner

%include "syscalls.inc"
%include "lib/io.inc"
%include "lib/string.inc"
%include "lib/math.inc"

; ===================== Constants =====================
MAX_VARS        equ 256         ; max scalar variables
VAR_NAME_LEN    equ 32          ; max variable name length
VAR_VAL_LEN     equ 256         ; max string value length
MAX_LINES       equ 500         ; max script lines
MAX_LINE_LEN    equ 256         ; max line length
INPUT_BUF_LEN   equ 256
MAX_SUBS        equ 64          ; max subroutine definitions
MAX_CALL_DEPTH  equ 32          ; max call stack depth
MAX_ARRAYS      equ 64          ; max arrays
MAX_ARR_ELEM    equ 128         ; max elements per array
EXPR_STACK_SZ   equ 64          ; expression eval stack
TOKEN_BUF_LEN   equ 256
MAX_LOOP_DEPTH  equ 16          ; nested loop depth
FILE_BUF_SIZE   equ 8192
COLOR_DEFAULT   equ 0x07
COLOR_BANNER    equ 0x0D        ; magenta
COLOR_PROMPT    equ 0x0B        ; cyan
COLOR_ERROR     equ 0x0C        ; red
COLOR_STRING    equ 0x0A        ; green

; Variable types
TYPE_UNDEF      equ 0
TYPE_INT        equ 1
TYPE_STR        equ 2

; Token types
TOK_EOF         equ 0
TOK_NUM         equ 1
TOK_STR         equ 2
TOK_IDENT       equ 3
TOK_SCALAR      equ 4           ; $name
TOK_ARRAY       equ 5           ; @name
TOK_HASH        equ 6           ; %name (reserved)
TOK_PLUS        equ 10
TOK_MINUS       equ 11
TOK_STAR        equ 12
TOK_SLASH       equ 13
TOK_PERCENT     equ 14
TOK_POWER       equ 15          ; **
TOK_DOT         equ 16          ; . (concat)
TOK_X           equ 17          ; x (repeat)
TOK_EQ          equ 20          ; ==
TOK_NE          equ 21          ; !=
TOK_LT          equ 22
TOK_GT          equ 23
TOK_LE          equ 24
TOK_GE          equ 25
TOK_SEQ         equ 26          ; eq
TOK_SNE         equ 27          ; ne
TOK_SLT         equ 28          ; lt
TOK_SGT         equ 29          ; gt
TOK_SLE         equ 30          ; le
TOK_SGE         equ 31          ; ge
TOK_AND         equ 32          ; &&
TOK_OR          equ 33          ; ||
TOK_NOT         equ 34          ; !
TOK_ASSIGN      equ 40          ; =
TOK_SEMI        equ 41          ; ;
TOK_COMMA       equ 42
TOK_LPAREN      equ 43
TOK_RPAREN      equ 44
TOK_LBRACE      equ 45          ; {
TOK_RBRACE      equ 46          ; }
TOK_LBRACKET    equ 47          ; [
TOK_RBRACKET    equ 48          ; ]
TOK_NEWLINE     equ 49
TOK_ARROW       equ 50          ; =>
TOK_DOTDOT      equ 51          ; ..
TOK_STDIN       equ 52          ; <STDIN> or <>
TOK_BACKSLASH   equ 53
TOK_CONCAT_EQ   equ 54          ; .=
TOK_PLUS_EQ     equ 55          ; +=
TOK_MINUS_EQ    equ 56          ; -=
TOK_AT          equ 57          ; raw @
TOK_PLUSPLUS     equ 58         ; ++
TOK_MINUSMINUS  equ 59         ; --

start:
        ; Initialize
        call perl_init

        ; Check for command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        je .repl_mode

        ; Check for -e flag
        cmp byte [args_buf], '-'
        jne .file_mode
        cmp byte [args_buf+1], 'e'
        jne .file_mode

        ; -e mode: execute inline code
        lea esi, [args_buf+2]
        ; skip spaces
.skip_e_space:
        cmp byte [esi], ' '
        jne .got_e_code
        inc esi
        jmp .skip_e_space
.got_e_code:
        ; Copy to script_buf line 0
        mov edi, script_buf
        call str_copy
        mov dword [script_lines], 1
        call run_script
        jmp .exit_clean

.file_mode:
        ; Load script from file
        mov esi, args_buf
        call load_script
        cmp eax, 0
        jl .file_error
        call run_script
        jmp .exit_clean

.file_error:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_ERROR
        int 0x80
        mov esi, err_cant_open
        call io_print
        mov esi, args_buf
        call io_println
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        jmp .exit_clean

.repl_mode:
        ; Print banner
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_BANNER
        int 0x80
        mov esi, msg_banner
        call io_print
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

        ; Interactive REPL
.repl_loop:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_PROMPT
        int 0x80
        mov esi, msg_prompt
        call io_print
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

        ; Read line
        mov edi, input_buf
        mov ecx, INPUT_BUF_LEN
        call io_read_line
        call io_newline

        ; Empty line?
        cmp byte [input_buf], 0
        je .repl_loop

        ; Check for exit/quit
        mov esi, input_buf
        mov edi, kw_exit_cmd
        call str_cmp
        je .exit_clean
        mov esi, input_buf
        mov edi, kw_quit_cmd
        call str_cmp
        je .exit_clean

        ; Execute the line
        mov esi, input_buf
        mov dword [parse_ptr], esi
        mov byte [had_error], 0
        call exec_line
        cmp byte [had_error], 0
        je .repl_loop
        ; Error already printed
        mov byte [had_error], 0
        jmp .repl_loop

.exit_clean:
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; INITIALIZATION
;=======================================================================
perl_init:
        pushad
        ; Seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

        ; Clear variable table
        mov edi, var_names
        mov ecx, MAX_VARS * VAR_NAME_LEN
        xor al, al
        rep stosb

        mov edi, var_types
        mov ecx, MAX_VARS
        xor al, al
        rep stosb

        mov edi, var_ints
        mov ecx, MAX_VARS
        xor eax, eax
        rep stosd

        ; Clear sub table
        mov edi, sub_names
        mov ecx, MAX_SUBS * VAR_NAME_LEN
        xor al, al
        rep stosb

        ; Clear arrays
        mov edi, arr_names
        mov ecx, MAX_ARRAYS * VAR_NAME_LEN
        xor al, al
        rep stosb
        mov edi, arr_counts
        mov ecx, MAX_ARRAYS
        xor eax, eax
        rep stosd

        mov dword [num_vars], 0
        mov dword [num_subs], 0
        mov dword [num_arrays], 0
        mov dword [script_lines], 0
        mov dword [call_depth], 0
        mov dword [loop_depth], 0
        mov byte [had_error], 0
        mov byte [in_sub_return], 0
        mov dword [return_val], 0
        mov byte [return_type], TYPE_UNDEF
        popad
        ret

;=======================================================================
; LOAD SCRIPT from file
; Input:  ESI = filename
; Output: EAX = 0 success, -1 fail
;=======================================================================
load_script:
        pushad

        ; Read entire file
        mov ebx, esi
        mov ecx, file_buffer
        mov eax, SYS_FREAD
        int 0x80
        cmp eax, 0
        jle .ls_fail

        mov [file_size], eax

        ; Parse into lines
        mov esi, file_buffer
        mov edi, script_buf
        xor ecx, ecx           ; line count
        xor edx, edx           ; char pos in current line

.ls_parse:
        cmp esi, file_buffer
        jl .ls_done_parse
        mov eax, esi
        sub eax, file_buffer
        cmp eax, [file_size]
        jge .ls_done_parse

        movzx eax, byte [esi]
        cmp al, 0x0A          ; newline
        je .ls_newline
        cmp al, 0x0D          ; CR
        je .ls_cr
        cmp al, 0
        je .ls_done_parse

        ; Store char
        cmp edx, MAX_LINE_LEN - 1
        jge .ls_skip_char
        mov [edi + edx], al
        inc edx
.ls_skip_char:
        inc esi
        jmp .ls_parse

.ls_cr:
        inc esi
        ; Skip following LF
        cmp byte [esi], 0x0A
        jne .ls_parse
        ; fall through to newline

.ls_newline:
        ; Terminate current line
        mov byte [edi + edx], 0
        inc ecx
        add edi, MAX_LINE_LEN
        xor edx, edx
        inc esi
        cmp ecx, MAX_LINES
        jge .ls_done_parse
        jmp .ls_parse

.ls_done_parse:
        ; Terminate last line if it has content
        cmp edx, 0
        je .ls_no_last
        mov byte [edi + edx], 0
        inc ecx
.ls_no_last:
        mov [esp + 28], ecx    ; return count via pushad
        mov [script_lines], ecx

        ; Success
        mov dword [esp + 28], 0
        popad
        ret

.ls_fail:
        mov dword [esp + 28], -1
        popad
        ret

;=======================================================================
; RUN SCRIPT - Execute all loaded script lines
;=======================================================================
run_script:
        pushad
        mov dword [current_line], 0

        ; First pass: find all sub definitions
        call find_subs

.rs_loop:
        mov eax, [current_line]
        cmp eax, [script_lines]
        jge .rs_done

        ; Get pointer to current line
        imul ebx, eax, MAX_LINE_LEN
        lea esi, [script_buf + ebx]
        mov dword [parse_ptr], esi

        ; Skip empty lines
        call skip_whitespace
        mov esi, [parse_ptr]
        cmp byte [esi], 0
        je .rs_next
        cmp byte [esi], '#'
        je .rs_next

        ; Execute
        call exec_line

        ; Check for error
        cmp byte [had_error], 0
        jne .rs_done

        ; Check for sub return
        cmp byte [in_sub_return], 0
        jne .rs_done

.rs_next:
        inc dword [current_line]
        jmp .rs_loop

.rs_done:
        popad
        ret

;=======================================================================
; FIND SUBS - Scan for sub definitions (first pass)
;=======================================================================
find_subs:
        pushad
        xor ecx, ecx           ; line index

.fs_loop:
        cmp ecx, [script_lines]
        jge .fs_done

        ; Get line pointer
        push ecx
        imul ebx, ecx, MAX_LINE_LEN
        lea esi, [script_buf + ebx]

        ; Skip whitespace
        call skip_ws_esi

        ; Check for "sub "
        cmp byte [esi], 's'
        jne .fs_next
        cmp byte [esi+1], 'u'
        jne .fs_next
        cmp byte [esi+2], 'b'
        jne .fs_next
        cmp byte [esi+3], ' '
        jne .fs_next

        ; Found sub definition
        add esi, 4
        call skip_ws_esi

        ; Read sub name
        mov edi, temp_name
        call read_ident_esi

        ; Store: name + start line
        pop ecx
        push ecx
        call store_sub_def

.fs_next:
        pop ecx
        inc ecx
        jmp .fs_loop

.fs_done:
        popad
        ret

;=======================================================================
; EXEC_LINE - Execute a single line of Perl
;=======================================================================
exec_line:
        pushad

        call skip_whitespace
        mov esi, [parse_ptr]
        cmp byte [esi], 0
        je .el_done
        cmp byte [esi], '#'     ; comment
        je .el_done

        ; Check for keywords
        ; --- use (ignore) ---
        cmp dword [esi], 'use '
        je .el_use

        ; --- my ---
        cmp byte [esi], 'm'
        jne .el_not_my
        cmp byte [esi+1], 'y'
        jne .el_not_my
        cmp byte [esi+2], ' '
        je .el_my
        cmp byte [esi+2], '('
        je .el_my
.el_not_my:

        ; --- print ---
        mov edi, kw_print
        call match_keyword
        jc .el_print

        ; --- say ---
        mov edi, kw_say
        call match_keyword
        jc .el_say

        ; --- if ---
        mov edi, kw_if
        call match_keyword
        jc .el_if

        ; --- unless ---
        mov edi, kw_unless
        call match_keyword
        jc .el_unless

        ; --- while ---
        mov edi, kw_while
        call match_keyword
        jc .el_while

        ; --- until ---
        mov edi, kw_until
        call match_keyword
        jc .el_until

        ; --- for/foreach ---
        mov edi, kw_foreach
        call match_keyword
        jc .el_foreach
        mov edi, kw_for
        call match_keyword
        jc .el_for

        ; --- sub ---
        mov edi, kw_sub
        call match_keyword
        jc .el_sub_def

        ; --- return ---
        mov edi, kw_return
        call match_keyword
        jc .el_return

        ; --- last ---
        mov edi, kw_last
        call match_keyword
        jc .el_last

        ; --- next ---
        mov edi, kw_next
        call match_keyword
        jc .el_next

        ; --- chomp ---
        mov edi, kw_chomp
        call match_keyword
        jc .el_chomp

        ; --- push ---
        mov edi, kw_push
        call match_keyword
        jc .el_push

        ; --- pop ---
        mov edi, kw_pop_kw
        call match_keyword
        jc .el_pop

        ; --- shift ---
        mov edi, kw_shift
        call match_keyword
        jc .el_shift

        ; --- unshift ---
        mov edi, kw_unshift
        call match_keyword
        jc .el_unshift

        ; --- die ---
        mov edi, kw_die
        call match_keyword
        jc .el_die

        ; --- exit ---
        mov edi, kw_exit
        call match_keyword
        jc .el_exit_kw

        ; --- open ---
        mov edi, kw_open
        call match_keyword
        jc .el_open

        ; --- close ---
        mov edi, kw_close
        call match_keyword
        jc .el_close

        ; --- Variable assignment: $var = ... ---
        cmp byte [esi], '$'
        je .el_scalar_op

        ; --- Array assignment: @arr = ... ---
        cmp byte [esi], '@'
        je .el_array_op

        ; --- Bare sub call: name(...) ---
        call try_sub_call
        cmp eax, 1
        je .el_done

        ; Unknown
        call report_syntax_error

.el_done:
        popad
        ret

; --- use strict; use warnings; etc. (no-op) ---
.el_use:
        ; Skip to end of line or semicolon
        jmp .el_skip_to_end

; --- my $var = expr; ---
.el_my:
        mov esi, [parse_ptr]
        add esi, 2              ; skip "my"
        call skip_ws_esi
        mov [parse_ptr], esi
        ; Fall through to handle $ or @ assignment
        cmp byte [esi], '$'
        je .el_scalar_op
        cmp byte [esi], '@'
        je .el_array_op
        ; my ($a, $b) = ... not supported, skip
        jmp .el_skip_to_end

; --- print ---
.el_print:
        call do_print
        mov byte [print_newline], 0
        jmp .el_done

; --- say (print + newline) ---
.el_say:
        call do_print
        call io_newline
        jmp .el_done

; --- if ---
.el_if:
        call do_if
        jmp .el_done

; --- unless ---
.el_unless:
        call do_unless
        jmp .el_done

; --- while ---
.el_while:
        call do_while
        jmp .el_done

; --- until ---
.el_until:
        call do_until
        jmp .el_done

; --- foreach ---
.el_foreach:
        call do_foreach
        jmp .el_done

; --- for (C-style or alias for foreach) ---
.el_for:
        ; Check if it's for (...;...;...) or for $var (list)
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '('
        je .el_for_c
        cmp byte [esi], '$'
        je .el_foreach_alias
        cmp byte [esi], 'm'     ; my
        je .el_foreach_alias
        jmp .el_for_c

.el_foreach_alias:
        call do_foreach
        jmp .el_done

.el_for_c:
        call do_for_c
        jmp .el_done

; --- sub definition (skip in execution, handled in find_subs) ---
.el_sub_def:
        call skip_sub_body
        jmp .el_done

; --- return ---
.el_return:
        call do_return
        jmp .el_done

; --- last (break) ---
.el_last:
        mov byte [loop_break], 1
        jmp .el_done

; --- next (continue) ---
.el_next:
        mov byte [loop_continue], 1
        jmp .el_done

; --- chomp ---
.el_chomp:
        call do_chomp
        jmp .el_done

; --- push ---
.el_push:
        call do_push
        jmp .el_done

; --- pop ---
.el_pop:
        call do_pop_op
        jmp .el_done

; --- shift ---
.el_shift:
        call do_shift
        jmp .el_done

; --- unshift ---
.el_unshift:
        call do_unshift
        jmp .el_done

; --- die ---
.el_die:
        call do_die
        jmp .el_done

; --- exit ---
.el_exit_kw:
        call do_exit
        jmp .el_done

; --- open ---
.el_open:
        call do_open
        jmp .el_done

; --- close ---
.el_close:
        call do_close
        jmp .el_done

; --- $var operation ---
.el_scalar_op:
        call do_scalar_op
        jmp .el_done

; --- @arr operation ---
.el_array_op:
        call do_array_op
        jmp .el_done

.el_skip_to_end:
        mov esi, [parse_ptr]
.el_ste_loop:
        cmp byte [esi], 0
        je .el_ste_done
        cmp byte [esi], ';'
        je .el_ste_done
        inc esi
        jmp .el_ste_loop
.el_ste_done:
        mov [parse_ptr], esi
        jmp .el_done

;=======================================================================
; PRINT / SAY
;=======================================================================
do_print:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi

.dp_next_item:
        call skip_ws_esi
        cmp byte [esi], 0
        je .dp_done
        cmp byte [esi], ';'
        je .dp_done

        ; String literal?
        cmp byte [esi], '"'
        je .dp_string

        ; Single-quoted string?
        cmp byte [esi], 0x27    ; '
        je .dp_sq_string

        ; Variable?
        cmp byte [esi], '$'
        je .dp_var

        ; Array in string context?
        cmp byte [esi], '@'
        je .dp_array

        ; Number or expression
        mov [parse_ptr], esi
        call eval_expr
        ; Result in expr_result / expr_result_str
        cmp byte [expr_result_type], TYPE_STR
        je .dp_print_str_result
        ; Print integer
        mov eax, [expr_result]
        mov edi, num_format_buf
        call math_int_to_str
        mov esi, num_format_buf
        call io_print
        mov esi, [parse_ptr]
        jmp .dp_after_item

.dp_print_str_result:
        mov esi, expr_result_str
        call io_print
        mov esi, [parse_ptr]
        jmp .dp_after_item

.dp_string:
        ; Print double-quoted string with interpolation
        inc esi                 ; skip opening "
        mov [parse_ptr], esi
        call print_dq_string
        mov esi, [parse_ptr]
        jmp .dp_after_item

.dp_sq_string:
        ; Print single-quoted string (no interpolation)
        inc esi
.dp_sq_loop:
        cmp byte [esi], 0
        je .dp_done
        cmp byte [esi], 0x27   ; closing '
        je .dp_sq_end
        cmp byte [esi], '\'
        jne .dp_sq_char
        ; Escape: only \' and \\ in single quotes
        inc esi
        cmp byte [esi], 0x27
        je .dp_sq_char
        cmp byte [esi], '\'
        je .dp_sq_char
        ; Print the backslash too
        push esi
        mov eax, SYS_PUTCHAR
        mov ebx, '\'
        int 0x80
        pop esi
.dp_sq_char:
        movzx ebx, byte [esi]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .dp_sq_loop
.dp_sq_end:
        inc esi                 ; skip closing '
        jmp .dp_after_item

.dp_var:
        mov [parse_ptr], esi
        call eval_variable
        cmp byte [expr_result_type], TYPE_STR
        je .dp_var_str
        mov eax, [expr_result]
        mov edi, num_format_buf
        call math_int_to_str
        mov esi, num_format_buf
        call io_print
        mov esi, [parse_ptr]
        jmp .dp_after_item
.dp_var_str:
        mov esi, expr_result_str
        call io_print
        mov esi, [parse_ptr]
        jmp .dp_after_item

.dp_array:
        mov [parse_ptr], esi
        call print_array
        mov esi, [parse_ptr]
        jmp .dp_after_item

.dp_after_item:
        call skip_ws_esi
        cmp byte [esi], ','
        jne .dp_after_nocomma
        inc esi
        jmp .dp_next_item
.dp_after_nocomma:
        cmp byte [esi], '.'
        jne .dp_done
        inc esi
        jmp .dp_next_item

.dp_done:
        ; Skip semicolon if present
        cmp byte [esi], ';'
        jne .dp_nosemi
        inc esi
.dp_nosemi:
        mov [parse_ptr], esi
        popad
        ret

;=======================================================================
; PRINT DOUBLE-QUOTED STRING with interpolation and escapes
;=======================================================================
print_dq_string:
        pushad
        mov esi, [parse_ptr]

.pdq_loop:
        cmp byte [esi], 0
        je .pdq_done
        cmp byte [esi], '"'
        je .pdq_end

        ; Check for variable interpolation
        cmp byte [esi], '$'
        je .pdq_interp
        cmp byte [esi], '@'
        je .pdq_interp_arr

        ; Check for escape sequences
        cmp byte [esi], '\'
        je .pdq_escape

        ; Regular character
        movzx ebx, byte [esi]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .pdq_loop

.pdq_escape:
        inc esi
        movzx eax, byte [esi]
        cmp al, 'n'
        je .pdq_esc_n
        cmp al, 't'
        je .pdq_esc_t
        cmp al, '\'
        je .pdq_esc_bs
        cmp al, '"'
        je .pdq_esc_dq
        cmp al, '$'
        je .pdq_esc_dollar
        cmp al, '@'
        je .pdq_esc_at
        ; Unknown escape, print as-is
        mov ebx, '\'
        mov eax, SYS_PUTCHAR
        int 0x80
        movzx ebx, byte [esi]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .pdq_loop

.pdq_esc_n:
        mov ebx, 0x0A
        jmp .pdq_esc_out
.pdq_esc_t:
        mov ebx, 0x09
        jmp .pdq_esc_out
.pdq_esc_bs:
        mov ebx, '\'
        jmp .pdq_esc_out
.pdq_esc_dq:
        mov ebx, '"'
        jmp .pdq_esc_out
.pdq_esc_dollar:
        mov ebx, '$'
        jmp .pdq_esc_out
.pdq_esc_at:
        mov ebx, '@'
        jmp .pdq_esc_out
.pdq_esc_out:
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .pdq_loop

.pdq_interp:
        ; Variable interpolation inside string
        mov [parse_ptr], esi
        call eval_variable
        cmp byte [expr_result_type], TYPE_STR
        je .pdq_interp_str
        mov eax, [expr_result]
        mov edi, num_format_buf
        push esi
        call math_int_to_str
        mov esi, num_format_buf
        call io_print
        pop esi
        mov esi, [parse_ptr]
        jmp .pdq_loop
.pdq_interp_str:
        push esi
        mov esi, expr_result_str
        call io_print
        pop esi
        mov esi, [parse_ptr]
        jmp .pdq_loop

.pdq_interp_arr:
        mov [parse_ptr], esi
        call print_array
        mov esi, [parse_ptr]
        jmp .pdq_loop

.pdq_end:
        inc esi                 ; skip closing "
.pdq_done:
        mov [parse_ptr], esi
        popad
        ret

;=======================================================================
; SCALAR OPERATIONS: $var = expr; $var++ etc.
;=======================================================================
do_scalar_op:
        pushad
        mov esi, [parse_ptr]

        ; Parse variable name
        inc esi                 ; skip $
        mov edi, temp_name
        call read_ident

        call skip_ws_esi

        ; Check for [index] (array element)
        cmp byte [esi], '['
        je .dso_arr_elem

        ; Check for assignment operators
        cmp byte [esi], '='
        je .dso_check_assign
        cmp byte [esi], '+'
        je .dso_check_plusop
        cmp byte [esi], '-'
        je .dso_check_minusop
        cmp byte [esi], '.'
        je .dso_check_dotop

        ; Maybe just evaluating the expression
        jmp .dso_done

.dso_check_assign:
        cmp byte [esi+1], '='  ; == is comparison, not assign
        je .dso_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        ; Store result in variable
        mov esi, temp_name
        call store_var_result
        jmp .dso_done

.dso_check_plusop:
        cmp byte [esi+1], '='  ; +=
        je .dso_plus_eq
        cmp byte [esi+1], '+'  ; ++
        je .dso_incr
        jmp .dso_done

.dso_plus_eq:
        add esi, 2
        mov [parse_ptr], esi
        call eval_expr
        mov esi, temp_name
        call get_var_int
        add eax, [expr_result]
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        mov esi, temp_name
        call store_var_result
        jmp .dso_done

.dso_incr:
        add esi, 2
        mov [parse_ptr], esi
        mov esi, temp_name
        call get_var_int
        inc eax
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        mov esi, temp_name
        call store_var_result
        jmp .dso_done

.dso_check_minusop:
        cmp byte [esi+1], '='  ; -=
        je .dso_minus_eq
        cmp byte [esi+1], '-'  ; --
        je .dso_decr
        jmp .dso_done

.dso_minus_eq:
        add esi, 2
        mov [parse_ptr], esi
        call eval_expr
        mov esi, temp_name
        call get_var_int
        sub eax, [expr_result]
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        mov esi, temp_name
        call store_var_result
        jmp .dso_done

.dso_decr:
        add esi, 2
        mov [parse_ptr], esi
        mov esi, temp_name
        call get_var_int
        dec eax
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        mov esi, temp_name
        call store_var_result
        jmp .dso_done

.dso_check_dotop:
        cmp byte [esi+1], '='  ; .= (concat-assign)
        jne .dso_done
        add esi, 2
        mov [parse_ptr], esi
        ; Get current string value
        push esi
        mov esi, temp_name
        call get_var_str       ; result in expr_result_str
        ; Copy current value
        mov esi, expr_result_str
        mov edi, concat_buf
        call str_copy
        pop esi
        ; Evaluate RHS
        call eval_expr
        ; Append
        mov esi, concat_buf
        mov edi, expr_result_str
        ; Find end of current string in concat_buf
        push edi
        mov edi, concat_buf
        call str_len
        add edi, eax
        ; Copy expr_result_str to concat_buf+len
        cmp byte [expr_result_type], TYPE_STR
        jne .dso_concat_int
        mov esi, expr_result_str
        call str_copy
        jmp .dso_concat_store
.dso_concat_int:
        mov eax, [expr_result]
        call math_int_to_str
        jmp .dso_concat_store
.dso_concat_store:
        pop edi
        ; Copy concat_buf into var
        mov esi, concat_buf
        mov edi, expr_result_str
        call str_copy
        mov byte [expr_result_type], TYPE_STR
        mov esi, temp_name
        call store_var_result
        jmp .dso_done

.dso_arr_elem:
        ; $arr[index] = expr
        inc esi                 ; skip [
        mov [parse_ptr], esi
        push edi               ; save temp_name location
        call eval_expr          ; get index
        mov ebx, [expr_result] ; EBX = index
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ']'
        jne .dso_ae_err
        inc esi
        call skip_ws_esi
        cmp byte [esi], '='
        jne .dso_ae_read
        cmp byte [esi+1], '='
        je .dso_ae_read
        inc esi
        mov [parse_ptr], esi
        pop edi                 ; edi still points at temp_name
        push ebx               ; save index
        call eval_expr          ; get value
        pop ebx
        ; Store into array
        mov esi, temp_name
        mov eax, ebx           ; index
        call set_array_elem
        jmp .dso_done

.dso_ae_read:
        pop edi
        jmp .dso_done
.dso_ae_err:
        pop edi
        call report_syntax_error
        jmp .dso_done

.dso_done:
        popad
        ret

;=======================================================================
; ARRAY OPERATIONS: @arr = (list); push/pop etc.
;=======================================================================
do_array_op:
        pushad
        mov esi, [parse_ptr]
        inc esi                 ; skip @
        mov edi, temp_name
        call read_ident

        call skip_ws_esi
        cmp byte [esi], '='
        jne .dao_done
        cmp byte [esi+1], '='
        je .dao_done
        inc esi
        call skip_ws_esi

        ; Expect ( list )
        cmp byte [esi], '('
        jne .dao_single
        inc esi

        ; Find or create array
        push esi
        mov esi, temp_name
        call find_or_create_array   ; EAX = array index
        mov [temp_arr_idx], eax
        ; Clear existing elements
        mov ebx, eax
        mov dword [arr_counts + ebx*4], 0
        pop esi

        ; Parse elements
.dao_parse_list:
        call skip_ws_esi
        cmp byte [esi], ')'
        je .dao_list_end
        cmp byte [esi], 0
        je .dao_done

        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]

        ; Push value to array
        mov eax, [temp_arr_idx]
        call array_push_result

        call skip_ws_esi
        cmp byte [esi], ','
        jne .dao_list_end
        inc esi
        jmp .dao_parse_list

.dao_list_end:
        cmp byte [esi], ')'
        jne .dao_done
        inc esi
        ; Skip semicolon
        call skip_ws_esi
        cmp byte [esi], ';'
        jne .dao_done
        inc esi
.dao_done:
        mov [parse_ptr], esi
        popad
        ret

.dao_single:
        ; @arr = expr  (single value)
        mov [parse_ptr], esi
        call eval_expr
        push esi
        mov esi, temp_name
        call find_or_create_array
        mov ebx, eax
        mov dword [arr_counts + ebx*4], 0
        mov [temp_arr_idx], eax
        call array_push_result
        pop esi
        mov esi, [parse_ptr]
        jmp .dao_done

;=======================================================================
; IF / ELSIF / ELSE
;=======================================================================
do_if:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi

        ; Expect ( condition )
        cmp byte [esi], '('
        jne .dif_bare_cond
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .dif_eval_done
        inc esi
        jmp .dif_eval_done

.dif_bare_cond:
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]

.dif_eval_done:
        call skip_ws_esi
        mov eax, [expr_result]

        ; Check if condition is true
        cmp eax, 0
        je .dif_false

        ; True: execute block
        cmp byte [esi], '{'
        jne .dif_inline_true
        inc esi
        mov [parse_ptr], esi
        call exec_block
        ; Skip any elsif/else
        mov esi, [parse_ptr]
        call skip_elsif_else
        jmp .dif_done

.dif_inline_true:
        ; Single statement (postfix if not common in block form)
        mov [parse_ptr], esi
        call exec_line
        jmp .dif_done

.dif_false:
        ; Skip the if block
        cmp byte [esi], '{'
        jne .dif_done
        inc esi
        mov [parse_ptr], esi
        call skip_block
        mov esi, [parse_ptr]
        call skip_ws_esi

        ; Check for elsif
        cmp dword [esi], 'elsi'
        jne .dif_check_else
        cmp byte [esi+4], 'f'
        jne .dif_check_else
        add esi, 5
        call skip_ws_esi
        mov [parse_ptr], esi
        call do_if              ; Recursive elsif
        jmp .dif_done

.dif_check_else:
        ; Check for else
        cmp dword [esi], 'else'
        jne .dif_done
        cmp byte [esi+4], ' '
        je .dif_else
        cmp byte [esi+4], '{'
        je .dif_else
        jmp .dif_done

.dif_else:
        add esi, 4
        call skip_ws_esi
        cmp byte [esi], '{'
        jne .dif_done
        inc esi
        mov [parse_ptr], esi
        call exec_block
        jmp .dif_done

.dif_done:
        popad
        ret

;=======================================================================
; UNLESS (negated if)
;=======================================================================
do_unless:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi

        cmp byte [esi], '('
        jne .dun_bare
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .dun_eval
        inc esi
        jmp .dun_eval
.dun_bare:
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
.dun_eval:
        call skip_ws_esi
        mov eax, [expr_result]
        cmp eax, 0
        jne .dun_skip          ; unless = execute if false

        ; Execute block
        cmp byte [esi], '{'
        jne .dun_done
        inc esi
        mov [parse_ptr], esi
        call exec_block
        jmp .dun_done

.dun_skip:
        cmp byte [esi], '{'
        jne .dun_done
        inc esi
        mov [parse_ptr], esi
        call skip_block

.dun_done:
        popad
        ret

;=======================================================================
; WHILE loop
;=======================================================================
do_while:
        pushad
        mov esi, [parse_ptr]

        ; Save condition position
        mov [while_cond_ptr], esi
        inc dword [loop_depth]

.dw_test:
        ; Restore condition parse position
        mov esi, [while_cond_ptr]
        call skip_ws_esi

        cmp byte [esi], '('
        jne .dw_bare_cond
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .dw_check
        inc esi
        jmp .dw_check
.dw_bare_cond:
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]

.dw_check:
        call skip_ws_esi
        mov eax, [expr_result]
        cmp eax, 0
        je .dw_exit

        ; Execute body
        cmp byte [esi], '{'
        jne .dw_exit
        inc esi
        mov [parse_ptr], esi

        ; Save body start for re-execution
        mov [while_body_ptr], esi

        call exec_block

        ; Check for break/continue
        cmp byte [loop_break], 1
        je .dw_break
        cmp byte [loop_continue], 1
        je .dw_continue
        cmp byte [had_error], 0
        jne .dw_exit

        jmp .dw_test

.dw_continue:
        mov byte [loop_continue], 0
        jmp .dw_test

.dw_break:
        mov byte [loop_break], 0

.dw_exit:
        dec dword [loop_depth]

        ; Skip past body if we didn't enter it
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '{'
        jne .dw_done
        inc esi
        mov [parse_ptr], esi
        call skip_block

.dw_done:
        popad
        ret

;=======================================================================
; UNTIL loop (negated while)
;=======================================================================
do_until:
        pushad
        mov esi, [parse_ptr]
        mov [while_cond_ptr], esi
        inc dword [loop_depth]

.du_test:
        mov esi, [while_cond_ptr]
        call skip_ws_esi
        cmp byte [esi], '('
        jne .du_bare
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .du_check
        inc esi
        jmp .du_check
.du_bare:
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]

.du_check:
        call skip_ws_esi
        mov eax, [expr_result]
        cmp eax, 0
        jne .du_exit            ; Until: exit when true

        cmp byte [esi], '{'
        jne .du_exit
        inc esi
        mov [parse_ptr], esi
        call exec_block

        cmp byte [loop_break], 1
        je .du_break
        cmp byte [loop_continue], 1
        je .du_cont
        cmp byte [had_error], 0
        jne .du_exit
        jmp .du_test

.du_cont:
        mov byte [loop_continue], 0
        jmp .du_test
.du_break:
        mov byte [loop_break], 0
.du_exit:
        dec dword [loop_depth]
        popad
        ret

;=======================================================================
; FOREACH loop: foreach $var (list) { ... }
;=======================================================================
do_foreach:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi

        ; Check for 'my' keyword
        cmp byte [esi], 'm'
        jne .dfe_no_my
        cmp byte [esi+1], 'y'
        jne .dfe_no_my
        cmp byte [esi+2], ' '
        jne .dfe_no_my
        add esi, 3
        call skip_ws_esi
.dfe_no_my:

        ; Get iterator variable name
        cmp byte [esi], '$'
        jne .dfe_err
        inc esi
        mov edi, foreach_var
        call read_ident

        call skip_ws_esi
        cmp byte [esi], '('
        jne .dfe_err
        inc esi

        ; Parse list into temp array
        mov dword [foreach_count], 0

.dfe_parse_list:
        call skip_ws_esi
        cmp byte [esi], ')'
        je .dfe_list_done
        cmp byte [esi], 0
        je .dfe_list_done

        ; Check for range operator: N..M
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi

        cmp byte [esi], '.'
        jne .dfe_store_elem
        cmp byte [esi+1], '.'
        jne .dfe_store_elem

        ; Range: start..end
        mov ecx, [expr_result]  ; start
        add esi, 2
        mov [parse_ptr], esi
        push ecx
        call eval_expr
        pop ecx
        mov edx, [expr_result]  ; end
        mov esi, [parse_ptr]

        ; Generate range
.dfe_range_loop:
        cmp ecx, edx
        jg .dfe_range_done
        mov eax, [foreach_count]
        cmp eax, MAX_ARR_ELEM
        jge .dfe_range_done
        mov [foreach_data + eax*4], ecx
        mov byte [foreach_types + eax], TYPE_INT
        inc dword [foreach_count]
        inc ecx
        jmp .dfe_range_loop
.dfe_range_done:
        jmp .dfe_skip_comma

.dfe_store_elem:
        mov eax, [foreach_count]
        cmp eax, MAX_ARR_ELEM
        jge .dfe_skip_comma
        mov ebx, [expr_result]
        mov [foreach_data + eax*4], ebx
        mov bl, [expr_result_type]
        mov [foreach_types + eax], bl
        inc dword [foreach_count]

.dfe_skip_comma:
        call skip_ws_esi
        cmp byte [esi], ','
        jne .dfe_list_done
        inc esi
        jmp .dfe_parse_list

.dfe_list_done:
        cmp byte [esi], ')'
        jne .dfe_err
        inc esi
        call skip_ws_esi
        cmp byte [esi], '{'
        jne .dfe_err
        inc esi

        ; Save body position
        mov [foreach_body_ptr], esi
        inc dword [loop_depth]

        ; Iterate
        mov dword [foreach_idx], 0
.dfe_loop:
        mov eax, [foreach_idx]
        cmp eax, [foreach_count]
        jge .dfe_loop_done

        ; Set iterator variable
        mov ebx, [foreach_data + eax*4]
        mov [expr_result], ebx
        mov bl, [foreach_types + eax]
        mov [expr_result_type], bl
        mov esi, foreach_var
        call store_var_result

        ; Execute body
        mov esi, [foreach_body_ptr]
        mov [parse_ptr], esi
        call exec_block

        cmp byte [loop_break], 1
        je .dfe_break
        cmp byte [loop_continue], 1
        je .dfe_cont
        cmp byte [had_error], 0
        jne .dfe_loop_done

        inc dword [foreach_idx]
        jmp .dfe_loop

.dfe_cont:
        mov byte [loop_continue], 0
        inc dword [foreach_idx]
        jmp .dfe_loop

.dfe_break:
        mov byte [loop_break], 0

.dfe_loop_done:
        dec dword [loop_depth]
        ; Skip remaining block text
        mov esi, [parse_ptr]
        jmp .dfe_done

.dfe_err:
        call report_syntax_error
.dfe_done:
        mov [parse_ptr], esi
        popad
        ret

;=======================================================================
; FOR C-style: for (init; cond; incr) { ... }
;=======================================================================
do_for_c:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi

        cmp byte [esi], '('
        jne .dfc_err
        inc esi

        ; Execute init statement
        mov [parse_ptr], esi
        call exec_line
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ';'
        jne .dfc_err
        inc esi

        ; Save condition position
        mov [for_cond_ptr], esi

        ; Find increment position (after second ;)
        call find_for_parts     ; sets for_incr_ptr, for_body_ptr
        inc dword [loop_depth]

.dfc_test:
        ; Evaluate condition
        mov esi, [for_cond_ptr]
        mov [parse_ptr], esi
        call eval_expr
        mov eax, [expr_result]
        cmp eax, 0
        je .dfc_exit

        ; Execute body
        mov esi, [for_body_ptr]
        mov [parse_ptr], esi
        call exec_block

        cmp byte [loop_break], 1
        je .dfc_break
        cmp byte [loop_continue], 1
        je .dfc_cont
        cmp byte [had_error], 0
        jne .dfc_exit

        ; Execute increment
.dfc_do_incr:
        mov esi, [for_incr_ptr]
        mov [parse_ptr], esi
        call exec_line
        jmp .dfc_test

.dfc_cont:
        mov byte [loop_continue], 0
        jmp .dfc_do_incr

.dfc_break:
        mov byte [loop_break], 0
.dfc_exit:
        dec dword [loop_depth]
        popad
        ret

.dfc_err:
        call report_syntax_error
        popad
        ret

;=======================================================================
; EXEC_BLOCK - Execute statements between { }
; Expects parse_ptr past the opening {
; Handles nested braces, multi-line in script mode
;=======================================================================
exec_block:
        pushad
        mov dword [block_depth], 1

.eb_line_loop:
        ; In script mode, if current line is exhausted, advance
        cmp dword [script_lines], 0
        je .eb_parse_stmt       ; REPL mode, single line

.eb_check_line:
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], 0
        jne .eb_parse_stmt

        ; Advance to next script line
        inc dword [current_line]
        mov eax, [current_line]
        cmp eax, [script_lines]
        jge .eb_done

        imul ebx, eax, MAX_LINE_LEN
        lea esi, [script_buf + ebx]
        mov [parse_ptr], esi

        jmp .eb_check_line

.eb_parse_stmt:
        mov esi, [parse_ptr]
        call skip_ws_esi
        mov [parse_ptr], esi

        cmp byte [esi], 0
        je .eb_line_loop        ; Try next line
        cmp byte [esi], '#'
        je .eb_skip_comment

        ; Check for closing brace
        cmp byte [esi], '}'
        je .eb_close_brace

        ; Check for opening brace in a nested construct
        ; (if/while/for will handle their own braces)

        ; Execute statement
        call exec_line

        ; Check for error/return/break
        cmp byte [had_error], 0
        jne .eb_done
        cmp byte [in_sub_return], 0
        jne .eb_done
        cmp byte [loop_break], 0
        jne .eb_done
        cmp byte [loop_continue], 0
        jne .eb_done

        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ';'
        jne .eb_no_semi
        inc esi
        mov [parse_ptr], esi
.eb_no_semi:
        jmp .eb_line_loop

.eb_skip_comment:
        ; Skip to end of line
        mov esi, [parse_ptr]
.eb_sc_loop:
        cmp byte [esi], 0
        je .eb_sc_done
        inc esi
        jmp .eb_sc_loop
.eb_sc_done:
        mov [parse_ptr], esi
        jmp .eb_line_loop

.eb_close_brace:
        inc esi
        mov [parse_ptr], esi
.eb_done:
        popad
        ret

;=======================================================================
; SKIP BLOCK - Skip { ... } without executing (handles nesting)
;=======================================================================
skip_block:
        pushad
        mov dword [block_depth], 1
        mov esi, [parse_ptr]

.sb_loop:
        cmp dword [block_depth], 0
        je .sb_done

        cmp byte [esi], 0
        je .sb_next_line
        cmp byte [esi], '{'
        je .sb_open
        cmp byte [esi], '}'
        je .sb_close
        cmp byte [esi], '"'
        je .sb_skip_str
        cmp byte [esi], 0x27
        je .sb_skip_sq
        cmp byte [esi], '#'
        je .sb_skip_comment
        inc esi
        jmp .sb_loop

.sb_open:
        inc dword [block_depth]
        inc esi
        jmp .sb_loop

.sb_close:
        dec dword [block_depth]
        inc esi
        jmp .sb_loop

.sb_skip_str:
        inc esi
.sb_str_loop:
        cmp byte [esi], 0
        je .sb_loop
        cmp byte [esi], '\'
        jne .sb_str_noesc
        inc esi
        cmp byte [esi], 0
        je .sb_loop
        inc esi
        jmp .sb_str_loop
.sb_str_noesc:
        cmp byte [esi], '"'
        je .sb_str_end
        inc esi
        jmp .sb_str_loop
.sb_str_end:
        inc esi
        jmp .sb_loop

.sb_skip_sq:
        inc esi
.sb_sq_loop:
        cmp byte [esi], 0
        je .sb_loop
        cmp byte [esi], 0x27
        je .sb_sq_end
        cmp byte [esi], '\'
        jne .sb_sq_noesc
        inc esi
        cmp byte [esi], 0
        je .sb_loop
.sb_sq_noesc:
        inc esi
        jmp .sb_sq_loop
.sb_sq_end:
        inc esi
        jmp .sb_loop

.sb_skip_comment:
        ; Skip to end of line
.sb_com_loop:
        cmp byte [esi], 0
        je .sb_loop
        inc esi
        jmp .sb_com_loop

.sb_next_line:
        ; Advance to next script line
        cmp dword [script_lines], 0
        je .sb_done
        inc dword [current_line]
        mov eax, [current_line]
        cmp eax, [script_lines]
        jge .sb_done
        imul ebx, eax, MAX_LINE_LEN
        lea esi, [script_buf + ebx]
        jmp .sb_loop

.sb_done:
        mov [parse_ptr], esi
        popad
        ret

;=======================================================================
; SKIP elsif/else after a taken if branch
;=======================================================================
skip_elsif_else:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi

.see_loop:
        ; Check for elsif
        cmp dword [esi], 'elsi'
        jne .see_check_else
        cmp byte [esi+4], 'f'
        jne .see_check_else
        add esi, 5
        call skip_ws_esi
        ; Skip condition
        cmp byte [esi], '('
        jne .see_done
        inc esi
        call skip_parens_esi
        call skip_ws_esi
        ; Skip block
        cmp byte [esi], '{'
        jne .see_done
        inc esi
        mov [parse_ptr], esi
        call skip_block
        mov esi, [parse_ptr]
        call skip_ws_esi
        jmp .see_loop

.see_check_else:
        cmp dword [esi], 'else'
        jne .see_done
        mov al, [esi+4]
        cmp al, ' '
        je .see_else
        cmp al, '{'
        je .see_else
        cmp al, 0
        je .see_else
        jmp .see_done

.see_else:
        add esi, 4
        call skip_ws_esi
        cmp byte [esi], '{'
        jne .see_done
        inc esi
        mov [parse_ptr], esi
        call skip_block
        mov esi, [parse_ptr]

.see_done:
        mov [parse_ptr], esi
        popad
        ret

skip_parens_esi:
        ; Skip balanced parentheses, ESI past opening (
        push ecx
        mov ecx, 1
.spe_loop:
        cmp ecx, 0
        je .spe_done
        cmp byte [esi], 0
        je .spe_done
        cmp byte [esi], '('
        jne .spe_noopen
        inc ecx
.spe_noopen:
        cmp byte [esi], ')'
        jne .spe_noclose
        dec ecx
.spe_noclose:
        inc esi
        jmp .spe_loop
.spe_done:
        pop ecx
        ret

;=======================================================================
; SKIP SUB BODY (don't execute sub definitions during sequential run)
;=======================================================================
skip_sub_body:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        ; Skip sub name
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        ; Skip to { and then skip block
        cmp byte [esi], '{'
        jne .ssb_done
        inc esi
        mov [parse_ptr], esi
        call skip_block
.ssb_done:
        popad
        ret

;=======================================================================
; EXPRESSION EVALUATOR
; Recursive descent: or -> and -> comparison -> add -> mul -> unary -> primary
; Result in [expr_result] (int) and [expr_result_str] (string)
;=======================================================================
eval_expr:
        pushad
        call eval_or
        popad
        ret

; --- OR level: expr || expr ---
eval_or:
        call eval_and
.eo_loop:
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '|'
        jne .eo_check_or_kw
        cmp byte [esi+1], '|'
        jne .eo_done
        add esi, 2
        mov [parse_ptr], esi
        ; Short-circuit: if LHS is true, skip RHS
        cmp dword [expr_result], 0
        jne .eo_done
        call eval_and
        jmp .eo_loop

.eo_check_or_kw:
        ; Check for 'or' keyword
        cmp byte [esi], 'o'
        jne .eo_done
        cmp byte [esi+1], 'r'
        jne .eo_done
        cmp byte [esi+2], ' '
        jne .eo_done
        add esi, 3
        mov [parse_ptr], esi
        cmp dword [expr_result], 0
        jne .eo_done
        call eval_and
        jmp .eo_loop

.eo_done:
        ret

; --- AND level: expr && expr ---
eval_and:
        call eval_comparison
.ea_loop:
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '&'
        jne .ea_check_and_kw
        cmp byte [esi+1], '&'
        jne .ea_done
        add esi, 2
        mov [parse_ptr], esi
        cmp dword [expr_result], 0
        je .ea_done             ; Short-circuit
        call eval_comparison
        jmp .ea_loop

.ea_check_and_kw:
        cmp byte [esi], 'a'
        jne .ea_done
        cmp byte [esi+1], 'n'
        jne .ea_done
        cmp byte [esi+2], 'd'
        jne .ea_done
        cmp byte [esi+3], ' '
        jne .ea_done
        add esi, 4
        mov [parse_ptr], esi
        cmp dword [expr_result], 0
        je .ea_done
        call eval_comparison
        jmp .ea_loop

.ea_done:
        ret

; --- COMPARISON level ---
eval_comparison:
        call eval_concat
        mov esi, [parse_ptr]
        call skip_ws_esi

        ; Numeric comparisons
        cmp byte [esi], '='
        je .ec_check_eq
        cmp byte [esi], '!'
        je .ec_check_ne
        cmp byte [esi], '<'
        je .ec_check_lt
        cmp byte [esi], '>'
        je .ec_check_gt

        ; String comparisons: eq ne lt gt le ge
        cmp byte [esi], 'e'
        je .ec_check_str_eq
        cmp byte [esi], 'n'
        je .ec_check_str_ne
        cmp byte [esi], 'l'
        je .ec_check_str_lt
        cmp byte [esi], 'g'
        je .ec_check_str_gt

        ret

.ec_check_eq:
        cmp byte [esi+1], '='
        jne .ec_ret
        add esi, 2
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_concat
        pop ebx
        cmp ebx, [expr_result]
        je .ec_true
        jmp .ec_false

.ec_check_ne:
        cmp byte [esi+1], '='
        jne .ec_check_not
        add esi, 2
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_concat
        pop ebx
        cmp ebx, [expr_result]
        jne .ec_true
        jmp .ec_false

.ec_check_not:
        ret

.ec_check_lt:
        cmp byte [esi+1], '='
        je .ec_le
        inc esi
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_concat
        pop ebx
        cmp ebx, [expr_result]
        jl .ec_true
        jmp .ec_false

.ec_le:
        add esi, 2
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_concat
        pop ebx
        cmp ebx, [expr_result]
        jle .ec_true
        jmp .ec_false

.ec_check_gt:
        cmp byte [esi+1], '='
        je .ec_ge
        inc esi
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_concat
        pop ebx
        cmp ebx, [expr_result]
        jg .ec_true
        jmp .ec_false

.ec_ge:
        add esi, 2
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_concat
        pop ebx
        cmp ebx, [expr_result]
        jge .ec_true
        jmp .ec_false

.ec_check_str_eq:
        cmp byte [esi+1], 'q'
        jne .ec_ret
        cmp byte [esi+2], ' '
        jne .ec_ret
        add esi, 3
        mov [parse_ptr], esi
        ; Save LHS string
        mov esi, expr_result_str
        mov edi, cmp_buf
        call str_copy
        call eval_concat
        mov esi, cmp_buf
        mov edi, expr_result_str
        call str_cmp
        je .ec_true
        jmp .ec_false

.ec_check_str_ne:
        cmp byte [esi+1], 'e'
        jne .ec_ret
        cmp byte [esi+2], ' '
        jne .ec_ret
        add esi, 3
        mov [parse_ptr], esi
        mov esi, expr_result_str
        mov edi, cmp_buf
        call str_copy
        call eval_concat
        mov esi, cmp_buf
        mov edi, expr_result_str
        call str_cmp
        jne .ec_true
        jmp .ec_false

.ec_check_str_lt:
        cmp byte [esi+1], 't'
        jne .ec_check_str_le
        cmp byte [esi+2], ' '
        jne .ec_check_str_le
        add esi, 3
        mov [parse_ptr], esi
        mov esi, expr_result_str
        mov edi, cmp_buf
        call str_copy
        call eval_concat
        mov esi, cmp_buf
        mov edi, expr_result_str
        call str_cmp
        jb .ec_true
        jmp .ec_false

.ec_check_str_le:
        cmp byte [esi+1], 'e'
        jne .ec_ret
        cmp byte [esi+2], ' '
        jne .ec_ret
        add esi, 3
        mov [parse_ptr], esi
        mov esi, expr_result_str
        mov edi, cmp_buf
        call str_copy
        call eval_concat
        mov esi, cmp_buf
        mov edi, expr_result_str
        call str_cmp
        jbe .ec_true
        jmp .ec_false

.ec_check_str_gt:
        cmp byte [esi+1], 't'
        jne .ec_check_str_ge
        cmp byte [esi+2], ' '
        jne .ec_check_str_ge
        add esi, 3
        mov [parse_ptr], esi
        mov esi, expr_result_str
        mov edi, cmp_buf
        call str_copy
        call eval_concat
        mov esi, cmp_buf
        mov edi, expr_result_str
        call str_cmp
        ja .ec_true
        jmp .ec_false

.ec_check_str_ge:
        cmp byte [esi+1], 'e'
        jne .ec_ret
        cmp byte [esi+2], ' '
        jne .ec_ret
        add esi, 3
        mov [parse_ptr], esi
        mov esi, expr_result_str
        mov edi, cmp_buf
        call str_copy
        call eval_concat
        mov esi, cmp_buf
        mov edi, expr_result_str
        call str_cmp
        jae .ec_true
        jmp .ec_false

.ec_true:
        mov dword [expr_result], 1
        mov byte [expr_result_type], TYPE_INT
        ret
.ec_false:
        mov dword [expr_result], 0
        mov byte [expr_result_type], TYPE_INT
.ec_ret:
        ret

; --- CONCAT level: expr . expr  /  expr x N ---
eval_concat:
        call eval_add
.ecc_loop:
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '.'
        je .ecc_dot
        ; Check for x operator (string repeat)
        cmp byte [esi], 'x'
        jne .ecc_done
        cmp byte [esi+1], ' '
        jne .ecc_done
        ; x repeat
        add esi, 2
        mov [parse_ptr], esi
        ; Save LHS string
        mov esi, expr_result_str
        mov edi, concat_buf
        call str_copy
        call eval_add
        ; Repeat concat_buf expr_result times
        mov ecx, [expr_result]
        cmp ecx, 0
        jle .ecc_empty_repeat
        mov edi, expr_result_str
        xor edx, edx
.ecc_rep_loop:
        cmp edx, ecx
        jge .ecc_rep_done
        push ecx
        push edx
        mov esi, concat_buf
        call str_len
        push eax
        mov esi, concat_buf
        call str_copy
        pop eax
        add edi, eax
        pop edx
        pop ecx
        inc edx
        jmp .ecc_rep_loop
.ecc_rep_done:
        mov byte [edi], 0
        mov byte [expr_result_type], TYPE_STR
        jmp .ecc_loop

.ecc_empty_repeat:
        mov byte [expr_result_str], 0
        mov byte [expr_result_type], TYPE_STR
        jmp .ecc_loop

.ecc_dot:
        cmp byte [esi+1], '.'  ; .. (range) is handled elsewhere
        je .ecc_done
        cmp byte [esi+1], '='  ; .= handled elsewhere
        je .ecc_done
        inc esi
        mov [parse_ptr], esi
        ; Save LHS string
        mov esi, expr_result_str
        mov edi, concat_buf
        call str_copy
        call eval_add
        ; Concatenate
        mov esi, concat_buf
        call str_len
        lea edi, [concat_buf + eax]
        cmp byte [expr_result_type], TYPE_STR
        jne .ecc_dot_int
        mov esi, expr_result_str
        call str_copy
        jmp .ecc_dot_store
.ecc_dot_int:
        mov eax, [expr_result]
        call math_int_to_str
.ecc_dot_store:
        mov esi, concat_buf
        mov edi, expr_result_str
        call str_copy
        mov byte [expr_result_type], TYPE_STR
        jmp .ecc_loop

.ecc_done:
        ret

; --- ADD level: expr +/- expr ---
eval_add:
        call eval_mul
.ead_loop:
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '+'
        je .ead_add
        cmp byte [esi], '-'
        je .ead_sub
        ret

.ead_add:
        cmp byte [esi+1], '+'  ; ++ is postincrement
        je .ead_done
        cmp byte [esi+1], '='  ; += handled elsewhere
        je .ead_done
        inc esi
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_mul
        pop ebx
        add [expr_result], ebx
        mov byte [expr_result_type], TYPE_INT
        jmp .ead_loop

.ead_sub:
        cmp byte [esi+1], '-'
        je .ead_done
        cmp byte [esi+1], '='
        je .ead_done
        inc esi
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_mul
        pop ebx
        mov eax, ebx
        sub eax, [expr_result]
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        jmp .ead_loop

.ead_done:
        ret

; --- MUL level: expr * / % ** expr ---
eval_mul:
        call eval_unary
.em_loop:
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '*'
        je .em_mul
        cmp byte [esi], '/'
        je .em_div
        cmp byte [esi], '%'
        je .em_mod
        ret

.em_mul:
        cmp byte [esi+1], '*'
        je .em_power
        inc esi
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_unary
        pop ebx
        imul ebx, [expr_result]
        mov [expr_result], ebx
        mov byte [expr_result_type], TYPE_INT
        jmp .em_loop

.em_power:
        add esi, 2
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_unary
        pop ebx
        ; ebx ** expr_result
        mov ecx, [expr_result]
        mov eax, 1
        cmp ecx, 0
        jle .em_pow_done
.em_pow_loop:
        imul eax, ebx
        dec ecx
        jnz .em_pow_loop
.em_pow_done:
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        jmp .em_loop

.em_div:
        inc esi
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_unary
        pop ebx
        mov eax, [expr_result]
        cmp eax, 0
        je .em_div0
        xchg eax, ebx
        cdq
        idiv ebx
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        jmp .em_loop

.em_div0:
        mov esi, err_div_zero
        call report_error
        mov dword [expr_result], 0
        ret

.em_mod:
        inc esi
        mov [parse_ptr], esi
        push dword [expr_result]
        call eval_unary
        pop ebx
        mov eax, [expr_result]
        cmp eax, 0
        je .em_div0
        xchg eax, ebx
        cdq
        idiv ebx
        mov [expr_result], edx
        mov byte [expr_result_type], TYPE_INT
        jmp .em_loop

; --- UNARY level: - ! not ---
eval_unary:
        mov esi, [parse_ptr]
        call skip_ws_esi

        cmp byte [esi], '-'
        je .eu_neg
        cmp byte [esi], '!'
        je .eu_not
        cmp byte [esi], 'n'
        jne .eu_primary

        ; Check for 'not'
        cmp byte [esi+1], 'o'
        jne .eu_primary
        cmp byte [esi+2], 't'
        jne .eu_primary
        cmp byte [esi+3], ' '
        jne .eu_primary
        add esi, 4
        mov [parse_ptr], esi
        call eval_unary
        cmp dword [expr_result], 0
        je .eu_not_true
        mov dword [expr_result], 0
        ret
.eu_not_true:
        mov dword [expr_result], 1
        mov byte [expr_result_type], TYPE_INT
        ret

.eu_neg:
        ; Make sure it's not -> or just a minus sign at end
        inc esi
        mov [parse_ptr], esi
        call eval_unary
        neg dword [expr_result]
        mov byte [expr_result_type], TYPE_INT
        ret

.eu_not:
        cmp byte [esi+1], '='      ; != is comparison
        je .eu_primary
        inc esi
        mov [parse_ptr], esi
        call eval_unary
        cmp dword [expr_result], 0
        je .eu_not_true
        mov dword [expr_result], 0
        mov byte [expr_result_type], TYPE_INT
        ret

.eu_primary:
        call eval_primary
        ret

; --- PRIMARY level: number, string, variable, ( expr ), function calls ---
eval_primary:
        mov esi, [parse_ptr]
        call skip_ws_esi

        ; Number literal
        cmp byte [esi], '0'
        jb .ep_not_num
        cmp byte [esi], '9'
        ja .ep_not_num
        call math_parse_signed
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        ; Advance parse_ptr
        add esi, ecx
        mov [parse_ptr], esi
        ; Also put numeric string repr
        mov eax, [expr_result]
        mov edi, expr_result_str
        call math_int_to_str
        ret

.ep_not_num:
        ; Double-quoted string
        cmp byte [esi], '"'
        je .ep_dq_string

        ; Single-quoted string
        cmp byte [esi], 0x27
        je .ep_sq_string

        ; Variable $xxx
        cmp byte [esi], '$'
        je .ep_variable

        ; Array @xxx or scalar(@xxx)
        cmp byte [esi], '@'
        je .ep_array_val

        ; Parenthesized expression
        cmp byte [esi], '('
        je .ep_paren

        ; <STDIN> or <>
        cmp byte [esi], '<'
        je .ep_stdin

        ; Built-in functions and keywords
        call try_builtin
        ret

.ep_dq_string:
        ; Parse double-quoted string into expr_result_str
        inc esi
        mov edi, expr_result_str
.ep_dqs_loop:
        cmp byte [esi], 0
        je .ep_dqs_done
        cmp byte [esi], '"'
        je .ep_dqs_end
        cmp byte [esi], '\'
        je .ep_dqs_esc
        cmp byte [esi], '$'
        je .ep_dqs_interp

        mov al, [esi]
        mov [edi], al
        inc edi
        inc esi
        jmp .ep_dqs_loop

.ep_dqs_esc:
        inc esi
        movzx eax, byte [esi]
        cmp al, 'n'
        jne .ep_dqs_esc_t
        mov byte [edi], 0x0A
        jmp .ep_dqs_esc_done
.ep_dqs_esc_t:
        cmp al, 't'
        jne .ep_dqs_esc_other
        mov byte [edi], 0x09
        jmp .ep_dqs_esc_done
.ep_dqs_esc_other:
        mov [edi], al
.ep_dqs_esc_done:
        inc edi
        inc esi
        jmp .ep_dqs_loop

.ep_dqs_interp:
        ; Interpolate variable into string
        mov [parse_ptr], esi
        push edi
        call eval_variable
        pop edi
        ; Copy result into string being built
        cmp byte [expr_result_type], TYPE_STR
        jne .ep_dqs_interp_int
        push esi
        mov esi, expr_result_str
.ep_dqs_copy_str:
        mov al, [esi]
        cmp al, 0
        je .ep_dqs_copy_done
        mov [edi], al
        inc edi
        inc esi
        jmp .ep_dqs_copy_str
.ep_dqs_copy_done:
        pop esi
        mov esi, [parse_ptr]
        jmp .ep_dqs_loop
.ep_dqs_interp_int:
        push esi
        mov eax, [expr_result]
        push edi
        mov edi, num_format_buf
        call math_int_to_str
        pop edi
        mov esi, num_format_buf
.ep_dqs_copy_int:
        mov al, [esi]
        cmp al, 0
        je .ep_dqs_int_done
        mov [edi], al
        inc edi
        inc esi
        jmp .ep_dqs_copy_int
.ep_dqs_int_done:
        pop esi
        mov esi, [parse_ptr]
        jmp .ep_dqs_loop

.ep_dqs_end:
        inc esi
.ep_dqs_done:
        mov byte [edi], 0
        mov [parse_ptr], esi
        mov byte [expr_result_type], TYPE_STR
        ; Set int value to string length
        push esi
        mov esi, expr_result_str
        call str_len
        mov [expr_result], eax
        pop esi
        ret

.ep_sq_string:
        ; Single-quoted string: no interpolation
        inc esi
        mov edi, expr_result_str
.ep_sqs_loop:
        cmp byte [esi], 0
        je .ep_sqs_done
        cmp byte [esi], 0x27
        je .ep_sqs_end
        cmp byte [esi], '\'
        jne .ep_sqs_char
        ; Only \' and \\ in single quotes
        cmp byte [esi+1], 0x27
        je .ep_sqs_esc
        cmp byte [esi+1], '\'
        je .ep_sqs_esc
        ; Literal backslash
.ep_sqs_char:
        mov al, [esi]
        mov [edi], al
        inc edi
        inc esi
        jmp .ep_sqs_loop
.ep_sqs_esc:
        inc esi
        mov al, [esi]
        mov [edi], al
        inc edi
        inc esi
        jmp .ep_sqs_loop
.ep_sqs_end:
        inc esi
.ep_sqs_done:
        mov byte [edi], 0
        mov [parse_ptr], esi
        mov byte [expr_result_type], TYPE_STR
        push esi
        mov esi, expr_result_str
        call str_len
        mov [expr_result], eax
        pop esi
        ret

.ep_variable:
        mov [parse_ptr], esi
        call eval_variable
        ret

.ep_array_val:
        ; scalar @arr => array count
        inc esi
        mov edi, temp_name
        call read_ident
        mov [parse_ptr], esi
        push esi
        mov esi, temp_name
        call find_array
        cmp eax, -1
        je .ep_arr_zero
        mov eax, [arr_counts + eax*4]
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        pop esi
        ret
.ep_arr_zero:
        mov dword [expr_result], 0
        mov byte [expr_result_type], TYPE_INT
        pop esi
        ret

.ep_paren:
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .ep_paren_done
        inc esi
.ep_paren_done:
        mov [parse_ptr], esi
        ret

.ep_stdin:
        ; <STDIN> or <> - read line from input
        inc esi                 ; skip <
        ; Skip to >
.ep_stdin_skip:
        cmp byte [esi], '>'
        je .ep_stdin_read
        cmp byte [esi], 0
        je .ep_stdin_read
        inc esi
        jmp .ep_stdin_skip
.ep_stdin_read:
        cmp byte [esi], '>'
        jne .ep_stdin_done_nogt
        inc esi
.ep_stdin_done_nogt:
        mov [parse_ptr], esi

        ; Check for piped stdin first
        push esi
        mov eax, SYS_STDIN_READ
        mov ebx, expr_result_str
        int 0x80
        cmp eax, 0
        jg .ep_stdin_got_piped
        pop esi

        ; Interactive: read from keyboard
        push esi
        mov edi, expr_result_str
        mov ecx, VAR_VAL_LEN
        call io_read_line
        call io_newline
        pop esi

        mov byte [expr_result_type], TYPE_STR
        push esi
        mov esi, expr_result_str
        call str_len
        mov [expr_result], eax
        pop esi
        ret

.ep_stdin_got_piped:
        ; Remove trailing newline if present
        mov esi, expr_result_str
        call str_len
        cmp eax, 0
        je .ep_stdin_piped_done
        dec eax
        cmp byte [expr_result_str + eax], 0x0A
        jne .ep_stdin_piped_done
        mov byte [expr_result_str + eax], 0
.ep_stdin_piped_done:
        pop esi
        mov byte [expr_result_type], TYPE_STR
        push esi
        mov esi, expr_result_str
        call str_len
        mov [expr_result], eax
        pop esi
        ret

;=======================================================================
; EVAL_VARIABLE - Evaluate $var or $arr[idx] or special variables
;=======================================================================
eval_variable:
        pushad
        mov esi, [parse_ptr]
        inc esi                 ; skip $

        ; Special variable: $_
        cmp byte [esi], '_'
        jne .ev_not_underscore
        cmp byte [esi+1], 0
        je .ev_underscore
        cmp byte [esi+1], ' '
        je .ev_underscore
        cmp byte [esi+1], ';'
        je .ev_underscore
        cmp byte [esi+1], ')'
        je .ev_underscore
        cmp byte [esi+1], ','
        je .ev_underscore
        cmp byte [esi+1], '"'
        je .ev_underscore
        cmp byte [esi+1], '.'
        je .ev_underscore
        cmp byte [esi+1], '['
        jne .ev_not_underscore

.ev_underscore:
        inc esi
        mov [parse_ptr], esi
        ; Copy _ variable value
        push esi
        mov esi, underscore_name
        call get_var_str
        pop esi
        popad
        ret

.ev_not_underscore:
        mov edi, temp_name
        call read_ident
        mov [parse_ptr], esi

        ; Check for array element $arr[idx]
        call skip_ws_esi
        cmp byte [esi], '['
        je .ev_arr_elem

        ; Regular scalar lookup
        push esi
        mov esi, temp_name
        call lookup_var
        pop esi
        mov [parse_ptr], esi
        popad
        ret

.ev_arr_elem:
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov ebx, [expr_result] ; index
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ']'
        jne .ev_ae_done
        inc esi
.ev_ae_done:
        mov [parse_ptr], esi
        push esi
        mov esi, temp_name
        mov eax, ebx
        call get_array_elem
        pop esi
        popad
        ret

;=======================================================================
; BUILT-IN FUNCTIONS
;=======================================================================
try_builtin:
        mov esi, [parse_ptr]
        call skip_ws_esi

        ; length(expr)
        mov edi, kw_length
        call match_keyword_esi
        jc .tb_length

        ; substr(str, offset, len)
        mov edi, kw_substr
        call match_keyword_esi
        jc .tb_substr

        ; abs(n)
        mov edi, kw_abs
        call match_keyword_esi
        jc .tb_abs

        ; int(n)
        mov edi, kw_int
        call match_keyword_esi
        jc .tb_int

        ; chr(n)
        mov edi, kw_chr
        call match_keyword_esi
        jc .tb_chr

        ; ord(s)
        mov edi, kw_ord
        call match_keyword_esi
        jc .tb_ord

        ; uc(s) / lc(s)
        mov edi, kw_uc
        call match_keyword_esi
        jc .tb_uc
        mov edi, kw_lc
        call match_keyword_esi
        jc .tb_lc

        ; index(s, substr)
        mov edi, kw_index
        call match_keyword_esi
        jc .tb_index

        ; rindex(s, substr)
        mov edi, kw_rindex
        call match_keyword_esi
        jc .tb_rindex

        ; join(sep, @arr)
        mov edi, kw_join
        call match_keyword_esi
        jc .tb_join

        ; split(pat, str)
        mov edi, kw_split
        call match_keyword_esi
        jc .tb_split

        ; reverse
        mov edi, kw_reverse
        call match_keyword_esi
        jc .tb_reverse

        ; sort
        mov edi, kw_sort
        call match_keyword_esi
        jc .tb_sort

        ; defined($var)
        mov edi, kw_defined
        call match_keyword_esi
        jc .tb_defined

        ; scalar(@arr)
        mov edi, kw_scalar
        call match_keyword_esi
        jc .tb_scalar

        ; rand / srand
        mov edi, kw_rand
        call match_keyword_esi
        jc .tb_rand

        ; Try as sub call
        mov [parse_ptr], esi
        call try_sub_call
        cmp eax, 1
        je .tb_sub_done
        ; If sub call didn't match, it's unknown - just return 0
        mov dword [expr_result], 0
        mov byte [expr_result_type], TYPE_UNDEF
        ret

.tb_sub_done:
        ret

; --- length(str) ---
.tb_length:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_length_bare
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_len_done
        inc esi
.tb_len_done:
        mov [parse_ptr], esi
        cmp byte [expr_result_type], TYPE_STR
        jne .tb_len_int
        push esi
        mov esi, expr_result_str
        call str_len
        mov [expr_result], eax
        pop esi
        mov byte [expr_result_type], TYPE_INT
        ret
.tb_len_int:
        ; Length of number = digits count
        push esi
        mov eax, [expr_result]
        mov edi, num_format_buf
        call math_int_to_str
        mov esi, num_format_buf
        call str_len
        mov [expr_result], eax
        pop esi
        mov byte [expr_result_type], TYPE_INT
        ret

.tb_length_bare:
        ; length $var - without parens
        mov [parse_ptr], esi
        call eval_expr
        jmp .tb_len_done

; --- substr(str, offset, len) ---
.tb_substr:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_substr_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr          ; get string
        mov esi, expr_result_str
        mov edi, concat_buf
        call str_copy
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ','
        jne .tb_substr_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr          ; get offset
        mov ebx, [expr_result]
        mov esi, [parse_ptr]
        call skip_ws_esi

        ; Optional length parameter
        mov ecx, 0x7FFFFFFF    ; default: rest of string
        cmp byte [esi], ','
        jne .tb_substr_nolen
        inc esi
        mov [parse_ptr], esi
        push ebx
        call eval_expr
        mov ecx, [expr_result]
        pop ebx
        mov esi, [parse_ptr]
        call skip_ws_esi

.tb_substr_nolen:
        cmp byte [esi], ')'
        jne .tb_substr_done
        inc esi
        mov [parse_ptr], esi

        ; Do substr: concat_buf[ebx..ebx+ecx]
        push esi
        mov esi, concat_buf
        call str_len
        ; Clamp offset
        cmp ebx, eax
        jge .tb_substr_empty
        cmp ebx, 0
        jge .tb_substr_pos_ok
        ; Negative offset: from end
        add ebx, eax
        cmp ebx, 0
        jl .tb_substr_empty
.tb_substr_pos_ok:
        ; Clamp length
        mov edx, eax
        sub edx, ebx
        cmp ecx, edx
        jle .tb_substr_len_ok
        mov ecx, edx
.tb_substr_len_ok:
        ; Copy substring
        lea esi, [concat_buf + ebx]
        mov edi, expr_result_str
        xor edx, edx
.tb_substr_copy:
        cmp edx, ecx
        jge .tb_substr_copy_done
        mov al, [esi + edx]
        cmp al, 0
        je .tb_substr_copy_done
        mov [edi + edx], al
        inc edx
        jmp .tb_substr_copy
.tb_substr_copy_done:
        mov byte [edi + edx], 0
        pop esi
        mov byte [expr_result_type], TYPE_STR
        ret

.tb_substr_empty:
        mov byte [expr_result_str], 0
        pop esi
        mov byte [expr_result_type], TYPE_STR
        ret

.tb_substr_done:
        mov [parse_ptr], esi
        ret

; --- abs(n) ---
.tb_abs:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_abs_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_abs_done
        inc esi
        mov [parse_ptr], esi
        mov eax, [expr_result]
        cmp eax, 0
        jge .tb_abs_done
        neg eax
        mov [expr_result], eax
.tb_abs_done:
        mov byte [expr_result_type], TYPE_INT
        ret

; --- int(n) - truncate to integer ---
.tb_int:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_int_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_int_done
        inc esi
        mov [parse_ptr], esi
.tb_int_done:
        mov byte [expr_result_type], TYPE_INT
        ret

; --- chr(n) ---
.tb_chr:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_chr_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_chr_done
        inc esi
        mov [parse_ptr], esi
        mov eax, [expr_result]
        mov byte [expr_result_str], al
        mov byte [expr_result_str + 1], 0
        mov byte [expr_result_type], TYPE_STR
.tb_chr_done:
        ret

; --- ord(s) ---
.tb_ord:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_ord_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_ord_done
        inc esi
        mov [parse_ptr], esi
        movzx eax, byte [expr_result_str]
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
.tb_ord_done:
        ret

; --- uc(s) / lc(s) ---
.tb_uc:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_uc_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_uc_done
        inc esi
        mov [parse_ptr], esi
        push esi
        mov esi, expr_result_str
        call str_upper
        pop esi
        mov byte [expr_result_type], TYPE_STR
.tb_uc_done:
        ret

.tb_lc:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_lc_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_lc_done
        inc esi
        mov [parse_ptr], esi
        push esi
        mov esi, expr_result_str
        call str_lower
        pop esi
        mov byte [expr_result_type], TYPE_STR
.tb_lc_done:
        ret

; --- index(str, substr) ---
.tb_index:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_idx_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, expr_result_str
        mov edi, concat_buf
        call str_copy
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ','
        jne .tb_idx_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_idx_done
        inc esi
        mov [parse_ptr], esi
        ; Search concat_buf for expr_result_str
        push esi
        mov esi, concat_buf
        mov edi, expr_result_str
        call str_str
        cmp eax, 0
        je .tb_idx_notfound
        ; Calculate offset
        sub eax, concat_buf
        mov [expr_result], eax
        jmp .tb_idx_fin
.tb_idx_notfound:
        mov dword [expr_result], -1
.tb_idx_fin:
        pop esi
        mov byte [expr_result_type], TYPE_INT
.tb_idx_done:
        ret

; --- rindex(str, substr) ---
.tb_rindex:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_ridx_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, expr_result_str
        mov edi, concat_buf
        call str_copy
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ','
        jne .tb_ridx_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_ridx_done
        inc esi
        mov [parse_ptr], esi
        ; Search from end
        push esi
        mov esi, concat_buf
        call str_len
        mov ecx, eax
        mov edi, expr_result_str
        call str_len
        mov edx, eax            ; substr len
        mov dword [expr_result], -1
        cmp ecx, edx
        jb .tb_ridx_fin
        sub ecx, edx
.tb_ridx_loop:
        cmp ecx, 0
        jl .tb_ridx_fin
        ; Compare at position ecx
        push ecx
        lea esi, [concat_buf + ecx]
        mov edi, expr_result_str
        push edx
        mov ecx, edx
        call mem_cmp
        pop edx
        pop ecx
        cmp eax, 0
        je .tb_ridx_found
        dec ecx
        jmp .tb_ridx_loop
.tb_ridx_found:
        mov [expr_result], ecx
.tb_ridx_fin:
        pop esi
        mov byte [expr_result_type], TYPE_INT
.tb_ridx_done:
        ret

; --- join(sep, @arr) ---
.tb_join:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_join_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr          ; separator string
        mov esi, expr_result_str
        mov edi, join_sep
        call str_copy
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ','
        jne .tb_join_done
        inc esi
        call skip_ws_esi
        cmp byte [esi], '@'
        jne .tb_join_done
        inc esi
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_join_done
        inc esi
        mov [parse_ptr], esi

        ; Join array elements
        push esi
        mov esi, temp_name
        call find_array
        cmp eax, -1
        je .tb_join_empty
        mov ecx, [arr_counts + eax*4]
        cmp ecx, 0
        je .tb_join_empty

        mov edi, expr_result_str
        xor edx, edx           ; element index
.tb_join_loop:
        cmp edx, ecx
        jge .tb_join_end
        ; Add separator if not first
        cmp edx, 0
        je .tb_join_no_sep
        push ecx
        push edx
        push esi
        mov esi, join_sep
.tb_join_sep_copy:
        mov al, [esi]
        cmp al, 0
        je .tb_join_sep_done
        mov [edi], al
        inc edi
        inc esi
        jmp .tb_join_sep_copy
.tb_join_sep_done:
        pop esi
        pop edx
        pop ecx
.tb_join_no_sep:
        ; Get element value
        push ecx
        push edx
        mov esi, temp_name
        mov eax, edx
        call get_array_elem
        ; Copy to output
        push esi
        cmp byte [expr_result_type], TYPE_STR
        jne .tb_join_elem_int
        mov esi, expr_result_str
        jmp .tb_join_elem_copy
.tb_join_elem_int:
        mov eax, [expr_result]
        push edi
        mov edi, num_format_buf
        call math_int_to_str
        pop edi
        mov esi, num_format_buf
.tb_join_elem_copy:
        mov al, [esi]
        cmp al, 0
        je .tb_join_elem_done
        mov [edi], al
        inc edi
        inc esi
        jmp .tb_join_elem_copy
.tb_join_elem_done:
        pop esi
        pop edx
        pop ecx
        inc edx
        jmp .tb_join_loop

.tb_join_end:
        mov byte [edi], 0
        pop esi
        mov byte [expr_result_type], TYPE_STR
        ret

.tb_join_empty:
        mov byte [expr_result_str], 0
        pop esi
        mov byte [expr_result_type], TYPE_STR
.tb_join_done:
        ret

; --- split(pattern, string) -> stores in temp array ---
.tb_split:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_split_done
        inc esi
        mov [parse_ptr], esi
        ; Get separator
        call eval_expr
        mov esi, expr_result_str
        mov edi, split_sep
        call str_copy
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ','
        jne .tb_split_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr          ; string to split
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_split_done
        inc esi
        mov [parse_ptr], esi

        ; Result is array count - stored for assignment
        mov dword [expr_result], 0
        mov byte [expr_result_type], TYPE_INT
.tb_split_done:
        ret

; --- reverse ---
.tb_reverse:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_rev_done
        inc esi
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_rev_done
        inc esi
        mov [parse_ptr], esi
        cmp byte [expr_result_type], TYPE_STR
        jne .tb_rev_done
        push esi
        mov esi, expr_result_str
        call str_reverse
        pop esi
        mov byte [expr_result_type], TYPE_STR
.tb_rev_done:
        ret

; --- sort(@arr) - returns sorted array count ---
.tb_sort:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_sort_done
        inc esi
        call skip_ws_esi
        cmp byte [esi], '@'
        jne .tb_sort_done
        inc esi
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_sort_done
        inc esi
        mov [parse_ptr], esi
        ; Simple bubble sort on the array (numeric)
        push esi
        mov esi, temp_name
        call find_array
        cmp eax, -1
        je .tb_sort_fin
        call sort_array
.tb_sort_fin:
        pop esi
        mov dword [expr_result], 0
        mov byte [expr_result_type], TYPE_INT
.tb_sort_done:
        ret

; --- defined($var) ---
.tb_defined:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_def_done
        inc esi
        call skip_ws_esi
        cmp byte [esi], '$'
        jne .tb_def_done
        inc esi
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_def_done
        inc esi
        mov [parse_ptr], esi
        push esi
        mov esi, temp_name
        call find_var
        cmp eax, -1
        je .tb_def_undef
        movzx ebx, byte [var_types + eax]
        cmp ebx, TYPE_UNDEF
        je .tb_def_undef
        mov dword [expr_result], 1
        jmp .tb_def_fin
.tb_def_undef:
        mov dword [expr_result], 0
.tb_def_fin:
        pop esi
        mov byte [expr_result_type], TYPE_INT
.tb_def_done:
        ret

; --- scalar(@arr) ---
.tb_scalar:
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_sca_done
        inc esi
        call skip_ws_esi
        cmp byte [esi], '@'
        jne .tb_sca_done
        inc esi
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_sca_done
        inc esi
        mov [parse_ptr], esi
        push esi
        mov esi, temp_name
        call find_array
        cmp eax, -1
        je .tb_sca_zero
        mov eax, [arr_counts + eax*4]
        mov [expr_result], eax
        jmp .tb_sca_fin
.tb_sca_zero:
        mov dword [expr_result], 0
.tb_sca_fin:
        pop esi
        mov byte [expr_result_type], TYPE_INT
.tb_sca_done:
        ret

; --- rand ---
.tb_rand:
        push esi
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tb_rand_noarg
        inc esi
        mov [parse_ptr], esi
        call eval_expr          ; max
        mov ecx, [expr_result]
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .tb_rand_do
        inc esi
.tb_rand_do:
        mov [parse_ptr], esi
        cmp ecx, 0
        jle .tb_rand_noarg
        mov eax, [rand_seed]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        xor edx, edx
        div ecx
        mov [expr_result], edx
        mov byte [expr_result_type], TYPE_INT
        pop esi
        ret

.tb_rand_noarg:
        mov eax, [rand_seed]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        mov [expr_result], eax
        mov byte [expr_result_type], TYPE_INT
        mov [parse_ptr], esi
        pop esi
        ret

;=======================================================================
; CHOMP - Remove trailing newline from variable
;=======================================================================
do_chomp:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '('
        jne .dc_bare
        inc esi
.dc_bare:
        call skip_ws_esi
        cmp byte [esi], '$'
        jne .dc_done
        inc esi
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .dc_no_paren
        inc esi
.dc_no_paren:
        call skip_ws_esi
        cmp byte [esi], ';'
        jne .dc_no_semi
        inc esi
.dc_no_semi:
        mov [parse_ptr], esi

        ; Get string value and remove trailing \n
        push esi
        mov esi, temp_name
        call get_var_str
        mov esi, expr_result_str
        call str_len
        cmp eax, 0
        je .dc_store
        dec eax
        cmp byte [expr_result_str + eax], 0x0A
        jne .dc_store
        mov byte [expr_result_str + eax], 0
.dc_store:
        mov byte [expr_result_type], TYPE_STR
        mov esi, temp_name
        call store_var_result
        pop esi

.dc_done:
        popad
        ret

;=======================================================================
; PUSH / POP / SHIFT / UNSHIFT
;=======================================================================
do_push:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '@'
        jne .dpu_done
        inc esi
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        cmp byte [esi], ','
        jne .dpu_done
        inc esi
        mov [parse_ptr], esi

        push esi
        mov esi, temp_name
        call find_or_create_array
        mov [temp_arr_idx], eax
        pop esi

        call eval_expr
        mov eax, [temp_arr_idx]
        call array_push_result

        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ';'
        jne .dpu_done
        inc esi
.dpu_done:
        mov [parse_ptr], esi
        popad
        ret

do_pop_op:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '('
        jne .dpo_bare
        inc esi
        call skip_ws_esi
.dpo_bare:
        cmp byte [esi], '@'
        jne .dpo_done
        inc esi
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .dpo_no_paren
        inc esi
.dpo_no_paren:
        mov [parse_ptr], esi
        push esi
        mov esi, temp_name
        call find_array
        cmp eax, -1
        je .dpo_fin
        ; Pop last element
        mov ecx, [arr_counts + eax*4]
        cmp ecx, 0
        je .dpo_fin
        dec ecx
        mov [arr_counts + eax*4], ecx
.dpo_fin:
        pop esi
.dpo_done:
        popad
        ret

do_shift:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '@'
        jne .dsh_done
        inc esi
        mov edi, temp_name
        call read_ident
        mov [parse_ptr], esi
        push esi
        mov esi, temp_name
        call find_array
        cmp eax, -1
        je .dsh_fin
        mov ecx, [arr_counts + eax*4]
        cmp ecx, 0
        je .dsh_fin
        ; Shift: remove first element, move rest down
        dec ecx
        mov [arr_counts + eax*4], ecx
        imul ebx, eax, MAX_ARR_ELEM * 4
        lea edi, [arr_data + ebx]
        lea esi, [arr_data + ebx + 4]
        push ecx
        shl ecx, 2
        cmp ecx, 0
        je .dsh_shift_done
        rep movsb
.dsh_shift_done:
        pop ecx
.dsh_fin:
        pop esi
        mov esi, [parse_ptr]
.dsh_done:
        popad
        ret

do_unshift:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '@'
        jne .dus_done
        inc esi
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        cmp byte [esi], ','
        jne .dus_done
        inc esi
        mov [parse_ptr], esi

        push esi
        mov esi, temp_name
        call find_or_create_array
        mov [temp_arr_idx], eax
        pop esi

        call eval_expr

        ; Shift existing elements up
        mov eax, [temp_arr_idx]
        mov ecx, [arr_counts + eax*4]
        cmp ecx, MAX_ARR_ELEM - 1
        jge .dus_skip
        ; Move elements up by 1
        imul ebx, eax, MAX_ARR_ELEM * 4
        push ecx
.dus_shift_up:
        cmp ecx, 0
        je .dus_shift_done
        mov edx, [arr_data + ebx + (ecx-1)*4]
        mov [arr_data + ebx + ecx*4], edx
        dec ecx
        jmp .dus_shift_up
.dus_shift_done:
        pop ecx
        ; Store new value at position 0
        mov edx, [expr_result]
        mov [arr_data + ebx], edx
        inc dword [arr_counts + eax*4]
.dus_skip:
        mov esi, [parse_ptr]
.dus_done:
        popad
        ret

;=======================================================================
; DIE / EXIT
;=======================================================================
do_die:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], '"'
        jne .dd_default
        ; Print the message
        mov [parse_ptr], esi
        call do_print
        call io_newline
        jmp .dd_exit
.dd_default:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_ERROR
        int 0x80
        mov esi, err_died
        call io_println
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
.dd_exit:
        mov eax, SYS_EXIT
        int 0x80
        popad
        ret

do_exit:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        ; Optional exit code (ignored but parsed)
        cmp byte [esi], ';'
        je .dex_go
        cmp byte [esi], 0
        je .dex_go
        mov [parse_ptr], esi
        call eval_expr
.dex_go:
        mov eax, SYS_EXIT
        int 0x80
        popad
        ret

;=======================================================================
; FILE I/O: open, close
;=======================================================================
do_open:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi

        ; Simple form: open(FH, "filename") or open(FH, "<filename")
        cmp byte [esi], '('
        jne .do_open_done
        inc esi
        call skip_ws_esi

        ; Skip filehandle name (we use a simple single-file model)
        mov edi, temp_name
        call read_ident
        call skip_ws_esi
        cmp byte [esi], ','
        jne .do_open_done
        inc esi
        call skip_ws_esi

        ; Get filename
        mov [parse_ptr], esi
        call eval_expr
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ')'
        jne .do_open_done
        inc esi
        mov [parse_ptr], esi

        ; Try to read the file
        push esi
        mov ebx, expr_result_str
        mov ecx, file_io_buf
        mov eax, SYS_FREAD
        int 0x80
        mov [file_io_size], eax
        mov dword [file_io_pos], 0
        pop esi

.do_open_done:
        popad
        ret

do_close:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        ; Skip to ; or end
.dcl_loop:
        cmp byte [esi], 0
        je .dcl_done
        cmp byte [esi], ';'
        je .dcl_done
        inc esi
        jmp .dcl_loop
.dcl_done:
        cmp byte [esi], ';'
        jne .dcl_nosemi
        inc esi
.dcl_nosemi:
        mov [parse_ptr], esi
        mov dword [file_io_size], 0
        mov dword [file_io_pos], 0
        popad
        ret

;=======================================================================
; RETURN from sub
;=======================================================================
do_return:
        pushad
        mov esi, [parse_ptr]
        call skip_ws_esi
        cmp byte [esi], ';'
        je .dr_no_val
        cmp byte [esi], 0
        je .dr_no_val
        mov [parse_ptr], esi
        call eval_expr
        mov eax, [expr_result]
        mov [return_val], eax
        mov al, [expr_result_type]
        mov [return_type], al
        jmp .dr_done
.dr_no_val:
        mov dword [return_val], 0
        mov byte [return_type], TYPE_UNDEF
.dr_done:
        mov byte [in_sub_return], 1
        popad
        ret

;=======================================================================
; SUB CALL
;=======================================================================
try_sub_call:
        push ebx
        push ecx
        push edx
        push esi
        push edi

        mov esi, [parse_ptr]
        call skip_ws_esi

        ; Read identifier
        mov edi, temp_name
        call read_ident_check
        cmp eax, 0
        je .tsc_no

        ; Look up sub
        push esi
        mov esi, temp_name
        call find_sub
        pop esi
        cmp eax, -1
        je .tsc_no

        ; Found sub - get start line
        mov ebx, eax            ; sub index
        mov ecx, [sub_lines + ebx*4]   ; start line

        ; Skip past arguments (we don't do formal params)
        call skip_ws_esi
        cmp byte [esi], '('
        jne .tsc_no_args
        inc esi
        ; Skip to closing )
.tsc_skip_args:
        cmp byte [esi], ')'
        je .tsc_args_done
        cmp byte [esi], 0
        je .tsc_no_args
        inc esi
        jmp .tsc_skip_args
.tsc_args_done:
        inc esi
.tsc_no_args:
        call skip_ws_esi
        cmp byte [esi], ';'
        jne .tsc_nosemi
        inc esi
.tsc_nosemi:
        mov [parse_ptr], esi

        ; Save current script position
        push dword [current_line]
        inc dword [call_depth]

        ; Jump to sub body (line after 'sub name {')
        inc ecx                 ; skip the sub definition line
        mov [current_line], ecx

        ; Find opening brace and execute block
        ; Re-parse from the sub definition line to find body
        mov eax, [sub_lines + ebx*4]
        imul edx, eax, MAX_LINE_LEN
        lea esi, [script_buf + edx]
        ; Skip to opening {
.tsc_find_brace:
        cmp byte [esi], '{'
        je .tsc_found_brace
        cmp byte [esi], 0
        je .tsc_next_line_brace
        inc esi
        jmp .tsc_find_brace
.tsc_next_line_brace:
        inc eax
        cmp eax, [script_lines]
        jge .tsc_call_done
        imul edx, eax, MAX_LINE_LEN
        lea esi, [script_buf + edx]
        mov [current_line], eax
        jmp .tsc_find_brace

.tsc_found_brace:
        inc esi
        mov [parse_ptr], esi
        call exec_block

        ; Clear return flag
        mov byte [in_sub_return], 0

.tsc_call_done:
        dec dword [call_depth]
        pop dword [current_line]

        mov eax, 1              ; return success
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

.tsc_no:
        mov eax, 0
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;=======================================================================
; VARIABLE STORAGE HELPERS
;=======================================================================

; find_var: ESI = name -> EAX = index or -1
find_var:
        push ebx
        push ecx
        push edi
        xor ecx, ecx
.fv_loop:
        cmp ecx, [num_vars]
        jge .fv_not_found
        imul ebx, ecx, VAR_NAME_LEN
        lea edi, [var_names + ebx]
        push esi
        push ecx
        call str_cmp
        pop ecx
        pop esi
        je .fv_found
        inc ecx
        jmp .fv_loop
.fv_found:
        mov eax, ecx
        pop edi
        pop ecx
        pop ebx
        ret
.fv_not_found:
        mov eax, -1
        pop edi
        pop ecx
        pop ebx
        ret

; lookup_var: ESI = name, sets expr_result/type
lookup_var:
        pushad
        call find_var
        cmp eax, -1
        je .lv_undef
        movzx ebx, byte [var_types + eax]
        cmp ebx, TYPE_STR
        je .lv_str
        ; Integer
        mov ebx, [var_ints + eax*4]
        mov [expr_result], ebx
        mov byte [expr_result_type], TYPE_INT
        ; Also set string representation
        push esi
        mov eax, ebx
        mov edi, expr_result_str
        call math_int_to_str
        pop esi
        popad
        ret
.lv_str:
        ; String value
        imul ebx, eax, VAR_VAL_LEN
        lea esi, [var_strs + ebx]
        mov edi, expr_result_str
        call str_copy
        mov byte [expr_result_type], TYPE_STR
        ; Set int value (attempt to parse)
        push esi
        mov esi, expr_result_str
        call math_parse_signed
        mov [expr_result], eax
        pop esi
        popad
        ret
.lv_undef:
        mov dword [expr_result], 0
        mov byte [expr_result_str], 0
        mov byte [expr_result_type], TYPE_UNDEF
        popad
        ret

; get_var_int: ESI = name -> EAX = int value
get_var_int:
        push ebx
        push ecx
        push edi
        call find_var
        cmp eax, -1
        je .gvi_zero
        mov eax, [var_ints + eax*4]
        pop edi
        pop ecx
        pop ebx
        ret
.gvi_zero:
        xor eax, eax
        pop edi
        pop ecx
        pop ebx
        ret

; get_var_str: ESI = name -> expr_result_str filled
get_var_str:
        pushad
        call find_var
        cmp eax, -1
        je .gvs_empty
        movzx ebx, byte [var_types + eax]
        cmp ebx, TYPE_STR
        je .gvs_str
        ; Int: convert to string
        mov eax, [var_ints + eax*4]
        mov edi, expr_result_str
        call math_int_to_str
        mov byte [expr_result_type], TYPE_INT
        popad
        ret
.gvs_str:
        imul ebx, eax, VAR_VAL_LEN
        lea esi, [var_strs + ebx]
        mov edi, expr_result_str
        call str_copy
        mov byte [expr_result_type], TYPE_STR
        popad
        ret
.gvs_empty:
        mov byte [expr_result_str], 0
        mov byte [expr_result_type], TYPE_UNDEF
        popad
        ret

; store_var_result: ESI = name, stores from expr_result/type
store_var_result:
        pushad
        call find_var
        cmp eax, -1
        jne .svr_found
        ; Create new variable
        mov eax, [num_vars]
        cmp eax, MAX_VARS
        jge .svr_done
        imul ebx, eax, VAR_NAME_LEN
        lea edi, [var_names + ebx]
        call str_copy
        inc dword [num_vars]

.svr_found:
        ; Store type
        mov bl, [expr_result_type]
        mov [var_types + eax], bl

        ; Store int value
        mov ebx, [expr_result]
        mov [var_ints + eax*4], ebx

        ; Store string value
        cmp byte [expr_result_type], TYPE_STR
        jne .svr_int_str
        imul ebx, eax, VAR_VAL_LEN
        push esi
        lea edi, [var_strs + ebx]
        mov esi, expr_result_str
        call str_copy
        pop esi
        jmp .svr_done

.svr_int_str:
        ; Also store string representation of int
        imul ebx, eax, VAR_VAL_LEN
        push esi
        lea edi, [var_strs + ebx]
        mov eax, [expr_result]
        push edi
        call math_int_to_str
        pop edi
        pop esi

.svr_done:
        popad
        ret

;=======================================================================
; SUB STORAGE HELPERS
;=======================================================================
store_sub_def:
        ; temp_name = sub name, ECX = line number
        pushad
        mov eax, [num_subs]
        cmp eax, MAX_SUBS
        jge .ssd_done
        imul ebx, eax, VAR_NAME_LEN
        lea edi, [sub_names + ebx]
        mov esi, temp_name
        call str_copy
        mov [sub_lines + eax*4], ecx
        inc dword [num_subs]
.ssd_done:
        popad
        ret

find_sub:
        ; ESI = name -> EAX = index or -1
        push ebx
        push ecx
        push edi
        xor ecx, ecx
.fsu_loop:
        cmp ecx, [num_subs]
        jge .fsu_nf
        imul ebx, ecx, VAR_NAME_LEN
        lea edi, [sub_names + ebx]
        push esi
        push ecx
        call str_cmp
        pop ecx
        pop esi
        je .fsu_found
        inc ecx
        jmp .fsu_loop
.fsu_found:
        mov eax, ecx
        pop edi
        pop ecx
        pop ebx
        ret
.fsu_nf:
        mov eax, -1
        pop edi
        pop ecx
        pop ebx
        ret

;=======================================================================
; ARRAY STORAGE HELPERS
;=======================================================================
find_array:
        ; ESI = name -> EAX = index or -1
        push ebx
        push ecx
        push edi
        xor ecx, ecx
.fa_loop:
        cmp ecx, [num_arrays]
        jge .fa_nf
        imul ebx, ecx, VAR_NAME_LEN
        lea edi, [arr_names + ebx]
        push esi
        push ecx
        call str_cmp
        pop ecx
        pop esi
        je .fa_found
        inc ecx
        jmp .fa_loop
.fa_found:
        mov eax, ecx
        pop edi
        pop ecx
        pop ebx
        ret
.fa_nf:
        mov eax, -1
        pop edi
        pop ecx
        pop ebx
        ret

find_or_create_array:
        ; ESI = name -> EAX = index
        call find_array
        cmp eax, -1
        jne .foca_done
        ; Create
        mov eax, [num_arrays]
        cmp eax, MAX_ARRAYS
        jge .foca_done
        imul ebx, eax, VAR_NAME_LEN
        lea edi, [arr_names + ebx]
        call str_copy
        mov dword [arr_counts + eax*4], 0
        inc dword [num_arrays]
.foca_done:
        ret

array_push_result:
        ; EAX = array index, pushes expr_result
        push ebx
        push ecx
        mov ecx, [arr_counts + eax*4]
        cmp ecx, MAX_ARR_ELEM
        jge .apr_done
        imul ebx, eax, MAX_ARR_ELEM * 4
        mov edx, [expr_result]
        mov [arr_data + ebx + ecx*4], edx
        inc dword [arr_counts + eax*4]
.apr_done:
        pop ecx
        pop ebx
        ret

get_array_elem:
        ; ESI = array name, EAX = index -> sets expr_result
        pushad
        push eax
        call find_array
        mov ecx, eax
        pop eax
        cmp ecx, -1
        je .gae_undef
        cmp eax, [arr_counts + ecx*4]
        jge .gae_undef
        cmp eax, 0
        jl .gae_undef
        imul ebx, ecx, MAX_ARR_ELEM * 4
        mov edx, [arr_data + ebx + eax*4]
        mov [expr_result], edx
        mov byte [expr_result_type], TYPE_INT
        ; Also set string repr
        mov eax, edx
        mov edi, expr_result_str
        call math_int_to_str
        popad
        ret
.gae_undef:
        mov dword [expr_result], 0
        mov byte [expr_result_type], TYPE_UNDEF
        mov byte [expr_result_str], 0
        popad
        ret

set_array_elem:
        ; ESI = array name, EAX = index, expr_result has value
        pushad
        push eax
        call find_or_create_array
        mov ecx, eax            ; array idx
        pop eax
        cmp eax, 0
        jl .sae_done
        cmp eax, MAX_ARR_ELEM
        jge .sae_done
        ; Extend count if needed
        cmp eax, [arr_counts + ecx*4]
        jl .sae_store
        mov edx, eax
        inc edx
        mov [arr_counts + ecx*4], edx
.sae_store:
        imul ebx, ecx, MAX_ARR_ELEM * 4
        mov edx, [expr_result]
        mov [arr_data + ebx + eax*4], edx
.sae_done:
        popad
        ret

print_array:
        ; Print array elements separated by spaces
        pushad
        mov esi, [parse_ptr]
        inc esi                 ; skip @
        mov edi, temp_name
        call read_ident
        mov [parse_ptr], esi

        push esi
        mov esi, temp_name
        call find_array
        cmp eax, -1
        je .pa_done
        mov ecx, [arr_counts + eax*4]
        xor edx, edx
.pa_loop:
        cmp edx, ecx
        jge .pa_done
        cmp edx, 0
        je .pa_no_space
        push ecx
        push edx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop edx
        pop ecx
.pa_no_space:
        push ecx
        push edx
        mov esi, temp_name
        mov eax, edx
        call get_array_elem
        cmp byte [expr_result_type], TYPE_STR
        jne .pa_int
        mov esi, expr_result_str
        call io_print
        jmp .pa_next
.pa_int:
        mov eax, [expr_result]
        mov edi, num_format_buf
        call math_int_to_str
        mov esi, num_format_buf
        call io_print
.pa_next:
        pop edx
        pop ecx
        inc edx
        jmp .pa_loop
.pa_done:
        pop esi
        popad
        ret

sort_array:
        ; EAX = array index; simple bubble sort on int values
        pushad
        mov ecx, [arr_counts + eax*4]
        cmp ecx, 2
        jl .sa_done
        imul ebx, eax, MAX_ARR_ELEM * 4
.sa_outer:
        mov byte [sort_swapped], 0
        xor edx, edx
.sa_inner:
        mov esi, edx
        inc esi
        cmp esi, ecx
        jge .sa_check
        mov edi, [arr_data + ebx + edx*4]
        mov eax, [arr_data + ebx + esi*4]
        cmp edi, eax
        jle .sa_no_swap
        ; Swap
        mov [arr_data + ebx + edx*4], eax
        mov [arr_data + ebx + esi*4], edi
        mov byte [sort_swapped], 1
.sa_no_swap:
        inc edx
        jmp .sa_inner
.sa_check:
        cmp byte [sort_swapped], 0
        jne .sa_outer
.sa_done:
        popad
        ret

;=======================================================================
; FIND FOR PARTS: Find the increment part and body of for(;;) loop
;=======================================================================
find_for_parts:
        pushad
        mov esi, [parse_ptr]
        ; parse_ptr is at the condition. Find the next ;
.ffp_find_semi:
        cmp byte [esi], 0
        je .ffp_err
        cmp byte [esi], ';'
        je .ffp_found_semi
        cmp byte [esi], '('
        jne .ffp_no_paren
        inc esi
        call skip_parens_esi
        jmp .ffp_find_semi
.ffp_no_paren:
        inc esi
        jmp .ffp_find_semi
.ffp_found_semi:
        inc esi
        mov [for_incr_ptr], esi

        ; Find the closing ) then {
.ffp_find_rparen:
        cmp byte [esi], 0
        je .ffp_err
        cmp byte [esi], ')'
        je .ffp_found_rparen
        inc esi
        jmp .ffp_find_rparen
.ffp_found_rparen:
        inc esi
        call skip_ws_esi
        cmp byte [esi], '{'
        jne .ffp_err
        inc esi
        mov [for_body_ptr], esi

.ffp_err:
        popad
        ret

;=======================================================================
; UTILITY FUNCTIONS
;=======================================================================

; Skip whitespace at [parse_ptr]
skip_whitespace:
        push esi
        mov esi, [parse_ptr]
.sw_loop:
        cmp byte [esi], ' '
        je .sw_next
        cmp byte [esi], 9
        je .sw_next
        jmp .sw_done
.sw_next:
        inc esi
        jmp .sw_loop
.sw_done:
        mov [parse_ptr], esi
        pop esi
        ret

; Skip whitespace at ESI (in place)
skip_ws_esi:
.swe_loop:
        cmp byte [esi], ' '
        je .swe_next
        cmp byte [esi], 9
        je .swe_next
        ret
.swe_next:
        inc esi
        jmp .swe_loop

; Read identifier from ESI into EDI, advance ESI
read_ident:
        push eax
        push ecx
        xor ecx, ecx
.ri_loop:
        movzx eax, byte [esi]
        cmp al, 'a'
        jb .ri_check_upper
        cmp al, 'z'
        jbe .ri_store
.ri_check_upper:
        cmp al, 'A'
        jb .ri_check_digit
        cmp al, 'Z'
        jbe .ri_store
.ri_check_digit:
        cmp al, '0'
        jb .ri_check_underscore
        cmp al, '9'
        jbe .ri_store
.ri_check_underscore:
        cmp al, '_'
        jne .ri_done
.ri_store:
        cmp ecx, VAR_NAME_LEN - 1
        jge .ri_skip
        mov [edi + ecx], al
        inc ecx
.ri_skip:
        inc esi
        jmp .ri_loop
.ri_done:
        mov byte [edi + ecx], 0
        pop ecx
        pop eax
        ret

read_ident_esi:
        ; Same as read_ident, for the find_subs context
        jmp read_ident

; Read identifier and check if valid (at least 1 char)
read_ident_check:
        push ecx
        push edi
        xor ecx, ecx
        movzx eax, byte [esi]
        cmp al, 'a'
        jb .ric_upper
        cmp al, 'z'
        jbe .ric_ok
.ric_upper:
        cmp al, 'A'
        jb .ric_under
        cmp al, 'Z'
        jbe .ric_ok
.ric_under:
        cmp al, '_'
        jne .ric_fail
.ric_ok:
        call read_ident
        mov eax, 1
        pop edi
        pop ecx
        ret
.ric_fail:
        mov eax, 0
        pop edi
        pop ecx
        ret

; Match keyword: ESI=input, EDI=keyword
; Returns CF set if match, advances parse_ptr past keyword
match_keyword:
        push eax
        push ecx
        push esi
        push edi
        mov esi, [parse_ptr]
        call skip_ws_esi
.mk_loop:
        mov al, [edi]
        cmp al, 0
        je .mk_end_kw
        cmp al, [esi]
        jne .mk_fail
        inc esi
        inc edi
        jmp .mk_loop
.mk_end_kw:
        ; Check that next char is not alphanumeric (word boundary)
        movzx eax, byte [esi]
        cmp al, 'a'
        jb .mk_not_alpha
        cmp al, 'z'
        jbe .mk_fail
.mk_not_alpha:
        cmp al, 'A'
        jb .mk_not_upper
        cmp al, 'Z'
        jbe .mk_fail
.mk_not_upper:
        cmp al, '0'
        jb .mk_not_digit
        cmp al, '9'
        jbe .mk_fail
.mk_not_digit:
        cmp al, '_'
        je .mk_fail
        ; Match!
        mov [parse_ptr], esi
        pop edi
        pop esi
        pop ecx
        pop eax
        stc
        ret
.mk_fail:
        pop edi
        pop esi
        pop ecx
        pop eax
        clc
        ret

; Match keyword using ESI directly (not parse_ptr)
match_keyword_esi:
        push eax
        push ecx
        push edi
        push esi             ; save start
.mke_loop:
        mov al, [edi]
        cmp al, 0
        je .mke_end_kw
        cmp al, [esi]
        jne .mke_fail
        inc esi
        inc edi
        jmp .mke_loop
.mke_end_kw:
        movzx eax, byte [esi]
        cmp al, 'a'
        jb .mke_not_alpha
        cmp al, 'z'
        jbe .mke_fail
.mke_not_alpha:
        cmp al, 'A'
        jb .mke_not_upper
        cmp al, 'Z'
        jbe .mke_fail
.mke_not_upper:
        cmp al, '0'
        jb .mke_not_digit
        cmp al, '9'
        jbe .mke_fail
.mke_not_digit:
        cmp al, '_'
        je .mke_fail
        ; Match! ESI is advanced past keyword
        add esp, 4             ; discard saved esi
        pop edi
        pop ecx
        pop eax
        stc
        ret
.mke_fail:
        pop esi                ; restore esi
        pop edi
        pop ecx
        pop eax
        clc
        ret

; Report syntax error
report_syntax_error:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_ERROR
        int 0x80
        mov esi, err_syntax
        call io_print
        ; Print current line number if in script mode
        cmp dword [script_lines], 0
        je .rse_no_line
        mov esi, err_at_line
        call io_print
        mov eax, [current_line]
        inc eax
        mov edi, num_format_buf
        call math_int_to_str
        mov esi, num_format_buf
        call io_print
.rse_no_line:
        call io_newline
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        mov byte [had_error], 1
        popad
        ret

report_error:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_ERROR
        int 0x80
        call io_println
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        mov byte [had_error], 1
        popad
        ret

;=======================================================================
; STRING CONSTANTS
;=======================================================================
msg_banner:     db "Mellivora Perl v5.0 (Tiny)", 0x0A
                db "==========================", 0x0A
                db "Type 'exit' or 'quit' to leave", 0x0A, 0
msg_prompt:     db "perl> ", 0

kw_print:       db "print", 0
kw_say:         db "say", 0
kw_if:          db "if", 0
kw_unless:      db "unless", 0
kw_while:       db "while", 0
kw_until:       db "until", 0
kw_for:         db "for", 0
kw_foreach:     db "foreach", 0
kw_sub:         db "sub", 0
kw_return:      db "return", 0
kw_last:        db "last", 0
kw_next:        db "next", 0
kw_chomp:       db "chomp", 0
kw_push:        db "push", 0
kw_pop_kw:      db "pop", 0
kw_shift:       db "shift", 0
kw_unshift:     db "unshift", 0
kw_die:         db "die", 0
kw_exit:        db "exit", 0
kw_open:        db "open", 0
kw_close:       db "close", 0

kw_length:      db "length", 0
kw_substr:      db "substr", 0
kw_abs:         db "abs", 0
kw_int:         db "int", 0
kw_chr:         db "chr", 0
kw_ord:         db "ord", 0
kw_uc:          db "uc", 0
kw_lc:          db "lc", 0
kw_index:       db "index", 0
kw_rindex:      db "rindex", 0
kw_join:        db "join", 0
kw_split:       db "split", 0
kw_reverse:     db "reverse", 0
kw_sort:        db "sort", 0
kw_defined:     db "defined", 0
kw_scalar:      db "scalar", 0
kw_rand:        db "rand", 0

kw_exit_cmd:    db "exit", 0
kw_quit_cmd:    db "quit", 0

underscore_name: db "_", 0

err_syntax:     db "Syntax error", 0
err_at_line:    db " at line ", 0
err_cant_open:  db "Can't open file: ", 0
err_div_zero:   db "Illegal division by zero", 0
err_died:       db "Died", 0

;=======================================================================
; BSS DATA
;=======================================================================
section .bss
        alignb 4

; Parser state
parse_ptr:      resd 1
current_line:   resd 1
script_lines:   resd 1
had_error:      resb 1
print_newline:  resb 1

; Control flow
in_sub_return:  resb 1
return_val:     resd 1
return_type:    resb 1
loop_depth:     resd 1
loop_break:     resb 1
loop_continue:  resb 1
call_depth:     resd 1
block_depth:    resd 1

; Variables (scalars)
num_vars:       resd 1
var_names:      resb MAX_VARS * VAR_NAME_LEN
var_types:      resb MAX_VARS
var_ints:       resd MAX_VARS
var_strs:       resb MAX_VARS * VAR_VAL_LEN

; Subroutines
num_subs:       resd 1
sub_names:      resb MAX_SUBS * VAR_NAME_LEN
sub_lines:      resd MAX_SUBS

; Arrays
num_arrays:     resd 1
arr_names:      resb MAX_ARRAYS * VAR_NAME_LEN
arr_counts:     resd MAX_ARRAYS
arr_data:       resd MAX_ARRAYS * MAX_ARR_ELEM

; Expression evaluator
expr_result:    resd 1
expr_result_type: resb 1
expr_result_str: resb VAR_VAL_LEN

; Temp buffers
temp_name:      resb VAR_NAME_LEN
temp_arr_idx:   resd 1
concat_buf:     resb 1024
cmp_buf:        resb VAR_VAL_LEN
num_format_buf: resb 32
join_sep:       resb 64
split_sep:      resb 64

; While/for state
while_cond_ptr: resd 1
while_body_ptr: resd 1
for_cond_ptr:   resd 1
for_incr_ptr:   resd 1
for_body_ptr:   resd 1

; Foreach state
foreach_var:    resb VAR_NAME_LEN
foreach_count:  resd 1
foreach_idx:    resd 1
foreach_data:   resd MAX_ARR_ELEM
foreach_types:  resb MAX_ARR_ELEM
foreach_body_ptr: resd 1

; Sort
sort_swapped:   resb 1

; Random
rand_seed:      resd 1

; Input/args
input_buf:      resb INPUT_BUF_LEN + 1
args_buf:       resb 256

; File I/O
file_buffer:    resb FILE_BUF_SIZE
file_size:      resd 1
file_io_buf:    resb FILE_BUF_SIZE
file_io_size:   resd 1
file_io_pos:    resd 1

; Script storage (lines)
script_buf:     resb MAX_LINES * MAX_LINE_LEN
