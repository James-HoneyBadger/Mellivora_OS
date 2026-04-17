; basic.asm - Tiny BASIC Interpreter for Mellivora OS
; Supports: PRINT, INPUT, LET, IF/THEN, GOTO, GOSUB/RETURN,
;           FOR/TO/STEP/NEXT, END, REM, LIST, RUN, NEW, LOAD, SAVE
;           POKE, SLEEP, DIM, PEEK(), ABS(), TIME(), array access A(N)
; Variables: A-Z (26 integer variables)
; Arrays: DIM A(size) — up to 4 arrays, 100 elements each
; Expressions: +, -, *, /, %, (, ), comparison (=, <, >, <=, >=, <>)
; String literals in PRINT with "..."
; Line numbers 1-9999
%include "syscalls.inc"

; Constants
MAX_LINES       equ 200
MAX_LINE_LEN    equ 80
PROG_SIZE       equ MAX_LINES * (4 + MAX_LINE_LEN) ; linenum(4) + text
GOSUB_DEPTH     equ 16
FOR_DEPTH       equ 8
INPUT_BUF_LEN   equ 80
MAX_ARRAYS      equ 4
ARRAY_MAX_SIZE  equ 100

start:
        ; Print welcome
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_banner
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Check for args (filename to LOAD)
        mov eax, SYS_GETARGS
        mov ebx, input_buf
        int 0x80
        cmp eax, 0
        je .repl
        ; Auto-load file
        call cmd_load_from_args

.repl:
        ; REPL: read-eval-print loop
        mov eax, SYS_PRINT
        mov ebx, msg_ready
        int 0x80

.prompt:
        mov eax, SYS_PRINT
        mov ebx, msg_prompt
        int 0x80

        ; Read a line
        call read_line
        cmp byte [input_buf], 0
        je .prompt

        ; Check if starts with a digit (line number -> store)
        movzx eax, byte [input_buf]
        cmp al, '0'
        jl .immediate
        cmp al, '9'
        jg .immediate

        ; Has line number - store in program
        mov esi, input_buf
        call parse_linenum          ; EAX = line number
        call store_line             ; Store rest of line at ESI
        jmp .prompt

.immediate:
        ; Immediate mode - execute directly
        mov esi, input_buf
        call exec_statement
        cmp byte [run_error], 0
        je .prompt
        ; Print error
        call print_error
        mov byte [run_error], 0
        jmp .prompt

;=======================================================================
; READ LINE from keyboard into input_buf
;=======================================================================
read_line:
        PUSHALL
        mov edi, input_buf
        xor ecx, ecx

.rl_loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 0x0D
        je .rl_done
        cmp al, 0x0A
        je .rl_done
        cmp al, 27             ; ESC
        je .rl_cancel
        cmp al, 0x08
        je .rl_bs
        cmp al, 0x7F
        je .rl_bs

        cmp ecx, INPUT_BUF_LEN - 1
        jge .rl_loop

        mov [edi + ecx], al
        inc ecx

        ; Echo
        push rcx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        jmp .rl_loop

.rl_bs:
        cmp ecx, 0
        je .rl_loop
        dec ecx
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        pop rcx
        jmp .rl_loop

.rl_cancel:
        xor ecx, ecx

.rl_done:
        mov byte [edi + ecx], 0
        ; Newline
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rcx
        POPALL
        ret

;=======================================================================
; PARSE LINE NUMBER from ESI, return in EAX, advance ESI past digits+spaces
;=======================================================================
parse_linenum:
        xor eax, eax
        xor edx, edx
.pln_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jl .pln_done
        cmp dl, '9'
        jg .pln_done
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pln_loop
.pln_done:
        ; Skip spaces
        cmp byte [esi], ' '
        jne .pln_ret
        inc esi
        jmp .pln_done
.pln_ret:
        ret

;=======================================================================
; STORE LINE: EAX=line number, ESI=rest of text
; If text is empty, delete the line
;=======================================================================
store_line:
        PUSHALL
        mov edx, eax            ; EDX = line number

        ; Check if text is empty (delete line)
        cmp byte [esi], 0
        je .delete_line

        ; Find insertion point (keep sorted by line number)
        mov edi, program_area
        mov ecx, [line_count]
        xor ebx, ebx           ; index

.sl_find:
        cmp ebx, ecx
        jge .sl_insert_here
        cmp edx, [edi]
        je .sl_replace          ; Same line number - replace
        jl .sl_insert_here      ; Insert before this
        add edi, 4 + MAX_LINE_LEN
        inc ebx
        jmp .sl_find

.sl_replace:
        ; Overwrite existing line text
        add edi, 4
        push rcx
        mov ecx, MAX_LINE_LEN - 1
        call copy_str_n
        pop rcx
        POPALL
        ret

.sl_insert_here:
        ; Shift lines down to make room
        push rsi
        push rdx
        mov eax, [line_count]
        cmp eax, MAX_LINES
        jge .sl_full
        ; Shift from end down to ebx
        mov ecx, [line_count]
        sub ecx, ebx           ; Number of entries to move
        jz .sl_no_shift
        ; Source: last entry
        mov esi, program_area
        mov eax, [line_count]
        dec eax
        imul eax, 4 + MAX_LINE_LEN
        add esi, eax
        ; Dest: one past
        lea eax, [esi + 4 + MAX_LINE_LEN]
        mov edi, eax
.sl_shift:
        push rcx
        mov ecx, 4 + MAX_LINE_LEN
        push rsi
        push rdi
        std
        add esi, ecx
        dec esi
        add edi, ecx
        dec edi
        rep movsb
        cld
        pop rdi
        pop rsi
        sub esi, 4 + MAX_LINE_LEN
        sub edi, 4 + MAX_LINE_LEN
        pop rcx
        dec ecx
        jnz .sl_shift

.sl_no_shift:
        ; Write new entry at ebx position
        mov edi, program_area
        imul ebx, 4 + MAX_LINE_LEN
        add edi, ebx
        pop rdx
        mov [edi], edx          ; Line number
        add edi, 4
        pop rsi
        mov ecx, MAX_LINE_LEN - 1
        call copy_str_n
        inc dword [line_count]
        POPALL
        ret

.sl_full:
        pop rdx
        pop rsi
        mov byte [run_error], ERR_MEM
        POPALL
        ret

.delete_line:
        ; Find and remove line with number EDX
        mov edi, program_area
        mov ecx, [line_count]
        xor ebx, ebx
.dl_find:
        cmp ebx, ecx
        jge .dl_notfound
        cmp edx, [edi]
        je .dl_found
        add edi, 4 + MAX_LINE_LEN
        inc ebx
        jmp .dl_find
.dl_found:
        ; Shift remaining lines up
        mov eax, [line_count]
        dec eax
        mov [line_count], eax
        sub eax, ebx            ; entries to move
        jz .dl_done
        mov esi, edi
        add esi, 4 + MAX_LINE_LEN
        mov ecx, eax
        imul ecx, 4 + MAX_LINE_LEN
        push rdi
        cld
        rep movsb
        pop rdi
.dl_done:
.dl_notfound:
        POPALL
        ret

;=======================================================================
; COPY_STR_N: copy string ESI->EDI, max ECX chars, null-terminate
;=======================================================================
copy_str_n:
        push rax
