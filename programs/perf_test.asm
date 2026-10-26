; perf_test.asm - the guest half of the Latency and Footprint gates
;
; docs/ROADMAP.md's gate table names numbers - "syscall < 1 us", "context switch
; < 5 us", "fork+exec < 5 ms", "< 16 MB RAM idle" - and until this program
; existed nothing measured them: `make bench` times host make targets, so it
; measures the developer's machine, not this one. This runs inside the guest,
; on the machine profile the rest of the suite uses, and reports the same
; numbers the gate names.
;
; The clock is rdtsc, which ring 3 may read because the kernel never sets
; CR4.TSD. Cycles are reported rather than microseconds on purpose: the TSC
; rate is not architectural and nothing in the guest states it, so converting
; needs an anchor the kernel does not publish yet (PLATFORM.md gives the
; nanosecond clock of record to M2). Measured against a sleep of known ticks,
; the guest TSC rate came out anywhere between 163 and 6427 cycles/us across
; runs on the same machine, which is not a conversion - it is a guess with
; four digits. The cycle counts themselves are steady to about 2% (a syscall
; round trip is 470-481 cycles over repeated runs), so they are what the gate
; compares, and PERF_CYCLES_PER_TICK records the anchor for when M2 publishes
; one.
;
; Output, one KEY=VALUE per line, then PERF_OK:
;   PERF_TICKS_SLEPT      ticks the calibration sleep actually consumed
;   PERF_CYCLES_PER_TICK  TSC cycles per 10 ms scheduler tick (anchor, uncalibrated)
;   PERF_SYSCALL_CYC      one SYS_GETPID round trip
;   PERF_YIELD_CYC        one SYS_YIELD with a second task ready
;   PERF_SPAWN_CYC        fork + exec + waitpid of programs/nop.asm
;   PERF_FREE_PAGES_BOOT  free frames once the kernel had finished booting
;   PERF_FREE_PAGES_NOW   free frames before this program allocated anything
;
; The free-frame pair is what the footprint gate needs: the test knows the
; profile's RAM, so reserved = total - free-at-boot, and "idle" is the same
; figure because nothing has allocated since.

%include "syscalls.inc"
%include "lib/string.inc"        ; io.inc's padded helpers call str_len
%include "lib/io.inc"

CAL_TICKS       equ 50              ; 500 ms of sleep (ticks are 10 ms)
ITERS_SYSCALL   equ 20000
ITERS_YIELD     equ 2000
ITERS_SPAWN     equ 8               ; each spawn is ~10 ms of real work

start:
        ; --- footprint, before this program touches anything ---
        mov eax, SYS_MEMINFO
        int 0x80
        mov [free_pages_boot], ebx
        mov [free_pages_now], eax

        ; --- calibrate the TSC against the tick ---
        mov eax, SYS_GETTIME
        int 0x80
        mov [ticks_before], eax
        rdtsc
        mov [tsc_before], eax
        mov [tsc_before + 4], edx
        mov eax, SYS_SLEEP
        mov ebx, CAL_TICKS
        int 0x80
        rdtsc
        mov [tsc_after], eax
        mov [tsc_after + 4], edx
        mov eax, SYS_GETTIME
        int 0x80
        sub eax, [ticks_before]
        mov [ticks_slept], eax
        ; 64-bit delta: a 500 ms window passes 2^32 cycles on a fast part, and
        ; a truncated delta is a wildly wrong rate rather than a small error.
        mov eax, [tsc_after]
        mov edx, [tsc_after + 4]
        sub eax, [tsc_before]
        sbb edx, [tsc_before + 4]
        mov ecx, [ticks_slept]
        test ecx, ecx
        jz .fail_calibration
        div ecx                         ; EAX = cycles per tick
        test eax, eax
        jz .fail_calibration
        mov [cycles_per_tick], eax

        ; --- one syscall round trip ---
        rdtsc
        mov [t_stamp], eax
        mov ecx, ITERS_SYSCALL
.syscall_loop:
        mov eax, SYS_GETPID
        int 0x80
        dec ecx
        jnz .syscall_loop
        rdtsc
        sub eax, [t_stamp]
        xor edx, edx
        mov ecx, ITERS_SYSCALL
        div ecx                         ; EAX = cycles per call
        mov [cyc_syscall], eax

        ; --- one yield with two tasks ready ---
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail
        test eax, eax
        jz .yield_child
        mov [yield_child_pid], eax

        rdtsc
        mov [t_stamp], eax
        mov ecx, ITERS_YIELD
