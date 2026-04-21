; renice.asm - Change scheduling priority of a running process
; Usage: renice <priority> <pid>
;   priority: 0=high, 1=normal, 2=low, 3=idle
;   pid: process ID (0=current shell task)

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Parse priority
        xor ecx, ecx
.parse_prio:
        mov al, [esi]
        cmp al, '0'
        jb .prio_done
        cmp al, '9'
        ja .prio_done
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_prio
.prio_done:
        cmp ecx, 3
        jg .bad_prio
        mov [prio_val], ecx

        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Parse pid
        xor ecx, ecx
.parse_pid:
        mov al, [esi]
        cmp al, '0'
        jb .pid_done
        cmp al, '9'
        ja .pid_done
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_pid
.pid_done:
        mov [target_pid], ecx

        ; Apply priority
        mov eax, SYS_SETPRIORITY
        mov ebx, [target_pid]
        mov ecx, [prio_val]
        int 0x80
        cmp eax, -1
        je .err

        ; Print confirmation
        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        mov eax, [target_pid]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_ok2
        int 0x80
        mov eax, [prio_val]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.bad_prio:
        mov eax, SYS_PRINT
        mov ebx, msg_badprio
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

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces


msg_usage:      db "Usage: renice <priority> <pid>", 10
                db "  priority: 0=high 1=normal 2=low 3=idle", 10, 0
msg_badprio:    db "renice: priority must be 0-3", 10, 0
msg_err:        db "renice: failed (invalid pid?)", 10, 0
msg_ok:         db "renice: pid ", 0
msg_ok2:        db " set to priority ", 0

prio_val:       dd 0
target_pid:     dd 0
arg_buf:        times 256 db 0
