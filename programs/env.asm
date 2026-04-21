; env.asm - Display environment variables
; Usage: env [NAME]
;   env           print all environment variables
;   env NAME      print value of specific variable

%include "syscalls.inc"

ENV_MAX         equ 32
ENV_ENTRY_SIZE  equ 128

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .print_all

        ; Lookup a specific variable by name
        mov eax, SYS_GETENV
        mov ebx, esi
        int 0x80
        test eax, eax
        jz .not_found

        ; EAX = pointer to value string
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.not_found:
        mov eax, SYS_PRINT
        mov ebx, msg_undef
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.print_all:
        xor ebp, ebp            ; slot index
.loop:
        cmp ebp, ENV_MAX
        jge .done

        mov eax, SYS_GETENV_SLOT
        mov ebx, ebp
        mov ecx, entry_buf
        int 0x80
        cmp eax, -1
        je .next                ; empty slot, skip

        ; Print "NAME=VALUE\n"
        mov eax, SYS_PRINT
        mov ebx, entry_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.next:
        inc ebp
        jmp .loop

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

msg_undef:      db "env: variable not set", 10, 0
arg_buf:        times 256 db 0
entry_buf:      times ENV_ENTRY_SIZE db 0