.yield_loop:
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .yield_loop
        rdtsc
        sub eax, [t_stamp]
        mov [yield_cycles], eax
        ; The peer is killed rather than reaped: this program measures how long
        ; a yield takes, and a child that has to be waited for adds its own
        ; bookkeeping to the window. The shell reaps the zombie.
        mov ebx, [yield_child_pid]
        mov ecx, 9                      ; SIGKILL
        mov eax, SYS_KILL
        int 0x80
        mov eax, [yield_cycles]
        xor edx, edx
        mov ecx, ITERS_YIELD
        div ecx
        mov [cyc_yield], eax
        jmp .spawn

.yield_child:
        mov ecx, ITERS_YIELD
.yield_child_loop:
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .yield_child_loop
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

        ; --- fork + exec + waitpid ---
.spawn:
        rdtsc
        mov [t_stamp], eax
        mov dword [spawn_left], ITERS_SPAWN
.spawn_loop:
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail
        test eax, eax
        jz .spawn_child
        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        dec dword [spawn_left]
        jnz .spawn_loop
        rdtsc
        sub eax, [t_stamp]
        xor edx, edx
        mov ecx, ITERS_SPAWN
        div ecx                         ; EAX = cycles per spawn
        mov [cyc_spawn], eax
        jmp .report

.spawn_child:
        mov eax, SYS_EXEC
        mov ebx, prog_nop
        int 0x80
        ; Reaching here means the exec was refused. Say so rather than exit 1
        ; silently: the parent would only report a failed spawn, and the reason
        ; lives on this side of the fork.
        mov esi, msg_fail_exec
        call io_print
        call io_newline
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

        ; --- report ---
.report:
        mov esi, msg_ticks_slept
        call io_print
        mov eax, [ticks_slept]
        call io_print_u32
        call io_newline

        mov esi, msg_cpu_rate
        call io_print
        mov eax, [cycles_per_tick]
        call io_print_u32
        call io_newline

        mov esi, msg_syscall
        call io_print
        mov eax, [cyc_syscall]
        call io_print_u32
        call io_newline

        mov esi, msg_yield
        call io_print
        mov eax, [cyc_yield]
        call io_print_u32
        call io_newline

        mov esi, msg_spawn
        call io_print
        mov eax, [cyc_spawn]
        call io_print_u32
        call io_newline

        mov esi, msg_free_boot
        call io_print
        mov eax, [free_pages_boot]
        call io_print_u32
        call io_newline

        mov esi, msg_free_now
        call io_print
        mov eax, [free_pages_now]
        call io_print_u32
        call io_newline

        mov esi, msg_ok
        call io_print
        call io_newline

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

        ; --- failure paths ---
.fail_calibration:
        mov esi, msg_fail_cal
        call io_print
        call io_newline
        jmp .exit_fail
.fail:
        mov esi, msg_fail
        call io_print
        call io_newline
.exit_fail:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

t_stamp:            dd 0
cycles_per_tick:    dd 0
cyc_syscall:        dd 0
cyc_yield:          dd 0
cyc_spawn:          dd 0
yield_cycles:       dd 0
yield_child_pid:    dd 0
spawn_left:         dd 0
free_pages_boot:    dd 0
free_pages_now:     dd 0
ticks_before:       dd 0
ticks_slept:        dd 0
tsc_before:         dd 0, 0
tsc_after:          dd 0, 0

msg_ticks_slept:   db "PERF_TICKS_SLEPT=", 0
msg_cpu_rate:      db "PERF_CYCLES_PER_TICK=", 0
msg_syscall:       db "PERF_SYSCALL_CYC=", 0
msg_yield:         db "PERF_YIELD_CYC=", 0
msg_spawn:         db "PERF_SPAWN_CYC=", 0
msg_free_boot:     db "PERF_FREE_PAGES_BOOT=", 0
msg_free_now:      db "PERF_FREE_PAGES_NOW=", 0
msg_ok:            db "PERF_OK", 0
msg_fail:          db "PERF_FAIL", 0
msg_fail_cal:      db "PERF_FAIL_CALIBRATION", 0
msg_fail_exec:     db "PERF_FAIL_EXEC", 0
prog_nop:          db "/bin/nop", 0
