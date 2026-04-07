; sysinfo.asm - System information display for Mellivora OS
; Uses INT 0x80 syscalls
%include "syscalls.inc"

start:
        ; Header
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F           ; White on blue
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_header
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; Cyan
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_arch
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_mode
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_addr
        int 0x80

        ; Get uptime
        mov eax, SYS_GETTIME
        int 0x80
        ; EAX = tick count
        push eax

        mov eax, SYS_PRINT
        mov ebx, msg_uptime
        int 0x80

        pop eax
        xor edx, edx
        mov ebx, 100
        div ebx
        ; Print seconds (simple decimal)
        call print_dec

        mov eax, SYS_PRINT
        mov ebx, msg_secs
        int 0x80

        ; CPU features check
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80

        ; Check if CPUID is available by toggling ID flag in EFLAGS
        pushfd
        pop eax
        mov ecx, eax
        xor eax, 0x200000       ; Toggle ID bit
        push eax
        popfd
        pushfd
        pop eax
        push ecx
        popfd
        xor eax, ecx
        jz .no_cpuid

        mov eax, SYS_PRINT
        mov ebx, msg_cpuid_yes
        int 0x80
        jmp .done_cpuid

.no_cpuid:
        mov eax, SYS_PRINT
        mov ebx, msg_cpuid_no
        int 0x80

.done_cpuid:
        ; Reset color
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_footer
        int 0x80

        mov eax, SYS_EXIT
        int 0x80

msg_header:     db " === Mellivora System Information === ", 0x0A, 0
msg_arch:       db "  Architecture:  i486+ (32-bit Protected Mode)", 0x0A, 0
msg_mode:       db "  Memory Model:  Flat, 4 GB address space", 0x0A, 0
msg_addr:       db "  Program Base:  0x00200000 (2 MB)", 0x0A, 0
msg_uptime:     db "  Uptime:        ", 0
msg_secs:       db " seconds", 0x0A, 0
msg_cpuid_yes:  db "  CPUID:         Available", 0x0A, 0
msg_cpuid_no:   db "  CPUID:         Not available (486SX/DX)", 0x0A, 0
msg_footer:     db 0x0A, "System info complete.", 0x0A, 0
