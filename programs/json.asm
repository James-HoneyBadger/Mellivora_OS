; json.asm — JSON pretty-printer for Mellivora OS
;
; Reads a compact JSON string and outputs it with 2-space indentation.
; Uses a single-pass state machine: no heap allocation, no recursion.
;
; Accepts optional filename argument; falls back to a hardcoded demo.
;
; Build:  nasm -f bin -O0 json.asm -o json.bin
; Run:    json [file.json]

%include "syscalls.inc"

; Parser states
ST_VALUE  equ 0     ; expecting a value (or start of container)
ST_KEY    equ 1     ; inside an object, expecting a key or '}'
ST_STRING equ 2     ; inside a "string"
ST_ESCAPE equ 3     ; after backslash inside a string
ST_BARE   equ 4     ; bare literal: number / true / false / null

INDENT_UNIT equ 2   ; spaces per indent level
MAX_DEPTH   equ 32  ; max nesting depth
FBUF_SIZE   equ 8192

;=======================================================================
start:
        mov eax, SYS_TASKNAME
        mov ebx, tname
        int 0x80

        ; Try to load a file from args
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        jle .use_demo

        ; Skip leading spaces
        mov esi, arg_buf
.skip_sp:
        cmp byte [esi], ' '
        jne .try_load
        inc esi
        jmp .skip_sp

.try_load:
        cmp byte [esi], 0
        je .use_demo

        ; Load file into file_buf
        mov eax, SYS_FREAD
        mov ebx, esi            ; filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .load_err
        cmp eax, 0
        je .load_err
        mov [json_len], eax
        mov dword [json_ptr], file_buf
        jmp .run

.load_err:
        mov eax, SYS_PRINT
        mov ebx, err_load
        int 0x80
        ; fall through to demo

.use_demo:
        mov esi, demo_json
        mov edi, file_buf
.copy_demo:
        lodsb
        stosb
        test al, al
        jnz .copy_demo
        ; Set length = strlen(demo_json)
        mov eax, edi
        sub eax, file_buf
        dec eax                 ; subtract null
        mov [json_len], eax
        mov dword [json_ptr], file_buf

.run:
        call pretty_print

        ; Trailing newline
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; pretty_print — format JSON at [json_ptr] / [json_len]
;=======================================================================
pretty_print:
        pushad
        mov dword [pp_pos],    0
        mov dword [pp_depth],  0
        mov dword [pp_state],  ST_VALUE
        ; container_type stack: 0=object, 1=array
        ; first_item stack: 1 means we haven't printed any items yet
        ; We combine them: stack bytes alternate type/first per depth.
        ; Use pp_ctype[depth] for '{' vs '[' and pp_first[depth] for comma.
        ; Both reset at push and used at pop.

.pp_loop:
        ; Fetch next character
        mov eax, [pp_pos]
        cmp eax, [json_len]
        jge .pp_done

        mov esi, [json_ptr]
        movzx ebx, byte [esi + eax]   ; ebx = current char
        add dword [pp_pos], 1

        ; ---- State: IN_STRING ----------------------------------------
        mov eax, [pp_state]
        cmp eax, ST_STRING
        jne .not_string

        cmp bl, '"'
        jne .str_check_esc
        call emit_char          ; emit closing "
        mov dword [pp_state], ST_VALUE
        jmp .pp_loop
.str_check_esc:
        cmp bl, '\'
        jne .str_literal
        call emit_char
        mov dword [pp_state], ST_ESCAPE
        jmp .pp_loop
.str_literal:
        call emit_char
        jmp .pp_loop

        ; ---- State: IN_ESCAPE ----------------------------------------
.not_string:
        cmp eax, ST_ESCAPE
        jne .not_escape
        call emit_char
        mov dword [pp_state], ST_STRING
        jmp .pp_loop

        ; ---- State: BARE (number / true / false / null) --------------
.not_escape:
        cmp eax, ST_BARE
        jne .not_bare
        ; End of bare literal on any structural char or whitespace
        cmp bl, ','
        je  .bare_end
        cmp bl, '}'
        je  .bare_end
        cmp bl, ']'
        je  .bare_end
        cmp bl, 0x20
        jle .bare_end_no_replay
        call emit_char
        jmp .pp_loop
.bare_end_no_replay:
        mov dword [pp_state], ST_VALUE
        jmp .pp_loop
.bare_end:
        mov dword [pp_state], ST_VALUE
        ; fall through to VALUE handling with the structural char

        ; ---- State: VALUE / KEY (structural chars) -------------------
.not_bare:
        ; Skip whitespace in input
        cmp bl, 0x20
        jle .pp_loop

        ; Open brace / bracket
        cmp bl, '{'
        je  .open_brace
        cmp bl, '['
        je  .open_bracket

        ; Close brace / bracket
        cmp bl, '}'
        je  .close_brace
        cmp bl, ']'
        je  .close_bracket

        ; Colon
        cmp bl, ':'
        je  .colon

        ; Comma
        cmp bl, ','
        je  .comma

        ; Quote → string
        cmp bl, '"'
        je  .open_string

        ; Otherwise: bare literal (number / keyword)
        call emit_char
        mov dword [pp_state], ST_BARE
        jmp .pp_loop

        ; -- '{' --
