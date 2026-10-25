; fd_frag_test.asm - read a multi-run v4 file through the descriptor layer.
;
; frag.txt (opened relative to /, the shell's cwd) is 18 741 bytes in three
; runs, so a reader that ignores the run list returns the extent block, or the
; wrong blocks entirely. This opens it, reads until EOF into one buffer, and
; requires both that more than four blocks arrived and that the last bytes are
; the marker line that ends the file.
;
; Prints FD_FRAG_OK on success, FD_FRAG_FAIL-<stage> otherwise.
%include "syscalls.inc"

O_RDONLY equ 0
BUF_SIZE equ 20480

start:
        mov eax, SYS_OPEN
        mov ebx, path
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je .fail_open
        mov [fd], eax
        mov dword [total], 0

.read:
        mov eax, SYS_READ
        mov ebx, [fd]
        mov ecx, buf
        add ecx, [total]
        mov edx, BUF_SIZE
        sub edx, [total]
        int 0x80
        cmp eax, -1
        je .close_fail
        test eax, eax
        jz .eof
        add [total], eax
        jmp .read

.eof:
        mov eax, SYS_CLOSE
        mov ebx, [fd]
        int 0x80

        ; More than four blocks were read: the run walk really happened.
        cmp dword [total], 4 * 4096
        jb .fail_short

        ; The last bytes must be the marker line.
        mov ecx, [total]
        cmp ecx, TAIL_LEN
        jb .fail_tail
        sub ecx, TAIL_LEN
        mov esi, buf
        add esi, ecx
        mov edi, tail
        mov ecx, TAIL_LEN
.compare:
        mov al, [esi]
        cmp al, [edi]
        jne .fail_tail
        inc esi
        inc edi
        dec ecx
        jnz .compare

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor eax, eax
        int 0x80

.close_fail:
        mov eax, SYS_CLOSE
        mov ebx, [fd]
        int 0x80
.fail_open:
        mov ebx, msg_open
        jmp .fail
.fail_short:
        mov ebx, msg_short
        jmp .fail
.fail_tail:
        ; Show the tail that was received instead.
        mov ecx, [total]
        cmp ecx, TAIL_LEN
        jb .fail
        sub ecx, TAIL_LEN
        mov ebx, buf
        add ebx, ecx
        mov byte [ebx + TAIL_LEN], 0
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, msg_tail
.fail:
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

path:      db "frag.txt", 0
msg_ok:    db 10, "FD_FRAG_OK", 10, 0
msg_open:  db 10, "FD_FRAG_FAIL-OPEN", 10, 0
msg_short: db 10, "FD_FRAG_FAIL-SHORT", 10, 0
msg_tail:  db 10, "FD_FRAG_FAIL-TAIL", 10, 0
tail:      db "LAST-FRAGMENT-MARKER", 10
TAIL_LEN   equ $ - tail
fd:        dd 0
total:     dd 0
buf:       times BUF_SIZE db 0
