; kill_leak_test.asm - SIGKILL must release the target's mmap frames
;
; A task torn down by a signal never runs its own exit path, so the teardown
; has to release what it owned: the frames behind its mmap areas (with their
; page-cache references) as well as its page tables. It used to free only the
; directory page, so every killed task leaked everything it had mapped.
;
;   1. fork a child that maps 8 MB and touches every page
;   2. once the free-page count shows those pages are really allocated, the
;      parent answers with SIGKILL and waits for it. A killed task leaves the
;      same zombie a normal exit does, with the high bit set and the signal in
;      the low byte.
;   3. the free-page count must be back within a small margin. A few pages may
;      still be pending: the killed task's own page directory is released at
;      the next context switch, not inside the kill.
;
; Prints KILLLEAK_OK, or KILLLEAK_FAIL <leaked pages>.

%include "syscalls.inc"

PAGES equ 2048                  ; 8 MB
SLACK equ 12
SPINS equ 2000

start:
        mov eax, SYS_MEMINFO
        int 0x80
        mov [free_before], eax

        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je fail
        test eax, eax
        jz .child
        mov [child_pid], eax

        mov ecx, SPINS
.wait:
        mov eax, SYS_MEMINFO
        int 0x80
        mov ebx, [free_before]
        sub ebx, eax
        cmp ebx, PAGES - 16             ; the child has faulted its pages in
        jae .kill
        mov eax, SYS_YIELD
        int 0x80
        dec ecx
        jnz .wait
        jmp fail                        ; the child never got going

.kill:
        mov eax, SYS_KILL
        mov ebx, [child_pid]
        mov ecx, SIGKILL
        int 0x80

        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        test eax, 0x80000000            ; abnormal end?
        jz fail
        and eax, 0x7F
        cmp eax, SIGKILL                ; ...and it names the signal
        jne fail

        mov eax, SYS_MEMINFO
        int 0x80
        mov ebx, [free_before]
        sub ebx, eax
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
        mov eax, SYS_MMAP
        xor ebx, ebx
        mov ecx, PAGES * 4096
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        cmp eax, -1
        je .child_exit
        mov edi, eax
        mov ecx, PAGES
        mov dword [edi], 1              ; touch every page
.touch:
        mov dword [edi], 0x5A5A5A5A
        add edi, 4096
        dec ecx
        jnz .touch

.hold:
        mov eax, SYS_YIELD
        int 0x80
        jmp .hold

.child_exit:
        mov eax, SYS_EXIT
        mov ebx, 1
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

msg_ok:      db "KILLLEAK_OK", 10, 0
msg_fail:    db "KILLLEAK_FAIL pages=0x", 0

free_before: dd 0
leaked:      dd 0
child_pid:   dd 0
