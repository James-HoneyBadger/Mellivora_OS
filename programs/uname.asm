; uname.asm - Print system information (Unix-style)
; Usage: uname [-a|-s|-n|-r|-m]
;   -s  OS name (default)
;   -n  hostname
;   -r  release version
;   -m  machine architecture
;   -a  all information

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov esi, arg_buf
        call skip_spaces

        ; Default: print OS name only
        cmp byte [esi], 0
        je .print_sysname
        cmp byte [esi], '-'
        jne .print_sysname

        movzx eax, byte [esi+1]

        cmp al, 'a'
        je .print_all
        cmp al, 's'
        je .print_sysname
        cmp al, 'n'
        je .print_nodename
        cmp al, 'r'
        je .print_release
        cmp al, 'm'
        je .print_machine
        jmp .usage

.print_all:
        mov eax, SYS_PRINT
        mov ebx, sysname
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, nodename
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, release
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, version
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, machine
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .exit

.print_sysname:
        mov eax, SYS_PRINT
        mov ebx, sysname
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .exit

.print_nodename:
        mov eax, SYS_PRINT
        mov ebx, nodename
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .exit

.print_release:
        mov eax, SYS_PRINT
        mov ebx, release
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .exit

.print_machine:
        mov eax, SYS_PRINT
        mov ebx, machine
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;--------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

;=======================================
; Data
;=======================================
sysname:    db "Mellivora", 0
nodename:   db "honeybadger", 0
release:    db "2.1.0", 0
version:    db "#1 SMP i486", 0
machine:    db "i486", 0

usage_str:  db "Usage: uname [-a|-s|-n|-r|-m]", 10
            db "  -s  OS name", 10
            db "  -n  hostname", 10
            db "  -r  release", 10
            db "  -m  architecture", 10
            db "  -a  all", 10, 0

arg_buf:    times 256 db 0