.csn:
        lodsb
        cmp al, 0
        je .csn_pad
        stosb
        dec ecx
        jnz .csn
        jmp .csn_end
.csn_pad:
        mov byte [edi], 0
.csn_end:
        pop rax
        ret

;=======================================================================
; EXECUTE STATEMENT at ESI
;=======================================================================
exec_statement:
        PUSHALL
        call skip_spc

        ; Check for immediate commands first
        call try_cmd_run
        jc .es_done
        call try_cmd_list
        jc .es_done
        call try_cmd_new
        jc .es_done
        call try_cmd_load
        jc .es_done
        call try_cmd_save
        jc .es_done
        call try_cmd_help
        jc .es_done
        call try_cmd_system
        jc .es_done

        ; BASIC statements
        call try_print
        jc .es_done
        call try_input
        jc .es_done
        call try_let
        jc .es_done
        call try_if
        jc .es_done
        call try_goto
        jc .es_done
        call try_gosub
        jc .es_done
        call try_return
        jc .es_done
        call try_for
        jc .es_done
        call try_next
        jc .es_done
        call try_end
        jc .es_done
        call try_rem
        jc .es_done
        call try_cls
        jc .es_done
        call try_color
        jc .es_done
        call try_beep_stmt
        jc .es_done
        call try_poke
        jc .es_done
        call try_sleep
        jc .es_done
        call try_dim
        jc .es_done

        ; Try implicit LET (A=5)
        call try_implicit_let
        jc .es_done

        ; Unknown statement
        mov byte [run_error], ERR_SYNTAX

.es_done:
        POPALL
        ret

;=======================================================================
; SKIP SPACES
;=======================================================================
skip_spc:
        cmp byte [esi], ' '
        jne .ss_ret
        inc esi
        jmp skip_spc
.ss_ret:
        ret

;=======================================================================
; KEYWORD MATCH: ESI=input, EDI=keyword (uppercase)
; CF=1 if match (ESI advanced past keyword+spaces), CF=0 if no match
;=======================================================================
match_kw:
        push rax
        push rdi
        push rsi
.mk_loop:
        mov al, [edi]
        cmp al, 0
        je .mk_match
        mov ah, [esi]
        ; Uppercase compare
        cmp ah, 'a'
        jl .mk_cmp
        cmp ah, 'z'
        jg .mk_cmp
        sub ah, 32
.mk_cmp:
        cmp ah, al
        jne .mk_fail
        inc esi
        inc edi
        jmp .mk_loop
.mk_match:
        ; Matched - update ESI on stack
        add rsp, 4             ; discard saved ESI
        pop rdi
        pop rax
        call skip_spc
        stc
        ret
.mk_fail:
        pop rsi
        pop rdi
        pop rax
        clc
        ret

;=======================================================================
; PRINT statement: PRINT expr [";" expr ...] | "string" [;]
;=======================================================================
try_print:
        push rsi
        mov edi, kw_print
        call match_kw
        jnc .tp_fail

.tp_item:
        call skip_spc
        cmp byte [esi], 0
        je .tp_newline
        cmp byte [esi], ':'
        je .tp_newline

        ; String literal?
        cmp byte [esi], '"'
        je .tp_string

        ; Expression
        call eval_expr
        cmp byte [run_error], 0
        jne .tp_ok
        call print_signed
        jmp .tp_sep

.tp_string:
        inc esi                 ; Skip opening "
.tp_str_loop:
        mov al, [esi]
        cmp al, 0
        je .tp_sep
        cmp al, '"'
        je .tp_str_end
        push rax
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rax
        inc esi
        jmp .tp_str_loop
.tp_str_end:
        inc esi                 ; Skip closing "

.tp_sep:
        call skip_spc
        cmp byte [esi], ';'
        je .tp_semi
        cmp byte [esi], ','
        je .tp_comma
        ; Fall through to newline

.tp_newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
.tp_ok:
        add rsp, 4             ; discard saved ESI
        stc
        ret

.tp_semi:
        inc esi
        jmp .tp_item

.tp_comma:
        inc esi
        ; Tab to next column (8-space tab)
        mov eax, SYS_PUTCHAR
        mov ebx, 0x09
        int 0x80
        jmp .tp_item

.tp_fail:
        pop rsi
        clc
        ret

;=======================================================================
; INPUT statement: INPUT ["prompt";] var
;=======================================================================
try_input:
        push rsi
        mov edi, kw_input
        call match_kw
        jnc .ti_fail

        ; Optional prompt string
        cmp byte [esi], '"'
        jne .ti_no_prompt
        inc esi
.ti_ploop:
        mov al, [esi]
        cmp al, '"'
        je .ti_pend
        cmp al, 0
        je .ti_pend
        movzx ebx, al
        push rax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rax
        inc esi
        jmp .ti_ploop
.ti_pend:
        cmp byte [esi], '"'
        jne .ti_get_var
        inc esi
        call skip_spc
        cmp byte [esi], ';'
        jne .ti_get_var
        inc esi
        call skip_spc
        jmp .ti_get_var

.ti_no_prompt:
        mov eax, SYS_PRINT
        mov ebx, msg_input_prompt
        int 0x80

.ti_get_var:
        ; Get variable name
        mov al, [esi]
        call to_upper
        cmp al, 'A'
        jl .ti_err
        cmp al, 'Z'
        jg .ti_err
        sub al, 'A'
        movzx ebx, al
        shl ebx, 2
        push rbx               ; Save var offset

        ; Read number from user
        call read_input_number

        pop rbx
        mov [variables + ebx], eax

        add rsp, 4
        stc
        ret

.ti_err:
        mov byte [run_error], ERR_SYNTAX
        add rsp, 4
        stc
        ret

.ti_fail:
        pop rsi
        clc
        ret

;=======================================================================
; LET statement: LET var = expr  (or implicit: var = expr)
;=======================================================================
try_let:
        push rsi
        mov edi, kw_let
        call match_kw
        jnc .tl_fail
        call do_assignment
        add rsp, 4
        stc
        ret
.tl_fail:
        pop rsi
        clc
        ret

