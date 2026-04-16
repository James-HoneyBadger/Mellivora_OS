; banner.asm - ASCII art banner display for Mellivora OS
%include "syscalls.inc"

start:
        ; Display colorful ASCII art banner
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; Light red
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line1
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line2
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; Green
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line3
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x09           ; Blue
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line4
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0D           ; Magenta
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line5
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; Cyan
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, line6
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; White
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, subtitle
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; Dark gray
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, credits
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        mov eax, SYS_EXIT
        int 0x80

line1:  db "  __  __      _ _ _                       ", 0x0A, 0
line2:  db " |  \/  | ___| | (_)_   _____  _ __ __ _  ", 0x0A, 0
line3:  db " | |\/| |/ _ \ | | \ \ / / _ \| '__/ _` | ", 0x0A, 0
line4:  db " | |  | |  __/ | | |\ V / (_) | | | (_| | ", 0x0A, 0
line5:  db " |_|  |_|\___|_|_|_| \_/ \___/|_|  \__,_| ", 0x0A, 0
line6:  db "                                           ", 0x0A, 0
subtitle: db 0x0A
        db "         64-bit Long Mode Operating System", 0x0A
        db "            x86-64 | 4GB RAM | LBA48 Disk", 0x0A, 0x0A, 0
credits: db "   Mellivora don't care - it just runs!", 0x0A, 0x0A, 0
