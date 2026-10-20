; syscall_test.asm - user-buffer validation regression test
;
; Exercises the syscalls that previously trusted raw ring-3 pointers:
; getcwd, getargs, setenv, getenv_slot, dmesg_read, readdir, stat, fwrite,
; send and recv. Valid calls must work; kernel pointers must be rejected.
;
; Prints SYSCALL_OK on success and SYSCALL_FAIL on any failure.

%include "syscalls.inc"

start:
        ; --- getcwd: valid ---
        mov byte [marker], 'A'
        mov eax, SYS_GETCWD
        mov ebx, pathbuf
        int 0x80
        cmp eax, 0
        jne .fail
        cmp byte [pathbuf], '/'
        jne .fail

        ; --- getcwd: kernel pointer rejected ---
        mov byte [marker], 'B'
        mov eax, SYS_GETCWD
        mov ebx, 0x00100000
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- setenv: valid ---
        mov byte [marker], 'C'
        mov eax, SYS_SETENV
        mov ebx, env_name
        mov ecx, env_val
        mov edx, 1
        int 0x80
        cmp eax, 0
        jne .fail

        ; --- getenv: the variable is visible ---
        mov byte [marker], 'D'
        mov eax, SYS_GETENV
        mov ebx, env_name
        int 0x80
        test eax, eax
        jz .fail

        ; --- setenv: kernel value pointer rejected ---
        mov byte [marker], 'E'
        mov eax, SYS_SETENV
        mov ebx, env_name
        mov ecx, 0x00100000
        mov edx, 1
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- getenv_slot: kernel destination rejected ---
        mov byte [marker], 'F'
        mov eax, SYS_GETENV_SLOT
        xor ebx, ebx
        mov ecx, 0x00100000
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- dmesg_read: kernel destination rejected ---
        mov byte [marker], 'G'
        mov eax, SYS_DMESG_READ
        xor ebx, ebx
        mov ecx, 0x00100000
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- getargs: kernel destination rejected ---
        mov byte [marker], 'H'
        mov eax, SYS_GETARGS
        mov ebx, 0x00100000
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- readdir: kernel name buffer rejected ---
        mov byte [marker], 'I'
        mov eax, SYS_READDIR
        mov ebx, 0x00100000
        xor ecx, ecx
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- stat: valid buffer ---
        mov byte [marker], 'J'
        mov eax, SYS_STAT
        mov ebx, stat_name
        mov ecx, statbuf
        int 0x80
        cmp eax, 0
        jne .fail

        ; --- stat: kernel buffer rejected ---
        mov byte [marker], 'K'
        mov eax, SYS_STAT
        mov ebx, stat_name
        mov ecx, 0x00100000
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- fwrite: valid ---
        mov byte [marker], 'L'
        mov eax, SYS_FWRITE
        mov ebx, tmp_name
        mov ecx, payload
        mov edx, payload_len
        mov esi, 1
        int 0x80
        cmp eax, 0
        jne .fail

        ; --- fwrite: kernel source rejected ---
        mov byte [marker], 'M'
        mov eax, SYS_FWRITE
        mov ebx, tmp_name
        mov ecx, 0x00100000
        mov edx, 16
        mov esi, 1
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- fread: data survives ---
        mov byte [marker], 'N'
        mov eax, SYS_FREAD
        mov ebx, tmp_name
        mov ecx, readbuf
        int 0x80
        cmp eax, payload_len
        jne .fail
        cmp byte [readbuf], 'X'
        jne .fail

        ; --- send: kernel source rejected ---
        mov byte [marker], 'O'
        mov eax, SYS_SEND
        xor ebx, ebx
        mov ecx, 0x00100000
        mov edx, 16
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- recv: kernel destination rejected ---
        mov byte [marker], 'P'
        mov eax, SYS_RECV
        xor ebx, ebx
        mov ecx, 0x00100000
        mov edx, 16
        int 0x80
        cmp eax, -1
        jne .fail

        mov eax, SYS_PRINT
        mov ebx, ok_msg
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.fail:
        mov eax, SYS_PRINT
        mov ebx, fail_msg
        int 0x80
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [marker]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

env_name:  db "MBTEST", 0
env_val:   db "hello", 0
stat_name: db "/bin/sysinfo", 0
tmp_name:  db "/tmp/syscall_test.tmp", 0
payload:   db "X1234567"
payload_len equ $ - payload

ok_msg:    db "SYSCALL_OK", 10, 0
fail_msg:  db "SYSCALL_FAIL at ", 0
marker:    db '?'

pathbuf:   times 256 db 0
readbuf:   times 64 db 0
statbuf:   times 12 db 0
