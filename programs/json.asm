; ==========================================================================
; json - JSON pretty-printer for Mellivora OS
;
; Usage: json <filename>       Pretty-print a JSON file
;        json -c <filename>    Compact (minify) a JSON file
;        json -v <filename>    Validate only (no output)
;
; Handles objects, arrays, strings, numbers, true/false/null.
; Indentation: 2 spaces per level.
; ==========================================================================
%include "syscalls.inc"

MAX_FILE_SIZE   equ 32768
MAX_DEPTH       equ 32
INDENT_SIZE     equ 2

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz show_usage

        ; Parse flags
        mov esi, arg_buf
        mov byte [mode], 0      ; 0=pretty, 1=compact, 2=validate
        cmp byte [esi], '-'
        jne .got_filename

        ; Check flag
        cmp word [esi], '-c'
        jne .not_compact
        mov byte [mode], 1
        jmp .skip_flag
.not_compact:
        cmp word [esi], '-v'
        jne show_usage
        mov byte [mode], 2
.skip_flag:
        ; Skip flag and space to get filename
        add esi, 2
.skip_sp:
        cmp byte [esi], ' '
        jne .got_fname2
        inc esi
        jmp .skip_sp
.got_fname2:
        cmp byte [esi], 0
        je show_usage
        jmp .load_file
.got_filename:
        ; ESI already points to filename
.load_file:
        ; Read file
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .file_err
        test eax, eax
        jz .file_err
        mov [file_len], eax

        ; Null-terminate
        mov edi, file_buf
        add edi, eax
        mov byte [edi], 0

        ; Parse/format
        mov esi, file_buf
        mov dword [depth], 0
        mov dword [pos], 0
        call skip_ws
        call json_value
        test eax, eax
        jnz .parse_err

        ; Validate mode: print OK
        cmp byte [mode], 2
        jne .add_newline
        mov eax, SYS_PRINT
        mov ebx, msg_valid
        int 0x80
        jmp .exit_ok

.add_newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
.exit_ok:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_file_err
        int 0x80
        jmp .exit_fail

.parse_err:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_parse_err
        int 0x80
.exit_fail:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; -------------------------------------------------------------------
; json_value - Parse and output one JSON value
; ESI = current position in input
; Returns: EAX=0 success, EAX=-1 error, ESI advanced
; -------------------------------------------------------------------
json_value:
        call skip_ws
        cmp byte [esi], 0
        je jv_err
        cmp byte [esi], '"'
        je json_string
        cmp byte [esi], '{'
        je json_object
        cmp byte [esi], '['
        je json_array
        cmp byte [esi], 't'
        je json_true
        cmp byte [esi], 'f'
        je json_false
        cmp byte [esi], 'n'
        je json_null
        ; Must be a number (digit or -)
        cmp byte [esi], '-'
        je json_number
        cmp byte [esi], '0'
        jb jv_err
        cmp byte [esi], '9'
        ja jv_err
        jmp json_number
jv_err:
        mov eax, -1
        ret

; -------------------------------------------------------------------
; json_string - Parse and output a JSON string
; -------------------------------------------------------------------
json_string:
        cmp byte [esi], '"'
        jne jv_err
        call emit_char          ; emit opening "
        inc esi
.js_loop:
        lodsb
        test al, al
        jz jv_err
        cmp al, '\'
        je .js_escape
        cmp al, '"'
        je .js_end
        call emit_al
        jmp .js_loop
.js_escape:
        call emit_al            ; emit backslash
        lodsb
        test al, al
        jz jv_err
        call emit_al            ; emit escaped char
        jmp .js_loop
.js_end:
        mov al, '"'
        call emit_al
        xor eax, eax
        ret

; -------------------------------------------------------------------
; json_number - Parse and output a JSON number
; -------------------------------------------------------------------
json_number:
        cmp byte [esi], '-'
        jne .jn_digits
        lodsb
        call emit_al
.jn_digits:
        ; Integer part
.jn_int:
        cmp byte [esi], '0'
        jb .jn_frac
        cmp byte [esi], '9'
        ja .jn_frac
        lodsb
        call emit_al
        jmp .jn_int
