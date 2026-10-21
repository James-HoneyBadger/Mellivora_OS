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
SYS_DATE    equ 25
SYS_MKDIR   equ 18
SYS_CLEAR   equ 23
SYS_EXEC    equ 12
SYS_FORK    equ 37
SYS_WAIT    equ 38
SYS_KILL    equ 39
SYS_UNLINK  equ 19
SYS_OPEN    equ 9
SYS_READ    equ 10
SYS_CLOSE   equ 11
SYS_SEEK    equ 13
SYS_CREATE  equ 20
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
RUN_PROG equ 0x40001A80         ; the program path, for the child's exec
RUN_ARGS equ 0x40001B00         ; the rest of the line, its command line
DATE_BUF equ 0x40001B00         ; sec, min, hour, day, month, year
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

        ; "clear"
        lea rsi, [rel cmd_clear]
        call line_is
        test rax, rax
        jnz .clear

        ; "date"
        lea rsi, [rel cmd_date]
        call line_is
        test rax, rax
        jnz .date

        ; "mkdir <name>"
        lea rsi, [rel cmd_mkdir]
        mov rcx, 6
        call prefix_is
        test rax, rax
        jnz .mkdir

        ; "rm <name>"
        lea rsi, [rel cmd_rm]
        mov rcx, 3
        call prefix_is
        test rax, rax
        jnz .rm

        ; "spawn <program>" - start it and do not wait
        lea rsi, [rel cmd_spawn]
        mov rcx, 6
        call prefix_is
        test rax, rax
        jnz .spawn

        ; "kill <pid>"
        lea rsi, [rel cmd_kill]
        mov rcx, 5
        call prefix_is
        test rax, rax
        jnz .kill

        ; "run <program> [arguments]"
        lea rsi, [rel cmd_run]
        mov rcx, 4
        call prefix_is
        test rax, rax
        jnz .run

        ; "cat <name>"
        lea rsi, [rel cmd_cat]
        mov rcx, 4
        call prefix_is
        test rax, rax
        jnz .cat

        ; "append <name> <text>" - add a line to a file, creating it if needed
        lea rsi, [rel cmd_append]
        mov rcx, 7
        call prefix_is
        test rax, rax
        jnz .append

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

.clear:
        mov eax, SYS_CLEAR
        syscall
        call begin_line
        lea rsi, [rel s_cleared]
        call add_str
        call end_line
        jmp .next

.spawn:
        ; The same fork as run, but the parent does not wait: the child runs in
        ; the background and the shell is ready for the next command.
        lea rsi, [CMD_BUF + 6]
        mov rdi, RUN_PROG
.copy:
        lodsb
        stosb
        test al, al
        jnz .copy

        mov eax, SYS_FORK
        syscall
        cmp rax, -1
        je .run_failed
        test rax, rax
        jz .spawn_child

        mov r15, rax                    ; the child's pid, before add_str
                                        ; clobbers EAX
        call begin_line
        lea rsi, [rel s_started]
        call add_str
        mov rsi, RUN_PROG
        call add_str
        lea rsi, [rel s_pid_is]
        call add_str
        mov rax, r15
        call add_dec
        call end_line
        jmp .next
.spawn_child:
        mov rdi, RUN_PROG
        xor esi, esi
        mov eax, SYS_EXEC
        syscall
        mov edi, 0x7F
        mov eax, SYS_EXIT
        syscall
.hang_spawn:
        jmp .hang_spawn

.kill:
        ; parse the pid out of the rest of the line
        lea rsi, [CMD_BUF + 5]
        xor eax, eax
        xor ecx, ecx
.digits:
        mov dl, [rsi]
        cmp dl, '0'
        jb .digits_done
        cmp dl, '9'
        ja .digits_done
        cmp ecx, 6
        jae .digits_done
        imul eax, eax, 10
        sub edx, '0'
        add eax, edx
        inc ecx
        inc rsi
        jmp .digits
.digits_done:
        test ecx, ecx
        jz .kill_usage

        mov rdi, rax
        mov esi, 15                     ; SIGTERM, the number a shell sends
        mov eax, SYS_KILL
        syscall
        mov r15, rdi
        mov r14, rax

        call begin_line
        test r14, r14
        jnz .kill_failed
        lea rsi, [rel s_killed]
        call add_str
        mov rax, r15
        call add_dec
        call end_line
        jmp .next
.kill_failed:
        lea rsi, [rel s_nokill]
        call add_str
        mov rax, r15
        call add_dec
        call end_line
        jmp .next
.kill_usage:
        call begin_line
        lea rsi, [rel s_kill_usage]
        call add_str
        call end_line
        jmp .next

