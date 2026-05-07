; vsh.asm - Veritas Shell (v9.0)
; A POSIX-ish shell for Mellivora OS with colored prompt, built-ins,
; simple command history (24 entries), piping placeholder, and fork() support.

%include "syscalls.inc"

VSH_HIST    equ 24          ; history size
VSH_LINE    equ 256         ; max command line length
VSH_PROMPT  equ "vsh$ "    ; prompt (ended at assembly time)

; ANSI/color escape constants (for bterm color control)
; The kernel shell (bterm) interprets ESC[color sequences sent via print_string.
COL_PROMPT  equ 0x02        ; green
COL_ERROR   equ 0x03        ; red-ish
COL_RESET   equ 0x00        ; default

start:
        ; Print banner
        mov eax, SYS_PRINT
        mov ebx, msg_banner
        int 0x80

.main_loop:
        ; Print colored prompt using SYS_SET_TERM_COLOR if available,
        ; otherwise fall back to plain string print.
        mov eax, SYS_PRINT
        mov ebx, prompt_str
        int 0x80

        ; Read a line of input
        mov eax, SYS_STDIN_READ
        mov ebx, cmd_line
        int 0x80
        cmp eax, -1
        je .exit_shell          ; EOF
        cmp eax, 0
        je .main_loop

        ; Strip trailing newline
        mov edi, cmd_line
        add edi, eax
        dec edi
        cmp byte [edi], 0x0A
        jne .no_strip
        mov byte [edi], 0
        dec eax
.no_strip:
        cmp eax, 0
        je .main_loop           ; empty line

        ; Save to history
        call vsh_history_push

        ; Trim leading spaces
        mov esi, cmd_line
        call skip_spaces

        ; Check for built-in commands
        call vsh_parse_cmd

        jmp .main_loop

.exit_shell:
        mov eax, SYS_PRINT
        mov ebx, msg_exit
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; vsh_parse_cmd - Parse and dispatch command from ESI (trimmed)
;=======================================================================
vsh_parse_cmd:
        pushad
        ; Check "exit" or "quit"
        push esi
        mov edi, bi_exit
        call str_startswith
        pop esi
        jc .do_exit

        push esi
        mov edi, bi_quit
        call str_startswith
        pop esi
        jc .do_exit

        ; Check "cd <dir>"
        push esi
        mov edi, bi_cd
        call str_startswith
        pop esi
        jc .do_cd

        ; Check "history"
        push esi
        mov edi, bi_history
        call str_startswith
        pop esi
        jc .do_history

        ; Check "echo"
        push esi
        mov edi, bi_echo
        call str_startswith
        pop esi
        jc .do_echo

        ; Check "help"
        push esi
        mov edi, bi_help
        call str_startswith
        pop esi
        jc .do_help

        ; Check "uname"
        push esi
        mov edi, bi_uname
        call str_startswith
        pop esi
        jc .do_uname

        ; Not a built-in — fork + exec
        call vsh_fork_exec
        jmp .parse_done

.do_exit:
        mov eax, SYS_PRINT
        mov ebx, msg_exit
        int 0x80
        popad
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.do_cd:
        ; Advance past "cd"
        add esi, 2
        call skip_spaces
        cmp byte [esi], 0
        je .cd_home
        ; SYS_CHDIR
        mov eax, SYS_CHDIR
        mov ebx, esi
        int 0x80
        cmp eax, -1
        jne .parse_done
        mov eax, SYS_PRINT
        mov ebx, err_cd
        int 0x80
        jmp .parse_done
.cd_home:
        mov eax, SYS_PRINT
        mov ebx, err_no_home
        int 0x80
        jmp .parse_done

.do_history:
        call vsh_print_history
        jmp .parse_done

.do_echo:
        add esi, 4              ; skip "echo"
        call skip_spaces
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PUTCHAR
        mov bl, 0x0A
        int 0x80
        jmp .parse_done

.do_help:
        mov eax, SYS_PRINT
        mov ebx, msg_help
        int 0x80
        jmp .parse_done

.do_uname:
        mov eax, SYS_PRINT
        mov ebx, msg_uname
        int 0x80
        jmp .parse_done

.parse_done:
        popad
        ret

;=======================================================================
; vsh_fork_exec - Fork and exec a program
; ESI points to the command string.
;=======================================================================
vsh_fork_exec:
        pushad
        ; Copy command to exec_buf
        mov edi, exec_buf
        mov ecx, VSH_LINE - 1
.cp_loop:
        lodsb
        stosb
        cmp al, 0
        je .cp_done
        dec ecx
        jnz .cp_loop
.cp_done:

        ; SYS_FORK to create child process
        mov eax, SYS_FORK
        int 0x80
        cmp eax, 0
        je .child_exec          ; child: EAX=0
        cmp eax, -1
        je .fork_fail

        ; Parent: EAX = child PID — wait for child
        mov [child_pid], eax
.wait_loop:
        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        cmp eax, 0
        je .wait_loop
        jmp .fork_done

.child_exec:
        ; In child: execute the program
        mov eax, SYS_EXEC
        mov ebx, exec_buf
        int 0x80
        ; If exec fails, print error and exit child
        mov eax, SYS_PRINT
        mov ebx, err_notfound
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.fork_fail:
        ; fork() not available — try direct exec (v8 compat)
        mov eax, SYS_EXEC
        mov ebx, exec_buf
        int 0x80
        ; If exec fails
        mov eax, SYS_PRINT
        mov ebx, err_notfound
        int 0x80
.fork_done:
        popad
        ret

;=======================================================================
; vsh_history_push - Add cmd_line to ring history buffer
;=======================================================================
vsh_history_push:
        pushad
        mov eax, [hist_head]
        ; Copy cmd_line to history[hist_head]
        imul edi, eax, VSH_LINE
        add edi, history_buf
        mov esi, cmd_line
        mov ecx, VSH_LINE - 1
        rep movsb
        mov byte [edi], 0
        ; Advance head
        inc eax
        cmp eax, VSH_HIST
        jl .hp_done
        xor eax, eax
.hp_done:
        mov [hist_head], eax
        inc dword [hist_count]
        popad
        ret

;=======================================================================
; vsh_print_history - Print all history entries
;=======================================================================
vsh_print_history:
        pushad
        mov ecx, [hist_count]
        cmp ecx, VSH_HIST
        jle .ph_use_count
        mov ecx, VSH_HIST
.ph_use_count:
        test ecx, ecx
        jz .ph_empty
        ; Start from oldest entry
        mov eax, [hist_head]
        sub eax, ecx
        ; Normalize to [0, VSH_HIST)
.ph_norm:
        cmp eax, 0
        jge .ph_loop_start
        add eax, VSH_HIST
        jmp .ph_norm
.ph_loop_start:
        push ecx
        push eax
.ph_loop:
        pop eax
        pop ecx
        cmp ecx, 0
        je .ph_done
        push ecx
        push eax

        ; Print index
        mov eax, SYS_PRINT
        mov ebx, [hist_count]
        sub ebx, ecx
        inc ebx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hist_sep
        int 0x80

        ; Print entry
        pop eax
        imul esi, eax, VSH_LINE
        add esi, history_buf
        push eax
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PUTCHAR
        mov bl, 0x0A
        int 0x80

        pop eax
        pop ecx
        push ecx
        push eax
        inc eax
        cmp eax, VSH_HIST
        jl .ph_no_wrap
        xor eax, eax
.ph_no_wrap:
        dec ecx
        jmp .ph_loop
.ph_done:
        pop eax
        pop ecx
        jmp .ph_exit
.ph_empty:
        mov eax, SYS_PRINT
        mov ebx, msg_no_history
        int 0x80
.ph_exit:
        popad
        ret

;=======================================================================
; str_startswith — Check if [ESI] starts with [EDI] (null-terminated)
; Sets carry if match.
;=======================================================================
str_startswith:
        push esi
        push edi
.ss_loop:
        movzx eax, byte [edi]
        test al, al
        jz .ss_match         ; EDI exhausted = matched
        movzx ecx, byte [esi]
        cmp al, cl
        jne .ss_no_match
        inc esi
        inc edi
        jmp .ss_loop
.ss_match:
        pop edi
        pop esi
        stc
        ret
.ss_no_match:
        pop edi
        pop esi
        clc
        ret

;=======================================================================
; skip_spaces — advance ESI past ASCII spaces
;=======================================================================
skip_spaces:
        lodsb
        cmp al, ' '
        je skip_spaces
        dec esi
        ret

;=======================================================================
; DATA
;=======================================================================
msg_banner:     db "Veritas Shell v9.0 (vsh) — Mellivora OS", 0x0A,
                db "Type 'help' for built-in commands.", 0x0A, 0
msg_exit:       db "logout", 0x0A, 0
msg_help:       db "Built-in commands:", 0x0A,
                db "  cd <dir>    Change directory", 0x0A,
                db "  echo <msg>  Print message", 0x0A,
                db "  history     Show command history", 0x0A,
                db "  uname       Print OS name", 0x0A,
                db "  help        Show this help", 0x0A,
                db "  exit/quit   Exit shell", 0x0A,
                db "Other commands are searched on the filesystem.", 0x0A, 0
msg_uname:      db "Mellivora OS v9.0 x86 (protected mode)", 0x0A, 0
msg_no_history: db "(no history)", 0x0A, 0
err_cd:         db "vsh: cd: no such directory", 0x0A, 0
err_no_home:    db "vsh: cd: no $HOME defined", 0x0A, 0
err_notfound:   db "vsh: command not found", 0x0A, 0

prompt_str:     db "vsh$ ", 0

bi_exit:        db "exit", 0
bi_quit:        db "quit", 0
bi_cd:          db "cd", 0
bi_history:     db "history", 0
bi_echo:        db "echo", 0
bi_help:        db "help", 0
bi_uname:       db "uname", 0

hist_sep:       db "  ", 0

cmd_line:       times VSH_LINE db 0
exec_buf:       times VSH_LINE db 0
child_pid:      dd 0
hist_head:      dd 0
hist_count:     dd 0

history_buf:    times VSH_HIST * VSH_LINE db 0
