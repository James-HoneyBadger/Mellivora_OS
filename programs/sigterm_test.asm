; sigterm_test.asm - regression test for signal delivery through kill()
;
; kill() used to destroy the target task outright, so a program could never
; survive a signal. It now delivers the signal: a task with a handler runs it,
; a task without one is still torn down immediately.
;
; Also covers process-group signalling: the parent puts the child in its own
; group and kills -pgid, so the group matcher is exercised too.
;
; Flow:
;   1. fork
;   2. child installs a SIGTERM handler, raises child_ready, then yields forever
;   3. parent waits for child_ready, setpgid(child, 0), kill(-pgid, SIGTERM)
;   4. child's handler prints GOT_TERM and exits 42
;   5. parent waitpid() must see 42
;
; Prints SIGTERM_OK on success, SIGTERM_FAIL otherwise.

%include "syscalls.inc"

EXIT_FROM_HANDLER   equ 42
WAIT_SPINS          equ 2000

start:
        ; The parent counts readiness announcements from its children.
        mov eax, SYS_SIGACTION
        mov ebx, SIGUSR1
        mov ecx, ready_handler
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne fail_main

        ; --- 1. fork ---
        mov eax, SYS_FORK
        int 0x80
        cmp eax, 0
        je child_main
        cmp eax, -1
        je fail_main
        mov [child_pid], eax

        ; --- 3. wait until the child has its handler installed ---
        ; fork() is copy-on-write, so a shared flag would never be visible here:
        ; the child announces readiness with SIGUSR1 instead.
        mov ecx, WAIT_SPINS
wait_ready:
        cmp dword [ready_count], 1
        je child_is_ready
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz wait_ready
        jmp fail_main

child_is_ready:
        ; Put the child in its own process group (pgid = child pid)
        mov eax, SYS_SETPGID
        mov ebx, [child_pid]
        xor ecx, ecx
        int 0x80
        cmp eax, 0
        jne fail_main

        ; kill(-pgid, SIGTERM): negative pid selects the process group
        mov eax, SYS_KILL
        mov ebx, [child_pid]
        neg ebx
        mov ecx, SIGTERM
        int 0x80
        cmp eax, 0
        jne fail_main

        ; --- 5. the handler must have exited with 42 ---
        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        cmp eax, EXIT_FROM_HANDLER
        jne fail_main

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

        ; --- 2. child ---
child_main:
        mov eax, SYS_SIGACTION
        mov ebx, SIGTERM
        mov ecx, term_handler
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne fail_child

        ; Announce readiness to the parent (SIGUSR1 to our own ppid)
        mov eax, SYS_GETPPID
        int 0x80
        mov ebx, eax
        mov ecx, SIGUSR1
        mov eax, SYS_KILL
        int 0x80
child_spin:
        mov eax, SYS_YIELD
        int 0x80
        jmp child_spin

; SIGTERM handler: runs in the child's context. EBX = signal number.
term_handler:
        mov eax, SYS_PRINT
        mov ebx, msg_got
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, EXIT_FROM_HANDLER
        int 0x80

; Parent: a child is armed. Must return so the interrupted code continues.
ready_handler:
        inc dword [ready_count]
        ret

fail_main:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

; The child exits quietly on failure; the parent's waitpid then reports a
; mismatch instead of a spurious pass.
fail_child:
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

msg_ok:      db "SIGTERM_OK", 10, 0
msg_got:     db "GOT_TERM", 10, 0
msg_fail:    db "SIGTERM_FAIL", 10, 0

child_pid:   dd 0
ready_count: dd 0
