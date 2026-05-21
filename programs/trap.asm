; trap.asm — demonstrate user-space signal handlers (v11.0)
;
; Installs a handler for SIGUSR1 (10), prints its own PID, then loops
; sleeping 1 second at a time.  Each time SIGUSR1 arrives the handler
; fires, prints a message, and returns transparently.
;
; Test from the shell:
;   trap &               (run in background)
;   kill -10 <pid>       (send SIGUSR1)
;
; The program exits after receiving SIGUSR1 three times.

%include "syscalls.inc"

start:
        ; --- Install SIGUSR1 handler ---
        mov eax, SYS_SIGACTION
        mov ebx, SIGUSR1
        mov ecx, sigusr1_handler
        xor edx, edx            ; don't need old handler
        int 0x80
        test eax, eax
        js  .install_fail

        ; --- Print PID so user knows what to kill ---
        mov eax, SYS_GETPID
        int 0x80
        mov [my_pid], eax

        mov eax, SYS_PRINT
        mov ebx, msg_ready
        int 0x80

        ; Print PID as decimal
        mov eax, [my_pid]
        call print_uint32
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80

        ; --- Main wait loop ---
.loop:
        cmp dword [sig_count], 3
        jge .done

        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        jmp .loop

.done:
        mov eax, SYS_PRINT
        mov ebx, msg_exit
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.install_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;------------------------------------------------------------------
; sigusr1_handler — called by the kernel signal-delivery machinery.
; The kernel pushes a 28-byte signal frame on the user stack:
;   [esp+0] = return address (points to sigreturn stub)
;   [esp+4] = signum  (first argument, cdecl)
; We can call syscalls normally; the stub will invoke sys_sigreturn
; automatically when we return.
;------------------------------------------------------------------
sigusr1_handler:
        ; EAX/ECX/EDX are scratch; don't need to save (kernel restores
        ; the interrupted context via sys_sigreturn).
        mov eax, SYS_PRINT
        mov ebx, msg_caught
        int 0x80

        ; Increment counter (thread-safe enough for a demo)
        inc dword [sig_count]

        ; Print count
        mov eax, [sig_count]
        call print_uint32
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80

        ret                     ; returns to sigreturn stub → sys_sigreturn

;------------------------------------------------------------------
; print_uint32 — print EAX as unsigned decimal
; Trashes EAX, ECX, EDX, ESI.
;------------------------------------------------------------------
print_uint32:
        mov  ecx, numbuf + 10
        mov  byte [ecx], 0
        mov  edx, 10
.pu_loop:
        xor  edx, edx
        div  dword [ten]
        add  dl, '0'
        dec  ecx
        mov  [ecx], dl
        test eax, eax
        jnz  .pu_loop
        mov  eax, SYS_PRINT
        mov  ebx, ecx
        int  0x80
        ret

;------------------------------------------------------------------
; Data
;------------------------------------------------------------------
my_pid:     dd 0
sig_count:  dd 0
ten:        dd 10

numbuf:     times 12 db 0

msg_ready:  db 'trap: waiting for SIGUSR1, my PID = ', 0
msg_caught: db 'trap: SIGUSR1 received (#', 0
msg_newline:db ')', 0x0A, 0
msg_exit:   db 'trap: received 3 signals, exiting.', 0x0A, 0
msg_fail:   db 'trap: sigaction failed', 0x0A, 0
