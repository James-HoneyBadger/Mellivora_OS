; argv_test.asm - regression test for per-task argv and SYS_EXECVE
;
; Covers three things the Phase 2 process model has to get right:
;
;   1. A launched task reads ITS OWN arguments via SYS_GETARGS, even after
;      other programs have been started.
;   2. execve(path, argv, envp) replaces the calling task's image and passes
;      argv through to the new image.
;   3. The parent survives the child's exec (private program window) and can
;      still waitpid() on it.
;
; Run as:  argv_test alpha
; Output:  ARGS:alpha
;          ARGS:from-execve
;          EXECVE_OK
;
; The exec'd copy is recognised by its argument string and stops instead of
; forking again.

%include "syscalls.inc"

start:
        ; --- report the arguments this launch received ---
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        cmp eax, 0
        jl .fail

        mov eax, SYS_PRINT
        mov ebx, msg_args
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, argbuf
        int 0x80
        call newline

        ; --- the exec'd copy stops here (its argv is the marker) ---
        mov esi, argbuf
        mov edi, marker
        call str_eq
        jc .do_fork
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.do_fork:
        mov eax, SYS_FORK
        int 0x80
        cmp eax, 0
        je .child
        cmp eax, -1
        je .fail
        mov [child_pid], eax

        ; --- parent: block until the child is gone, then report ---
        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        cmp eax, -1
        je .fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

        ; --- child: replace ourselves with argv_test from-execve ---
.child:
        mov eax, SYS_EXECVE
        mov ebx, prog_path
        mov ecx, child_argv
        xor edx, edx
        int 0x80

        ; execve only returns when it failed
.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

;---------------------------------------
; str_eq - compare two NUL-terminated strings
;   ESI, EDI -> CF=0 equal, CF=1 different
;---------------------------------------
str_eq:
        cld
.se_loop:
        lodsb
        cmp al, [edi]
        jne .se_diff
        test al, al
        jz .se_equal
        inc edi
        jmp .se_loop
.se_diff:
        stc
        ret
.se_equal:
        clc
        ret

newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

msg_args:   db "ARGS:", 0
msg_ok:     db "EXECVE_OK", 10, 0
msg_fail:   db "EXECVE_FAIL", 10, 0
prog_path:  db "argv_test", 0
marker:     db "from-execve", 0
argv0:      db "argv_test", 0
argv1:      db "from-execve", 0
child_argv: dd argv0, argv1, 0

child_pid:  dd 0
argbuf:     times 512 db 0
