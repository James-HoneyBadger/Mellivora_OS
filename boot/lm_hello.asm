;=======================================================================
; lm_hello.asm - the 64-bit demo program, /hello on the HBFS volume
;
; /init execs this. It proves the parts of P2 that need ring 3: a fault the
; handler resumes, write with a user buffer under SMAP, a file read off the
; disk, brk grow/shrink, a voluntary yield, timer preemption, and two tasks
; reading their own value back from the same virtual address.
;=======================================================================

[BITS 64]
[ORG 0]

SYS_PING   equ 1
SYS_WHOAMI equ 2
SYS_EXIT   equ 3
SYS_TICKS  equ 4
SYS_GETTID equ 5
SYS_WRITE  equ 6
SYS_YIELD  equ 7
SYS_BRK    equ 8
SYS_OPEN   equ 9
SYS_READ   equ 10
SYS_CLOSE  equ 11

USER_SLOT  equ 0x40001FE8       ; PING result, in this task's stack page
TASK_SLOT  equ 0x40001FF0       ; the task id, same address in every space
BUF        equ 0x40001F00       ; where /motd is read

start:
        int3

        ; write(1, "hello\n", 6)
        mov eax, SYS_WRITE
        mov edi, 1
        lea rsi, [rel msg]
        mov edx, msg_end - msg
        syscall
        cmp rax, msg_end - msg
        jne fail

        ; open("/motd"), read it, close it, write what was read.
        lea rdi, [rel motd_path]
        mov eax, SYS_OPEN
        syscall
        test rax, rax
        js fail
        mov r15, rax
        mov edi, r15d
        mov rsi, BUF
        mov edx, 64
        mov eax, SYS_READ
        syscall
        test rax, rax
        jle fail
        mov r12, rax
        mov edi, r15d
        mov eax, SYS_CLOSE
        syscall
        mov edi, 1
        mov rsi, BUF
        mov rdx, r12
        mov eax, SYS_WRITE
        syscall
        cmp rax, r12
        jne fail

        ; PING round trip through this task's own stack page.
        mov eax, SYS_PING
        mov edi, 0x1234
        syscall
        mov [abs USER_SLOT], rax

        ; The same virtual address in every task, backed by a different frame.
        mov eax, SYS_GETTID
        syscall
        mov [abs TASK_SLOT], rax

        ; brk: grow a page, write and read through it, shrink back.
        mov eax, SYS_BRK
        xor edi, edi
        syscall
        mov r13, rax
        lea rdi, [r13 + 4096]
        mov eax, SYS_BRK
        syscall
        cmp rax, rdi
        jne fail
        mov r8d, 0xFEEDFACE
        mov [r13], r8
        cmp qword [r13], r8
        jne fail
        mov rdi, r13
        mov eax, SYS_BRK
        syscall

        ; Hand the CPU to the other task once, on purpose.
        mov eax, SYS_YIELD
        syscall

        ; Wait for two more ticks; the other task runs whenever one arrives.
        mov eax, SYS_TICKS
        syscall
        lea r14, [rax + 2]
        mov ebx, 100000
.poll:
        mov eax, SYS_TICKS
        syscall
        cmp rax, r14
        jae done
        mov r8d, 2000
.spin:
        dec r8d
        jnz .spin
        dec ebx
        jnz .poll
        jmp fail

done:
        mov eax, SYS_WHOAMI
        syscall
        mov rsi, rax                    ; the nucleus checks this is its CPU id
        mov rdx, [abs USER_SLOT]        ; PING result back off the stack page
        mov rdi, [abs TASK_SLOT]        ; ... and the id this space held
        mov eax, SYS_EXIT               ; exit code = that id
        syscall
.hang:
        jmp .hang

fail:
        mov eax, SYS_EXIT
        mov edi, 0xBAD
        syscall
.hang:
        jmp .hang

msg:     db "hello", 10
msg_end:
motd_path: db "/motd", 0
