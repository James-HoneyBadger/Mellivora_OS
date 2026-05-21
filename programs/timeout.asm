; timeout.asm — run a command with a time limit
;
; Combines SYS_FORK, SYS_EXEC, SYS_WAITPID, SYS_ALARM, and SYS_KILL to
; enforce a wall-clock deadline on any program.  The parent sets an alarm
; for N seconds (new in v10 via SYS_ALARM) so the user can also see the
; alarm remaining via `alarm` with no arguments while the child is running.
; If the child exits before the deadline the alarm is cancelled and the
; child's exit code is forwarded.
;
; Usage: timeout <seconds> <command>
;
; Exit codes:
;   0–255   child's own exit code (if it finished in time)
;   124     command timed out and was killed

%include "syscalls.inc"

TICK_RATE       equ 100         ; PIT runs at 100 Hz
EXIT_TIMEOUT    equ 124

start:
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        test eax, eax
        jle .usage

        ; --- Parse seconds ---
        mov esi, argbuf
        call skip_ws
        cmp byte [esi], 0
        je .usage

        xor ecx, ecx
.dig:
        mov al, [esi]
        cmp al, '0'
        jb .dig_done
        cmp al, '9'
        ja .dig_done
        sub al, '0'
        imul ecx, 10
        movzx eax, al
        add ecx, eax
        inc esi
        jmp .dig
.dig_done:
        test ecx, ecx
        jz .usage
        mov [secs], ecx

        ; --- Skip whitespace, rest of line is the command ---
        call skip_ws
        cmp byte [esi], 0
        je .usage
        mov edi, cmdbuf
        xor ecx, ecx
.cp:
        mov al, [esi]
        cmp al, 0
        je .cp_done
        cmp al, 10
        je .cp_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .cp
.cp_done:
        mov byte [edi + ecx], 0
        cmp ecx, 0
        je .usage

        ; --- Fork ---
        mov eax, SYS_FORK
        int 0x80
        cmp eax, 0
        je .child
        cmp eax, -1
        je .no_fork

        ; ============================================================
        ; Parent
        ; ============================================================
        mov [child_pid], eax

        ; Set alarm so the user can observe it externally too
        mov eax, SYS_ALARM
        mov ebx, [secs]
        int 0x80                ; ignore previous remaining

        ; Compute deadline tick = now + secs * TICK_RATE
        mov eax, SYS_GETTIME
        int 0x80
        mov ecx, [secs]
        imul ecx, TICK_RATE
        add eax, ecx
        mov [deadline], eax

        ; --- Poll loop ---
.poll:
        ; Check if child has exited
        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        cmp eax, 0
        jne .child_done

        ; Check elapsed time
        mov eax, SYS_GETTIME
        int 0x80
        cmp eax, [deadline]
        jb .yield_and_poll

        ; Timed out — kill child and cancel alarm
        mov eax, SYS_KILL
        mov ebx, [child_pid]
        int 0x80

        mov eax, SYS_ALARM
        xor ebx, ebx            ; cancel alarm
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_timeout
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, EXIT_TIMEOUT
        int 0x80

.yield_and_poll:
        mov eax, SYS_YIELD
        int 0x80
        jmp .poll

.child_done:
        ; Child exited normally — cancel alarm and forward exit code
        mov [child_exit], eax

        mov eax, SYS_ALARM
        xor ebx, ebx
        int 0x80

        mov eax, SYS_EXIT
        mov ebx, [child_exit]
        int 0x80

        ; ============================================================
        ; Child
        ; ============================================================
.child:
        mov eax, SYS_EXEC
        mov ebx, cmdbuf
        int 0x80
        ; exec failed — print error and exit
        mov eax, SYS_PRINT
        mov ebx, msg_notfound
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 127
        int 0x80

        ; ============================================================
        ; Fallback: fork not available — run directly
        ; ============================================================
.no_fork:
        ; Set alarm for the time limit then exec; kernel will deliver
        ; SIGALRM if the process overruns.  The child will not return
        ; to us, so we cannot cancel the alarm afterwards.
        mov eax, SYS_ALARM
        mov ebx, [secs]
        int 0x80

        mov eax, SYS_EXEC
        mov ebx, cmdbuf
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_notfound
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 127
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; ---- Helpers ----------------------------------------------------
skip_ws:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_ws

; ---- Data -------------------------------------------------------
msg_usage:
        db "Usage: timeout <seconds> <command>", 10
        db "  Kills command after <seconds> seconds; exits 124 on timeout.", 10, 0
msg_timeout:    db "timeout: command timed out", 10, 0
msg_notfound:   db "timeout: command not found", 10, 0

secs:           dd 0
child_pid:      dd 0
child_exit:     dd 0
deadline:       dd 0

section .bss
argbuf:         resb 512
cmdbuf:         resb 256
