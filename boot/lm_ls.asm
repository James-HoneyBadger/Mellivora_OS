;=======================================================================
; lm_ls.asm - the 64-bit directory demo, /ls on the HBFS volume
;
; Exercises the file metadata calls the 32-bit tree's own programs expect:
; getcwd, readdir, stat, chdir and seek, plus the open/read/close they sit on
; top of. It runs as its own task, so a call that returns nonsense fails the
; boot test rather than a shell session.
;=======================================================================

[BITS 64]
[ORG 0x40000000]        ; the address the nucleus maps the code page at

SYS_EXIT    equ 3
SYS_WRITE   equ 6
SYS_OPEN    equ 9
SYS_READ    equ 10
SYS_CLOSE   equ 11
SYS_SEEK    equ 13
SYS_STAT    equ 14
SYS_READDIR equ 15
SYS_CHDIR   equ 16
SYS_GETCWD  equ 17

; This program lives in one code page plus one stack page (0x40001000), so its
; buffers sit low in the stack page and the call stack grows down from
; 0x40002000 into the space above them.
CWD_BUF  equ 0x40001C00         ; getcwd target
STAT_BUF equ 0x40001800         ; struct vfs_stat: type, size, blocks, start
DATA_BUF equ 0x40001500         ; what seek/read pulls out of /motd
DEC_BUF  equ 0x40001700

; struct vfs_stat: u32 type, u32 size, u32 blocks, u32 start, char name[256]
STAT_TYPE  equ STAT_BUF + 0
STAT_SIZE  equ STAT_BUF + 4
NAME_BUF   equ STAT_BUF + 16

start:
        ; getcwd(buf, size) -> length
        mov rdi, CWD_BUF
        mov esi, 128
        mov eax, SYS_GETCWD
        syscall
        test rax, rax
        js fail
        mov r12, rax

        lea rsi, [rel s_cwd]
        mov edx, s_cwd_end - s_cwd
        call print
        mov rsi, CWD_BUF
        mov rdx, r12
        call print
        call newline

        ; List the working directory.
        xor edi, edi                    ; NULL path = the cwd
        call list_dir

        ; stat("/motd"): a real file, so type 1 and a non-zero size.
        lea rdi, [rel motd_path]
        mov rsi, STAT_BUF
        mov eax, SYS_STAT
        syscall
        test rax, rax
        jnz fail

        lea rsi, [rel s_motd]
        mov edx, s_motd_end - s_motd
        call print
        mov eax, [STAT_TYPE]
        call put_dec
        lea rsi, [rel s_size]
        mov edx, s_size_end - s_size
        call print
        mov eax, [STAT_SIZE]
        call put_dec
        call newline

        ; open/seek/read: skip the first four bytes of /motd and read four.
        lea rdi, [rel motd_path]
        mov eax, SYS_OPEN
        syscall
        test rax, rax
        js fail
        mov r13, rax

        mov rdi, r13
        mov esi, 4
        xor edx, edx                    ; whence 0 = from the start
        mov eax, SYS_SEEK
        syscall
        cmp rax, 4
        jne fail

        mov rdi, r13
        mov rsi, DATA_BUF
        mov edx, 4
        mov eax, SYS_READ
        syscall
        cmp rax, 4
        jne fail

        mov rdi, r13
        mov eax, SYS_CLOSE
        syscall

        lea rsi, [rel s_seek]
        mov edx, s_seek_end - s_seek
        call print
        mov rsi, DATA_BUF
        mov edx, 4
        call print
        call newline

        ; chdir into the subdirectory and list that.
        lea rdi, [rel docs_path]
        mov eax, SYS_CHDIR
        syscall
        test rax, rax
        jnz fail

        mov rdi, CWD_BUF
        mov esi, 128
        mov eax, SYS_GETCWD
        syscall
        test rax, rax
        js fail
        mov r12, rax
        lea rsi, [rel s_cwd]
        mov edx, s_cwd_end - s_cwd
        call print
        mov rsi, CWD_BUF
        mov rdx, r12
        call print
        call newline

        xor edi, edi
        call list_dir

        ; A name that is not there must report -1, not a stale entry.
        lea rdi, [rel missing_path]
        mov rsi, STAT_BUF
        mov eax, SYS_STAT
        syscall
        cmp rax, -1
        jne fail
        lea rsi, [rel s_miss]
        mov edx, s_miss_end - s_miss
        call print
        call newline

        xor edi, edi
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

fail:
        mov eax, SYS_EXIT
        mov edi, 0xBAD
        syscall
.hang:
        jmp .hang

;-----------------------------------------------------------------------
; list_dir: rdi = path (0 = the working directory). Prints one line per
; entry: index, name, type and size, until readdir says there are no more.
;-----------------------------------------------------------------------
list_dir:
        mov r15, rdi
        xor r14, r14
.loop:
        mov rdi, r15
        mov rsi, r14
        mov rdx, STAT_BUF
        mov eax, SYS_READDIR
        syscall
        cmp rax, 1
        jne .done

        lea rsi, [rel s_entry]
        mov edx, s_entry_end - s_entry
        call print
        mov rax, r14
        call put_dec
        lea rsi, [rel s_name]
        mov edx, s_name_end - s_name
        call print
        mov rsi, NAME_BUF
        mov rdx, 255
        call print_name
        lea rsi, [rel s_type]
        mov edx, s_type_end - s_type
        call print
        mov eax, [STAT_TYPE]
        call put_dec
        lea rsi, [rel s_size]
        mov edx, s_size_end - s_size
        call print
        mov eax, [STAT_SIZE]
        call put_dec
        call newline

        inc r14
        cmp r14, 16
        jb .loop
.done:
        ret

; The name buffer is a fixed 256 bytes, so print up to its NUL.
print_name:
        xor eax, eax
.count:
        cmp byte [NAME_BUF + rax], 0
        je .have
        inc eax
        cmp eax, 255
        jb .count
.have:
        mov rsi, NAME_BUF
        mov rdx, rax
        call print
        ret

; print: rsi = buffer, rdx = length.
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

; put_dec: eax = value, printed in decimal.
put_dec:
        lea rdi, [DEC_BUF + 31]
        mov byte [rdi], 0
.loop:
        mov ecx, 10
        xor edx, edx
        div ecx
        add dl, '0'
        dec rdi
        mov [rdi], dl
        test eax, eax
        jnz .loop
        lea rdx, [DEC_BUF + 31]
        sub rdx, rdi
        mov rsi, rdi
        call print
        ret

s_nl:      db 10
s_cwd:     db "cwd="
s_cwd_end:
s_entry:   db "ent "
s_entry_end:
s_name:    db " name="
s_name_end:
s_type:    db " type="
s_type_end:
s_size:    db " size="
s_size_end:
s_motd:    db "stat /motd type="
s_motd_end:
s_seek:    db "seek4="
s_seek_end:
s_miss:    db "miss=ok"
s_miss_end:
motd_path:    db "/motd", 0
docs_path:    db "docs", 0
missing_path: db "/nope", 0
