; hbx_thread.asm - thread-local storage that is per *thread*, not per task.
;
; The payload declares a TLS directory with the TLS1 marker at the end, so the
; loader gives the program a block copied from the template. What this proves is
; the part only a second thread can show: two threads of one process share the
; image - and therefore share the dword the module reads its base from - but
; each has a block of its own, so the same `mov ebx, [tls_base]` names a
; different block in each of them.
;
; The test is arranged so a kernel that forgot to install the incoming thread's
; view fails rather than passes: both threads write their block and then yield
; eight times, checking after every switch that the slot still names their own
; block and that their block still holds their own value. A shared block, a slot
; left pointing at the other thread's block, or a block re-copied on every
; switch each break one of those checks.
;
;   THREAD_TLS_OK     every check held in both threads
;   THREAD_TLS_FAIL n the nth check did not
;
; Nothing is written back into the image: an HBX section is read-only unless it
; says otherwise, and the wrapper writes one .text section. Every value this
; program has to keep lives in a register (the ABI preserves EBX..EDI across a
; syscall) or in the page it maps for the second thread's stack.
%include "syscalls.inc"

STACK_SIZE equ 8192
STATE_OFF  equ STACK_SIZE

TLS_TEMPLATE_VALUE equ 0x1234
MAIN_VALUE         equ 0xA1
THREAD_VALUE       equ 0xB2

; The shared page: what each side needs to compare with the other.
ST_MAIN_BLOCK    equ 0
ST_THREAD_STATUS equ 4
ST_THREAD_BLOCK  equ 8

start:
        ; One mapping: the second thread's stack, then a page of shared state.
        mov eax, SYS_MMAP
        xor ebx, ebx
        mov ecx, STACK_SIZE + 4096
        mov edx, 3                      ; PROT_READ | PROT_WRITE
        int 0x80
        cmp eax, -1
        je fail_mmap
        mov ebp, eax                    ; EBP = the mapping
        lea esi, [ebp + STATE_OFF]      ; ESI = the shared page

        ; ---- main thread: its own block, copied from the template ----
        mov ebx, [tls_base]
        test ebx, ebx
        jz fail_1
        cmp dword [ebx], TLS_TEMPLATE_VALUE
        jne fail_2
        mov dword [ebx], MAIN_VALUE
        mov [esi + ST_MAIN_BLOCK], ebx
        mov dword [esi + ST_THREAD_BLOCK], 0

        ; ---- the second thread, with its own stack and one argument ----
        ; The stack top is below the state page, with a little slack so a
        ; thread that starts using its stack cannot reach it.
        lea eax, [ebp + STACK_SIZE - 64]
        sub eax, 4
        mov [eax], esi                  ; [esp + 4]: the argument
        sub eax, 4
        mov dword [eax], 0              ; [esp]: return address, never used
        mov ecx, eax

        mov eax, SYS_CLONE
        mov ebx, thread_entry
        xor edx, edx
        int 0x80
        cmp eax, 0
        jle fail_3
        mov edi, eax                    ; EDI = the thread's id

        ; ---- yield until the thread is done, checking our own storage ----
        mov ebx, [tls_base]             ; main's block, again
        mov ecx, 8
.yield_main:
        mov eax, SYS_YIELD
        int 0x80
        cmp dword [tls_base], ebx
        jne fail_4                      ; the slot no longer names our block
        cmp dword [ebx], MAIN_VALUE
        jne fail_5                      ; our block was overwritten
        dec ecx
        jnz .yield_main

        ; ---- collect the thread's status ----
        mov eax, SYS_WAITPID
        mov ebx, edi
        int 0x80
        cmp eax, THREAD_VALUE
        jne fail_6
        cmp dword [esi + ST_THREAD_STATUS], THREAD_VALUE
        jne fail_7
        mov eax, [esi + ST_THREAD_BLOCK]
        test eax, eax
        jz fail_8                       ; the thread never saw a block
        cmp eax, [esi + ST_MAIN_BLOCK]
        je fail_8                       ; both threads had one block

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;-----------------------------------------------------------------------
; thread_entry - the second thread
;   In : [esp + 4] = the shared page
;-----------------------------------------------------------------------
thread_entry:
        mov edx, [esp + 4]              ; EDX = state
        mov ebx, [tls_base]
        test ebx, ebx
        jz fail_1
        mov [edx + ST_THREAD_BLOCK], ebx
        cmp ebx, [edx + ST_MAIN_BLOCK]
        je fail_8                       ; the same block as main's
        cmp dword [ebx], TLS_TEMPLATE_VALUE
        jne fail_2                      ; not a fresh copy of the template
        mov dword [ebx], THREAD_VALUE

        mov ebp, ebx                    ; our block, across the yields
        mov esi, edx                    ; the shared page, likewise
        mov edi, 8
.yield_thread:
        mov eax, SYS_YIELD
        int 0x80
        cmp dword [tls_base], ebp
        jne fail_9                      ; the slot names main's block again
        cmp dword [ebp], THREAD_VALUE
        jne fail_10                     ; or our block holds main's value
        dec edi
        jnz .yield_thread

        mov dword [esi + ST_THREAD_STATUS], THREAD_VALUE
        mov eax, SYS_THREAD_EXIT
        mov ebx, THREAD_VALUE
        int 0x80
        jmp fail_11                     ; thread_exit does not return

;-----------------------------------------------------------------------
; fail_n - print the check's number and end this thread
;-----------------------------------------------------------------------
%macro FAIL 1
fail_%1:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, %1 + '0'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_THREAD_EXIT
        mov ebx, 1
        int 0x80
%endmacro
        FAIL 1
        FAIL 2
        FAIL 3
        FAIL 4
        FAIL 5
        FAIL 6
        FAIL 7
        FAIL 8
        FAIL 9
        FAIL 10
        FAIL 11

fail_mmap:
        mov eax, SYS_PRINT
        mov ebx, msg_mmap
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; ---- the thread-local storage itself ----
tls_template:
        dd TLS_TEMPLATE_VALUE           ; what a fresh block must contain
        dd 0                            ; spare, so a wrong base shows up
tls_template_end:
tls_index: dd 0                         ; the loader writes this module's index
tls_base:  dd 0                         ; the loader writes this thread's block

msg_ok:   db "THREAD_TLS_OK", 0x0A, 0
msg_fail: db "THREAD_TLS_FAIL ", 0
msg_mmap: db "THREAD_TLS_FAIL mmap", 0x0A, 0

align 4
tls_meta:      db "TLS1"                ; cleared by the build tool
               dd tls_template_end - tls_template
               dd tls_template - $$    ; the template
               dd tls_index - $$       ; where this module's index goes
               dd tls_base - $$        ; where this thread's block goes
