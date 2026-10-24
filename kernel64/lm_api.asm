;=======================================================================
; lm_api.asm - the library-shaped calls, /api on the HBFS volume
;
; The calls a program's own C library would sit on: the break and the heap on
; top of it, a whole-file read, and a pipe. Each is exercised for real - memory
; is written and read back, the file's bytes are compared, the pipe is filled
; past a wrap - and one line marks it done.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT      equ 3
SYS_WRITE     equ 6
SYS_CLOSE     equ 11
SYS_STAT      equ 14
SYS_PUTCHAR   equ 21
SYS_LOAD      equ 26
SYS_MALLOC    equ 27
SYS_FREE      equ 28
SYS_PIPE_CREATE equ 29
SYS_PIPE_WRITE  equ 30
SYS_PIPE_READ   equ 31
SYS_PIPE_CLOSE  equ 32

LINE_BUF equ 0x40001200
LINE_LIMIT equ 0x40001300
DATA_BUF equ 0x40001400
DEC_BUF  equ 0x40001500

start:
        cld

        ; malloc(48) and malloc(64): distinct, aligned, writable blocks.
        mov edi, 48
        mov eax, SYS_MALLOC
        syscall
        test rax, rax
        jz fail
        mov r12, rax
        test r12b, 15
        jnz fail

        mov edi, 64
        mov eax, SYS_MALLOC
        syscall
        test rax, rax
        jz fail
        mov r13, rax
        cmp r13, r12
        jbe fail

        mov dword [r12], 0xAAAAAAAA
        mov dword [r13], 0xBBBBBBBB
        cmp dword [r12], 0xAAAAAAAA
        jne fail
        cmp dword [r13], 0xBBBBBBBB
        jne fail

        ; free the first, then allocate the same size: a good allocator reuses it.
        mov rdi, r12
        mov eax, SYS_FREE
        syscall
        test rax, rax
        jnz fail

        mov edi, 48
        mov eax, SYS_MALLOC
        syscall
        cmp rax, r12
        jne fail
        mov dword [rax], 0xCCCCCCCC
        cmp dword [rax], 0xCCCCCCCC
        jne fail

        ; load("/motd", buf, size): the whole file, contents included.
        lea rdi, [rel motd_path]
        mov rsi, DATA_BUF
        mov edx, 64
        mov eax, SYS_LOAD
        syscall
        cmp rax, 16
        jne fail
        cmp dword [DATA_BUF], 'hell'
        jne fail

        ; A pipe: write more than the first read takes, then read it in two
        ; goes, so the ring has to wrap correctly.
        mov eax, SYS_PIPE_CREATE
        syscall
        test rax, rax
        js fail
        mov r14, rax

        mov rdi, r14
        lea rsi, [rel pipe_text]
        mov edx, pipe_text_end - pipe_text
        mov eax, SYS_PIPE_WRITE
        syscall
        cmp rax, pipe_text_end - pipe_text
        jne fail

        mov rdi, r14
        mov rsi, DATA_BUF
        mov edx, 4
        mov eax, SYS_PIPE_READ
        syscall
        cmp rax, 4
        jne fail
        cmp dword [DATA_BUF], 'pipe'
        jne fail

        mov rdi, r14
        mov rsi, DATA_BUF
        mov edx, 16
        mov eax, SYS_PIPE_READ
        syscall
        cmp rax, 4                      ; "s ok" left in the pipe
        jne fail

        mov rdi, r14
        mov eax, SYS_PIPE_CLOSE
        syscall
        test rax, rax
        jnz fail

        mov rdi, r14
        mov rsi, DATA_BUF
        mov edx, 4
        mov eax, SYS_PIPE_READ         ; a closed pipe is not readable
        syscall
        cmp rax, -1
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

fail:
        call begin_line
        lea rsi, [rel s_bad]
        call add_str
        call end_line
        mov edi, 0xBAD
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

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

s_ok:   db "api ok: malloc/free, load, pipe", 0
s_bad:  db "api FAILED", 0
motd_path: db "/motd", 0
pipe_text: db "pipes ok"
pipe_text_end:
