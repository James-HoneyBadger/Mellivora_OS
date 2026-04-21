; kill.asm - Send signal to a process
; Usage: kill [-SIGNAL] <pid>
;   kill <pid>          send SIGTERM (15)
;   kill -9 <pid>       send SIGKILL
;   kill -l             list signal names

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces

        ; Default signal = SIGTERM
        mov dword [signum], SIGTERM

        ; Check for -l flag
        cmp byte [esi], '-'
        jne .parse_pid
        cmp byte [esi+1], 'l'
        je .list_signals

        ; Parse signal number: -N or -SIGNAME
        inc esi
        cmp byte [esi], '0'
        jb .named_sig
        cmp byte [esi], '9'
        jg .named_sig

        ; Numeric signal
        xor ecx, ecx
.parse_sig:
        mov al, [esi]
        cmp al, '0'
        jb .sig_done
        cmp al, '9'
        ja .sig_done
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_sig
.sig_done:
        mov [signum], ecx
        call skip_spaces
        jmp .parse_pid_val

.named_sig:
        ; Compare against known names
        mov ecx, esi
        mov edi, sig_int_str
        call str_eq
        cmp eax, 1
        jne .try_kill
        mov dword [signum], SIGINT
        add esi, 3
        call skip_spaces
        jmp .parse_pid_val

.try_kill:
        mov ecx, esi
        mov edi, sig_kill_str
        call str_eq
        cmp eax, 1
        jne .try_term
        mov dword [signum], SIGKILL
        add esi, 4
        call skip_spaces
        jmp .parse_pid_val

.try_term:
        mov dword [signum], SIGTERM
        ; skip to space
.skip_name:
        mov al, [esi]
        cmp al, ' '
        je .parse_pid_val
        cmp al, 0
        je .parse_pid_val
        inc esi
        jmp .skip_name

.parse_pid:
        call skip_spaces
.parse_pid_val:
        cmp byte [esi], 0
        je .usage

        xor ecx, ecx
.parse_p:
        mov al, [esi]
        cmp al, '0'
        jb .p_done
        cmp al, '9'
        ja .p_done
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_p
.p_done:
        mov [target_pid], ecx

        ; Send signal
        mov eax, SYS_SIGNAL
        mov ebx, [target_pid]
        mov ecx, [signum]
        int 0x80
        cmp eax, -1
        je .err

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.list_signals:
        mov eax, SYS_PRINT
        mov ebx, sig_list
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .exit

.err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80

.exit:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;---------------------------------------
; str_eq: compare [ecx] vs [edi], case-insensitive prefix
; Returns EAX=1 if edi is a prefix of ecx
;---------------------------------------
str_eq:
        push esi
        push ecx
        push edi
.se_loop:
        mov al, [edi]
        test al, al
        jz .se_match
        mov bl, [ecx]
        or al, 0x20
        or bl, 0x20
        cmp al, bl
        jne .se_no
        inc ecx
        inc edi
        jmp .se_loop
.se_match:
        mov eax, 1
        jmp .se_ret
.se_no:
        xor eax, eax
.se_ret:
        pop edi
        pop ecx
        pop esi
        ret

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

msg_usage:      db "Usage: kill [-SIGNAL] <pid>", 10
                db "       kill -l  (list signals)", 10, 0
msg_err:        db "kill: failed (process not found?)", 10, 0
sig_int_str:    db "INT", 0
sig_kill_str:   db "KILL", 0
sig_list:       db " 2) SIGINT    9) SIGKILL   10) SIGUSR1", 10
                db "12) SIGUSR2  14) SIGALRM   15) SIGTERM", 10
                db "20) SIGTSTP  25) SIGCONT", 10, 0

signum:         dd SIGTERM
target_pid:     dd 0
arg_buf:        times 256 db 0
