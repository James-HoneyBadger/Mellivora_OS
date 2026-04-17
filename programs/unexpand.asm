; unexpand.asm - Convert spaces to tabs [HBU]
; Usage: unexpand [-t N] [file...]
; Default tab stop is 8. Reads from stdin if no file given.
; Only converts leading spaces (initial whitespace) to tabs.
;
%include "syscalls.inc"

MAX_FILE        equ 65536

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        mov [args_len], eax

        ; Defaults
        mov dword [tab_stop], 8
        mov dword [file_count], 0

        ; Parse arguments
        cmp eax, 0
        jle .read_stdin

        mov esi, args_buf
.parse:
        cmp byte [esi], 0
        je .files_ready
        cmp byte [esi], ' '
        jne .not_sp
        inc esi
        jmp .parse
.not_sp:
        cmp byte [esi], '-'
        jne .get_file
        inc esi
        cmp byte [esi], 't'
        jne show_usage
        inc esi
.skip_sp:
        cmp byte [esi], ' '
        jne .got_num
        inc esi
        jmp .skip_sp
.got_num:
        xor eax, eax
.pn_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .pn_done
        cmp dl, '9'
        ja .pn_done
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pn_loop
.pn_done:
        cmp eax, 0
        je show_usage
        mov [tab_stop], eax
        jmp .parse

.get_file:
        mov eax, [file_count]
        cmp eax, 8
        jge .parse
        mov [file_ptrs + rax*8], rsi
        inc dword [file_count]
.skip_word:
        cmp byte [esi], 0
        je .files_ready
        cmp byte [esi], ' '
        je .term_word
        inc esi
        jmp .skip_word
.term_word:
        mov byte [esi], 0
        inc esi
        jmp .parse

.files_ready:
        cmp dword [file_count], 0
        je .read_stdin

        xor ebx, ebx
.file_loop:
        cmp ebx, [file_count]
        jge .done
        push rbx
        mov eax, SYS_FREAD
        mov rbx, [file_ptrs + rbx*8]
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, -1
        je .file_err
        mov [file_len], eax
        call unexpand_buffer
        pop rbx
        inc ebx
        jmp .file_loop

.file_err:
        pop rbx
        push rbx
        mov eax, SYS_PRINT
        mov ebx, err_open
        int 0x80
        pop rbx
        push rbx
        mov eax, SYS_PRINT
        mov rbx, [file_ptrs + rbx*8]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rbx
        inc ebx
        jmp .file_loop

.read_stdin:
        mov eax, SYS_STDIN_READ
        mov ebx, file_buf
        int 0x80
        cmp eax, -1
        je .done
        cmp eax, 0
        je .done
        mov [file_len], eax
        call unexpand_buffer

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; unexpand_buffer: process file converting leading spaces to tabs
;---------------------------------------
unexpand_buffer:
        PUSHALL
        mov esi, file_buf
        mov ecx, [file_len]
        mov byte [at_line_start], 1
        xor edx, edx           ; column counter
        xor ebx, ebx           ; pending spaces count

.ub_loop:
        or ecx, ecx
        jz .ub_flush
        mov al, [esi]
        inc esi
        dec ecx

        cmp al, 0x0A
        je .ub_nl

        cmp byte [at_line_start], 0
        je .ub_nonlead

        ; At line start: collect spaces
        cmp al, ' '
        je .ub_space
        cmp al, 9              ; tab passthrough
        je .ub_tab_pass

        ; Non-space at start — flush pending, switch to non-leading
        call .flush_pending
        mov byte [at_line_start], 0
        jmp .ub_print_char

.ub_space:
        inc ebx                 ; pending spaces
        inc edx                 ; column
        ; Check if we hit a tab stop
        push rax
        mov eax, edx
        push rdx
        xor edx, edx
        push rcx
        mov ecx, [tab_stop]
        div ecx
        pop rcx
        cmp edx, 0             ; column % tab_stop == 0?
        pop rdx
        pop rax
        jne .ub_loop
        ; Emit a tab
        push rcx
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, 9
        int 0x80
        pop rdx
        pop rcx
        xor ebx, ebx           ; reset pending
        jmp .ub_loop

.ub_tab_pass:
        ; Flush any pending spaces first, then emit tab
        call .flush_pending
        push rcx
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, 9
        int 0x80
        pop rdx
        pop rcx
        ; Advance column to next tab stop
        push rax
        mov eax, edx
        push rdx
        xor edx, edx
        push rcx
        mov ecx, [tab_stop]
        div ecx
        pop rcx
        pop rdx
        pop rax
        ; Round up to next tab stop
        push rax
        push rdx
        mov eax, edx
        xor edx, edx
        push rcx
        mov ecx, [tab_stop]
        div ecx
        pop rcx
        ; next_stop = (col/tab + 1) * tab
        inc eax
        push rdx
        imul eax, [tab_stop]
        pop rdx
        pop rdx
        mov edx, eax
        pop rax
        jmp .ub_loop

.ub_nonlead:
        ; Non-leading chars: print as-is
.ub_print_char:
        push rcx
        push rdx
        push rbx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rbx
        pop rdx
        pop rcx
        inc edx
        jmp .ub_loop

.ub_nl:
        call .flush_pending
        push rcx
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rdx
        pop rcx
        xor edx, edx
        xor ebx, ebx
        mov byte [at_line_start], 1
        jmp .ub_loop

.ub_flush:
        call .flush_pending
        POPALL
        ret

; Flush pending spaces as literal spaces
.flush_pending:
        or ebx, ebx
        jz .fp_done
        push rcx
        push rdx
        mov ecx, ebx
.fp_loop:
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rcx
        loop .fp_loop
        pop rdx
        pop rcx
        xor ebx, ebx
.fp_done:
        ret

show_usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

section .data
usage_str:      db "Usage: unexpand [-t N] [file...]", 0x0A
                db "Convert leading spaces to tabs.", 0x0A
                db "  -t N   set tab stop to N (default: 8)", 0x0A, 0
err_open:       db "unexpand: cannot open ", 0

section .bss
args_buf:       resb 512
args_len:       resd 1
tab_stop:       resd 1
file_count:     resd 1
file_ptrs:      resq 8
file_buf:       resb MAX_FILE
file_len:       resd 1
at_line_start:  resb 1
