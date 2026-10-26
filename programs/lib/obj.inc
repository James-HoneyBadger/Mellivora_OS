; obj.inc - helpers for programs that talk to the object manager
;
; These print through SYS_PUTCHAR rather than SYS_PRINT: a character argument
; carries no pointer, so output cannot be lost to pointer validation and a
; diagnostic never lies about what happened.

; obj_say - print a NUL-terminated string
;   In : EBX = string            Out: nothing; other registers preserved
obj_say:
        pushad
        mov esi, ebx
.os_loop:
        movzx ebx, byte [esi]
        test ebx, ebx
        jz .os_done
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .os_loop
.os_done:
        popad
        ret

; obj_hex - print EAX as eight hex digits
;   In : EAX = value             Out: nothing; other registers preserved
obj_hex:
        pushad
        mov esi, eax
        mov ecx, 8
.oh_loop:
        rol esi, 4
        mov eax, esi
        and eax, 0x0F
        cmp al, 10
        jb .oh_digit
        add al, 'A' - 10
        jmp .oh_out
.oh_digit:
        add al, '0'
.oh_out:
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .oh_loop
        popad
        ret

; obj_dot - one progress mark
obj_dot:
        pushad
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        popad
        ret

; obj_nl - newline
obj_nl:
        pushad
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        popad
        ret
