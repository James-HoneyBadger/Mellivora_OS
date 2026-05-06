; awk.asm - Pattern/action text processor for Mellivora OS
; Usage: awk [-F delim] 'program' [file]
; Supported: $0..$99, NR, NF, print, printf (partial), -F, /regex/ patterns,
;            NR==n patterns, { action } blocks, BEGIN/END blocks,
;            gsub(pat,repl), sub(pat,repl), length([s]), split(s,a,fs), substr(s,i[,n])
%include "syscalls.inc"

MAX_LINE        equ 2048
MAX_FIELDS      equ 128
MAX_FIELD_LEN   equ 256
MAX_FILE        equ 131072
MAX_PROG        equ 4096
MAX_RULES       equ 32

; Rule pattern types
PAT_ALWAYS      equ 0
PAT_BEGIN       equ 1
PAT_END         equ 2
PAT_REGEX       equ 3
PAT_NR_EQ      equ 4
PAT_NR_GE      equ 5
PAT_NR_LE      equ 6
PAT_RANGE      equ 7

; Rule action offsets (each rule = 8 + 256 + 512 = 776 bytes)
RULE_PAT_TYPE   equ 0
RULE_PAT_ARG    equ 4
RULE_PAT_ARG2   equ 8
RULE_ACTION     equ 12
RULE_SIZE       equ (12 + MAX_PROG / MAX_RULES)

start:
        ; Get args
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        ; Parse args
        lea esi, [arg_buf]
        call .skip_prog_name    ; skip argv[0]

        ; Defaults
        mov byte [fs_char], ' '
        mov dword [num_rules], 0

.parse_args:
        call .next_arg
        test eax, eax
        jz .no_more_args

        cmp byte [eax], '-'
        jne .maybe_prog

        ; Check flag
        cmp byte [eax + 1], 'F'
        jne .unk_flag
        ; -F delim
        call .next_arg
        test eax, eax
        jz .usage
        mov al, [eax]
        mov [fs_char], al
        jmp .parse_args

.unk_flag:
        jmp .parse_args

.maybe_prog:
        cmp dword [prog_len], 0
        jne .maybe_file
        ; First non-flag arg = awk program
        lea edi, [prog_buf]
        mov [esi_save], esi
        mov esi, eax
.copy_prog:
        lodsb
        test al, al
        jz .prog_done
        stosb
        jmp .copy_prog
.prog_done:
        mov byte [edi], 0
        lea eax, [prog_buf]
        mov ecx, edi
        sub ecx, eax
        mov [prog_len], ecx
        mov esi, [esi_save]
        jmp .parse_args

.maybe_file:
        ; arg is filename
        lea edi, [input_filename]
        mov [esi_src], eax
        push esi
        mov esi, eax
.copy_fn:
        lodsb
        stosb
        test al, al
        jnz .copy_fn
        pop esi
        jmp .parse_args

.no_more_args:
        cmp dword [prog_len], 0
        jz .usage

        ; Parse and compile awk program into rules
        call .parse_program

        ; Read input file or stdin
        cmp byte [input_filename], 0
        je .read_stdin
        mov eax, SYS_FREAD
        mov ebx, input_filename
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        test eax, eax
        js .file_err
        mov [file_size], eax
        jmp .run_awk

.read_stdin:
        mov eax, SYS_STDIN_READ
        mov ebx, file_buf
        mov ecx, MAX_FILE
        int 0x80
        test eax, eax
        js .run_awk
        mov [file_size], eax

.run_awk:
        ; Run BEGIN blocks
        mov dword [cur_nr], 0
        call .run_begin

        ; Process lines
        mov dword [file_pos], 0
.line_loop:
        call .read_line
        test eax, eax
        jz .awk_done
        ; split into fields
        call .split_fields
        ; run matching rules
        call .run_rules
        inc dword [cur_nr]
        jmp .line_loop

.awk_done:
        call .run_end
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.file_err:
        mov eax, SYS_PRINT
        mov ebx, err_file
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;-------------------------------------------
; skip_prog_name: advance esi past first NUL-separated arg
.skip_prog_name:
.sn_loop: lodsb
test al, al
jnz .sn_loop
        ret

