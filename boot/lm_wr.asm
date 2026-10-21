;=======================================================================
; lm_wr.asm - the write-path demo, /wr on the HBFS volume
;
; Proves the disk is writable, not just readable: create a file, stat it, read
; it back through the same open/read path, make a directory, put a file in it,
; then unlink that file and watch the directory report it gone. /persist.txt is
; deliberately left behind so the host can read it out of the image afterwards.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT    equ 3
SYS_WRITE   equ 6
SYS_OPEN    equ 9
SYS_READ    equ 10
SYS_CLOSE   equ 11
SYS_STAT    equ 14
SYS_READDIR equ 15
SYS_MKDIR   equ 18
SYS_UNLINK  equ 19
SYS_CREATE  equ 20

STAT_BUF equ 0x40001800
DATA_BUF equ 0x40001500
DEC_BUF  equ 0x40001700
STAT_TYPE equ STAT_BUF + 0
STAT_SIZE equ STAT_BUF + 4
NAME_BUF  equ STAT_BUF + 16

start:
        ; create("/persist.txt", text, len)
        lea rdi, [rel persist_path]
        lea rsi, [rel persist_text]
        mov edx, persist_text_end - persist_text
        mov eax, SYS_CREATE
        syscall
        test rax, rax
        js fail

        ; stat it: a text file with the size we just wrote.
        lea rdi, [rel persist_path]
        mov rsi, STAT_BUF
        mov eax, SYS_STAT
        syscall
        test rax, rax
        jnz fail
        mov eax, [STAT_TYPE]
        cmp eax, 1
        jne fail
        mov eax, [STAT_SIZE]
        cmp eax, persist_text_end - persist_text
        jne fail

        ; Read it back off the disk and print it, so the proof is on COM1 too.
        lea rdi, [rel persist_path]
        mov eax, SYS_OPEN
        syscall
        test rax, rax
        js fail
        mov r13, rax

        mov rdi, r13
        mov rsi, DATA_BUF
        mov edx, 64
        mov eax, SYS_READ
        syscall
        test rax, rax
        jle fail
        mov r12, rax

        mov rdi, r13
        mov eax, SYS_CLOSE
        syscall

        lea rsi, [rel s_readback]
        mov edx, s_readback_end - s_readback
        call print
        mov rsi, DATA_BUF
        mov rdx, r12
        call print

        ; A directory, and a file inside it.
        lea rdi, [rel tmpdir_path]
        mov eax, SYS_MKDIR
        syscall
        test rax, rax
        jnz fail

        lea rdi, [rel inner_path]
        lea rsi, [rel inner_text]
        mov edx, inner_text_end - inner_text
        mov eax, SYS_CREATE
        syscall
        test rax, rax
        js fail

        lea rdi, [rel inner_path]
        mov rsi, STAT_BUF
        mov eax, SYS_STAT
        syscall
        test rax, rax
        jnz fail
        mov eax, [STAT_SIZE]
        cmp eax, inner_text_end - inner_text
        jne fail

        ; It must show up in the directory listing...
        lea rdi, [rel tmpdir_path]
        xor esi, esi
        mov rdx, STAT_BUF
        mov eax, SYS_READDIR
        syscall
        cmp rax, 1
        jne fail

        ; ...and be gone from it after unlink.
        lea rdi, [rel inner_path]
        mov eax, SYS_UNLINK
        syscall
        test rax, rax
        jnz fail

        lea rdi, [rel inner_path]
        mov rsi, STAT_BUF
        mov eax, SYS_STAT
        syscall
        cmp rax, -1
        jne fail

        lea rdi, [rel tmpdir_path]
        xor esi, esi
        mov rdx, STAT_BUF
        mov eax, SYS_READDIR
        syscall
        cmp rax, 0                      ; entry 0 is free again
        jne fail

        lea rsi, [rel s_ok]
        mov edx, s_ok_end - s_ok
        call print
        call newline

        xor edi, edi
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

fail:
        lea rsi, [rel s_bad]
        mov edx, s_bad_end - s_bad
        call print
        call newline
        mov edi, 0xBAD
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

print:
        mov edi, 1
        mov eax, SYS_WRITE
        syscall
        ret

newline:
        lea rsi, [rel s_nl]
        mov edx, 1
        call print
        ret

s_nl:        db 10
s_readback:  db "read back: "
s_readback_end:
s_ok:        db "write path ok"
s_ok_end:
s_bad:       db "write path FAILED"
s_bad_end:
persist_path: db "/persist.txt", 0
tmpdir_path:  db "/tmpdir", 0
inner_path:   db "/tmpdir/inner.txt", 0
persist_text: db "written by mellivora64", 10
persist_text_end:
inner_text:   db "nested", 10
inner_text_end:
