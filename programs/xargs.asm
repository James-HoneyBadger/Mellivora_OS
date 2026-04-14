; xargs.asm - Build and execute commands from stdin
; Usage: cmd | xargs COMMAND
; Reads lines from stdin, appends each as argument to COMMAND, executes.

%include "syscalls.inc"

start:
        ; Get command argument
        mov eax, SYS_GETARGS
        mov ebx, cmd_buf
        int 0x80
        cmp eax, 0
        jle .usage
        mov [cmd_len], eax

        ; Read stdin
        mov eax, SYS_STDIN_READ
        mov ebx, stdin_buf
        int 0x80
        cmp eax, 0
        jl .usage
        mov [stdin_len], eax

        ; Process stdin line by line
        mov esi, stdin_buf

.next_line:
        ; Skip leading whitespace/newlines
.skip_ws:
        cmp esi, stdin_buf
        jb .done
        movzx eax, byte [esi]
        test al, al
        jz .done
        cmp al, 0x0A
        je .skip_one
        cmp al, 0x0D
        je .skip_one
        cmp al, ' '
        je .skip_one
        cmp al, 0x09
        je .skip_one
        jmp .build_cmd
.skip_one:
        inc esi
        jmp .skip_ws

.build_cmd:
        ; Copy COMMAND into exec_buf
        mov edi, exec_buf
        push esi
        mov esi, cmd_buf
        mov ecx, [cmd_len]
.copy_cmd:
        lodsb
        test al, al
        jz .cmd_copied
        stosb
        dec ecx
        jnz .copy_cmd
.cmd_copied:
        ; Add space separator
        mov byte [edi], ' '
        inc edi
        pop esi

        ; Copy stdin argument (until newline or null)
.copy_arg:
        movzx eax, byte [esi]
        test al, al
        jz .exec_it
        cmp al, 0x0A
        je .arg_done
        cmp al, 0x0D
        je .arg_done
        mov [edi], al
        inc edi
        inc esi
        jmp .copy_arg
.arg_done:
        inc esi                 ; Skip newline

.exec_it:
        mov byte [edi], 0

        ; Print the constructed command
        push esi
        mov eax, SYS_PRINT
        mov ebx, exec_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; Execute: use SYS_EXEC with the command name
        ; Parse program name from exec_buf
        mov esi, exec_buf
        mov edi, prog_name
.pn_copy:
        lodsb
        cmp al, ' '
        je .pn_done
        test al, al
        jz .pn_done
        stosb
        jmp .pn_copy
.pn_done:
        mov byte [edi], 0
        pop esi

        ; Unfortunately SYS_EXEC replaces the current process,
        ; so we can only execute the last line.
        ; For now, just print what would be executed.
        jmp .next_line

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; Data
msg_usage:      db "Usage: cmd | xargs COMMAND", 0x0A, 0
cmd_buf:        times 256 db 0
cmd_len:        dd 0
stdin_buf:      times 32768 db 0
stdin_len:      dd 0
exec_buf:       times 512 db 0
prog_name:      times 256 db 0
