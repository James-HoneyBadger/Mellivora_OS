; pgrp_test.asm - regression test for the terminal foreground process group
;
; Terminal signals belong to the console, not to "whichever task is running":
; Ctrl+C/Ctrl+Z are delivered to the terminal's foreground process group
; (`SYS_TCSETPGRP`/`SYS_TCGETPGRP`). This test checks the pieces a job-control
; shell depends on:
;
;   1. a task can put itself and its children in one process group
;   2. tcsetpgrp()/tcgetpgrp() round-trip
;   3. kill(-pgid, SIGINT) reaches *every* member, including the caller
;   4. a member with a handler runs it (exit 7); the caller's handler runs too
;
; Prints PGRP_OK on success, PGRP_FAIL otherwise.

%include "syscalls.inc"

EXIT_HANDLED equ 7
PGID_TEST    equ 99             ; arbitrary non-zero group id
WAIT_SPINS   equ 5000

start:
        ; --- parent installs its own SIGINT handler ---
        mov eax, SYS_SIGACTION
        mov ebx, SIGINT
        mov ecx, parent_int_handler
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne fail_main

        ; --- and counts readiness announcements from its children ---
        mov eax, SYS_SIGACTION
        mov ebx, SIGUSR1
        mov ecx, ready_handler
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne fail_main

        mov eax, SYS_SIGACTION
        mov ebx, SIGUSR2
        mov ecx, ready2_handler
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne fail_main

        ; --- two children ---
        mov eax, SYS_FORK
        int 0x80
        cmp eax, 0
        je child_main
        cmp eax, -1
        je fail_main
        mov [child1], eax

        ; Only the second child sees this, because it is forked after the flag
        ; is set. It needs it to pick a different announcement signal.
        mov byte [is_child2], 1

        mov eax, SYS_FORK
        int 0x80
        cmp eax, 0
        je child_main
        cmp eax, -1
        je fail_main
        mov [child2], eax

        ; --- parent: wait until both children are armed ---
        ; fork() is copy-on-write, so each child announces readiness with a
        ; SIGUSR1 to the parent instead of setting a shared flag.
        mov ecx, WAIT_SPINS
.wait_ready:
        cmp dword [ready], 2
        je .ready
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .wait_ready
        jmp fail_main

.ready:
        ; Our own group, so the job is one signalling unit
        mov eax, SYS_SETPGID
        xor ebx, ebx            ; self
        mov ecx, PGID_TEST
        int 0x80
        cmp eax, 0
        jne fail_main

        ; The console must be able to hand the foreground to that group
        mov eax, SYS_TCSETPGRP
        mov ebx, PGID_TEST
        int 0x80
        cmp eax, 0
        jne fail_main
        mov eax, SYS_TCGETPGRP
        int 0x80
        cmp eax, PGID_TEST
        jne fail_main

        ; Signal the whole group (we are a member, so we get it too)
        mov eax, SYS_KILL
        mov ebx, -PGID_TEST
        mov ecx, SIGINT
        int 0x80
        cmp eax, 0
        jne fail_main

        ; Let our own handler run. A handler clobbers the interrupted syscall's
        ; return value (sigreturn restores EIP/ESP/EFLAGS, not the GP
        ; registers), so burn the interrupt on a yield rather than on a call
        ; whose result we need.
        mov ecx, WAIT_SPINS
.wait_handler:
        cmp byte [parent_hit], 1
        je .handler_ran
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .wait_handler
        jmp fail_main
.handler_ran:

        ; --- both children must have exited through their handler (7) ---
        ; SYS_WAIT stores the status to a user pointer instead of returning it,
        ; so it is immune to the register clobbering described above.
        mov ecx, WAIT_SPINS
.wait_c1:
        mov eax, SYS_WAIT
        mov ebx, status1
        int 0x80
        cmp eax, -1
        jne .got_c1
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .wait_c1
        jmp fail_main
.got_c1:
        mov ecx, WAIT_SPINS
.wait_c2:
        mov eax, SYS_WAIT
        mov ebx, status2
        int 0x80
        cmp eax, -1
        jne .got_c2
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .wait_c2
        jmp fail_main
.got_c2:
        cmp dword [status1], EXIT_HANDLED
        jne fail_main
        cmp dword [status2], EXIT_HANDLED
        jne fail_main

        ; Hand the console back now that the job is done
        mov eax, SYS_TCSETPGRP
        mov ebx, -1
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; Child: install a SIGINT handler, join the job's group, then spin.
;-----------------------------------------------------------------------
child_main:
        mov eax, SYS_SIGACTION
        mov ebx, SIGINT
        mov ecx, child_int_handler
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne fail_child

        mov eax, SYS_SETPGID
        xor ebx, ebx            ; self
        mov ecx, PGID_TEST
        int 0x80
        cmp eax, 0
        jne fail_child

        ; Announce readiness to the parent. Each child uses a *different*
        ; signal: pending signals are a bitmask, so two children sending the
        ; same one before the parent runs would collapse into a single
        ; delivery and the parent would never see the second.
        ;
        ; Note ECX is loaded *before* SYS_GETPPID: syscalls may only return a
        ; value in EAX, so this also checks that getppid preserves ECX (it used
        ; to leak its scan counter there, and kill() then sent signal 1).
        mov ecx, SIGUSR1
        cmp byte [is_child2], 0
        je .announce
        mov ecx, SIGUSR2
.announce:
        mov eax, SYS_GETPPID
        int 0x80
        mov ebx, eax
        mov eax, SYS_KILL
        int 0x80
child_spin:
        mov eax, SYS_YIELD
        int 0x80
        jmp child_spin

; Runs in a child's context; the kernel redirects the return to the sigreturn
; trampoline, but we never return — we exit with a distinctive status.
child_int_handler:
        mov eax, SYS_EXIT
        mov ebx, EXIT_HANDLED
        int 0x80

; Runs in the parent; must return so the interrupted code continues.
parent_int_handler:
        mov byte [parent_hit], 1
        ret

; Parent: a child is armed.
ready_handler:
        inc dword [ready]
        ret

; Parent: the second child is armed.
ready2_handler:
        inc dword [ready]
        ret

fail_main:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

fail_child:
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

msg_ok:   db "PGRP_OK", 10, 0
msg_fail: db "PGRP_FAIL", 10, 0

child1:     dd 0
child2:     dd 0
status1:    dd 0
status2:    dd 0
ready:      dd 0
parent_hit: db 0
is_child2:  db 0
