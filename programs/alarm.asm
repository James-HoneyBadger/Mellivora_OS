; alarm.asm — set, cancel, or query the POSIX-style alarm() timer
;
; The kernel delivers SIGALRM to the calling task when the deadline
; expires.  sched_check_alarms() runs every PIT tick (100 Hz) and sets
; TCB_SIG_PEND |= (1 << SIGALRM) when the tick deadline is reached.
;
; Usage:
;   alarm <seconds>   — set alarm; print previous remaining time if any
;   alarm 0           — cancel any pending alarm
;   alarm             — query remaining time (non-destructive)

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        test eax, eax
        jle .query_only

        ; --- Skip leading whitespace, then parse integer ---
        mov esi, argbuf
        call skip_ws
        cmp byte [esi], 0
        je .query_only

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
        mov [seconds], ecx
        mov byte [has_arg], 1

; ---- Set or cancel alarm ----------------------------------------
        ; EBX = new seconds; returns EAX = previous remaining (seconds)
        mov eax, SYS_ALARM
        mov ebx, [seconds]
        int 0x80
        mov [prev_secs], eax

        ; Report previous alarm if one was active
        mov eax, [prev_secs]
        test eax, eax
        jz .no_prev
        mov eax, SYS_PRINT
        mov ebx, msg_prev_a
        int 0x80
        mov eax, [prev_secs]
        call print_uint
        mov eax, SYS_PRINT
        mov ebx, msg_prev_b
        int 0x80
.no_prev:

        ; Report what we just did
        mov eax, [seconds]
        test eax, eax
        jz .show_cancel

        mov eax, SYS_PRINT
        mov ebx, msg_set_a
        int 0x80
        mov eax, [seconds]
        call print_uint
        mov eax, SYS_PRINT
        mov ebx, msg_set_b
        int 0x80
        jmp .done

.show_cancel:
        mov eax, SYS_PRINT
        mov ebx, msg_cancelled
        int 0x80
        jmp .done

; ---- Query remaining time (no arguments) ------------------------
.query_only:
        ; alarm(0) cancels any pending alarm and returns seconds remaining.
        ; If an alarm was active we restore it immediately so this is
        ; effectively a read-only probe.
        mov eax, SYS_ALARM
        xor ebx, ebx
        int 0x80
        mov [prev_secs], eax

        test eax, eax
        jz .none_set

        ; Restore the alarm we just cancelled
        mov eax, SYS_ALARM
        mov ebx, [prev_secs]
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_remaining_a
        int 0x80
        mov eax, [prev_secs]
        call print_uint
        mov eax, SYS_PRINT
        mov ebx, msg_remaining_b
        int 0x80
        jmp .done

.none_set:
        mov eax, SYS_PRINT
        mov ebx, msg_none
        int 0x80

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
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

; print_uint: EAX = unsigned integer to print (clobbers nothing else)
print_uint:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        mov edi, numbuf + 11
        mov byte [edi], 0
        mov ecx, 10
        test eax, eax
        jnz .l
        dec edi
        mov byte [edi], '0'
        jmp .p
.l:
        xor edx, edx
        div ecx
        add dl, '0'
        dec edi
        mov [edi], dl
        test eax, eax
        jnz .l
.p:
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; ---- Data -------------------------------------------------------
msg_prev_a:     db "Previous alarm had ", 0
msg_prev_b:     db " second(s) remaining.", 10, 0
msg_set_a:      db "Alarm set for ", 0
msg_set_b:      db " second(s).", 10, 0
msg_cancelled:  db "Alarm cancelled.", 10, 0
msg_remaining_a: db "Alarm fires in ", 0
msg_remaining_b: db " second(s).", 10, 0
msg_none:       db "No alarm is currently set.", 10, 0

seconds:        dd 0
prev_secs:      dd 0
has_arg:        db 0

section .bss
argbuf:         resb 256
numbuf:         resb 16
