;=======================================================================
; lm_tls.asm - /tls, thread-local storage that is really per thread
;
; Both threads live in one address space, so the only thing that can keep their
; data apart is the base the kernel loads into FS on every switch. The main path
; points FS at one block, the thread points it at another, and each writes and
; then reads back through `fs:[0]` - which reads the block the *task* is
; pointing at, not the one the address space would suggest.
;
; The main path also yields while the thread runs, so the switch really happens
; in the middle of its own value being live.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT   equ 3
SYS_WRITE  equ 6
SYS_YIELD  equ 7
SYS_MALLOC equ 27
SYS_WAIT   equ 38
SYS_CLONE  equ 43
SYS_SET_TLS equ 46

LINE_BUF   equ 0x40001200
LINE_LIMIT equ 0x40001500
DEC_BUF    equ 0x40001600
THREAD_SEEN equ 0x40001700      ; what the thread read back from its own block
TLS_MAIN   equ 0x40001710       ; the main path's block, at a fixed address
TLS_THREAD equ 0x40001780       ; and the thread's, which it finds without help:
                                ; a new thread starts with zeroed registers, so a
                                ; register is not a way to pass it anything

start:
        cld

        ; A stack for the thread, then the two blocks it and this task will use.
        mov edi, 4096
        mov eax, SYS_MALLOC
        syscall
        test rax, rax
        jz fail
        lea rsi, [rax + 4096]

        ; FS points at this task's block, then the value goes in through it.
        mov edi, TLS_MAIN
        mov eax, SYS_SET_TLS
        syscall
        test rax, rax
        jnz fail
        mov qword [fs:0], 0x1111

        lea rdi, [rel thread_entry]
        mov eax, SYS_CLONE
        syscall
        test rax, rax
        js fail
        mov r14, rax

        ; Give the thread time to run and to overwrite its own block.
        mov r15, 10
.run:
        mov eax, SYS_YIELD
        syscall
        dec r15
        jnz .run

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

        ; What this task reads through FS has to be its own value, not the one
        ; the thread wrote into its block.
        mov rax, [fs:0]
        mov r15, rax

        call begin_line
        lea rsi, [rel s_main]
        call add_str
        mov rax, r15
        call add_hex4
        lea rsi, [rel s_thread]
        call add_str
        mov rax, [THREAD_SEEN]
        call add_hex4
        call end_line

        ; The proof: two different values, one address space, two bases.
        cmp r15, 0x1111
        jne fail
        cmp qword [THREAD_SEEN], 0x2222
        jne fail

        call begin_line
        lea rsi, [rel s_ok]
        call add_str
        call end_line
        xor edi, edi
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

;-----------------------------------------------------------------------
; thread_entry: the second task, with a block and a base of its own.
;-----------------------------------------------------------------------
thread_entry:
        cld
        mov edi, TLS_THREAD             ; its own block, at a known address
        mov eax, SYS_SET_TLS
        syscall
        test rax, rax
        jnz .done
        mov qword [fs:0], 0x2222

        ; A few switches while the value is live, then read it back through FS.
        mov ebx, 5
.loop:
        mov eax, SYS_YIELD
        syscall
        dec ebx
        jnz .loop

        mov rax, [fs:0]
        mov [THREAD_SEEN], rax
.done:
        mov edi, 0x22
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

add_hex4:
        lea rsi, [DEC_BUF + 31]
        mov byte [rsi], 0
        mov ecx, 4
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

s_main:   db "tls: main read ", 0
s_thread: db " thread read ", 0
s_ok:     db "tls: two bases, one address space", 0
s_fail:   db "tls FAILED", 0
