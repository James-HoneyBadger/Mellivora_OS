;=======================================================================
; lm_sh.asm - a line-based shell, /sh on the HBFS volume
;
; Reads a line from the console, echoes it, and runs one of three commands:
; "ls" lists the working directory, "cat <name>" prints a file, "quit" leaves.
; It is the first program here that waits for input, which is what makes the
; console a console: the kernel hands back one character at a time and the
; shell yields between polls, so the other tasks keep running while it waits.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT    equ 3
SYS_WRITE   equ 6
SYS_YIELD   equ 7
SYS_SLEEP   equ 24
SYS_OPEN    equ 9
SYS_READ    equ 10
SYS_CLOSE   equ 11
SYS_STAT    equ 14
SYS_READDIR equ 15
SYS_GETCWD  equ 17
SYS_PUTCHAR equ 21
SYS_GETCHAR equ 22

LINE_BUF equ 0x40001200         ; the line being assembled / written
CMD_BUF  equ 0x40001380         ; the command line as typed
OUT_BUF  equ 0x40001400         ; one line of output
DATA_BUF equ 0x40001500         ; file contents
DEC_BUF  equ 0x40001600
STAT_BUF equ 0x40001800
CWD_BUF  equ 0x40001C00
CWD_LEN  equ 0x40001B80         ; writable state lives in the stack page
LINE_LIMIT equ 0x40001380

STAT_TYPE equ STAT_BUF + 0
STAT_SIZE equ STAT_BUF + 4
NAME_BUF  equ STAT_BUF + 16

start:
        cld
        mov qword [CWD_LEN], 1
        call prompt
.read:
        call read_line                ; -> r12 = length, CMD_BUF filled
        mov r13, r12
        test r13, r13
        jz .read                      ; a bare newline just prompts again

        ; "quit"
        lea rsi, [rel cmd_quit]
        call line_is
        test rax, rax
        jnz .quit

        ; "ls"
        lea rsi, [rel cmd_ls]
        call line_is
        test rax, rax
        jnz .ls

        ; "cat <name>"
        lea rsi, [rel cmd_cat]
        mov rcx, 4
        call prefix_is
        test rax, rax
        jnz .cat

        call begin_line
        lea rsi, [rel s_unknown]
        call add_str
        call end_line
        jmp .next

.ls:
        xor r14, r14
.ent:
        xor edi, edi
        mov rsi, r14
        mov rdx, STAT_BUF
        mov eax, SYS_READDIR
        syscall
        cmp rax, 1
        jne .next
        call begin_line
        lea rsi, [rel s_ent]
        call add_str
        mov rsi, NAME_BUF
        call add_str
        cmp dword [STAT_TYPE], 2        ; a directory is shown with a trailing /
        jne .named
        lea rsi, [rel s_slash]
        call add_str
.named:
        lea rsi, [rel s_size]
        call add_str
        mov eax, [STAT_SIZE]
        call add_dec
        call end_line
        inc r14
        cmp r14, 16
        jb .ent
        jmp .next

.cat:
        ; the name starts four characters in
        lea rdi, [CMD_BUF + 4]
        call copy_to_out
        mov rdi, OUT_BUF
        mov eax, SYS_OPEN
        syscall
        test rax, rax
        js .no_file
        mov r15, rax
.bytes:
        mov rdi, r15
        mov rsi, DATA_BUF
        mov edx, 128
        mov eax, SYS_READ
        syscall
        cmp rax, 1
        jl .closed
        mov r12, rax
        mov edi, 1
        mov rsi, DATA_BUF
        mov rdx, r12
        mov eax, SYS_WRITE
        syscall
        jmp .bytes
.closed:
        mov rdi, r15
        mov eax, SYS_CLOSE
        syscall
        jmp .next
.no_file:
        call begin_line
        lea rsi, [rel s_notfound]
        call add_str
        mov rsi, OUT_BUF
        call add_str
        call end_line

.next:
        call prompt
        jmp .read

.quit:
        call begin_line
        lea rsi, [rel s_bye]
        call add_str
        call end_line
        mov eax, SYS_EXIT
        mov edi, 0x5A
        syscall
.hang:
        jmp .hang

;-----------------------------------------------------------------------
; read_line: echo one line into CMD_BUF, stopping at CR or LF.
; Returns its length in r12.
;-----------------------------------------------------------------------
read_line:
        xor r12, r12
.key:
        mov eax, SYS_GETCHAR
        syscall
        cmp rax, -1
        je .yield
        cmp al, 10
        je .done
        cmp al, 13
        je .done
        cmp al, 127                   ; backspace
        je .back
        cmp r12, 63
        jae .key
        mov [CMD_BUF + r12], al
        inc r12
        movzx edi, al
        mov eax, SYS_PUTCHAR
        syscall
        jmp .key
.back:
        test r12, r12
        jz .key
        dec r12
        mov edi, 8
        mov eax, SYS_PUTCHAR
        syscall
        mov edi, ' '
        mov eax, SYS_PUTCHAR
        syscall
        mov edi, 8
        mov eax, SYS_PUTCHAR
        syscall
        jmp .key
.yield:
        ; nothing to read yet: sleep a tick instead of spinning, so a waiting
        ; shell costs the CPU almost nothing and the other tasks run
        mov edi, 10
        mov eax, SYS_SLEEP
        syscall
        jmp .key
.done:
        mov byte [CMD_BUF + r12], 0
        call begin_line
        call end_line                ; just the newline
        ret

prompt:
        ; refresh the working directory first, so the prompt shows the path the
        ; shell is actually in
        mov rdi, CWD_BUF
        mov esi, 128
        mov eax, SYS_GETCWD
        syscall
        test rax, rax
        js .nolength
        mov [CWD_LEN], rax
.nolength:
        call begin_line
        lea rsi, [rel s_prompt]
        call add_str
        lea rsi, [CWD_BUF]
        mov rdx, [CWD_LEN]
        call add_bytes
        lea rsi, [rel s_arrow]
        call add_str
        call end_line
        ret

; line_is: rsi = a word; 1 if CMD_BUF is exactly that.
line_is:
        xor ecx, ecx
.loop:
        mov al, [CMD_BUF + rcx]
        mov dl, [rsi + rcx]
        cmp al, dl
        jne .no
        test al, al
        jz .yes
        inc rcx
        jmp .loop
.no:
        xor eax, eax
        ret
.yes:
        mov eax, 1
        ret

; prefix_is: rsi = a word, rcx = its length; 1 if CMD_BUF starts with it.
prefix_is:
        test rcx, rcx
        jz .no
        xor rdx, rdx
.loop:
        mov al, [CMD_BUF + rdx]
        mov r8b, [rsi + rdx]
        cmp al, r8b
        jne .no
        inc rdx
        dec rcx
        jnz .loop
        mov eax, 1
        ret
.no:
        xor eax, eax
        ret

; copy_to_out: rdi = NUL-terminated name; copies it to OUT_BUF.
copy_to_out:
        mov rsi, rdi
        mov rdi, OUT_BUF
.loop:
        lodsb
        stosb
        test al, al
        jnz .loop
        ret

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

s_prompt:  db "mellivora:", 0
s_arrow:   db "> ", 0
s_slash:   db "/", 0
s_ent:     db "  ", 0
s_size:    db " size=", 0
s_unknown: db "sh: unknown command", 0
s_notfound: db "sh: no such file: ", 0
s_bye:     db "bye", 0
cmd_quit:  db "quit", 0
cmd_ls:    db "ls", 0
cmd_cat:   db "cat ", 0