try_implicit_let:
        push rsi
        mov al, [esi]
        call to_upper
        cmp al, 'A'
        jl .til_fail
        cmp al, 'Z'
        jg .til_fail
        ; Check next char is = or space then = or ( for array
        push rax
        mov al, [esi + 1]
        cmp al, '='
        je .til_ok
        cmp al, '('
        je .til_ok              ; array assignment A(n)=expr
        cmp al, ' '
        jne .til_fail2
        mov al, [esi + 2]
        cmp al, '='
        jne .til_fail2
.til_ok:
        pop rax
        call do_assignment
        add rsp, 4
        stc
        ret
.til_fail2:
        pop rax
.til_fail:
        pop rsi
        clc
        ret

do_assignment:
        mov al, [esi]
        call to_upper
        sub al, 'A'
        movzx ebx, al
        inc esi

        ; Check for array assignment: A(n) = expr
        cmp byte [esi], '('
        je .da_array

        shl ebx, 2
        push rbx
        call skip_spc
        cmp byte [esi], '='
        jne .da_err
        inc esi
        call skip_spc
        call eval_expr
        pop rbx
        mov [variables + ebx], eax
        ret

.da_array:
        ; Array assignment
        inc esi                 ; skip (
        push rbx               ; var index
        call eval_expr          ; index
        cmp byte [esi], ')'
        jne .da_arr_err
        inc esi
        call skip_spc
        cmp byte [esi], '='
        jne .da_arr_err
        inc esi
        call skip_spc
        push rax               ; array index
        call eval_expr          ; value
        mov ecx, eax            ; value to store
        pop rax                 ; array index
        pop rbx                 ; var index
        ; Find array
        push rsi
        mov esi, array_table
        mov edx, MAX_ARRAYS
.da_arr_find:
        cmp [esi], ebx
        je .da_arr_store
        add esi, 4 + ARRAY_MAX_SIZE * 4
        dec edx
        jnz .da_arr_find
        pop rsi
        mov byte [run_error], ERR_SYNTAX
        ret
.da_arr_store:
        cmp eax, 0
        jl .da_arr_bad
        cmp eax, ARRAY_MAX_SIZE
        jge .da_arr_bad
        mov [esi + 4 + eax * 4], ecx
        pop rsi
        ret
.da_arr_bad:
        pop rsi
        mov byte [run_error], ERR_SYNTAX
        ret
.da_arr_err:
        pop rbx
        mov byte [run_error], ERR_SYNTAX
        ret
.da_err:
        pop rbx
        mov byte [run_error], ERR_SYNTAX
        ret

;=======================================================================
; IF statement: IF expr rel expr THEN statement
;=======================================================================
try_if:
        push rsi
        mov edi, kw_if
        call match_kw
        jnc .tif_fail

        ; Evaluate left side
        call eval_expr
        push rax

        ; Get comparison operator
        call skip_spc
        xor edx, edx           ; comparison type
        mov al, [esi]
        cmp al, '='
        je .tif_eq
        cmp al, '<'
        je .tif_lt_start
        cmp al, '>'
        je .tif_gt_start
        jmp .tif_err

.tif_eq:
        mov dl, 1               ; =
        inc esi
        jmp .tif_rhs
.tif_lt_start:
        inc esi
        cmp byte [esi], '='
        je .tif_le
        cmp byte [esi], '>'
        je .tif_ne
        mov dl, 2               ; <
        jmp .tif_rhs
.tif_le:
        mov dl, 4               ; <=
        inc esi
        jmp .tif_rhs
.tif_ne:
        mov dl, 5               ; <>
        inc esi
        jmp .tif_rhs
.tif_gt_start:
        inc esi
        cmp byte [esi], '='
        je .tif_ge
        mov dl, 3               ; >
        jmp .tif_rhs
.tif_ge:
        mov dl, 6               ; >=
        inc esi

.tif_rhs:
        call skip_spc
        call eval_expr
        mov ecx, eax            ; RHS
        pop rbx                  ; LHS

        ; Compare
        cmp dl, 1
        je .tif_check_eq
        cmp dl, 2
        je .tif_check_lt
        cmp dl, 3
        je .tif_check_gt
        cmp dl, 4
        je .tif_check_le
        cmp dl, 5
        je .tif_check_ne
        cmp dl, 6
        je .tif_check_ge
        jmp .tif_err

.tif_check_eq:
        cmp ebx, ecx
        je .tif_true
        jmp .tif_false
.tif_check_lt:
        cmp ebx, ecx
        jl .tif_true
        jmp .tif_false
.tif_check_gt:
        cmp ebx, ecx
        jg .tif_true
        jmp .tif_false
.tif_check_le:
        cmp ebx, ecx
        jle .tif_true
        jmp .tif_false
.tif_check_ne:
        cmp ebx, ecx
        jne .tif_true
        jmp .tif_false
.tif_check_ge:
        cmp ebx, ecx
        jge .tif_true
        jmp .tif_false

.tif_true:
        ; Skip past THEN
        call skip_spc
        mov edi, kw_then
        call match_kw
        jnc .tif_no_then
        ; Execute statement after THEN
        call exec_statement
.tif_no_then:
        add rsp, 4
        stc
        ret

.tif_false:
        ; Condition false - skip rest of line
        add rsp, 4
        stc
        ret

.tif_err:
        add rsp, 4              ; clean up pushed LHS
        mov byte [run_error], ERR_SYNTAX
        add rsp, 4
        stc
        ret

.tif_fail:
        pop rsi
        clc
        ret

;=======================================================================
; GOTO statement
;=======================================================================
try_goto:
        push rsi
        mov edi, kw_goto
        call match_kw
        jnc .tg_fail
        call eval_expr
        mov [goto_target], eax
        mov byte [goto_flag], 1
        add rsp, 4
        stc
        ret
.tg_fail:
        pop rsi
        clc
        ret

;=======================================================================
; GOSUB / RETURN
;=======================================================================
try_gosub:
        push rsi
        mov edi, kw_gosub
        call match_kw
        jnc .tgs_fail
        ; Push current line index on return stack
        mov eax, [gosub_sp]
        cmp eax, GOSUB_DEPTH
        jge .tgs_overflow
        mov ebx, [current_line_idx]
        mov [gosub_stack + eax * 4], ebx
        inc dword [gosub_sp]
        ; GOTO target
        call eval_expr
        mov [goto_target], eax
        mov byte [goto_flag], 1
        add rsp, 4
        stc
        ret
.tgs_overflow:
        mov byte [run_error], ERR_GOSUB
        add rsp, 4
        stc
        ret
.tgs_fail:
        pop rsi
        clc
        ret

try_return:
        push rsi
        mov edi, kw_return
        call match_kw
        jnc .tr_fail
        mov eax, [gosub_sp]
        cmp eax, 0
        je .tr_underflow
        dec eax
        mov [gosub_sp], eax
        mov ebx, [gosub_stack + eax * 4]
        inc ebx                 ; Return to NEXT line after GOSUB
        mov [return_line_idx], ebx
        mov byte [return_flag], 1
        add rsp, 4
        stc
        ret
.tr_underflow:
        mov byte [run_error], ERR_RETURN
        add rsp, 4
        stc
        ret
.tr_fail:
        pop rsi
        clc
        ret

;=======================================================================
; FOR / NEXT
;=======================================================================
try_for:
        push rsi
        mov edi, kw_for
        call match_kw
        jnc .tf_fail

        ; Get variable
        mov al, [esi]
        call to_upper
        cmp al, 'A'
        jl .tf_err
        cmp al, 'Z'
        jg .tf_err
        sub al, 'A'
        movzx edx, al          ; var index
        inc esi
        call skip_spc

        ; Expect =
        cmp byte [esi], '='
        jne .tf_err
        inc esi
        call skip_spc

        ; Start value
        call eval_expr
        push rax                ; start val
        push rdx                ; var index

        ; Expect TO
        call skip_spc
        mov edi, kw_to
        call match_kw
        jnc .tf_err2

        ; End value
        call eval_expr
        mov ecx, eax            ; end value

        ; Check for STEP
        push rcx
        call skip_spc
        push rsi
        mov edi, kw_step
        call match_kw
        jnc .tf_no_step
        call eval_expr
        mov ebp, eax            ; step value
        add rsp, 4              ; pop saved esi
        pop rcx
        jmp .tf_have_step
.tf_no_step:
        pop rsi
        pop rcx
        mov ebp, 1              ; default step = 1
.tf_have_step:

        pop rdx                 ; var index
        pop rax                 ; start value

        ; Set variable to start
        mov [variables + edx * 4], eax

        ; Push FOR frame (16 bytes: var, end, line, step)
        mov ebx, [for_sp]
        cmp ebx, FOR_DEPTH
        jge .tf_err
        shl ebx, 4              ; * 16 per frame
        mov [for_stack + ebx], edx          ; var index
        mov [for_stack + ebx + 4], ecx      ; end value
        mov eax, [current_line_idx]
        mov [for_stack + ebx + 8], eax      ; line index of FOR
        mov [for_stack + ebx + 12], ebp     ; step value
        inc dword [for_sp]

        add rsp, 4
        stc
        ret

.tf_err2:
        add rsp, 8
.tf_err:
        mov byte [run_error], ERR_SYNTAX
        add rsp, 4
        stc
        ret

.tf_fail:
        pop rsi
        clc
        ret

try_next:
        push rsi
        mov edi, kw_next
        call match_kw
        jnc .tn_fail

        ; Get variable (optional)
        mov al, [esi]
        call to_upper
        cmp al, 'A'
        jl .tn_use_top
        cmp al, 'Z'
        jg .tn_use_top
        sub al, 'A'
        movzx edx, al
        jmp .tn_check

.tn_use_top:
        mov eax, [for_sp]
        cmp eax, 0
        je .tn_err
        dec eax
        shl eax, 4
        mov edx, [for_stack + eax]      ; var index from top frame

.tn_check:
        ; Find FOR frame for this variable
        mov eax, [for_sp]
        cmp eax, 0
        je .tn_err

.tn_search:
        dec eax
        mov ebx, eax
        shl ebx, 4
        cmp edx, [for_stack + ebx]
        je .tn_found
        cmp eax, 0
        jg .tn_search
        jmp .tn_err

.tn_found:
        ; Increment variable by STEP
        mov ecx, [for_stack + ebx + 12]    ; step value
        add [variables + edx * 4], ecx
        mov ecx, [variables + edx * 4]
        mov edi, [for_stack + ebx + 4]     ; end value
        mov ebp, [for_stack + ebx + 12]    ; step

        ; For positive step: done if var > end
        ; For negative step: done if var < end
        cmp ebp, 0
        jl .tn_neg_step
        cmp ecx, edi
        jg .tn_done_loop
        jmp .tn_continue
.tn_neg_step:
        cmp ecx, edi
        jl .tn_done_loop
.tn_continue:
        ; Loop back: set return to FOR line + 1
        mov eax, [for_sp]
        dec eax
        shl eax, 4
        mov ebx, [for_stack + eax + 8]     ; FOR line index
        inc ebx
        mov [return_line_idx], ebx
        mov byte [return_flag], 1
        add rsp, 4
        stc
        ret

.tn_done_loop:
        ; Pop FOR frame
        dec dword [for_sp]
        add rsp, 4
        stc
        ret

.tn_err:
        mov byte [run_error], ERR_FOR
        add rsp, 4
        stc
        ret

.tn_fail:
        pop rsi
        clc
        ret

;=======================================================================
; END / REM / CLS / COLOR / BEEP
;=======================================================================
try_end:
        push rsi
        mov edi, kw_end
        call match_kw
        jnc .te_fail
        mov byte [end_flag], 1
        add rsp, 4
        stc
        ret
.te_fail:
        pop rsi
        clc
        ret

try_rem:
        push rsi
        mov edi, kw_rem
        call match_kw
        jnc .trem_fail
        ; Skip rest of line (it's a comment)
        add rsp, 4
        stc
        ret
.trem_fail:
        pop rsi
        clc
        ret

try_cls:
        push rsi
        mov edi, kw_cls
        call match_kw
        jnc .tcls_fail
        mov eax, SYS_CLEAR
        int 0x80
        add rsp, 4
        stc
        ret
.tcls_fail:
        pop rsi
        clc
        ret

try_color:
        push rsi
        mov edi, kw_color
        call match_kw
        jnc .tclr_fail
        call eval_expr
        mov ebx, eax
        mov eax, SYS_SETCOLOR
        int 0x80
        add rsp, 4
        stc
        ret
.tclr_fail:
        pop rsi
        clc
        ret

try_beep_stmt:
        push rsi
        mov edi, kw_beep
        call match_kw
        jnc .tb_fail
        mov eax, SYS_BEEP
        mov ebx, 1000
        mov ecx, 15
        int 0x80
        add rsp, 4
        stc
        ret
.tb_fail:
        pop rsi
        clc
        ret

;---------------------------------------
; POKE address, value
;---------------------------------------
try_poke:
        push rsi
        mov edi, kw_poke
        call match_kw
        jnc .tpk_fail
        call eval_expr
        mov edx, eax            ; address
        call skip_spc
        cmp byte [esi], ','
        jne .tpk_err
        inc esi
        call skip_spc
        call eval_expr
        ; Write byte
        mov [edx], al
        add rsp, 4
        stc
        ret
.tpk_err:
        mov byte [run_error], ERR_SYNTAX
        add rsp, 4
        stc
        ret
.tpk_fail:
        pop rsi
        clc
        ret

;---------------------------------------
; SLEEP expr (in centiseconds / ticks)
;---------------------------------------
try_sleep:
        push rsi
        mov edi, kw_sleep
        call match_kw
        jnc .tsl_fail
        call eval_expr
        mov ebx, eax
        mov eax, SYS_SLEEP
        int 0x80
        add rsp, 4
        stc
        ret
.tsl_fail:
        pop rsi
        clc
        ret

;---------------------------------------
; DIM var(size) - Allocate an array
;---------------------------------------
try_dim:
        push rsi
        mov edi, kw_dim
        call match_kw
        jnc .tdm_fail

        ; Get variable letter
        mov al, [esi]
        call to_upper
        cmp al, 'A'
        jl .tdm_err
        cmp al, 'Z'
        jg .tdm_err
        sub al, 'A'
        movzx edx, al
        inc esi

        ; Expect (size)
        cmp byte [esi], '('
        jne .tdm_err
        inc esi
        call eval_expr
        cmp byte [esi], ')'
        jne .tdm_err
        inc esi

        ; Clamp size to ARRAY_MAX_SIZE
        cmp eax, ARRAY_MAX_SIZE
        jle .tdm_size_ok
        mov eax, ARRAY_MAX_SIZE
.tdm_size_ok:
        cmp eax, 1
        jl .tdm_err

        ; Find free array slot or existing for this var
        push rax
        push rdx
        mov edi, array_table
        mov ecx, MAX_ARRAYS
.tdm_find:
        cmp dword [edi], edx
        je .tdm_reinit
        cmp dword [edi], -1
        je .tdm_alloc
        add edi, 4 + ARRAY_MAX_SIZE * 4
        dec ecx
        jnz .tdm_find
        ; No free slot
        pop rdx
        pop rax
        mov byte [run_error], ERR_MEM
        add rsp, 4
        stc
        ret

.tdm_alloc:
        ; Allocate: set var id
        pop rdx
        mov [edi], edx
        jmp .tdm_zero

.tdm_reinit:
        pop rdx                 ; discard

.tdm_zero:
        pop rax                 ; size
        ; Zero the array data
        push rax
        push rdi
        add edi, 4
        mov ecx, ARRAY_MAX_SIZE
        push rax
        xor eax, eax
        rep stosd
        pop rax
        pop rdi
        ; Store size at last position (used for bounds checking)
        ; We store it after the data: [edi + 4 + ARRAY_MAX_SIZE * 4 - 4]
        ; Actually store at a known offset — just use the variable's dim size
        ; We'll keep it simple: arrays are always ARRAY_MAX_SIZE elements
        pop rax
        add rsp, 4
        stc
        ret

.tdm_err:
        mov byte [run_error], ERR_SYNTAX
        add rsp, 4
        stc
        ret
.tdm_fail:
        pop rsi
        clc
        ret

;=======================================================================
; IMMEDIATE COMMANDS: RUN, LIST, NEW, LOAD, SAVE
;=======================================================================
try_cmd_run:
        push rsi
        mov edi, kw_run
        call match_kw
        jnc .tcr_fail
        call run_program
        add rsp, 4
        stc
        ret
.tcr_fail:
        pop rsi
        clc
        ret

try_cmd_list:
        push rsi
        mov edi, kw_list
        call match_kw
        jnc .tcl_fail
        call list_program
        add rsp, 4
        stc
        ret
.tcl_fail:
        pop rsi
        clc
        ret

try_cmd_new:
        push rsi
        mov edi, kw_new
        call match_kw
        jnc .tcn_fail
        mov dword [line_count], 0
        add rsp, 4
        stc
        ret
.tcn_fail:
        pop rsi
        clc
        ret

try_cmd_load:
        push rsi
        mov edi, kw_load
        call match_kw
        jnc .tclod_fail
        call cmd_load
        add rsp, 4
        stc
        ret
.tclod_fail:
        pop rsi
        clc
        ret

try_cmd_save:
        push rsi
        mov edi, kw_save
        call match_kw
        jnc .tcsv_fail
        call cmd_save
        add rsp, 4
        stc
        ret
.tcsv_fail:
        pop rsi
        clc
        ret

try_cmd_help:
        push rsi
        mov edi, kw_help
        call match_kw
        jnc .tch_fail
        call cmd_help
        add rsp, 4
        stc
        ret
.tch_fail:
        pop rsi
        clc
        ret

try_cmd_system:
        push rsi
        mov edi, kw_system
        call match_kw
        jnc .tcsys_fail
        ; Exit BASIC, return to shell
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
        ; Does not return
.tcsys_fail:
        pop rsi
        clc
        ret

;---------------------------------------
; cmd_help - Print available commands
;---------------------------------------
cmd_help:
        PUSHALL
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_help
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        POPALL
        ret

;=======================================================================
; RUN PROGRAM
;=======================================================================
run_program:
        PUSHALL
        mov dword [current_line_idx], 0
        mov dword [gosub_sp], 0
        mov dword [for_sp], 0
        mov byte [end_flag], 0
        mov byte [run_error], 0
        mov byte [goto_flag], 0
        mov byte [return_flag], 0

.run_loop:
        mov eax, [current_line_idx]
        cmp eax, [line_count]
        jge .run_end

        cmp byte [end_flag], 0
        jne .run_end

        ; Get line pointer
        imul eax, 4 + MAX_LINE_LEN
        lea esi, [program_area + eax + 4]

        call exec_statement

        ; Check for errors
        cmp byte [run_error], 0
        jne .run_err

        ; Check GOTO
        cmp byte [goto_flag], 0
        jne .run_goto

        ; Check RETURN
        cmp byte [return_flag], 0
        jne .run_return

        ; Next line
        inc dword [current_line_idx]
        jmp .run_loop

.run_goto:
        mov byte [goto_flag], 0
        ; Find line with goto_target
        mov edx, [goto_target]
        mov edi, program_area
        mov ecx, [line_count]
        xor ebx, ebx
.rg_find:
        cmp ebx, ecx
        jge .rg_notfound
        cmp edx, [edi]
        je .rg_found
        add edi, 4 + MAX_LINE_LEN
        inc ebx
        jmp .rg_find
.rg_found:
        mov [current_line_idx], ebx
        jmp .run_loop
.rg_notfound:
        mov byte [run_error], ERR_LINE
        jmp .run_err

.run_return:
        mov byte [return_flag], 0
        mov eax, [return_line_idx]
        mov [current_line_idx], eax
        jmp .run_loop

.run_err:
        ; Print error with line number
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80

        call print_error

        ; Show line number
        mov eax, [current_line_idx]
        cmp eax, [line_count]
        jge .re_noline
        imul eax, 4 + MAX_LINE_LEN
        mov eax, [program_area + eax]
        push rax
        mov eax, SYS_PRINT
        mov ebx, msg_at_line
        int 0x80
        pop rax
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
.re_noline:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

.run_end:
        mov byte [run_error], 0
        POPALL
        ret

;=======================================================================
; LIST PROGRAM
;=======================================================================
list_program:
        PUSHALL
        mov ecx, [line_count]
        cmp ecx, 0
        je .lp_empty
        mov edi, program_area
        xor ebx, ebx

.lp_loop:
        ; Print line number
        mov eax, [edi]
        call print_dec

        ; Print space
        push rbx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rbx

        ; Print line text
        push rbx
        lea ebx, [edi + 4]
        mov eax, SYS_PRINT
        int 0x80

        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rbx

        add edi, 4 + MAX_LINE_LEN
        inc ebx
        cmp ebx, ecx
        jl .lp_loop

.lp_empty:
        POPALL
        ret

;=======================================================================
; EXPRESSION EVALUATOR: returns result in EAX
; Handles +, -, *, /, %, unary -, parentheses, variables, numbers
;=======================================================================
eval_expr:
        call skip_spc
        call eval_add_sub
        ret

eval_add_sub:
        call eval_mul_div
        push rax

.eas_loop:
        call skip_spc
        cmp byte [esi], '+'
        je .eas_add
        cmp byte [esi], '-'
        je .eas_sub
        pop rax
        ret

.eas_add:
        inc esi
        call skip_spc
        call eval_mul_div
        pop rbx
        add eax, ebx
        push rax
        jmp .eas_loop

.eas_sub:
        inc esi
        call skip_spc
        call eval_mul_div
        pop rbx
        sub ebx, eax
        mov eax, ebx
        push rax
        jmp .eas_loop

eval_mul_div:
        call eval_unary
        push rax

.emd_loop:
        call skip_spc
        cmp byte [esi], '*'
        je .emd_mul
        cmp byte [esi], '/'
        je .emd_div
        cmp byte [esi], '%'
        je .emd_mod
        pop rax
        ret

.emd_mul:
        inc esi
        call skip_spc
        call eval_unary
        pop rbx
        imul eax, ebx
        push rax
        jmp .emd_loop

.emd_div:
        inc esi
        call skip_spc
        call eval_unary
        cmp eax, 0
        je .emd_div0
        mov ebx, eax
        pop rax
        cdq
        idiv ebx
        push rax
        jmp .emd_loop

.emd_mod:
        inc esi
        call skip_spc
        call eval_unary
        cmp eax, 0
        je .emd_div0
        mov ebx, eax
        pop rax
        cdq
        idiv ebx
        mov eax, edx
        push rax
        jmp .emd_loop

.emd_div0:
        mov byte [run_error], ERR_DIV0
        pop rax
        xor eax, eax
        ret

eval_unary:
        cmp byte [esi], '-'
        je .eu_neg
        cmp byte [esi], '('
        je .eu_paren
        jmp eval_atom

.eu_neg:
        inc esi
        call skip_spc
        call eval_unary
        neg eax
        ret

.eu_paren:
        inc esi
        call skip_spc
        call eval_expr
        call skip_spc
        cmp byte [esi], ')'
        jne .eu_no_close
        inc esi
        ret
.eu_no_close:
        mov byte [run_error], ERR_SYNTAX
        ret

eval_atom:
        call skip_spc
        movzx eax, byte [esi]

        ; Number?
        cmp al, '0'
        jl .ea_var
        cmp al, '9'
        jg .ea_var
        jmp .ea_number

.ea_var:
        ; Variable A-Z?
        call to_upper
        cmp al, 'A'
        jl .ea_rnd
        cmp al, 'Z'
        jg .ea_rnd
        ; Check it's not a keyword start (peek ahead)
        sub al, 'A'
        movzx eax, al
        mov eax, [variables + eax * 4]
        inc esi
        ret

.ea_rnd:
        ; RND function?
        push rsi
        mov edi, kw_rnd
        call match_kw
        jnc .ea_peek

        ; Expect (expr)
        cmp byte [esi], '('
        jne .ea_rnd_noarg
        inc esi
        call eval_expr
        cmp byte [esi], ')'
        jne .ea_rnd_noarg
        inc esi
        ; EAX = max value
        push rax
        call random
        pop rbx
        cmp ebx, 0
        je .ea_rnd_done
        xor edx, edx
        div ebx
        mov eax, edx
        inc eax                 ; 1..N
.ea_rnd_done:
        ret
.ea_rnd_noarg:
        call random
        ret

.ea_peek:
        ; PEEK(addr) function
        mov edi, kw_peek
        call match_kw
        jnc .ea_abs
        cmp byte [esi], '('
        jne .ea_peek_err
        inc esi
        call eval_expr
        cmp byte [esi], ')'
        jne .ea_peek_err
        inc esi
        movzx eax, byte [eax]
        ret
.ea_peek_err:
        mov byte [run_error], ERR_SYNTAX
        xor eax, eax
        ret

.ea_abs:
        ; ABS(expr) function
        mov edi, kw_abs
        call match_kw
        jnc .ea_time
        cmp byte [esi], '('
        jne .ea_abs_err
        inc esi
        call eval_expr
        cmp byte [esi], ')'
        jne .ea_abs_err
        inc esi
        cmp eax, 0
        jge .ea_abs_done
        neg eax
.ea_abs_done:
        ret
.ea_abs_err:
        mov byte [run_error], ERR_SYNTAX
        xor eax, eax
        ret

.ea_time:
        ; TIME function (returns tick count, no parens needed)
        mov edi, kw_time
        call match_kw
        jnc .ea_arr
        mov eax, SYS_GETTIME
        int 0x80
        ret

.ea_arr:
        ; Array access: A(expr)
        ; Check if it's a variable letter followed by (
        mov al, [esi]
        call to_upper
        cmp al, 'A'
        jl .ea_err2
        cmp al, 'Z'
        jg .ea_err2
        cmp byte [esi + 1], '('
        jne .ea_err2
        sub al, 'A'
        movzx edx, al          ; var letter index
        add esi, 2              ; skip letter and (
        push rdx
        call eval_expr
        pop rdx
        cmp byte [esi], ')'
        jne .ea_arr_err
        inc esi
        ; Find array for variable edx
        push rsi
        mov esi, array_table
        mov ecx, MAX_ARRAYS
.ea_arr_find:
        cmp dword [esi], edx
        je .ea_arr_got
        add esi, 4 + ARRAY_MAX_SIZE * 4
        dec ecx
        jnz .ea_arr_find
        pop rsi
        mov byte [run_error], ERR_SYNTAX
        xor eax, eax
        ret
.ea_arr_got:
        ; Check bounds
        cmp eax, 0
        jl .ea_arr_bounds
        cmp eax, ARRAY_MAX_SIZE
        jge .ea_arr_bounds
        mov eax, [esi + 4 + eax * 4]
        pop rsi
        ret
.ea_arr_bounds:
        pop rsi
        mov byte [run_error], ERR_SYNTAX
        xor eax, eax
        ret
.ea_arr_err:
        mov byte [run_error], ERR_SYNTAX
        xor eax, eax
        ret

.ea_err2:
        pop rsi
.ea_err:
        pop rsi
        mov byte [run_error], ERR_SYNTAX
        xor eax, eax
        ret

.ea_number:
        xor eax, eax
.ea_nloop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jl .ea_ndone
        cmp dl, '9'
        jg .ea_ndone
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .ea_nloop
.ea_ndone:
        ret

;=======================================================================
; PRINT SIGNED INTEGER in EAX
;=======================================================================
print_signed:
        PUSHALL
        test eax, eax
        jns .ps_pos
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        pop rax
        neg eax
.ps_pos:
        call print_dec
        POPALL
        ret

;=======================================================================
; TO_UPPER: convert AL to uppercase
;=======================================================================
to_upper:
        cmp al, 'a'
        jl .tu_ret
        cmp al, 'z'
        jg .tu_ret
        sub al, 32
.tu_ret:
        ret

;=======================================================================
; READ NUMBER from keyboard for INPUT
;=======================================================================
read_input_number:
        push rcx
        push rdx
        push rbx
        xor ecx, ecx           ; sign (0=pos, 1=neg)
        xor esi, esi            ; accumulator... reuse
        push rsi
        xor esi, esi
        mov edi, input_buf

.rin_loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 0x0D
        je .rin_done
        cmp al, 0x0A
        je .rin_done
        cmp al, '-'
        je .rin_neg
        cmp al, 0x08
        je .rin_bs

        cmp al, '0'
        jl .rin_loop
        cmp al, '9'
        jg .rin_loop

        ; Echo
        push rax
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rax

        sub al, '0'
        movzx eax, al
        imul esi, 10
        add esi, eax
        jmp .rin_loop

.rin_neg:
        xor ecx, 1
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        pop rax
        jmp .rin_loop

.rin_bs:
        ; Simple: just divide by 10
        xor edx, edx
        mov eax, esi
        mov ebx, 10
        div ebx
        mov esi, eax
        push rsi
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        pop rsi
        jmp .rin_loop

.rin_done:
        ; Newline
        push rsi
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rsi

        mov eax, esi
        cmp ecx, 0
        je .rin_pos
        neg eax
.rin_pos:
        add rsp, 4              ; pop saved esi
        pop rbx
        pop rdx
        pop rcx
        ret

;=======================================================================
; RANDOM number generator (LCG)
;=======================================================================
random:
        push rbx
        push rdx
        mov eax, [rand_seed]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        pop rdx
        pop rbx
        ret

;=======================================================================
; PRINT ERROR MESSAGE
;=======================================================================
print_error:
        PUSHALL
        movzx eax, byte [run_error]
        cmp eax, ERR_SYNTAX
        je .pe_syntax
        cmp eax, ERR_DIV0
        je .pe_div0
        cmp eax, ERR_LINE
        je .pe_line
        cmp eax, ERR_GOSUB
        je .pe_gosub
        cmp eax, ERR_RETURN
        je .pe_return
        cmp eax, ERR_FOR
        je .pe_for
        cmp eax, ERR_MEM
        je .pe_mem
        cmp eax, ERR_FILE
        je .pe_file
        jmp .pe_generic
.pe_syntax:
        mov ebx, err_syntax_msg
        jmp .pe_print
.pe_div0:
        mov ebx, err_div0_msg
        jmp .pe_print
.pe_line:
        mov ebx, err_line_msg
        jmp .pe_print
.pe_gosub:
        mov ebx, err_gosub_msg
        jmp .pe_print
.pe_return:
        mov ebx, err_return_msg
        jmp .pe_print
.pe_for:
        mov ebx, err_for_msg
        jmp .pe_print
.pe_mem:
        mov ebx, err_mem_msg
        jmp .pe_print
.pe_file:
        mov ebx, err_file_msg
        jmp .pe_print
.pe_generic:
        mov ebx, err_generic_msg
.pe_print:
        mov eax, SYS_SETCOLOR
        push rbx
        mov ebx, 0x0C
        int 0x80
        pop rbx
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        POPALL
        ret

;=======================================================================
; LOAD file: ESI = filename
;=======================================================================
cmd_load:
        PUSHALL
        ; Get filename from input
        call skip_spc
        cmp byte [esi], '"'
        je .cl_quoted
        ; Unquoted - copy to filename_tmp
        mov edi, filename_tmp
        call .cl_copy_word
        jmp .cl_do_load
.cl_quoted:
        inc esi
        mov edi, filename_tmp
.cl_qloop:
        lodsb
        cmp al, '"'
        je .cl_qend
        cmp al, 0
        je .cl_qend
        stosb
        jmp .cl_qloop
.cl_qend:
        mov byte [edi], 0

.cl_do_load:
        ; Clear current program
        mov dword [line_count], 0

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename_tmp
        mov ecx, file_buffer
        int 0x80
        cmp eax, 0
        jle .cl_err

        ; Parse file into lines
        mov esi, file_buffer
        mov [file_size], eax

.cl_parse:
        cmp byte [esi], 0
        je .cl_done
        ; Read one line into input_buf
        mov edi, input_buf
        xor ecx, ecx
.cl_line_loop:
        lodsb
        cmp al, 0
        je .cl_line_done
        cmp al, 0x0A
        je .cl_line_done
        cmp al, 0x0D
        je .cl_skip_cr
        stosb
        inc ecx
        jmp .cl_line_loop
.cl_skip_cr:
        jmp .cl_line_loop
.cl_line_done:
        mov byte [edi], 0
        cmp ecx, 0
        je .cl_parse

        ; Parse line number and store
        push rsi
        mov esi, input_buf
        movzx eax, byte [esi]
        cmp al, '0'
        jl .cl_skip_line
        cmp al, '9'
        jg .cl_skip_line
        call parse_linenum
        call store_line
.cl_skip_line:
        pop rsi
        jmp .cl_parse

.cl_done:
        mov eax, SYS_PRINT
        mov ebx, msg_loaded
        int 0x80
        POPALL
        ret

.cl_err:
        mov byte [run_error], ERR_FILE
        call print_error
        mov byte [run_error], 0
        POPALL
        ret

.cl_copy_word:
        lodsb
        cmp al, ' '
        je .cl_cw_done
        cmp al, 0
        je .cl_cw_done
        stosb
        jmp .cl_copy_word
.cl_cw_done:
        mov byte [edi], 0
        ret

;=======================================================================
; LOAD from command-line args
;=======================================================================
cmd_load_from_args:
        PUSHALL
        mov esi, input_buf
        call skip_spc
        mov edi, filename_tmp
.cla_copy:
        lodsb
        cmp al, ' '
        je .cla_done
        cmp al, 0
        je .cla_done
        stosb
        jmp .cla_copy
.cla_done:
        mov byte [edi], 0

        mov dword [line_count], 0
        mov eax, SYS_FREAD
        mov ebx, filename_tmp
        mov ecx, file_buffer
        int 0x80
        cmp eax, 0
        jle .cla_err

        mov esi, file_buffer
        mov [file_size], eax

.cla_parse:
        cmp byte [esi], 0
        je .cla_loaded
        mov edi, input_buf
        xor ecx, ecx
.cla_ll:
        lodsb
        cmp al, 0
        je .cla_ld
        cmp al, 0x0A
        je .cla_ld
        cmp al, 0x0D
        je .cla_ll
        stosb
        inc ecx
        jmp .cla_ll
.cla_ld:
        mov byte [edi], 0
        cmp ecx, 0
        je .cla_parse
        push rsi
        mov esi, input_buf
        movzx eax, byte [esi]
        cmp al, '0'
        jl .cla_sk
        cmp al, '9'
        jg .cla_sk
        call parse_linenum
        call store_line
.cla_sk:
        pop rsi
        jmp .cla_parse

.cla_loaded:
        mov eax, SYS_PRINT
        mov ebx, msg_loaded
        int 0x80
        POPALL
        ret
.cla_err:
        POPALL
        ret

;=======================================================================
; SAVE program to file
;=======================================================================
cmd_save:
        PUSHALL
        call skip_spc
        cmp byte [esi], '"'
        je .cs_quoted
        mov edi, filename_tmp
        call .cs_copy_word
        jmp .cs_do_save
.cs_quoted:
        inc esi
        mov edi, filename_tmp
.cs_qloop:
        lodsb
        cmp al, '"'
        je .cs_qend
        cmp al, 0
        je .cs_qend
        stosb
        jmp .cs_qloop
.cs_qend:
        mov byte [edi], 0

.cs_do_save:
        ; Build file content from program lines
        mov edi, file_buffer
        xor edx, edx           ; total size
        mov ecx, [line_count]
        cmp ecx, 0
        je .cs_empty
        mov esi, program_area
        xor ebx, ebx

.cs_line:
        ; Write line number as text
        push rbx
        push rcx
        mov eax, [esi]
        call write_dec_to_buf    ; writes to EDI, advances EDI, adds to EDX
        mov byte [edi], ' '
        inc edi
        inc edx

        ; Write line text
        push rsi
        add esi, 4
.cs_text:
        lodsb
        cmp al, 0
        je .cs_eol
        mov [edi], al
        inc edi
        inc edx
        jmp .cs_text
.cs_eol:
        mov byte [edi], 0x0A
        inc edi
        inc edx
        pop rsi
        pop rcx
        pop rbx

        add esi, 4 + MAX_LINE_LEN
        inc ebx
        cmp ebx, ecx
        jl .cs_line

.cs_empty:
        ; Write file
        mov eax, SYS_FWRITE
        mov ebx, filename_tmp
        mov ecx, file_buffer
        ; EDX = size already set
        int 0x80
        cmp eax, 0
        jl .cs_err

        mov eax, SYS_PRINT
        mov ebx, msg_saved_file
        int 0x80
        POPALL
        ret

.cs_err:
        mov byte [run_error], ERR_FILE
        call print_error
        mov byte [run_error], 0
        POPALL
        ret

.cs_copy_word:
        lodsb
        cmp al, ' '
        je .cs_cw_done
        cmp al, 0
        je .cs_cw_done
        stosb
        jmp .cs_copy_word
.cs_cw_done:
        mov byte [edi], 0
        ret

;=======================================================================
; WRITE DECIMAL to buffer at EDI (advances EDI, adds count to EDX)
;=======================================================================
write_dec_to_buf:
        PUSHALL
        xor ecx, ecx
        mov ebx, 10
        test eax, eax
        jnz .wd_nz
        mov byte [edi], '0'
        inc edi
        inc edx
        mov [rsp + 72], edi     ; update EDI in PUSHALL frame
        mov [rsp + 96], edx     ; update EDX
        POPALL
        ret
.wd_nz:
.wd_push:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        test eax, eax
        jnz .wd_push
        mov edx, [rsp + rcx * 8 + 88]  ; original EDX from PUSHALL (FRAME_RDX=88)
        mov edi, [rsp + rcx * 8 + 72]  ; original EDI from PUSHALL (FRAME_RDI=72)
.wd_pop:
        pop rax
        add al, '0'
        mov [edi], al
        inc edi
        inc edx
        dec ecx
        jnz .wd_pop
        mov [rsp + 72], edi
        mov [rsp + 96], edx
        POPALL
        ret

;=======================================================================
; DATA
;=======================================================================

; Error codes
ERR_SYNTAX      equ 1
ERR_DIV0        equ 2
ERR_LINE        equ 3
ERR_GOSUB       equ 4
ERR_RETURN      equ 5
ERR_FOR         equ 6
ERR_MEM         equ 7
ERR_FILE        equ 8

; Keywords
kw_print:       db "PRINT", 0
kw_input:       db "INPUT", 0
kw_let:         db "LET", 0
kw_if:          db "IF", 0
kw_then:        db "THEN", 0
kw_goto:        db "GOTO", 0
kw_gosub:       db "GOSUB", 0
kw_return:      db "RETURN", 0
kw_for:         db "FOR", 0
kw_to:          db "TO", 0
kw_next:        db "NEXT", 0
kw_end:         db "END", 0
kw_rem:         db "REM", 0
kw_run:         db "RUN", 0
kw_list:        db "LIST", 0
kw_new:         db "NEW", 0
kw_load:        db "LOAD", 0
kw_save:        db "SAVE", 0
kw_cls:         db "CLS", 0
kw_color:       db "COLOR", 0
kw_beep:        db "BEEP", 0
kw_rnd:         db "RND", 0
kw_step:        db "STEP", 0
kw_peek:        db "PEEK", 0
kw_poke:        db "POKE", 0
kw_abs:         db "ABS", 0
kw_time:        db "TIME", 0
kw_sleep:       db "SLEEP", 0
kw_dim:         db "DIM", 0
kw_help:        db "HELP", 0
kw_system:      db "SYSTEM", 0

; Messages
msg_banner:     db "Mellivora BASIC v2.2", 0x0A
                db "====================", 0x0A
                db "Type HELP for commands", 0x0A, 0
msg_help:       db "Mellivora BASIC Commands", 0x0A
                db "========================", 0x0A
                db 'PRINT expr/"text"  Display output', 0x0A
                db "INPUT var          Read from keyboard", 0x0A
                db "LET var = expr     Assign variable (A-Z)", 0x0A
                db "IF expr THEN stmt  Conditional", 0x0A
                db "GOTO line          Jump to line number", 0x0A
                db "GOSUB line         Call subroutine", 0x0A
                db "RETURN             Return from GOSUB", 0x0A
                db "FOR v=a TO b [STEP c]  Loop", 0x0A
                db "NEXT var           End of FOR loop", 0x0A
                db "DIM var(size)      Declare array", 0x0A
                db "PEEK(addr)         Read memory byte", 0x0A
                db "POKE addr, val     Write memory byte", 0x0A
                db "COLOR n            Set text colour", 0x0A
                db "CLS                Clear screen", 0x0A
                db "BEEP               Play beep sound", 0x0A
                db "SLEEP n            Pause n centiseconds", 0x0A
                db "REM text           Comment line", 0x0A
                db "END                Stop program", 0x0A
                db "RUN                Run program", 0x0A
                db "LIST               List program", 0x0A
                db "NEW                Clear program", 0x0A
                db 'LOAD "file"       Load from disk', 0x0A
                db 'SAVE "file"       Save to disk', 0x0A
                db "HELP               Show this help", 0x0A
                db "SYSTEM             Exit to shell", 0x0A, 0
msg_ready:      db "Ready.", 0x0A, 0
msg_prompt:     db "] ", 0
msg_input_prompt: db "? ", 0
msg_at_line:    db " at line ", 0
msg_loaded:     db "Loaded.", 0x0A, 0
msg_saved_file: db "Saved.", 0x0A, 0

; Error messages
err_syntax_msg: db "?SYNTAX ERROR", 0x0A, 0
err_div0_msg:   db "?DIVISION BY ZERO", 0x0A, 0
err_line_msg:   db "?UNDEFINED LINE", 0x0A, 0
err_gosub_msg:  db "?GOSUB STACK OVERFLOW", 0x0A, 0
err_return_msg: db "?RETURN WITHOUT GOSUB", 0x0A, 0
err_for_msg:    db "?FOR/NEXT ERROR", 0x0A, 0
err_mem_msg:    db "?OUT OF MEMORY", 0x0A, 0
err_file_msg:   db "?FILE ERROR", 0x0A, 0
err_generic_msg: db "?ERROR", 0x0A, 0

; Variables
variables:      times 26 dd 0   ; A-Z
rand_seed:      dd 12345

; Control flow
line_count:     dd 0
current_line_idx: dd 0
goto_target:    dd 0
goto_flag:      db 0
end_flag:       db 0
return_flag:    db 0
return_line_idx: dd 0
run_error:      db 0

; GOSUB stack
gosub_sp:       dd 0
gosub_stack:    times GOSUB_DEPTH dd 0

; FOR stack (var_idx, end_val, line_idx, step per entry = 16 bytes)
for_sp:         dd 0
for_stack:      times FOR_DEPTH * 4 dd 0

; Buffers
input_buf:      times INPUT_BUF_LEN + 1 db 0
filename_tmp:   times 64 db 0
file_size:      dd 0

; Program storage
program_area:   times PROG_SIZE db 0

; File I/O buffer
file_buffer:    times 16384 db 0

; Array table: MAX_ARRAYS entries, each = 4 (var_id) + ARRAY_MAX_SIZE*4 (data)
; var_id = -1 means free
array_table:
%rep MAX_ARRAYS
        dd -1                   ; var_id (-1 = free)
        times ARRAY_MAX_SIZE dd 0
%endrep
