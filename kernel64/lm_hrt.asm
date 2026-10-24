;=======================================================================
; lm_hrt.asm - /hrt, sleeps that are deadlines and not ticks
;
; Two hundred one-millisecond sleeps. On a tick this is the one program that
; cannot work: a sleep expressed in ticks rounds to zero (nothing happens) or up
; to ten milliseconds (two seconds of waiting for two hundred milliseconds of
; sleep). Against a deadline queue it takes about two hundred milliseconds, which
; is what the second number on each line is for.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT   equ 3
SYS_WRITE  equ 6
SYS_SLEEP  equ 24
SYS_TIME   equ 47

SLOW       equ 0x40001700              ; how many sleeps overran by more than 5 ms

LINE_BUF   equ 0x40001200
LINE_LIMIT equ 0x40001500
DEC_BUF    equ 0x40001600

start:
        cld
        mov eax, SYS_TIME
        syscall
        mov r12, rax                    ; the counter before

        mov r13d, 200
        xor r15d, r15d                  ; the worst single sleep, in nanoseconds
        mov qword [SLOW], 0
.each:
        mov eax, SYS_TIME
        syscall
        mov rbx, rax                    ; before this one
        mov edi, 1
        mov eax, SYS_SLEEP
        syscall
        mov eax, SYS_TIME
        syscall
        sub rax, rbx
        cmp rax, r15
        jbe .counted
        mov r15, rax                    ; a wake-up that was held off by a stall
.counted:
        cmp rax, 5000000                ; more than five times what was asked for
        jbe .ontime
        inc qword [SLOW]
.ontime:
        dec r13d
        jnz .each

        mov eax, SYS_TIME
        syscall
        sub rax, r12
        xor edx, edx
        mov ecx, 1000000
        div rcx                         ; nanoseconds -> milliseconds
        mov r14, rax

        call begin_line
        lea rsi, [rel s_many]
        call add_str
        mov rax, r14
        call add_dec
        lea rsi, [rel s_ms]
        call add_str

        lea rsi, [rel s_worst]
        call add_str
        mov rax, r15
        xor edx, edx
        mov ecx, 1000                   ; nanoseconds -> microseconds
        div rcx
        call add_dec
        lea rsi, [rel s_over]
        call add_str
        mov rax, [SLOW]
        call add_dec
        call end_line

        ; One long sleep, to show the same queue serves a wait that has nothing
        ; to do with the tick either.
        mov eax, SYS_TIME
        syscall
        mov r12, rax
        mov edi, 50
        mov eax, SYS_SLEEP
        syscall
        mov eax, SYS_TIME
        syscall
        sub rax, r12
        xor edx, edx
        mov ecx, 1000000
        div rcx
        mov r15, rax

        call begin_line
        lea rsi, [rel s_long]
        call add_str
        mov rax, r15
        call add_dec
        lea rsi, [rel s_ms]
        call add_str
        call end_line

        ; The verdict is on the individual sleeps and not on the total, because
        ; the total also contains whatever this machine did to the guest in
        ; between. A sleep expressed in ticks is *never* close to what was asked
        ; for - it is zero or a whole ten milliseconds - while a deadline is close
        ; every time the wake-up is not held up by the task that was running. A
        ; tenth of them being late is a wide enough gate for a busy host and a
        ; thousand times narrower than the tick.
        cmp qword [SLOW], 20
        ja .slow

        call begin_line
        lea rsi, [rel s_ok]
        call add_str
        call end_line
        xor edi, edi
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

.slow:
        call begin_line
        lea rsi, [rel s_slow]
        call add_str
        call end_line
        mov edi, 0x51
        mov eax, SYS_EXIT
        syscall
.hang2:
        jmp .hang2

;-----------------------------------------------------------------------
; Line assembly.
;-----------------------------------------------------------------------
begin_line:
        lea rbp, [LINE_BUF]
        ret

add_str:
        cld
        lodsb
        test al, al
        jz .done
        cmp rbp, LINE_LIMIT
        jae .done
        mov [rbp], al
        inc rbp
        jmp add_str
.done:
        ret

add_dec:
        lea rsi, [DEC_BUF + 31]
        mov byte [rsi], 0
.digits:
        mov ecx, 10
        xor edx, edx
        div ecx
        add dl, '0'
        dec rsi
        mov [rsi], dl
        test eax, eax
        jnz .digits
        call add_str
        ret

end_line:
        mov byte [rbp], 10
        inc rbp
        lea rsi, [LINE_BUF]
        mov rdx, rbp
        sub rdx, rsi
        mov edi, 1
        mov eax, SYS_WRITE
        syscall
        ret

s_many: db "hrt: 200 x 1ms sleeps took ", 0
s_long: db "hrt: one 50ms sleep took ", 0
s_ms:   db "ms", 0
s_worst: db ", worst ", 0
s_over: db "us, over 5ms=", 0
s_ok:   db "hrt: deadlines, not ticks", 0
s_slow: db "hrt: FAILED these are ticks, not deadlines", 0