.jn_frac:
        cmp byte [esi], '.'
        jne .jn_exp
        lodsb
        call emit_al
.jn_frac_d:
        cmp byte [esi], '0'
        jb .jn_exp
        cmp byte [esi], '9'
        ja .jn_exp
        lodsb
        call emit_al
        jmp .jn_frac_d
.jn_exp:
        cmp byte [esi], 'e'
        je .jn_exp_go
        cmp byte [esi], 'E'
        jne .jn_done
.jn_exp_go:
        lodsb
        call emit_al
        cmp byte [esi], '+'
        je .jn_exp_sign
        cmp byte [esi], '-'
        jne .jn_exp_d
.jn_exp_sign:
        lodsb
        call emit_al
.jn_exp_d:
        cmp byte [esi], '0'
        jb .jn_done
        cmp byte [esi], '9'
        ja .jn_done
        lodsb
        call emit_al
        jmp .jn_exp_d
.jn_done:
        xor eax, eax
        ret

; -------------------------------------------------------------------
; json_true / json_false / json_null
; -------------------------------------------------------------------
json_true:
        cmp dword [esi], 'true'
        jne jv_err
        mov eax, SYS_PRINT
        mov ebx, str_true
        cmp byte [mode], 2
        jne .jt_out
        add esi, 4
        xor eax, eax
        ret
.jt_out:
        int 0x80
        add esi, 4
        xor eax, eax
        ret

json_false:
        cmp dword [esi], 'fals'
        jne jv_err
        cmp byte [esi + 4], 'e'
        jne jv_err
        mov eax, SYS_PRINT
        mov ebx, str_false
        cmp byte [mode], 2
        jne .jf_out
        add esi, 5
        xor eax, eax
        ret
.jf_out:
        int 0x80
        add esi, 5
        xor eax, eax
        ret

json_null:
        cmp dword [esi], 'null'
        jne jv_err
        mov eax, SYS_PRINT
        mov ebx, str_null
        cmp byte [mode], 2
        jne .jn_out2
        add esi, 4
        xor eax, eax
        ret
.jn_out2:
        int 0x80
        add esi, 4
        xor eax, eax
        ret

; -------------------------------------------------------------------
; json_object - Parse and output { key: value, ... }
; -------------------------------------------------------------------
json_object:
        cmp dword [depth], MAX_DEPTH
        jge jv_err
        inc esi                 ; skip '{'
        call emit_brace_open

        inc dword [depth]
        call skip_ws

        cmp byte [esi], '}'
        je .jo_end

.jo_pair:
        ; Newline + indent
        call emit_newline_indent

        ; Key (must be string)
        cmp byte [esi], '"'
        jne jv_err
        call json_string
        test eax, eax
        jnz .jo_ret

        call skip_ws
        cmp byte [esi], ':'
        jne jv_err
        inc esi

        ; ": " or ":"
        call emit_colon

        call skip_ws
        call json_value
        test eax, eax
        jnz .jo_ret

        call skip_ws
        cmp byte [esi], ','
        jne .jo_close
        inc esi
        call emit_comma
        call skip_ws
        jmp .jo_pair

.jo_close:
        cmp byte [esi], '}'
        jne jv_err
.jo_end:
        dec dword [depth]
        inc esi
        ; Newline + indent before closing brace (pretty mode only)
        cmp byte [mode], 0
        jne .jo_emit_close
        call emit_newline_indent
.jo_emit_close:
        mov al, '}'
        call emit_al
        xor eax, eax
.jo_ret:
        ret

; -------------------------------------------------------------------
; json_array - Parse and output [ value, ... ]
; -------------------------------------------------------------------
json_array:
        cmp dword [depth], MAX_DEPTH
        jge jv_err
        inc esi                 ; skip '['
        call emit_bracket_open

        inc dword [depth]
        call skip_ws

        cmp byte [esi], ']'
        je .ja_end

.ja_elem:
        call emit_newline_indent

        call json_value
        test eax, eax
        jnz .ja_ret

        call skip_ws
        cmp byte [esi], ','
        jne .ja_close
        inc esi
        call emit_comma
        call skip_ws
        jmp .ja_elem

.ja_close:
        cmp byte [esi], ']'
        jne jv_err
