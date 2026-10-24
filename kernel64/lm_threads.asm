;=======================================================================
; lm_threads.asm - /threads, two threads in one address space
;
; The difference between fork and clone, made visible: the thread this program
; starts runs in the *same* memory, so it writes into a buffer this one can then
; read. It gets its own stack, out of the heap, and its own return path - it
; exits, and the waitpid that collects it is the program's.
;
; Nothing here is serialised: the thread writes and the main path only reads after
; the thread has exited, which is the only ordering this kernel can offer until
; there is something to block on.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT    equ 3
SYS_GETTID  equ 5
SYS_WRITE   equ 6
SYS_YIELD   equ 7
SYS_MALLOC  equ 27
SYS_WAIT    equ 38
SYS_CLONE   equ 43

LINE_BUF   equ 0x40001200
LINE_LIMIT equ 0x40001500
DEC_BUF    equ 0x40001600
SHARED     equ 0x40001700       ; the buffer both threads touch
THREAD_TOP equ 0x40001800       ; where the thread's stack pointer starts

start:
        cld

        mov eax, SYS_GETTID
        syscall
        mov r15, rax                    ; this thread's id

        call begin_line
        lea rsi, [rel s_main]
        call add_str
        mov eax, r15d
        call add_dec
        call end_line

        ; A stack for the thread: a heap block, and its top is the far end.
        mov edi, 4096
        mov eax, SYS_MALLOC
        syscall
        test rax, rax
        jz fail
        lea rsi, [rax + 4096]

        lea rdi, [rel thread_entry]
        mov eax, SYS_CLONE
        syscall
        test rax, rax
        js fail
        mov r14, rax                    ; the thread's id

        call begin_line
        lea rsi, [rel s_cloned]
        call add_str
        mov eax, r14d
        call add_dec
        call end_line

        ; Wait for it the way a syscall that cannot block has to: ask again.
.again:
        mov rdi, r14
        mov eax, SYS_WAIT
        syscall
        cmp rax, -2
        jne .reaped
        mov eax, SYS_YIELD
        syscall
        jmp .again

.reaped:
        mov r13, rax                    ; the thread's exit code

        ; What the thread wrote is now in this program's memory, which is the
        ; whole point of sharing it.
        call begin_line
        lea rsi, [rel s_shared]
        call add_str
        lea rsi, [SHARED]
        call add_str
        call end_line

        call begin_line
        lea rsi, [rel s_code]
        call add_str
        mov rax, r13
        call add_hex8
        call end_line

        xor edi, edi
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
; thread_entry: where the other thread begins. It has no arguments - it finds
; the buffer itself, which it can, because it is the same buffer.
;-----------------------------------------------------------------------
thread_entry:
        cld
        mov rdi, SHARED
        lea rsi, [rel s_written]
.copy:
        lodsb
        test al, al
        jz .copied
        mov [rdi], al
        inc rdi
        jmp .copy
.copied:
        mov byte [rdi], 0

        mov edi, 7                      ; the code the main path has to collect
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

;-----------------------------------------------------------------------
; Line assembly. RBP is the write cursor.
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

; add_dec: eax = value, in decimal. syscall clobbers rcx and r11, so the value
; has to be loaded after the last add_str and the divide reloads ecx each digit.
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

; add_hex8: rax in eight hex digits, because an exit code reads better that way.
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

s_main:    db "threads: main tid=", 0
s_cloned:  db "threads: cloned tid=", 0
s_shared:  db "threads: shared buffer reads '", 0
s_code:    db "threads: thread exit=0x", 0
s_fail:    db "threads FAILED", 0
s_written: db "hello from the other thread", 0
