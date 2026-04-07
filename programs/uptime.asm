; uptime.asm - System uptime display for Mellivora OS
; Shows how long the system has been running using PIT tick counter
;
%include "syscalls.inc"

start:
        ; Get system tick count via SYS_GETTIME
        ; Returns tick count in EAX (18.2 ticks per second from PIT)
        mov eax, SYS_GETTIME
        int 0x80
        mov [ticks], eax

        ; Convert ticks to seconds (approximately ticks / 18)
        xor edx, edx
        mov ecx, 18
        div ecx
        mov [total_secs], eax

        ; Calculate hours, minutes, seconds
        xor edx, edx
        mov ecx, 3600
        div ecx
        mov [hours], eax

        mov eax, edx            ; remainder
        xor edx, edx
        mov ecx, 60
        div ecx
        mov [minutes], eax
        mov [seconds], edx

        ; Display
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; cyan
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_header
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; white
        int 0x80

        ; Hours
        mov eax, [hours]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_hours
        int 0x80

        ; Minutes
        mov eax, [minutes]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_mins
        int 0x80

        ; Seconds
        mov eax, [seconds]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_secs
        int 0x80

        ; Show ticks
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_ticks
        int 0x80
        mov eax, [ticks]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_ticks2
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        mov eax, SYS_EXIT
        int 0x80

; Data
msg_header:     db "System uptime: ", 0
msg_hours:      db " hour(s), ", 0
msg_mins:       db " minute(s), ", 0
msg_secs:       db " second(s)", 0x0A, 0
msg_ticks:      db "(", 0
msg_ticks2:     db " timer ticks)", 0x0A, 0

ticks:          dd 0
total_secs:     dd 0
hours:          dd 0
minutes:        dd 0
seconds:        dd 0
