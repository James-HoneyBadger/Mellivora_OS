; pkginfo.asm - Print Mellivora system identification & versions
; Suggestion #14: Distribution/onboarding. The first thing a packager,
; bug-reporter, or curious user runs.

%include "syscalls.inc"

PAGE_KB equ 4

start:
        mov eax, SYS_PRINT
        mov ebx, banner
        int 0x80

        ; OS line
        mov eax, SYS_PRINT
        mov ebx, lbl_os
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, val_os
        int 0x80

        ; Version
        mov eax, SYS_PRINT
        mov ebx, lbl_ver
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, val_ver
        int 0x80

        ; Build / arch
        mov eax, SYS_PRINT
        mov ebx, lbl_arch
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, val_arch
        int 0x80

        ; Hostname (best-effort: env)
        mov eax, SYS_PRINT
        mov ebx, lbl_host
        int 0x80
        mov eax, SYS_GETENV
        mov ebx, env_host
        mov ecx, host_buf
        int 0x80
        cmp byte [host_buf], 0
        jne .has_host
        mov dword [host_buf], 'mell'
        mov dword [host_buf+4], 'ivor'
        mov word  [host_buf+8], 'a'
        mov byte  [host_buf+9], 0
.has_host:
        mov eax, SYS_PRINT
        mov ebx, host_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Memory
        mov eax, SYS_MEMINFO
        int 0x80
        mov [free_pages], eax
        mov [boot_pages], ebx

        mov eax, SYS_PRINT
        mov ebx, lbl_mem
        int 0x80
        mov eax, [boot_pages]
        mov ecx, PAGE_KB
        mul ecx
        call print_uint
        mov eax, SYS_PRINT
        mov ebx, str_kb_total
        int 0x80
        mov eax, [free_pages]
        mov ecx, PAGE_KB
        mul ecx
        call print_uint
        mov eax, SYS_PRINT
        mov ebx, str_kb_free
        int 0x80

        ; Uptime
        mov eax, SYS_GETTIME
        int 0x80
        mov ecx, 100
        xor edx, edx
        div ecx
        mov [secs], eax
        mov eax, SYS_PRINT
        mov ebx, lbl_up
        int 0x80
        mov eax, [secs]
        call print_uint
        mov eax, SYS_PRINT
        mov ebx, str_secs
        int 0x80

        ; Footer
        mov eax, SYS_PRINT
        mov ebx, footer
        int 0x80

        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------
print_uint:
        push ebx
        push ecx
        push edx
        push edi
        mov edi, numbuf + 11
        mov byte [edi], 0
        mov ebx, 10
        test eax, eax
        jnz .l
        dec edi
        mov byte [edi], '0'
        jmp .o
.l:
        xor edx, edx
        div ebx
        add dl, '0'
        dec edi
        mov [edi], dl
        test eax, eax
        jnz .l
.o:
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret

banner:    db 'Mellivora OS - System Information', 10
           db '=================================', 10, 0
lbl_os:    db 'OS:       ', 0
val_os:    db 'Mellivora "Expanded"', 10, 0
lbl_ver:   db 'Version:  ', 0
val_ver:   db '8.5.0', 10, 0
lbl_arch:  db 'Arch:     ', 0
val_arch:  db 'x86 (i486+) 32-bit protected mode', 10, 0
lbl_host:  db 'Host:     ', 0
lbl_mem:   db 'Memory:   ', 0
str_kb_total: db ' KB total, ', 0
str_kb_free:  db ' KB free', 10, 0
lbl_up:    db 'Uptime:   ', 0
str_secs:  db ' s', 10, 0
footer:
        db 10, 'Report bugs at https://github.com/James-HoneyBadger/Mellivora_OS', 10, 0
env_host:  db 'HOSTNAME', 0

free_pages: dd 0
boot_pages: dd 0
secs:       dd 0
host_buf:   times 64 db 0
numbuf:     times 12 db 0