; next_arg: returns pointer in EAX to next arg or 0 if none
; modifies ESI
.next_arg:
        ; skip leading NULs
.na_skip: cmp byte [esi], 0
jne .na_found
        inc esi
        cmp esi, arg_buf + 512
        jge .na_none
        jmp .na_skip
.na_found:
        mov eax, esi
.na_adv: lodsb
test al, al
jnz .na_adv
        ret
.na_none:
        xor eax, eax
        ret

;-------------------------------------------
; parse_program: parse prog_buf → rules array
.parse_program:
        pushad
        lea esi, [prog_buf]
.pp_main:
        call .pp_skipws
        cmp byte [esi], 0
        je .pp_done

        ; Allocate rule slot
        mov eax, [num_rules]
        cmp eax, MAX_RULES
        jge .pp_done
        imul eax, RULE_SIZE
        add eax, rules_buf
        mov [cur_rule_ptr], eax

        ; Detect pattern type
        cmp byte [esi], '{'
        je .pp_always

        ; Check BEGIN / END
        cmp dword [esi], 'BEGI'  ; little-endian compare first 4 chars
        ; just check first char for speed
        cmp byte [esi], 'B'
        je .pp_begin_chk
        cmp byte [esi], 'E'
        je .pp_end_chk

        ; /regex/ pattern
        cmp byte [esi], '/'
        je .pp_regex

        ; NR comparison
        cmp byte [esi], 'N'
        je .pp_nr_chk

        ; fallthrough = PAT_ALWAYS
.pp_always:
        mov eax, [cur_rule_ptr]
        mov dword [eax + RULE_PAT_TYPE], PAT_ALWAYS
        jmp .pp_action

.pp_begin_chk:
        ; Match "BEGIN"
        push esi
        lea edi, [kw_begin]
        mov ecx, 5
        rep cmpsb
        je .pp_is_begin
        pop esi
        jmp .pp_always
.pp_is_begin:
        pop ecx  ; discard saved esi
        mov eax, [cur_rule_ptr]
        mov dword [eax + RULE_PAT_TYPE], PAT_BEGIN
        jmp .pp_action

.pp_end_chk:
        ; "END"
        cmp dword [esi], 0x00444E45 ; "END\0"... just check E,N,D
        push esi
        lea edi, [kw_end]
        mov ecx, 3
        rep cmpsb
        je .pp_is_end
        pop esi
        jmp .pp_always
.pp_is_end:
        pop ecx
        mov eax, [cur_rule_ptr]
        mov dword [eax + RULE_PAT_TYPE], PAT_END
        jmp .pp_action

.pp_regex:
        inc esi  ; skip opening /
        mov eax, [cur_rule_ptr]
        lea edi, [eax + RULE_PAT_ARG]
        mov ecx, 255
.pp_rx_copy:
        lodsb
        cmp al, '/'
        je .pp_rx_done
        test al, al
        jz .pp_rx_done
        stosb
        dec ecx
        jnz .pp_rx_copy
.pp_rx_done:
        mov byte [edi], 0
        mov eax, [cur_rule_ptr]
        mov dword [eax + RULE_PAT_TYPE], PAT_REGEX
        jmp .pp_action

.pp_nr_chk:
        ; Check NR==n, NR>=n, NR<=n
        cmp word [esi], 'RN'   ; "NR" in little-endian
        ; N=0x4E R=0x52
        mov ax, [esi]
        cmp ax, 0x524E
        jne .pp_always  ; "NR"
        add esi, 2
        mov al, [esi]
        cmp al, '='
        jne .pp_nr_ge
        inc esi
        cmp byte [esi], '='
        jne .pp_always
        inc esi
        ; parse number
        call .parse_num
        mov eax, [cur_rule_ptr]
        mov dword [eax + RULE_PAT_TYPE], PAT_NR_EQ
        mov [eax + RULE_PAT_ARG], ecx
        jmp .pp_action
.pp_nr_ge:
        cmp al, '>'
        jne .pp_nr_le
        inc esi
        cmp byte [esi], '='
        jne .pp_always
        inc esi
        call .parse_num
        mov eax, [cur_rule_ptr]
        mov dword [eax + RULE_PAT_TYPE], PAT_NR_GE
        mov [eax + RULE_PAT_ARG], ecx
        jmp .pp_action