.run:
        ; Split "run <program> <arguments>" at the second space: the program path
        ; is what exec loads, the rest is the command line the child sees.
        lea rsi, [CMD_BUF + 4]
        mov rdi, RUN_PROG
        xor ecx, ecx
.split:
        mov al, [rsi]
        test al, al
        jz .split_done
        cmp al, ' '
        je .have_prog
        mov [rdi], al
        inc rdi
        inc rsi
        inc ecx
        jmp .split
.have_prog:
        mov byte [rdi], 0
        inc rsi                         ; step over the space
        mov rdi, RUN_ARGS
        xor edx, edx
.args:
        mov al, [rsi]
        test al, al
        jz .args_done
        mov [rdi], al
        inc rdi
        inc rsi
        inc edx
        jmp .args
.args_done:
        mov byte [rdi], 0
        test ecx, ecx
        jz .run_usage
.split_done:
        mov byte [rdi], 0               ; no arguments: terminate the path here
        test ecx, ecx
        jz .run_usage

        mov eax, SYS_FORK
        syscall
        cmp rax, -1
        je .run_failed
        test rax, rax
        jnz .parent

        ; The child becomes the program. If exec fails there is nothing to go
        ; back to, so it exits with a code the parent can report.
        mov rdi, RUN_PROG
        mov rsi, RUN_ARGS
        mov eax, SYS_EXEC
        syscall
        mov edi, 0x7F
        mov eax, SYS_EXIT
        syscall
.hang_child:
        jmp .hang_child

.parent:
        mov r15, rax                    ; the child's pid
.wait:
        mov rdi, r15
        mov eax, SYS_WAIT
        syscall
        cmp rax, -2                     ; still running: spin so the tick can
        jne .reaped                     ; hand the CPU to the child
        mov ecx, 40000
.spin:
        pause
        dec ecx
        jnz .spin
        jmp .wait
.reaped:
        mov [RUN_PROG + 64], rax        ; stash: registers have been unreliable here

        call begin_line
        lea rsi, [rel s_ran]
        call add_str
        mov rsi, RUN_PROG
        call add_str
        lea rsi, [rel s_exit]
        call add_str
        mov rax, [RUN_PROG + 64]
        call add_hex8
        call end_line
        jmp .next

.run_usage:
        call begin_line
        lea rsi, [rel s_usage]
        call add_str
        call end_line
        jmp .next
.run_failed:
        call begin_line
        lea rsi, [rel s_nofork]
        call add_str
        call end_line
        jmp .next

.date:
        mov rdi, DATE_BUF
        mov eax, SYS_DATE
        syscall
        cmp rax, -1
        je .clock_failed

        call begin_line
        call add_dec                    ; the four-digit year from RAX...
        lea rsi, [rel s_dash]
        call add_str
        movzx eax, byte [DATE_BUF + 4]  ; month
        call add_2dec
        lea rsi, [rel s_dash]
        call add_str
        movzx eax, byte [DATE_BUF + 3]  ; day
        call add_2dec
        lea rsi, [rel s_space]
        call add_str
        movzx eax, byte [DATE_BUF + 2]  ; hour
        call add_2dec
        lea rsi, [rel s_colon]
        call add_str
        movzx eax, byte [DATE_BUF + 1]  ; minute
        call add_2dec
        lea rsi, [rel s_colon]
        call add_str
        movzx eax, byte [DATE_BUF + 0]  ; second
        call add_2dec
        call end_line
        jmp .next
.clock_failed:
        call begin_line
        lea rsi, [rel s_noclock]
        call add_str
        call end_line
        jmp .next

.mkdir:
        lea rdi, [CMD_BUF + 6]
        mov eax, SYS_MKDIR
        syscall
        mov r15, rax
        call begin_line
        test r15, r15
        jnz .mkdir_failed
        lea rsi, [rel s_made]
        call add_str
        lea rsi, [CMD_BUF + 6]
        call add_str
        call end_line
        jmp .next
.mkdir_failed:
        lea rsi, [rel s_cant]
        call add_str
        lea rsi, [CMD_BUF + 6]
        call add_str
        call end_line
        jmp .next

;-----------------------------------------------------------------------
; append <name> <text>: the one thing whole-file CREATE cannot do. The line is
; assembled in DATA_BUF with a newline on the end, then either handed to CREATE
; (a file that is not there yet) or written through an open handle seeked to the
; end. That seek-then-write is exactly what a program appending to a log does.
;-----------------------------------------------------------------------
.append:
        lea rsi, [CMD_BUF + 7]
        mov rdi, OUT_BUF
