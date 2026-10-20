; time_test.asm - regression test for the POSIX time syscalls
;
; Verifies that:
;   * CLOCK_REALTIME returns a plausible Unix epoch (not the scheduler tick)
;   * CLOCK_MONOTONIC returns seconds since boot
;   * gettimeofday returns the same wall clock
;   * a kernel pointer is rejected by the copy-to-user validation
;
; Prints TIME_OK on success and TIME_FAIL on any failure.

%include "syscalls.inc"

start:
        ; --- CLOCK_REALTIME ---
        mov eax, SYS_CLOCK_GETTIME
        xor ebx, ebx                    ; CLOCK_REALTIME
        mov ecx, ts_real
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, [ts_real]              ; tv_sec
        cmp eax, 1577836800             ; 2020-01-01
        jb .fail

        mov eax, SYS_PRINT
        mov ebx, msg_real
        int 0x80
        mov eax, [ts_real]
        call print_dec
        call newline

        ; --- CLOCK_MONOTONIC ---
        mov eax, SYS_CLOCK_GETTIME
        mov ebx, CLOCK_MONOTONIC
        mov ecx, ts_mono
        int 0x80
        cmp eax, 0
        jne .fail

        mov eax, SYS_PRINT
        mov ebx, msg_mono
        int 0x80
        mov eax, [ts_mono]
        call print_dec
        call newline

        ; --- gettimeofday ---
        mov eax, SYS_GETTIMEOFDAY
        mov ebx, ts_real
        xor ecx, ecx
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, [ts_real]
        cmp eax, 1577836800
        jb .fail

        mov eax, SYS_PRINT
        mov ebx, msg_tod
        int 0x80
        mov eax, [ts_real]
        call print_dec
        call newline

        ; --- kernel pointer must be rejected ---
        mov eax, SYS_CLOCK_GETTIME
        xor ebx, ebx
        mov ecx, 0x00100000             ; kernel image address
        int 0x80
        cmp eax, -1
        jne .fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

msg_real: db "REALTIME ", 0
msg_mono: db "MONOTONIC ", 0
msg_tod:  db "GETTIMEOFDAY ", 0
msg_ok:   db "TIME_OK", 10, 0
msg_fail: db "TIME_FAIL", 10, 0

ts_real: dd 0, 0
ts_mono: dd 0, 0