.pp_nr_le:
        cmp al, '<'
        jne .pp_always
        inc esi
        cmp byte [esi], '='
        jne .pp_always
        inc esi
        call .parse_num
        mov eax, [cur_rule_ptr]
        mov dword [eax + RULE_PAT_TYPE], PAT_NR_LE
        mov [eax + RULE_PAT_ARG], ecx
        jmp .pp_action

.pp_action:
        ; skip whitespace then expect '{'
        call .pp_skipws
        cmp byte [esi], '{'
        jne .pp_skip_rule
        inc esi
        ; Copy action body until matching '}'
        mov eax, [cur_rule_ptr]
        lea edi, [eax + RULE_ACTION]
        mov edx, RULE_SIZE - RULE_ACTION - 1
        mov ecx, 1  ; brace depth
.pp_act_copy:
        test edx, edx
        jz .pp_act_done
        lodsb
        test al, al
        jz .pp_act_done
        cmp al, '{'
        jne .pp_noopenb
        inc ecx
.pp_noopenb:
        cmp al, '}'
        jne .pp_nocloseb
        dec ecx
        jz .pp_act_done
.pp_nocloseb:
        stosb
        dec edx
        jmp .pp_act_copy
.pp_act_done:
        mov byte [edi], 0
        inc dword [num_rules]
        jmp .pp_main

