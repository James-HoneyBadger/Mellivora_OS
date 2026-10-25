; handle_test.asm - a handle carries the rights it was opened with
;
; File permissions decide whether an open succeeds. They do not decide what the
; resulting handle may do: a file opened read-only cannot be written back, and
; one opened write-only cannot be read, even by root, for whom the mode check
; would allow either. This program runs as root for exactly that reason - every
; file operation below is permitted by the file's mode, so a refusal can only
; come from the handle.
;
;   1. O_RDONLY: write refused, read works
;   2. O_WRONLY: read refused, write works
;   3. O_RDWR:   both work
;   4. a closed handle is refused
;
; Prints HANDLE_OK when every right was enforced and every control worked,
; HANDLE_FAIL otherwise. A dot per check shows how far it got.

%include "syscalls.inc"

O_RDONLY equ 0
O_WRONLY equ 1
O_RDWR   equ 2

start:
        ; --- fixture ---
        mov eax, SYS_FWRITE
        mov ebx, path
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne .fail

        ; --- 1: a read-only handle cannot write, but can read ---
        mov eax, SYS_OPEN
        mov ebx, path
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je .fail
        mov [fd], eax

        mov eax, SYS_READ
        mov ebx, [fd]
        mov ecx, read_buf
        mov edx, payload_len
        int 0x80
        cmp eax, 1
        jl .fail                        ; the control: reading must work
        call dot

        mov eax, SYS_WRITE
        mov ebx, [fd]
        mov ecx, payload
        mov edx, payload_len
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        mov eax, SYS_CLOSE
        mov ebx, [fd]
        int 0x80

        ; --- 2: a write-only handle cannot read, but can write ---
        mov eax, SYS_OPEN
        mov ebx, path
        mov ecx, O_WRONLY
        int 0x80
        cmp eax, -1
        je .fail
        mov [fd], eax

        mov eax, SYS_READ
        mov ebx, [fd]
        mov ecx, read_buf
        mov edx, payload_len
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        mov eax, SYS_WRITE
        mov ebx, [fd]
        mov ecx, payload
        mov edx, payload_len
        int 0x80
        cmp eax, payload_len
        jne .fail                       ; the control: writing must work
        call dot

        mov eax, SYS_CLOSE
        mov ebx, [fd]
        int 0x80

        ; --- 3: a read-write handle does both ---
        mov eax, SYS_OPEN
        mov ebx, path
        mov ecx, O_RDWR
        int 0x80
        cmp eax, -1
        je .fail
        mov [fd], eax

        mov eax, SYS_READ
        mov ebx, [fd]
        mov ecx, read_buf
        mov edx, payload_len
        int 0x80
        cmp eax, 1
        jl .fail
        call dot

        mov eax, SYS_SEEK
        mov ebx, [fd]
        xor ecx, ecx
        xor edx, edx
        int 0x80
        mov eax, SYS_WRITE
        mov ebx, [fd]
        mov ecx, payload
        mov edx, payload_len
        int 0x80
        cmp eax, payload_len
        jne .fail
        call dot

        ; --- 4: closing takes the rights away with the handle ---
        mov eax, SYS_CLOSE
        mov ebx, [fd]
        int 0x80

        mov eax, SYS_READ
        mov ebx, [fd]
        mov ecx, read_buf
        mov edx, payload_len
        int 0x80
        cmp eax, -1
        jne .fail
        call dot

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

dot:
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        ret

path:        db "/handle_test.txt", 0
payload:     db "handle rights fixture", 10
payload_len  equ $ - payload
read_buf:    times 64 db 0
msg_ok:      db 10, "HANDLE_OK", 10, 0
msg_fail:    db 10, "HANDLE_FAIL", 10, 0
fd:          dd 0
