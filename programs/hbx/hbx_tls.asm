; hbx_tls.asm - thread-local storage, and what makes it per-thread.
;
; The payload declares a TLS directory with the TLS1 marker at the end: the
; template (a known constant, and a spare dword), and the two slots the loader
; fills - this module's index and this thread's block.
;
; What the program proves is not that it can read its own storage, but that the
; storage is *this task's*: it prints what the block held before it wrote
; anything, then writes 42. Two launches must both see 0x1234, because the
; second launch gets a fresh block copied from the same template - which is the
; whole difference between a thread-local variable and a global one.
;
;   TLS_SEEN 0x00001234   the template was copied into this task's block
;   TLS_SET 0x0000002A    this task's own copy now says 42
;   TLS_TASK_OK
%include "syscalls.inc"

start:
        ; The block, through the slot the loader filled at launch.
        db 0x8B, 0x1D                   ; mov ebx, [base_slot]
slot_disp: dd tls_base
        mov esi, ebx                    ; ESI = this thread's block

        mov eax, SYS_PRINT
        mov ebx, msg_seen
        int 0x80
        mov eax, [esi]
        call print_hex

        mov dword [esi], 42
        mov eax, SYS_PRINT
        mov ebx, msg_set
        int 0x80
        mov eax, [esi]
        call print_hex

        ; The template's spare dword is zero and stayed zero, which is what
        ; proves the block was filled from the template rather than shared with
        ; it, left uninitialised, or written past.
        cmp dword [esi + 4], 0
        jne .fail
        cmp dword [esi], 42
        jne .fail
        ; And the index the loader assigned is this module's own.
        cmp dword [tls_index], 0
        jne .fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;-----------------------------------------------------------------------
; print_hex - EAX as eight hex digits and a newline, through SYS_PUTCHAR
;-----------------------------------------------------------------------
print_hex:
        mov ebp, eax
        mov ecx, 8
.hex:
        rol ebp, 4
        mov ebx, ebp
        and ebx, 0x0F
        cmp ebx, 10
        jb .digit
        add ebx, 'A' - 10
        jmp .emit
.digit:
        add ebx, '0'
.emit:
        mov eax, SYS_PUTCHAR
        int 0x80
        loop .hex
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        ret

; ---- the thread-local storage itself ----
tls_template:
        dd 0x1234                       ; what a fresh block must contain
        dd 0                            ; spare, so a wrong base shows up
tls_template_end:
tls_index: dd 0                         ; the loader writes this module's index
tls_base:  dd 0                         ; the loader writes this thread's block

msg_seen:     db "TLS_SEEN 0x", 0
msg_set:      db "TLS_SET 0x", 0
msg_ok:       db "TLS_TASK_OK", 0x0A, 0
msg_fail:     db "TLS_TASK_FAIL", 0x0A, 0

align 4
tls_meta:      db "TLS1"                ; cleared by the build tool
               dd tls_template_end - tls_template
               dd tls_template - $$    ; the template
               dd tls_index - $$       ; where this module's index goes
               dd tls_base - $$        ; where this thread's block goes