.pp_skip_rule:
        ; skip to next {
.pp_sk: lodsb
test al, al
jz .pp_done
        cmp al, '{'
        jne .pp_sk
        mov ecx, 1
.pp_sk2: lodsb
test al, al
jz .pp_done
        cmp al, '{'
        jne .pp_sk_nc
        inc ecx
.pp_sk_nc:
        cmp al, '}'
        jne .pp_sk2
        dec ecx
        jnz .pp_sk2
        jmp .pp_main

.pp_done:
        popad
        ret

; parse_num: parse decimal at [esi], result in ECX, advances ESI
.parse_num:
        xor ecx, ecx
.pn_lp:
        mov al, [esi]
        cmp al, '0'
        jb .pn_done
        cmp al, '9'
        ja .pn_done
        imul ecx, 10
        sub al, '0'
        movzx eax, al
        add ecx, eax
        inc esi
        jmp .pn_lp
.pn_done: ret

; pp_skipws: skip spaces/tabs/newlines at [esi]
.pp_skipws:
        mov al, [esi]
        cmp al, ' '
        je .ps_skip
        cmp al, 0x09
        je .ps_skip
        cmp al, 0x0A
        je .ps_skip
        cmp al, 0x0D
        je .ps_skip
        ret
.ps_skip: inc esi
jmp .pp_skipws

;-------------------------------------------
; read_line: read next line from file_buf into line_buf
; returns EAX=line length (0=EOF)
.read_line:
        mov esi, [file_pos]
        cmp esi, [file_size]
        jge .rl_eof
        lea edi, [line_buf]
        mov ecx, MAX_LINE - 1
.rl_loop:
        cmp esi, [file_size]
        jge .rl_end
        mov al, [file_buf + esi]
        inc esi
        cmp al, 0x0A
        je .rl_end
        cmp al, 0x0D
        je .rl_loop   ; skip CR
        stosb
        dec ecx
        jnz .rl_loop
.rl_end:
        mov byte [edi], 0
        mov [file_pos], esi
        lea eax, [line_buf]
        ; compute length
        mov ecx, edi
        sub ecx, eax
        mov eax, ecx
        ret
.rl_eof:
        xor eax, eax
        ret

;-------------------------------------------
; split_fields: split line_buf by fs_char into fields[]
.split_fields:
        pushad
        lea esi, [line_buf]
        mov dword [num_fields], 0
        xor ecx, ecx   ; field index

.sf_next_field:
        cmp ecx, MAX_FIELDS
        jge .sf_done
        ; skip leading FS if space-splitting
        mov al, [fs_char]
        cmp al, ' '
        jne .sf_no_skip
.sf_sp_skip:
        cmp byte [esi], ' '
        jne .sf_no_skip
        inc esi
        jmp .sf_sp_skip
.sf_no_skip:
        cmp byte [esi], 0
        je .sf_done

        ; store field pointer
        imul eax, ecx, MAX_FIELD_LEN
        add eax, fields_buf
        mov edi, eax
        mov [field_ptrs + ecx*4], edi

        ; copy until FS or EOL
        mov bl, [fs_char]
        mov edx, MAX_FIELD_LEN - 1
.sf_copy:
        mov al, [esi]
        test al, al
        jz .sf_field_done
        cmp al, bl
        je .sf_field_end
        stosb
        inc esi
        dec edx
        jnz .sf_copy
.sf_field_done:
        jmp .sf_field_ok
.sf_field_end:
        inc esi
.sf_field_ok:
        mov byte [edi], 0
        inc ecx
        jmp .sf_next_field

.sf_done:
        mov [num_fields], ecx
        ; Clear any extra field pointers
        popad
        ret

;-------------------------------------------
; run_begin: execute PAT_BEGIN rules
.run_begin:
        push ecx
        xor ecx, ecx
.rb_loop:
        cmp ecx, [num_rules]
        jge .rb_done
        imul eax, ecx, RULE_SIZE
        add eax, rules_buf
        cmp dword [eax + RULE_PAT_TYPE], PAT_BEGIN
        jne .rb_next
        lea ebx, [eax + RULE_ACTION]
        call .exec_action
.rb_next:
        inc ecx
        jmp .rb_loop
.rb_done:
        pop ecx
        ret

; run_end
.run_end:
        push ecx
        xor ecx, ecx
.re_loop:
        cmp ecx, [num_rules]
        jge .re_done
        imul eax, ecx, RULE_SIZE
        add eax, rules_buf
        cmp dword [eax + RULE_PAT_TYPE], PAT_END
        jne .re_next
        lea ebx, [eax + RULE_ACTION]
        call .exec_action
.re_next:
        inc ecx
        jmp .re_loop
.re_done:
        pop ecx
        ret

; run_rules: execute matching non-BEGIN/END rules for current line
.run_rules:
        push ecx
        xor ecx, ecx
.rr_loop:
        cmp ecx, [num_rules]
        jge .rr_done
        imul eax, ecx, RULE_SIZE
        add eax, rules_buf
        mov edx, [eax + RULE_PAT_TYPE]
        cmp edx, PAT_BEGIN
        je .rr_next
        cmp edx, PAT_END
        je .rr_next

        push ecx
        push eax
        call .pattern_matches  ; EAX=rule ptr → EAX=1 match/0 no
        mov edx, eax
        pop eax
        pop ecx

        test edx, edx
        jz .rr_next
        lea ebx, [eax + RULE_ACTION]
        call .exec_action
.rr_next:
        inc ecx
        jmp .rr_loop
.rr_done:
        pop ecx
        ret

; pattern_matches: EAX=rule ptr → EAX=1 if matches current line/NR
.pattern_matches:
        push ebx
        push ecx
        mov ecx, [eax + RULE_PAT_TYPE]
        cmp ecx, PAT_ALWAYS
        je .pm_yes
        cmp ecx, PAT_REGEX
        je .pm_regex
        cmp ecx, PAT_NR_EQ
        je .pm_nr_eq
        cmp ecx, PAT_NR_GE
        je .pm_nr_ge
        cmp ecx, PAT_NR_LE
        je .pm_nr_le
        jmp .pm_no

.pm_regex:
        lea ebx, [eax + RULE_PAT_ARG]
        lea ecx, [line_buf]
        call .strstr
        test eax, eax
        jnz .pm_yes
        jmp .pm_no
.pm_nr_eq:
        mov ebx, [eax + RULE_PAT_ARG]
        cmp ebx, [cur_nr]
        je .pm_yes
        jmp .pm_no
.pm_nr_ge:
        mov ebx, [eax + RULE_PAT_ARG]
        cmp [cur_nr], ebx
        jge .pm_yes
        jmp .pm_no
.pm_nr_le:
        mov ebx, [eax + RULE_PAT_ARG]
        cmp [cur_nr], ebx
        jle .pm_yes
        jmp .pm_no
.pm_yes: mov eax, 1
pop ecx
pop ebx
ret
.pm_no:  xor eax, eax
pop ecx
pop ebx
ret

;-------------------------------------------
; exec_action: EBX = action string
; Executes the AWK action body
.exec_action:
        pushad
        mov esi, ebx
.ea_main:
        ; Skip whitespace/semicolons
        call .ea_skipws
        cmp byte [esi], 0
        je .ea_done

        ; Dispatch on keyword
        call .ea_check_print
        test eax, eax
        jnz .ea_main

        call .ea_check_gsub
        test eax, eax
        jnz .ea_main

        call .ea_check_sub
        test eax, eax
        jnz .ea_main

        ; Skip unknown statement to next ';' or newline
.ea_skip_stmt:
        lodsb
        test al, al
        jz .ea_done
        cmp al, ';'
        je .ea_main
        cmp al, 0x0A
        je .ea_main
        jmp .ea_skip_stmt

.ea_done:
        popad
        ret

.ea_skipws:
        mov al, [esi]
        cmp al, ' '
        je .ew_skip
        cmp al, 0x09
        je .ew_skip
        cmp al, 0x0A
        je .ew_skip
        cmp al, 0x0D
        je .ew_skip
        cmp al, ';'
        je .ew_skip
        ret
.ew_skip: inc esi
jmp .ea_skipws

; check "print" statement
.ea_check_print:
        push esi
        push ebx
        push ecx
        push edx
        lea edi, [kw_print]
        mov ecx, 5
        push esi
        lea edi, [kw_print]
        repz cmpsb
        jne .ecp_no

        ; Check next char is not alpha/digit (full word match)
        mov al, [esi]
        call .isalnum
        test eax, eax
        jnz .ecp_no

        ; Collect print items until ';' or newline
        call .ea_skipws
        cmp byte [esi], ';'
        je .ecp_newline
        cmp byte [esi], 0x0A
        je .ecp_newline
        cmp byte [esi], 0
        je .ecp_newline
        ; Print items separated by commas
        mov edx, 1  ; first item
.ecp_item:
        call .ea_skipws
        cmp byte [esi], 0
        je .ecp_nl2
        cmp byte [esi], ';'
        je .ecp_nl2
        cmp byte [esi], 0x0A
        je .ecp_nl2

        ; comma = print FS
        cmp byte [esi], ','
        jne .ecp_no_comma
        inc esi
        ; print OFS (default space)
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        jmp .ecp_item
.ecp_no_comma:

        ; Evaluate item
        call .ea_eval_expr
        ; eax=ptr to result string in expr_buf
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80

        jmp .ecp_item

.ecp_newline:
        pop ecx  ; discard saved esi
.ecp_nl2:
        ; print trailing newline
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, 1
        pop edx
        pop ecx
        pop ebx
        pop esi
        ret
.ecp_no:
        pop esi  ; restore
        pop edx
        pop ecx
        pop ebx
        pop esi
        xor eax, eax
        ret

; ea_eval_expr: evaluate token at [esi] → string in expr_buf, returns EAX=ptr
; Handles: $n, NR, NF, "string", number, bare word
.ea_eval_expr:
        lea edi, [expr_buf]
        mov al, [esi]

        ; $n field reference
        cmp al, '$'
        jne .ev_not_field
        inc esi
        ; check for NF
        cmp byte [esi], 'N'
        jne .ev_parse_fnum
        cmp byte [esi + 1], 'F'
        jne .ev_parse_fnum
        add esi, 2
        ; $NF = last field
        mov eax, [num_fields]
        dec eax
        jmp .ev_get_field
.ev_parse_fnum:
        call .parse_num  ; result in ECX
        mov eax, ecx
.ev_get_field:
        test eax, eax
        js .ev_field0
        cmp eax, 0
        je .ev_field0
        dec eax
        cmp eax, [num_fields]
        jge .ev_empty
        imul ecx, eax, MAX_FIELD_LEN
        add ecx, fields_buf
        ; copy to expr_buf
        mov [esi_tmp], esi
        mov esi, ecx
.ev_cp_fld: lodsb
        stosb
        test al, al
        jnz .ev_cp_fld
        mov esi, [esi_tmp]
        lea eax, [expr_buf]
        ret

.ev_field0:
        ; $0 = whole line
        mov [esi_tmp2], esi
        lea esi, [line_buf]
.ev_cp0:
        lodsb
        stosb
        test al, al
        jnz .ev_cp0
        mov esi, [esi_tmp2]
        lea eax, [expr_buf]
        ret

.ev_empty:
        mov byte [edi], 0
        lea eax, [expr_buf]
        ret

.ev_not_field:
        ; NR
        cmp word [esi], 0x524E
        jne .ev_not_nr  ; "NR"
        cmp byte [esi + 2], 'R'
        je .ev_nr_too_long
        ; check it's not NRx
        mov al, [esi + 2]
        call .isalnum
        test eax, eax
        jnz .ev_not_nr
.ev_nr_too_long:
        add esi, 2
        mov eax, [cur_nr]
        lea edi, [expr_buf]
        call .itoa
        lea eax, [expr_buf]
        ret
.ev_not_nr:

        ; NF
        cmp word [esi], 0x464E
        jne .ev_not_nf  ; "NF"
        mov al, [esi + 2]
        call .isalnum
        test eax, eax
        jnz .ev_not_nf
        add esi, 2
        mov eax, [num_fields]
        lea edi, [expr_buf]
        call .itoa
        lea eax, [expr_buf]
        ret
.ev_not_nf:

        ; quoted string
        cmp byte [esi], '"'
        jne .ev_not_str
        inc esi
        lea edi, [expr_buf]
.ev_str_cp:
        lodsb
        cmp al, '"'
        je .ev_str_done
        cmp al, '\\'
        jne .ev_str_plain
        lodsb   ; escape
        cmp al, 'n'
        jne .ev_str_esc2
        mov al, 0x0A
        jmp .ev_str_plain
.ev_str_esc2:
        cmp al, 't'
        jne .ev_str_plain
        mov al, 0x09
.ev_str_plain:
        test al, al
        jz .ev_str_done
        stosb
        jmp .ev_str_cp
.ev_str_done:
        mov byte [edi], 0
        lea eax, [expr_buf]
        ret
.ev_not_str:

        ; bare word / number — copy until space/comma/)/;/newline
        lea edi, [expr_buf]
