; expand.asm - Convert tabs to spaces [HBU]
; Usage: expand [-t N] [file...]
; Default tab stop is 8. Reads from stdin if no file given.
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
        ; Skip spaces
.skip_sp:
        cmp byte [esi], ' '
        jne .got_num
        inc esi
        jmp .skip_sp
.got_num:
        ; Parse number
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
        ; Save pointer to filename
        mov eax, [file_count]
        cmp eax, 8
        jge .parse              ; max 8 files
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

        ; Process each file
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
        call expand_buffer
        pop rbx
        inc ebx
        jmp .file_loop

.file_err:
        pop rbx
        push rbx
        mov eax, SYS_PRINT
        mov ebx, err_open
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, [rsp]
        mov rbx, [file_ptrs + rbx*8]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rbx
        inc ebx
        jmp .file_loop

.read_stdin:
        ; Read from stdin
        mov eax, SYS_STDIN_READ
        mov ebx, file_buf
        int 0x80
        cmp eax, -1
        je .done
        cmp eax, 0
        je .done
        mov [file_len], eax
        call expand_buffer

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; expand_buffer: process file_buf[0..file_len) converting tabs
;---------------------------------------
expand_buffer:
        PUSHALL
        mov esi, file_buf
        mov ecx, [file_len]
        xor edx, edx           ; column counter
.eb_loop:
        or ecx, ecx
        jz .eb_done
        mov al, [esi]
        inc esi
        dec ecx
        cmp al, 9              ; TAB?
        je .eb_tab
        cmp al, 0x0A           ; newline?
        je .eb_nl
        ; Regular character - print it
        push rcx
        push rdx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rdx
        pop rcx
        inc edx
        jmp .eb_loop

.eb_tab:
        ; Calculate spaces to next tab stop
        mov eax, edx            ; current column
        xor ebx, ebx
        push rcx
        mov ecx, [tab_stop]
        ; spaces = tab_stop - (col % tab_stop)
        push rdx
        xor edx, edx
        div ecx                 ; EAX=col/tab_stop, EDX=col%tab_stop
        mov eax, ecx
        sub eax, edx            ; spaces = tab_stop - remainder
        pop rdx
        mov ebx, eax            ; EBX = spaces to print
        pop rcx
.eb_spaces:
        or ebx, ebx
        jz .eb_loop
        push rcx
        push rdx
        push rbx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rbx
        pop rdx
        pop rcx
        inc edx
        dec ebx
        jmp .eb_spaces

.eb_nl:
        push rcx
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rdx
        pop rcx
        xor edx, edx           ; reset column
        jmp .eb_loop

.eb_done:
        POPALL
        ret

show_usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

section .data
usage_str:      db "Usage: expand [-t N] [file...]", 0x0A
                db "Convert tabs to spaces.", 0x0A
                db "  -t N   set tab stop to N (default: 8)", 0x0A, 0
err_open:       db "expand: cannot open ", 0

section .bss
args_buf:       resb 512
args_len:       resd 1
tab_stop:       resd 1
file_count:     resd 1
file_ptrs:      resq 8
file_buf:       resb MAX_FILE
file_len:       resd 1
