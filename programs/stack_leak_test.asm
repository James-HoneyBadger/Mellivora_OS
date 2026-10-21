; stack_leak_test.asm - launching a program must not leak the pages it drops
;
; A task's stack region is 64 pages; only the top USER_STACK_INIT_PAGES (16) are
; mapped at exec and the rest are left unmapped so the stack can grow down on
; demand. The frames behind those 48 pages belong to the task that is being
; replaced, so dropping the mapping has to return them to the allocator.
;
; This forks and execs `hello` several times, waits for each child, and checks
; that the free-page count came back to where it started. A few pages of slack
; cover one-off allocations elsewhere in the launch path.
;
; Prints STACKLEAK_OK on success, or STACKLEAK_FAIL <leaked pages>.

%include "syscalls.inc"

ITERATIONS equ 16
SLACK      equ 4

start:
        mov eax, SYS_MEMINFO
        int 0x80
        mov [free_before], eax

        mov dword [left], ITERATIONS
.loop:
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je fail
        test eax, eax
        jz .child

        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        dec dword [left]
        jnz .loop

        mov eax, SYS_MEMINFO
        int 0x80
        mov ebx, [free_before]
        sub ebx, eax                    ; pages that never came back
        jbe .ok
        mov [leaked], ebx
        cmp ebx, SLACK
        ja  fail

.ok:
        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.child:
        mov eax, SYS_EXEC
        mov ebx, cmd_hello
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 127                    ; exec failed
        int 0x80

fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov edx, [leaked]
        mov ecx, 8
.hex:
        rol edx, 4
        mov ebx, edx
        and ebx, 0x0F
        cmp ebx, 10
        jb .hex_digit
        add ebx, 'a' - 10 - '0'
.hex_digit:
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .hex
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

cmd_hello:   db "hello", 0
msg_ok:      db "STACKLEAK_OK", 10, 0
msg_fail:    db "STACKLEAK_FAIL pages=0x", 0

free_before: dd 0
leaked:      dd 0
left:        dd 0