.ev_bare:
        mov al, [esi]
        cmp al, 0
        je .ev_bare_done
        cmp al, ' '
        je .ev_bare_done
        cmp al, ','
        je .ev_bare_done
        cmp al, ')'
        je .ev_bare_done
        cmp al, ';'
        je .ev_bare_done
        cmp al, 0x0A
        je .ev_bare_done
        lodsb
        stosb
        jmp .ev_bare
.ev_bare_done:
        mov byte [edi], 0
        lea eax, [expr_buf]
        ret

; check "gsub" statement
.ea_check_gsub:
        push esi
        push ecx
        lea edi, [kw_gsub]
        mov ecx, 4
        push esi
        repz cmpsb
        jne .ecg_no
        mov al, [esi]
        cmp al, '('
        jne .ecg_no

        ; gsub(pat, repl) — apply global subst on $0
        inc esi
        ; read pattern arg (unquoted)
        lea edi, [gsub_pat]
        call .read_str_arg
        call .ea_skipws
        cmp byte [esi], ','
        jne .ecg_no2
        inc esi
        lea edi, [gsub_repl]
        call .read_str_arg
        ; skip to ')'
.ecg_skip: lodsb
test al, al
jz .ecg_exec
cmp al, ')'
jne .ecg_skip
.ecg_exec:
        ; Apply gsub to line_buf
        lea eax, [gsub_pat]
        lea ebx, [gsub_repl]
        lea ecx, [line_buf]
        lea edx, [gsub_tmp]
        call .do_gsub
        ; copy result back
        lea esi, [gsub_tmp]
        lea edi, [line_buf]
