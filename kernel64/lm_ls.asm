;=======================================================================
; lm_ls.asm - the 64-bit directory demo, /ls on the HBFS volume
;
; Exercises the file metadata calls the 32-bit tree's own programs expect:
; getcwd, readdir, stat, chdir and seek, plus the open/read/close they sit on
; top of. It runs as its own task, so a call that returns nonsense fails the
; boot test rather than a shell session.
;
; Every line is assembled in a buffer and written with one syscall: the other
; tasks print at the same time, and a line built from six writes can have
; another task's output in the middle of it.
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

; This program lives in one read-only code page plus one stack page
; (0x40001000), so its buffers sit low in the stack page and the call stack
; grows down from 0x40002000 into the space above them.
LINE_BUF equ 0x40001200         ; the line being assembled
CWD_BUF  equ 0x40001C00         ; getcwd's target, kept out of LINE_BUF
LINE_LIMIT equ 0x40001500       ; where the next buffer starts
DATA_BUF equ 0x40001500         ; what seek/read pulls out of /motd
DEC_BUF  equ 0x40001600
STAT_BUF equ 0x40001800         ; struct vfs_stat: type, size, blocks, start

STAT_TYPE equ STAT_BUF + 0
STAT_SIZE equ STAT_BUF + 4
NAME_BUF  equ STAT_BUF + 16     ; the name field of the same struct

start:
        cld                             ; string instructions run forwards

        ; getcwd(buf, size) -> length
        mov rdi, CWD_BUF
        mov esi, 128
        mov eax, SYS_GETCWD
        syscall
        test rax, rax
        js fail
        mov r12, rax

        call begin_line
        lea rsi, [rel s_cwd]
        call add_str
        lea rsi, [CWD_BUF]
        mov rdx, r12
        call add_bytes
        call end_line

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

        call begin_line
        lea rsi, [rel s_motd]
        call add_str
        mov eax, [STAT_TYPE]
        call add_dec
        lea rsi, [rel s_size]
        call add_str
        mov eax, [STAT_SIZE]
        call add_dec
        call end_line

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

        call begin_line
        lea rsi, [rel s_seek]
        call add_str
        mov rsi, DATA_BUF
        mov edx, 4
        call add_bytes
        call end_line

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

        call begin_line
        lea rsi, [rel s_cwd]
        call add_str
        lea rsi, [CWD_BUF]
        mov rdx, r12
        call add_bytes
        call end_line

        xor edi, edi
        call list_dir

        ; A name that is not there must report -1, not a stale entry.
        lea rdi, [rel missing_path]
        mov rsi, STAT_BUF
        mov eax, SYS_STAT
        syscall
        cmp rax, -1
        jne fail

        call begin_line
        lea rsi, [rel s_miss]
        call add_str
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
        ; 1 is an entry, 0 is a slot whose file has been deleted, -1 is the end of
        ; the directory. A listing skips the holes: readdir reports slots - /wr
        ; depends on that - and it is the caller that wanted entries.
        cmp rax, 0
        je .skip
        cmp rax, 1
        jne .done

        call begin_line
        lea rsi, [rel s_entry]
        call add_str
        mov rax, r14
        call add_dec
        lea rsi, [rel s_name]
        call add_str
        mov rsi, NAME_BUF
        call add_str
        lea rsi, [rel s_type]
        call add_str
        mov eax, [STAT_TYPE]
        call add_dec
        lea rsi, [rel s_size]
        call add_str
        mov eax, [STAT_SIZE]
        call add_dec
        call end_line

        ; An index that keeps growing, because readdir is what says when there is
        ; nothing left. There used to be a `cmp r14, 16` here - a limit that was
        ; invisible while the root held fewer than sixteen entries and then quietly
        ; hid every one after that, including the `docs` directory the test suite
        ; lists for.
.skip:
        inc r14
        jmp .loop
.done:
        ret

;-----------------------------------------------------------------------
; Line assembly. RBP is the write cursor; nothing here uses it as a frame.
;-----------------------------------------------------------------------
begin_line:
        lea rbp, [LINE_BUF]
        ret

; add_str: rsi = NUL-terminated string.
add_str:
        cld                             ; the direction flag is not ours to assume
        lodsb
        test al, al
        jz .done
        cmp rbp, LINE_LIMIT             ; never run into the buffers below
        jae .done
        mov [rbp], al
        inc rbp
        jmp add_str
.done:
        ret

; add_bytes: rsi = bytes, rdx = length.
add_bytes:
        cld
        test rdx, rdx
        jz .done
.loop:
        cmp rbp, LINE_LIMIT
        jae .done
        lodsb
        mov [rbp], al
        inc rbp
        dec rdx
        jnz .loop
.done:
        ret

; add_dec: eax = value, in decimal. syscall clobbers rcx and r11, and the
; divide needs ecx, so it is reloaded every digit.
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
        call add_str                    ; rsi now points at the first digit
        ret

; end_line: newline, then one write of the whole line.
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

s_cwd: db "cwd=", 0
s_entry: db "ent ", 0
s_name: db " name=", 0
s_type: db " type=", 0
s_size: db " size=", 0
s_motd: db "stat /motd type=", 0
s_seek: db "seek4=", 0
s_miss: db "miss=ok", 0
s_fail: db "ls FAILED", 0
motd_path:    db "/motd", 0
docs_path:    db "docs", 0
missing_path: db "/nope", 0
