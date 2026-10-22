;=======================================================================
; lm_futex.asm - /futex, a thread that sleeps until it is told
;
; The two threads share one word. The second one waits on it: futex_wait parks it
; in the kernel rather than spinning in user space, and futex_wake puts it back on
; the run queue. Nothing polls.
;
; The order matters and is the whole discipline of a futex: the waiter checks the
; word, and calls wait *with the value it saw* - so a wake that arrives in between
; the check and the call is not lost, because the kernel re-reads the word and
; returns at once if it has already changed.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT       equ 3
SYS_WRITE      equ 6
SYS_YIELD      equ 7
SYS_MALLOC     equ 27
SYS_WAIT       equ 38
SYS_CLONE      equ 43
SYS_FUTEX_WAIT equ 44
SYS_FUTEX_WAKE equ 45

LINE_BUF   equ 0x40001200
LINE_LIMIT equ 0x40001500
DEC_BUF    equ 0x40001600
FLAG       equ 0x40001700       ; the word both threads look at
SEEN       equ 0x40001708       ; what the sleeper writes when it wakes

start:
        cld
        mov qword [FLAG], 0
        mov qword [SEEN], 0

        ; A stack for the sleeper, out of the heap.
        mov edi, 4096
        mov eax, SYS_MALLOC
        syscall
        test rax, rax
        jz fail
        lea rsi, [rax + 4096]
        lea rdi, [rel sleeper]
        mov eax, SYS_CLONE
        syscall
        test rax, rax
        js fail
        mov r14, rax

        call begin_line
        lea rsi, [rel s_cloned]
        call add_str
        mov eax, r14d
        call add_dec
        call end_line

        ; Let the sleeper get as far as parking itself, so this is a wake of a
        ; task that is really asleep and not a lucky race.
        mov r13, 8
.pause:
        mov eax, SYS_YIELD
        syscall
        dec r13
        jnz .pause

        mov qword [FLAG], 1             ; the news
        lea rdi, [FLAG]
        mov esi, 1
        mov eax, SYS_FUTEX_WAKE
        syscall

        ; Collect the sleeper, then print what it left behind.
.wait:
        mov rdi, r14
        mov eax, SYS_WAIT
        syscall
        cmp rax, -2
        jne .collected
        mov eax, SYS_YIELD
        syscall
        jmp .wait
.collected:
        mov r15, rax

        call begin_line
        lea rsi, [rel s_woke]
        call add_str
        mov rax, [SEEN]
        call add_dec
        lea rsi, [rel s_exit]
        call add_str
        mov rax, r15
        call add_hex8
        call end_line

        xor edi, edi
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

;-----------------------------------------------------------------------
; sleeper: waits for the flag, then records that it saw it.
;-----------------------------------------------------------------------
sleeper:
        cld
.again:
        mov rax, [FLAG]                 ; what the word says now
        test rax, rax
        jnz .awake                      ; already set: do not wait at all
        lea rdi, [FLAG]
        mov rsi, rax                    ; ... and wait for that value to change
        mov eax, SYS_FUTEX_WAIT
        syscall
        jmp .again                      ; woken (or raced): look again
.awake:
        mov qword [SEEN], rax
        mov edi, 3                      ; the code the parent collects
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

fail:
        call begin_line
        lea rsi, [rel s_fail]
        call add_str
        call end_line
        mov edi, 0xBAD
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

add_hex8:
        lea rsi, [DEC_BUF + 31]
        mov byte [rsi], 0
        mov ecx, 8
.digits:
        mov edx, eax
        and edx, 0xF
        cmp dl, 10
        jb .digit
        add dl, 'a' - 10
        jmp .store
.digit:
        add dl, '0'
.store:
        dec rsi
        mov [rsi], dl
        shr eax, 4
        dec ecx
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

s_cloned: db "futex: sleeper tid=", 0
s_woke:   db "futex: sleeper woke with flag=", 0
s_exit:   db " exit=0x", 0
s_fail:   db "futex FAILED", 0