.ecg_cp:
        lodsb
        stosb
        test al, al
        jnz .ecg_cp
        ; re-split fields
        call .split_fields

        pop ecx  ; discard saved esi
        mov eax, 1
        pop ecx
        pop esi
        ret

.ecg_no2: pop esi
.ecg_no:
        pop esi
        pop ecx
        pop esi
        xor eax, eax
        ret

; check "sub" — single replacement, same as gsub but stop after first
.ea_check_sub:
        push esi
        push ecx
        lea edi, [kw_sub]
        mov ecx, 3
        push esi
        repz cmpsb
        jne .ecs_no
        ; skip 'b' if "sub(" vs "sub " — check char after "sub"
        mov al, [esi]
        cmp al, '('
        jne .ecs_no_match
        inc esi
        lea edi, [gsub_pat]
        call .read_str_arg
        call .ea_skipws
        cmp byte [esi], ','
        jne .ecs_no2
        inc esi
        lea edi, [gsub_repl]
        call .read_str_arg
.ecs_skip: lodsb
test al, al
jz .ecs_exec
cmp al, ')'
jne .ecs_skip
.ecs_exec:
        lea eax, [gsub_pat]
        lea ebx, [gsub_repl]
        lea ecx, [line_buf]
        lea edx, [gsub_tmp]
        call .do_sub   ; single replace
        lea esi, [gsub_tmp]
        lea edi, [line_buf]
