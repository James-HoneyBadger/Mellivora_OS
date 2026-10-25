; v4write_test.asm - exercise the HBFS v4 writer.
;
; Creates a file and a directory on a v4 volume, reads the file back and
; compares it byte for byte. Every one of those operations modifies a
; checksummed metadata run, so the host-side checker has to agree with the
; volume afterwards - that is what tests/test_v4_mount.py asserts.
;
; Prints V4WRITE_OK or V4WRITE_FAIL-<stage>.
%include "syscalls.inc"

start:
        mov eax, SYS_FWRITE
        mov ebx, name
        mov ecx, payload
        mov edx, payload_len
        mov esi, 0
        int 0x80
        cmp eax, 0
        jne .fail_write

        mov eax, SYS_MKDIR
        mov ebx, dirname
        int 0x80
        cmp eax, 0
        jne .fail_mkdir

        mov eax, SYS_FREAD
        mov ebx, name
        mov ecx, rbuf
        int 0x80
        cmp eax, payload_len
        jne .fail_read

        mov esi, payload
        mov edi, rbuf
        mov ecx, payload_len
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

.fail_write:
        mov ebx, msg_write
        jmp .fail
.fail_mkdir:
        mov ebx, msg_mkdir
        jmp .fail
.fail_read:
        mov ebx, msg_read
.fail:
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

name:      db "v4out.txt", 0
dirname:   db "v4dir", 0
payload:   db "written by the HBFS v4 writer, and read back after restamping"
payload_len equ $ - payload
msg_ok:    db 10, "V4WRITE_OK", 10, 0
msg_write: db 10, "V4WRITE_FAIL-WRITE", 10, 0
msg_mkdir: db 10, "V4WRITE_FAIL-MKDIR", 10, 0
msg_read:  db 10, "V4WRITE_FAIL-READ", 10, 0
rbuf:      times 256 db 0
