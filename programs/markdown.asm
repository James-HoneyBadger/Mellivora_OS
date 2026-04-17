; ==========================================================================
; markdown - Terminal Markdown renderer for Mellivora OS
; Usage: markdown <file.md>
; Renders Markdown files with VGA colors:
;   # H1 = bright white, ## H2 = bright cyan, ### H3 = bright green
;   **bold** = bright yellow, *italic* = bright magenta
;   `code` = bright red on dark, ```code blocks``` = cyan
;   - lists = bullet prefix, > blockquote = blue prefix
;   --- = horizontal rule
; ==========================================================================

%include "syscalls.inc"

; VGA color constants
C_DEFAULT   equ 0x07            ; light gray on black
C_H1        equ 0x0F            ; bright white
C_H2        equ 0x0B            ; bright cyan
C_H3        equ 0x0A            ; bright green
C_BOLD      equ 0x0E            ; bright yellow
C_ITALIC    equ 0x0D            ; bright magenta
C_CODE      equ 0x0C            ; bright red
C_CODEBLK   equ 0x03            ; cyan
C_QUOTE     equ 0x09            ; bright blue
C_LIST      equ 0x06            ; brown/dark yellow
C_RULE      equ 0x08            ; dark gray

MAX_FILE    equ 60000
MAX_LINE    equ 256

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80
        cmp byte [arg_buf], 0
        je .usage

        ; Load file
        mov eax, SYS_FREAD
        mov ebx, arg_buf
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jle .err_read

        mov [file_len], eax
        mov byte [file_buf + eax], 0

        ; Process file line by line
        mov esi, file_buf
        mov byte [in_code_block], 0

.next_line:
        cmp esi, file_buf
        jb .done
        mov eax, file_buf
        add eax, [file_len]
        cmp esi, eax
        jge .done

        ; Extract line into line_buf
        mov edi, line_buf
        xor ecx, ecx           ; line length
.copy_line:
        lodsb
        cmp al, 10
        je .line_ready
        cmp al, 0
        je .line_eof
        stosb
        inc ecx
        cmp ecx, MAX_LINE - 1
        jl .copy_line
.line_ready:
        mov byte [edi], 0
        jmp .process_line
.line_eof:
        mov byte [edi], 0
        dec esi                 ; back up so we detect end