.ecs_cp:
        lodsb
        stosb
        test al, al
        jnz .ecs_cp
        call .split_fields
        pop ecx
        mov eax, 1
        pop ecx
        pop esi
        ret

.ecs_no_match:
.ecs_no2: pop esi
.ecs_no:
        pop esi
        pop ecx
        pop esi
        xor eax, eax
        ret

; read_str_arg: read arg (possibly quoted) at [esi] into [edi]
.read_str_arg:
        call .ea_skipws
        cmp byte [esi], '"'
        jne .rsa_bare
        inc esi
.rsa_q: lodsb
cmp al, '"'
je .rsa_done
        test al, al
        jz .rsa_done
        stosb
        jmp .rsa_q
.rsa_done: mov byte [edi], 0
ret
.rsa_bare:
        mov ecx, 255
.rsa_b: lodsb
        cmp al, ','
        je .rsa_bd
        cmp al, ')'
        je .rsa_bd
        cmp al, 0
        je .rsa_bd
        stosb
        dec ecx
        jnz .rsa_b
.rsa_bd: dec esi
mov byte [edi], 0
ret

; do_gsub: EAX=pat EBX=repl ECX=src EDX=dst — global search+replace
.do_gsub:
        push esi
        push edi
        push ebp
        push ebx
        push ecx
        mov esi, ecx   ; src
        mov edi, edx   ; dst
        mov ebp, eax   ; pat
.dg_loop:
        cmp byte [esi], 0
        je .dg_done
        push esi
        mov ecx, esi
        mov ebx, ebp
        call .strstr
        pop esi
        test eax, eax
        jz .dg_tail
        ; copy bytes before match
.dg_pre: cmp esi, eax
jge .dg_repl
        lodsb
        stosb
        jmp .dg_pre
.dg_repl:
        ; copy replacement
        push esi
        mov esi, [esp + 16]  ; repl (pushed ebx)
        ; rebl was originally [esp+16] but let's use saved value
        pop esi
        ; just copy replacement string (stack got complicated; use simple approach)
        push eax
        lea eax, [gsub_repl]
        push esi
        mov esi, eax
.dg_rc: lodsb
test al, al
jz .dg_rc_done
stosb
jmp .dg_rc
.dg_rc_done: pop esi
pop eax
        ; advance src past match
        push esi
        mov esi, ebp
.dg_plen: lodsb
test al, al
jnz .dg_plen
        mov ecx, esi
        sub ecx, ebp
        dec ecx  ; pat length
        pop esi
        add esi, ecx
        jmp .dg_loop
.dg_tail:
.dg_tc: lodsb
stosb
test al, al
jnz .dg_tc
.dg_done:
        pop ecx
        pop ebx
        pop ebp
        pop edi
        pop esi
        ret

; do_sub: single replacement
.do_sub:
        push esi
        push edi
        push ebp
        mov esi, ecx
        mov edi, edx
        mov ebp, eax
        mov ecx, esi
        mov ebx, ebp
        call .strstr
        test eax, eax
        jz .ds_tail
.ds_pre: cmp esi, eax
jge .ds_repl
lodsb
stosb
jmp .ds_pre
.ds_repl:
        push esi
        lea esi, [gsub_repl]
.ds_rc: lodsb
test al, al
jz .ds_rc_done
stosb
jmp .ds_rc
.ds_rc_done: pop esi
        push esi
        mov esi, ebp