.ja_end:
        dec dword [depth]
        inc esi
        cmp byte [mode], 0
        jne .ja_emit_close
        call emit_newline_indent
.ja_emit_close:
        mov al, ']'
        call emit_al
        xor eax, eax
.ja_ret:
        ret

; -------------------------------------------------------------------
; Emit helpers (respect mode: 0=pretty, 1=compact, 2=validate)
; -------------------------------------------------------------------
emit_char:
        cmp byte [mode], 2
        je .ec_skip
        mov al, [esi]
        push rax
        push rbx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rbx
        pop rax
.ec_skip:
        ret

emit_al:
        cmp byte [mode], 2
        je .ea_skip
        push rax
        push rbx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rbx
        pop rax
.ea_skip:
        ret

emit_brace_open:
        cmp byte [mode], 2
        je .ebo_skip
        push rax
        push rbx
        mov eax, SYS_PUTCHAR
        mov ebx, '{'
        int 0x80
        pop rbx
        pop rax
.ebo_skip:
        ret

emit_bracket_open:
        cmp byte [mode], 2
        je .ebko_skip
        push rax
        push rbx
        mov eax, SYS_PUTCHAR
        mov ebx, '['
        int 0x80
        pop rbx
        pop rax
.ebko_skip:
        ret

emit_colon:
        cmp byte [mode], 2
        je .eco_skip
        push rax
        push rbx
        cmp byte [mode], 0
        jne .eco_compact
        ; Pretty: ": "
        mov eax, SYS_PRINT
        mov ebx, str_colon_sp
        int 0x80
        jmp .eco_done
.eco_compact:
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
.eco_done:
        pop rbx
        pop rax
.eco_skip:
        ret

emit_comma:
        cmp byte [mode], 2
        je .ecm_skip
        cmp byte [mode], 0
        jne .ecm_compact
        ; Pretty mode: comma only (newline+indent done by caller)
        push rax
        push rbx
        mov eax, SYS_PUTCHAR
        mov ebx, ','
        int 0x80
        pop rbx
        pop rax
        ret
.ecm_compact:
        push rax
        push rbx
        mov eax, SYS_PUTCHAR
        mov ebx, ','
        int 0x80
        pop rbx
        pop rax
.ecm_skip:
        ret

emit_newline_indent:
        cmp byte [mode], 0
        jne .eni_skip
        push rax
        push rbx
        push rcx
        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        ; Indent: depth * INDENT_SIZE spaces
        mov ecx, [depth]
        imul ecx, INDENT_SIZE
.eni_loop:
        test ecx, ecx
        jz .eni_done
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec ecx
        jmp .eni_loop
.eni_done:
        pop rcx
        pop rbx
        pop rax
.eni_skip:
        ret

; -------------------------------------------------------------------
; skip_ws - Skip whitespace at ESI
; -------------------------------------------------------------------
skip_ws:
        cmp byte [esi], ' '
        je .sw_next
        cmp byte [esi], 0x09    ; tab
        je .sw_next
        cmp byte [esi], 0x0A    ; LF
        je .sw_next
        cmp byte [esi], 0x0D    ; CR
        je .sw_next
        ret
.sw_next:
        inc esi
        jmp skip_ws

; -------------------------------------------------------------------
show_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -------------------------------------------------------------------
; Strings
; -------------------------------------------------------------------
msg_usage:      db "Usage: json [-c|-v] <filename>", 0x0A
                db "  -c  Compact/minify output", 0x0A
                db "  -v  Validate only (no output)", 0x0A, 0
msg_file_err:   db "json: cannot read file", 0x0A, 0
msg_parse_err:  db "json: parse error", 0x0A, 0
msg_valid:      db "json: valid", 0x0A, 0
str_true:       db "true", 0
str_false:      db "false", 0
str_null:       db "null", 0
str_colon_sp:   db ": ", 0

; -------------------------------------------------------------------
; BSS
; -------------------------------------------------------------------
mode:           db 0
depth:          dd 0
pos:            dd 0
file_len:       dd 0
arg_buf:        times 256 db 0
file_buf:       times (MAX_FILE_SIZE + 1) db 0
