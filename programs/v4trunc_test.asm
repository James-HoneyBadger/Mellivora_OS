; v4trunc_test.asm - shrink an extent file and read the result back.
;
; frag.txt is three runs totalling 18 741 bytes. Truncating it to 5 000 bytes
; keeps two blocks, frees the rest of the second run and the whole third run,
; and rewrites the run list. Reading it back proves the mapping still walks
; what is left; the host checker proves the extent block and bitmap agree.
;
; Prints TRUNC_OK or TRUNC_FAIL-<stage>.
%include "syscalls.inc"

NEW_SIZE equ 5000

start:
        mov eax, SYS_TRUNCATE
        mov ebx, name
        mov ecx, NEW_SIZE
        int 0x80
        cmp eax, 0
        jne .fail_trunc

        mov eax, SYS_FREAD
        mov ebx, name
        mov ecx, buf
        int 0x80
        cmp eax, NEW_SIZE
        jne .fail_read

        mov esi, buf
        mov edi, prefix
        mov ecx, prefix_len
.compare:
        mov al, [esi]
        cmp al, [edi]
        jne .fail_read
        inc esi
        inc edi
        dec ecx
        jnz .compare

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor eax, eax
        int 0x80

.fail_trunc:
        mov ebx, msg_trunc
        jmp .fail
.fail_read:
        mov ebx, msg_read
.fail:
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

name:       db "frag.txt", 0
prefix:     db "fragment run 001 "
prefix_len  equ $ - prefix
msg_ok:     db 10, "TRUNC_OK", 10, 0
msg_trunc:  db 10, "TRUNC_FAIL-TRUNCATE", 10, 0
msg_read:   db 10, "TRUNC_FAIL-READ", 10, 0
buf:        times 8192 db 0