.process_line:
        mov [line_len_val], ecx
        mov edi, line_buf

        ; Check for code block toggle (```)
        cmp byte [edi], '`'
        jne .not_code_toggle
        cmp byte [edi + 1], '`'
        jne .not_code_toggle
        cmp byte [edi + 2], '`'
        jne .not_code_toggle
        xor byte [in_code_block], 1
        ; Print blank line as separator
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .next_line

.not_code_toggle:
        ; If in code block, print as-is in code color
        cmp byte [in_code_block], 0
        je .not_in_code
        mov eax, SYS_SETCOLOR
        mov ebx, C_CODEBLK
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        jmp .next_line

.not_in_code:
        ; Empty line?
        cmp byte [edi], 0
        jne .not_empty
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .next_line

.not_empty:
        ; Check for horizontal rule (--- or *** or ___)
        cmp byte [edi], '-'
        je .check_rule_dash
        cmp byte [edi], '*'
        je .check_rule_star
        cmp byte [edi], '_'
        je .check_rule_under
        jmp .not_rule

.check_rule_dash:
        cmp byte [edi + 1], '-'
        jne .not_rule
        cmp byte [edi + 2], '-'
        jne .not_rule
        jmp .draw_rule
.check_rule_star:
        cmp byte [edi + 1], '*'
        jne .not_rule
        cmp byte [edi + 2], '*'
        jne .check_bold_start
        ; Could be *** rule if nothing else follows
        cmp byte [edi + 3], 0
        je .draw_rule
        cmp byte [edi + 3], '*'
        je .draw_rule
        jmp .not_rule
.check_rule_under:
        cmp byte [edi + 1], '_'
        jne .not_rule
        cmp byte [edi + 2], '_'
        jne .not_rule

.draw_rule:
        mov eax, SYS_SETCOLOR
        mov ebx, C_RULE
        int 0x80
        mov ecx, 76
.rule_loop:
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, 0xC4           ; horizontal line char
        int 0x80
        pop rcx
        loop .rule_loop
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        jmp .next_line

.check_bold_start:
.not_rule:
        ; Check for headers (# ## ###)
        cmp byte [edi], '#'
        jne .not_header
        cmp byte [edi + 1], '#'
        jne .h1
        cmp byte [edi + 2], '#'
        jne .h2
        ; H3
        mov eax, SYS_SETCOLOR
        mov ebx, C_H3
        int 0x80
        add edi, 3
        jmp .skip_header_space
.h2:
        mov eax, SYS_SETCOLOR
        mov ebx, C_H2
        int 0x80
        add edi, 2
        jmp .skip_header_space
.h1:
        mov eax, SYS_SETCOLOR
        mov ebx, C_H1
        int 0x80
        inc edi
.skip_header_space:
        cmp byte [edi], ' '
        jne .print_header
        inc edi
.print_header:
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        jmp .next_line

.not_header:
        ; Check for blockquote (> )
        cmp byte [edi], '>'
        jne .not_quote
        inc edi
        cmp byte [edi], ' '
        jne .print_quote
        inc edi
.print_quote:
        mov eax, SYS_SETCOLOR
        mov ebx, C_QUOTE
        int 0x80
        push rdi
        mov eax, SYS_PRINT
        mov ebx, quote_prefix
        int 0x80
        pop rdi
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        jmp .next_line

.not_quote:
        ; Check for list items (- or * followed by space)
        cmp byte [edi], '-'
        je .check_list
        cmp byte [edi], '*'
        je .check_list
        jmp .not_list
.check_list:
        cmp byte [edi + 1], ' '
        jne .not_list
        add edi, 2
        mov eax, SYS_SETCOLOR
        mov ebx, C_LIST
        int 0x80
        push rdi
        mov eax, SYS_PRINT
        mov ebx, bullet_prefix
        int 0x80
        pop rdi
        ; Render rest of line with inline formatting
        jmp .render_inline

.not_list:
        ; Regular paragraph — render with inline formatting
.render_inline:
        ; Walk through line char by char, handling **bold**, *italic*, `code`
        mov esi, edi

.inline_loop:
        lodsb
        test al, al
        jz .inline_done

        ; Check for backtick (inline code)
        cmp al, '`'
        je .inline_code

        ; Check for ** (bold)
        cmp al, '*'
        jne .inline_normal
        cmp byte [esi], '*'
        je .inline_bold

        ; Single * = italic
        mov eax, SYS_SETCOLOR
        mov ebx, C_ITALIC
        int 0x80
.italic_loop:
        lodsb
        test al, al
        jz .inline_done
        cmp al, '*'
        je .italic_end
        push rax
        mov eax, SYS_PUTCHAR
        movzx ebx, al
        pop rax
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .italic_loop
.italic_end:
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        jmp .inline_loop

.inline_bold:
        inc esi                 ; skip second *
        mov eax, SYS_SETCOLOR
        mov ebx, C_BOLD
        int 0x80
.bold_loop:
        lodsb
        test al, al
        jz .inline_done
        cmp al, '*'
        jne .bold_char
        cmp byte [esi], '*'
        jne .bold_char
        inc esi                 ; skip closing **
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        jmp .inline_loop
.bold_char:
        mov eax, SYS_PUTCHAR
        movzx ebx, al
        int 0x80
        jmp .bold_loop

.inline_code:
        mov eax, SYS_SETCOLOR
        mov ebx, C_CODE
        int 0x80
.code_loop:
        lodsb
        test al, al
        jz .inline_done
        cmp al, '`'
        je .code_end
        mov eax, SYS_PUTCHAR
        movzx ebx, al
        int 0x80
        jmp .code_loop
.code_end:
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        jmp .inline_loop

.inline_normal:
        mov eax, SYS_PUTCHAR
        movzx ebx, al
        int 0x80
        jmp .inline_loop

.inline_done:
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .next_line

.done:
        ; Reset color
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        jmp .exit

.err_read:
        mov eax, SYS_PRINT
        mov ebx, err_file
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ---- Data ----
msg_usage:      db 'Usage: markdown <file.md>', 10, 0
err_file:       db 'Error: cannot read file.', 10, 0
quote_prefix:   db '  | ', 0
bullet_prefix:  db '  ', 0xF9, ' ', 0  ; middle dot bullet

; ---- BSS ----
in_code_block:  db 0
arg_buf:        times 256 db 0
line_buf:       times 260 db 0
line_len_val:   dd 0
file_len:       dd 0
file_buf:       times 61440 db 0
