; sockfd_test.asm - regression test for the unified fd table
;
; Sockets used to live in their own 0..31 namespace, so a program had two
; unrelated fd spaces. They now occupy slots in the same VFD table as files,
; which is what lets close() work on a socket and stops a file fd and a socket
; fd from ever meaning the same thing.
;
; Verifies:
;   1. SYS_SOCKET returns a usable fd
;   2. SYS_OPEN returns a different fd (one namespace, no collisions)
;   3. read() on the file works
;   4. read() on the socket is refused (sockets use send/recv)
;   5. SYS_CLOSE closes the socket
;   6. SYS_CLOSE closes the file
;
; Prints SOCKFD_OK on success, SOCKFD_FAIL on any failure.

%include "syscalls.inc"

SOCK_DGRAM      equ 2
O_RDONLY        equ 0

start:
        ; --- 1. create a UDP socket ---
        mov eax, SYS_SOCKET
        mov ebx, SOCK_DGRAM
        int 0x80
        cmp eax, 0
        jl .fail
        mov [sock_fd], eax

        ; --- 2. open a file: must get a different fd ---
        mov eax, SYS_OPEN
        mov ebx, proc_path
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, 0
        jl .fail
        mov [file_fd], eax
        cmp eax, [sock_fd]
        je .fail                ; same slot in one namespace would be a bug

        ; --- 3. the file fd is readable ---
        mov eax, SYS_READ
        mov ebx, [file_fd]
        mov ecx, buf
        mov edx, 32
        int 0x80
        cmp eax, 0
        jle .fail

        ; --- 4. the socket is not a byte stream ---
        mov eax, SYS_READ
        mov ebx, [sock_fd]
        mov ecx, buf
        mov edx, 32
        int 0x80
        cmp eax, -1
        jne .fail

        ; --- 5. close the socket through the unified close ---
        mov eax, SYS_CLOSE
        mov ebx, [sock_fd]
        int 0x80
        cmp eax, 0
        jne .fail

        ; --- 6. close the file ---
        mov eax, SYS_CLOSE
        mov ebx, [file_fd]
        int 0x80
        cmp eax, 0
        jne .fail

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

proc_path:  db "/proc/version", 0
msg_ok:     db "SOCKFD_OK", 10, 0
msg_fail:   db "SOCKFD_FAIL", 10, 0

sock_fd:    dd 0
file_fd:    dd 0
buf:        times 64 db 0
