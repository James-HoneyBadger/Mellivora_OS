; man.asm - Manual page viewer for Mellivora OS
; Usage: man <topic>
; Looks up /docs/man/<topic>.txt and displays with pagination.

%include "syscalls.inc"

PAGE_LINES equ 23

start:
        ; Get topic argument
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Build path: "/docs/man/<topic>.txt"
        mov esi, arg_buf
        mov edi, path_buf

        ; Copy prefix
        push rsi
        mov esi, path_prefix
.cp_pfx:
        lodsb
        test al, al
        jz .pfx_done
        stosb
        jmp .cp_pfx
.pfx_done:
        pop rsi

        ; Copy topic name
.cp_topic:
        lodsb
        cmp al, ' '
        je .topic_done
        test al, al
        jz .topic_done
        stosb
        jmp .cp_topic
.topic_done:
        ; Append ".txt\0"
        mov dword [edi], '.txt'
        mov byte [edi + 4], 0

        ; Read the man page file
        mov eax, SYS_FREAD
        mov ebx, path_buf
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .not_found
        mov [file_size], eax

        ; Null-terminate
        mov edi, file_buf
        add edi, eax
        mov byte [edi], 0

        ; Page through file
        mov esi, file_buf
        xor ecx, ecx           ; line count

.page_loop:
        cmp byte [esi], 0
        je .eof

.print_char:
        mov al, [esi]
        test al, al
        jz .eof
        cmp al, 0x0A
        je .newline

        ; Print character
        push rcx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        inc esi
        jmp .print_char

.newline:
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rcx
        inc esi
        inc ecx
        cmp ecx, PAGE_LINES
        jl .page_loop

        ; Show pager prompt
        push rsi
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70           ; Inverse video
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_more
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Wait for key
        mov eax, SYS_GETCHAR
        int 0x80
        pop rsi
        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        cmp al, 27             ; Esc
        je .quit

        ; Clear prompt line
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0D
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_blank
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0D
        int 0x80
        pop rax

        cmp al, ' '
        je .next_page
        ; Single line on any other key
        mov ecx, PAGE_LINES - 1
        jmp .page_loop

.next_page:
        xor ecx, ecx
        jmp .page_loop

.eof:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
.quit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.not_found:
        ; Print "No manual entry for <topic>"
        mov eax, SYS_PRINT
        mov ebx, msg_no_entry
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, arg_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; Data
path_prefix:    db "/docs/man-", 0
msg_usage:      db "Usage: man <topic>", 0x0A
                db "Topics: shell, syscalls, editor, fs, asm,", 0x0A
                db "        networking, compression, crypto,", 0x0A
                db "        games, admin, texttools, gui,", 0x0A
                db "        debugging, scripting, ipc,", 0x0A
                db "        multimedia", 0x0A, 0
msg_no_entry:   db "No manual entry for ", 0
msg_more:       db " -- MANUAL -- (Space=page, q=quit) ", 0
msg_blank:      db "                                      ", 0

arg_buf:        times 256 db 0
path_buf:       times 280 db 0
file_size:      dd 0
file_buf:       times 32768 db 0
