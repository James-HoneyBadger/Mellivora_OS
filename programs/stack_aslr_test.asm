; stack_aslr_test.asm - report where this launch's user stack sits
;
; The stack top is randomised per launch, so the only way to check it is to
; launch the program more than once and compare. tests/test_runtime.sh runs it
; a few times in one boot and requires at least two different addresses.
;
; Run as:  stack_aslr_test
; Output:  ASLR_TOP 3xxxxxx0
;          ASLR_OK

%include "syscalls.inc"

start:
        mov eax, SYS_PRINT
        mov ebx, msg_prefix
        int 0x80

        ; ESP here is the launch top minus this program's own prologue, which
        ; is fixed, so the value tracks the randomised top.
        mov eax, esp
        call print_hex

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80

        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;---------------------------------------
; print_hex - print EAX as 8 lowercase hex digits
;---------------------------------------
print_hex:
        pushad
        mov edi, hexbuf
        mov ecx, 8
.digit:
        rol eax, 4
        push eax
        and al, 0x0F
        cmp al, 10
        jb .decimal
        add al, 'a' - 10
        jmp .store
.decimal:
        add al, '0'
.store:
        mov [edi], al
        inc edi
        pop eax
        dec ecx
        jnz .digit
        mov byte [edi], 0

        mov eax, SYS_PRINT
        mov ebx, hexbuf
        int 0x80
        popad
        ret

msg_prefix:  db 'ASLR_TOP ', 0
msg_ok:      db 13, 10, 'ASLR_OK', 13, 10, 0
hexbuf:      times 9 db 0