.open_brace:
        ; If not first item in current container, print comma+newline+indent
        call maybe_comma
        call emit_char          ; '{'
        ; Push context
        mov eax, [pp_depth]
        mov byte [pp_ctype + eax], '{'
        mov byte [pp_first + eax], 1
        inc dword [pp_depth]
        call emit_newline_indent
        mov dword [pp_state], ST_KEY
        jmp .pp_loop

        ; -- '[' --
.open_bracket:
        call maybe_comma
        call emit_char          ; '['
        mov eax, [pp_depth]
        mov byte [pp_ctype + eax], '['
        mov byte [pp_first + eax], 1
        inc dword [pp_depth]
        call emit_newline_indent
        mov dword [pp_state], ST_VALUE
        jmp .pp_loop

        ; -- '}' --
.close_brace:
        dec dword [pp_depth]
        call emit_newline_indent
        call emit_char          ; '}'
        mov dword [pp_state], ST_VALUE
        jmp .pp_loop

        ; -- ']' --
.close_bracket:
        dec dword [pp_depth]
        call emit_newline_indent
        call emit_char          ; ']'
        mov dword [pp_state], ST_VALUE
        jmp .pp_loop

        ; -- ':' --
.colon:
        mov bl, ':'
        call emit_char
        mov bl, ' '
        call emit_char
        mov dword [pp_state], ST_VALUE
        jmp .pp_loop

        ; -- ',' --
.comma:
        call emit_char          ; ','
        call emit_newline_indent
        ; After comma in object: expect key; in array: expect value
        mov eax, [pp_depth]
        dec eax
        cmp byte [pp_ctype + eax], '{'
        je  .comma_obj
        mov dword [pp_state], ST_VALUE
        jmp .pp_loop
.comma_obj:
        mov dword [pp_state], ST_KEY
        jmp .pp_loop

        ; -- '"' --
.open_string:
        call maybe_comma
        call emit_char          ; '"'
        mov dword [pp_state], ST_STRING
        jmp .pp_loop

.pp_done:
        popad
        ret

;=======================================================================
; maybe_comma — if we are not the first item in a container, print comma
; and newline+indent before this item.  Mark item as no longer first.
;=======================================================================
maybe_comma:
        ; Use depth-1 for current container
        ; (depth was already incremented when the container opened)
        ; Actually depth points to the NEXT free slot; depth-1 is current.
        ; But we're called BEFORE incrementing for '{', so depth IS current.
        ; Hmm — actually we call maybe_comma for values that appear INSIDE
        ; the current container.  pp_depth is the depth of the current
        ; container (0 = top level).
        push eax
        push ebx
        mov eax, [pp_depth]
        cmp eax, 0
        je .mc_done             ; top level: no comma needed

        dec eax                 ; index of current container
        cmp byte [pp_first + eax], 1
        je  .mc_first_item
        ; Not first: emit comma + newline + indent
        mov bl, ','
        call emit_char
        call emit_newline_indent
        jmp .mc_done
.mc_first_item:
        mov byte [pp_first + eax], 0
.mc_done:
        pop ebx
        pop eax
        ret

;=======================================================================
; emit_newline_indent — print newline then (pp_depth * INDENT_UNIT) spaces
;=======================================================================
emit_newline_indent:
        push eax
        push ebx
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ; spaces
        mov ecx, [pp_depth]
        imul ecx, INDENT_UNIT
.sp_loop:
        test ecx, ecx
        jz .sp_done
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec ecx
        jmp .sp_loop
.sp_done:
        pop ecx
        pop ebx
        pop eax
        ret

;=======================================================================
; emit_char — print character in BL
;=======================================================================
emit_char:
        push eax
        push ebx
        movzx ebx, bl
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ebx
        pop eax
        ret

;=======================================================================
; DATA
;=======================================================================
tname:    db "json", 0
err_load: db "json: cannot read file", 0x0A, 0

demo_json:
        db '{"name":"Mellivora","version":"12.3.0","arch":{"bits":32,'
        db '"target":"x86","asm":"NASM"},"features":["multitasking",'
        db '"networking","vbe","audio","filesys"],"stable":true,'
        db '"year":2026}', 0

;=======================================================================
; BSS
;=======================================================================
section .bss
pp_pos:         resd 1
pp_depth:       resd 1
pp_state:       resd 1
pp_ctype:       resb MAX_DEPTH      ; container type per level: '{' or '['
pp_first:       resb MAX_DEPTH      ; 1 if no items emitted yet at this depth

json_ptr:       resd 1
json_len:       resd 1

arg_buf:        resb 256
file_buf:       resb FBUF_SIZE
