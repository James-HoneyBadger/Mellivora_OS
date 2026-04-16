; ==========================================================================
; strace - System call tracer for Mellivora OS
;
; Usage: strace <program> [args]   Trace syscalls made by <program>
;        strace                    Display last trace log
;
; Works via parent-resume: strace enables tracing, execs the target.
; When the target exits, parent-resume re-runs strace which detects
; existing trace data and dumps it.
; ==========================================================================
%include "syscalls.inc"

STRACE_DISABLE  equ 0
STRACE_ENABLE   equ 1
STRACE_READ     equ 2
STRACE_QUERY    equ 3
TRACE_BUF_SZ    equ 4096

start:
        ; Check if trace data exists (dump mode vs exec mode)
        mov eax, SYS_STRACE
        mov ebx, STRACE_QUERY
        int 0x80
        test eax, eax
        jnz dump_trace

        ; --- Exec mode: enable tracing and run the target ---
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz show_usage

        ; Enable tracing (clears any stale data)
        mov eax, SYS_STRACE
        mov ebx, STRACE_ENABLE
        int 0x80

        ; Exec target program (replaces strace in memory)
        ; On target exit, parent-resume re-runs strace -> dump mode
        mov eax, SYS_EXEC
        mov ebx, arg_buf
        int 0x80

        ; If exec failed, disable tracing and report error
        mov eax, SYS_STRACE
        mov ebx, STRACE_DISABLE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_not_found
        int 0x80
        jmp exit

; -------------------------------------------------------------------
; Dump the trace buffer
; -------------------------------------------------------------------
dump_trace:
        mov [entry_count], eax

        ; Read binary trace buffer from kernel
        mov eax, SYS_STRACE
        mov ebx, STRACE_READ
        mov ecx, trace_buf
        mov edx, TRACE_BUF_SZ
        int 0x80

        ; Disable tracing and clear kernel buffer
        mov eax, SYS_STRACE
        mov ebx, STRACE_DISABLE
        int 0x80

        ; Header
        mov eax, SYS_PRINT
        mov ebx, msg_header
        int 0x80

        ; Print each trace entry: sys#NN(XXXXXXXX, XXXXXXXX, XXXXXXXX)
        mov esi, trace_buf
        mov ecx, [entry_count]
.loop:
        test ecx, ecx
        jz .done
        push rcx
        push rsi

        ; "sys#"
        mov eax, SYS_PRINT
        mov ebx, msg_sys
        int 0x80

        ; Syscall number as decimal
        mov eax, [esi]
        call print_dec

        ; "("
        mov eax, SYS_PUTCHAR
        mov ebx, '('
        int 0x80

        pop rsi
        push rsi

        ; EBX arg as hex
        mov eax, [esi + 4]
        call print_hex

        ; ", "
        mov eax, SYS_PRINT
        mov ebx, msg_comma
        int 0x80

        ; ECX arg as hex
        mov eax, [esi + 8]
        call print_hex

        ; ", "
        mov eax, SYS_PRINT
        mov ebx, msg_comma
        int 0x80

        ; EDX arg as hex
        mov eax, [esi + 12]
        call print_hex

        ; ")\n"
        mov eax, SYS_PRINT
        mov ebx, msg_rpnl
        int 0x80

        pop rsi
        add esi, 16
        pop rcx
        dec ecx
        jmp .loop

.done:
        ; Summary
        mov eax, SYS_PRINT
        mov ebx, msg_total
        int 0x80
        mov eax, [entry_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_calls
        int 0x80
        jmp exit

; -------------------------------------------------------------------
show_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -------------------------------------------------------------------
; print_hex - Print EAX as 8-digit uppercase hex
; -------------------------------------------------------------------
print_hex:
        PUSHALL
        mov ecx, 8
        mov edx, eax
.ph_loop:
        rol edx, 4
        mov eax, edx
        and eax, 0x0F
        cmp eax, 10
        jb .ph_digit
        add eax, ('A' - 10)
        jmp .ph_out
.ph_digit:
        add eax, '0'
.ph_out:
        push rcx
        push rdx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rdx
        pop rcx
        dec ecx
        jnz .ph_loop
        POPALL
        ret

; -------------------------------------------------------------------
; Strings
; -------------------------------------------------------------------
msg_header:     db "--- syscall trace ---", 0x0A, 0
msg_sys:        db "sys#", 0
msg_comma:      db ", ", 0
msg_rpnl:       db ")", 0x0A, 0
msg_total:      db "--- ", 0
msg_calls:      db " syscalls traced ---", 0x0A, 0
msg_usage:      db "Usage: strace <program> [args]", 0x0A
                db "  Traces syscalls made by <program>.", 0x0A
                db "  Run 'strace' with no args to view log.", 0x0A, 0
msg_not_found:  db "strace: command not found", 0x0A, 0

; -------------------------------------------------------------------
; BSS
; -------------------------------------------------------------------
entry_count:    dd 0
arg_buf:        times 256 db 0
trace_buf:      times TRACE_BUF_SZ db 0