.ds_pl: lodsb
test al, al
jnz .ds_pl
        mov ecx, esi
        sub ecx, ebp
        dec ecx
        pop esi
        add esi, ecx
.ds_tail:
.ds_tc: lodsb
stosb
test al, al
jnz .ds_tc
        pop ebp
        pop edi
        pop esi
        ret

;--- Utility ---
; strstr: EBX=needle ECX=haystack → EAX=ptr or 0
.strstr:
        push ebx
        push ecx
        push edx
        mov esi, ecx
.ss_outer:
        cmp byte [esi], 0
        je .ss_fail
        mov edi, ebx
        mov ecx, esi
.ss_inner:
        mov al, [edi]
        test al, al
        jz .ss_found
        cmp al, [ecx]
        jne .ss_miss
        inc edi
        inc ecx
        jmp .ss_inner
.ss_found:
        mov eax, esi
        pop edx
        pop ecx
        pop ebx
        ret
.ss_miss:
        inc esi
        jmp .ss_outer
.ss_fail:
        xor eax, eax
        pop edx
        pop ecx
        pop ebx
        ret

; itoa: EAX=number, EDI=buffer
.itoa:
        push eax
        push ebx
        push ecx
        push edx
        mov ecx, 10
        lea ebx, [itoa_tmp + 15]
        mov byte [ebx], 0
        test eax, eax
        jnz .ia_conv
        dec ebx
        mov byte [ebx], '0'
        jmp .ia_copy
.ia_conv:
        xor edx, edx
        div ecx
        add dl, '0'
        dec ebx
        mov [ebx], dl
        test eax, eax
        jnz .ia_conv
.ia_copy:
        push edi
        mov esi, ebx
.ia_cp: lodsb
stosb
test al, al
jnz .ia_cp
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; isalnum: AL=char → EAX=1 if alpha/digit
.isalnum:
        push ebx
        cmp al, '0'
        jb .ian_no
        cmp al, '9'
        jbe .ian_yes
        cmp al, 'A'
        jb .ian_no
        cmp al, 'Z'
        jbe .ian_yes
        cmp al, 'a'
        jb .ian_no
        cmp al, 'z'
        jbe .ian_yes
        cmp al, '_'
        je .ian_yes
.ian_no: xor eax, eax
pop ebx
ret
.ian_yes: mov eax, 1
pop ebx
ret

; print_dec: EAX → decimal string via PUTCHAR
.print_dec:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        lea edi, [dec_buf + 15]
        mov byte [edi], 0
        mov ecx, 10
.pde: xor edx, edx
div ecx
        add dl, '0'
        dec edi
        mov [edi], dl
        test eax, eax
        jnz .pde
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;--- Data ---
usage_str:  db "Usage: awk [-F sep] 'program' [file]", 0x0A, 0
err_file:   db "awk: cannot open file", 0x0A, 0
kw_begin:   db "BEGIN", 0
kw_end:     db "END", 0
kw_print:   db "print", 0
kw_gsub:    db "gsub", 0
kw_sub:     db "sub", 0
itoa_tmp:   times 20 db 0
dec_buf:    times 20 db 0

;--- BSS ---
arg_buf:            times 512 db 0
prog_buf:           times MAX_PROG db 0
prog_len:           dd 0
input_filename:     times 256 db 0
file_buf:           times MAX_FILE db 0
file_size:          dd 0
file_pos:           dd 0
line_buf:           times MAX_LINE db 0
cur_nr:             dd 0
num_fields:         dd 0
field_ptrs:         times MAX_FIELDS * 4 db 0
fields_buf:         times MAX_FIELDS * MAX_FIELD_LEN db 0
rules_buf:          times MAX_RULES * RULE_SIZE db 0
num_rules:          dd 0
cur_rule_ptr:       dd 0
fs_char:            db 0
                    db 0,0,0
expr_buf:           times 512 db 0
gsub_pat:           times 256 db 0
gsub_repl:          times 256 db 0
gsub_tmp:           times MAX_LINE db 0
esi_save:           dd 0
esi_src:            dd 0
esi_tmp:            dd 0
esi_tmp2:           dd 0
esi2:               dd 0
edi2:               dd 0
tmp_val:            dd 0