.ap_name:
        lodsb
        test al, al
        jz .ap_usage                    ; a name with no text
        cmp al, ' '
        je .ap_named
        stosb
        jmp .ap_name
.ap_named:
        mov byte [rdi], 0

        mov rdi, DATA_BUF
        xor ecx, ecx
.ap_text:
        lodsb
        test al, al
        jz .ap_text_done
        stosb
        inc ecx
        jmp .ap_text
.ap_text_done:
        mov byte [rdi], 10              ; a line, so the next one starts fresh
        inc ecx
        mov r14, rcx

        mov rdi, OUT_BUF
        mov esi, 1                      ; bit 0: open for writing
        mov eax, SYS_OPEN
        syscall
        test rax, rax
        js .ap_create
        mov r15, rax

        mov rdi, r15
        xor esi, esi
        mov edx, 2                      ; whence 2: from the end
        mov eax, SYS_SEEK
        syscall

        mov rdi, r15
        mov rsi, DATA_BUF
        mov rdx, r14
        mov eax, SYS_WRITE
        syscall
        mov r13, rax                    ; bytes written

        mov rdi, r15
        mov eax, SYS_CLOSE
        syscall

        cmp r13, r14
        jl .ap_failed
        jmp .ap_done

.ap_create:
        mov rdi, OUT_BUF
        mov rsi, DATA_BUF
        mov rdx, r14
        mov eax, SYS_CREATE
        syscall
        test rax, rax
        js .ap_failed

.ap_done:
        call begin_line
        lea rsi, [rel s_appended]
        call add_str
        mov rsi, OUT_BUF
        call add_str
        call end_line
        jmp .next
.ap_failed:
        call begin_line
        lea rsi, [rel s_cant]
        call add_str
        mov rsi, OUT_BUF
        call add_str
        call end_line
        jmp .next
.ap_usage:
        call begin_line
        lea rsi, [rel s_ap_usage]
        call add_str
        call end_line
        jmp .next

.rm:
        lea rdi, [CMD_BUF + 3]
        mov eax, SYS_UNLINK
        syscall
        mov r15, rax
        call begin_line
        test r15, r15
        jnz .rm_failed
        lea rsi, [rel s_removed]
        call add_str
        lea rsi, [CMD_BUF + 3]
        call add_str
        call end_line
        jmp .next
.rm_failed:
        lea rsi, [rel s_cant]
        call add_str
        lea rsi, [CMD_BUF + 3]
        call add_str
        call end_line
        jmp .next

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

; add_2dec: eax = a value under 100, written as exactly two digits.
add_2dec:
        mov ecx, 10
        xor edx, edx
        div ecx
        add al, '0'
        mov [rbp], al
        inc rbp
        add dl, '0'
        mov [rbp], dl
        inc rbp
        ret

; add_hex8: eax = value, written as 0x and its eight hex digits. The digits
; come out of a rotate, so the top nibble of the 32-bit value is emitted first.
add_hex8:
        mov edx, eax                    ; before add_str, which clobbers AL
        lea rsi, [rel s_hex]
        call add_str
        mov ecx, 8
.digit:
        rol edx, 4
        push rcx
        mov eax, edx
        and eax, 0xF
        cmp al, 10
        jb .under
        add al, 'a' - 10
        jmp .store
.under:
        add al, '0'
.store:
        mov [rbp], al
        inc rbp
        pop rcx
        loop .digit
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
cmd_append: db "append ", 0
cmd_run:   db "run ", 0
cmd_spawn: db "spawn ", 0
cmd_kill:  db "kill ", 0
s_started: db "sh: started ", 0
s_pid_is:  db " pid=", 0
s_killed:  db "sh: killed ", 0
s_nokill:  db "sh: cannot kill ", 0
s_kill_usage: db "usage: kill <pid>", 0
s_ran:     db "sh: ", 0
s_exit:    db " exited ", 0
s_hex:     db "0x", 0
s_usage:   db "usage: run <program> [arguments]", 0
s_nofork:  db "sh: cannot fork", 0
cmd_date:  db "date", 0
cmd_clear: db "clear", 0
s_cleared: db "sh: cleared the screen", 0
cmd_mkdir: db "mkdir ", 0
cmd_rm:    db "rm ", 0
s_dash:    db "-", 0
s_colon:   db ":", 0
s_space:   db " ", 0
s_made:    db "sh: created ", 0
s_appended: db "sh: appended to ", 0
s_ap_usage: db "usage: append <file> <text>", 0
s_removed: db "sh: removed ", 0
s_cant:    db "sh: cannot: ", 0
s_noclock: db "sh: no clock", 0
