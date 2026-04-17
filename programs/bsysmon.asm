; bsysmon.asm - BSysMon - Burrows System Monitor
; Displays system information: uptime, memory, disk stats.

%include "syscalls.inc"
%include "lib/gui.inc"

WIN_W   equ 340
WIN_H   equ 260
LINE_H  equ 20

start:
        mov eax, 160
        mov ebx, 90
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

.main_loop:
        call gui_compose
        call render_info
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        jne .main_loop
        cmp bl, 27
        je .close
        cmp bl, 'q'
        je .close
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================
; render_info - Draw system info panel
;=======================================
render_info:
        PUSHALL

        ; Background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, WIN_H
        mov edi, 0x00202830
        call gui_fill_rect

        ; Title bar area
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, 28
        mov edi, 0x00304060
        call gui_fill_rect

        mov eax, [win_id]
        mov ebx, 10
        mov ecx, 6
        mov esi, hdr_str
        mov edi, 0x0080CCFF
        call gui_draw_text

        ; ---- System section ----
        mov dword [line_y], 36

        mov esi, lbl_os
        call draw_label
        mov esi, val_os
        call draw_value
        add dword [line_y], LINE_H

        ; ---- Date/Time ----
        mov esi, lbl_time
        call draw_label
        ; Read RTC time via SYS_DATE
        mov eax, SYS_DATE
        mov ebx, rtc_buf
        int 0x80
        ; rtc_buf: [0]=sec, [1]=min, [2]=hour, [3]=day, [4]=month, [5]=year
        ; Format: HH:MM:SS
        movzx eax, byte [rtc_buf + 2]
        call byte_to_dec
        mov [time_buf], dl
        mov [time_buf+1], al
        mov byte [time_buf+2], ':'
        movzx eax, byte [rtc_buf + 1]
        call byte_to_dec
        mov [time_buf+3], dl
        mov [time_buf+4], al
        mov byte [time_buf+5], ':'
        movzx eax, byte [rtc_buf]
        call byte_to_dec
        mov [time_buf+6], dl
        mov [time_buf+7], al
        mov byte [time_buf+8], 0
        mov esi, time_buf
        call draw_value
        add dword [line_y], LINE_H

        ; ---- Date ----
        mov esi, lbl_date
        call draw_label
        mov eax, SYS_DATE
        mov ebx, rtc_buf
        int 0x80
        ; rtc_buf: [3]=day, [4]=month, [5]=year (BCD)
        ; Format: MM/DD/20YY
        movzx eax, byte [rtc_buf + 4]
        call byte_to_dec
        mov [date_buf], dl
        mov [date_buf+1], al
        mov byte [date_buf+2], '/'
        movzx eax, byte [rtc_buf + 3]
        call byte_to_dec
        mov [date_buf+3], dl
        mov [date_buf+4], al
        mov byte [date_buf+5], '/'
        mov byte [date_buf+6], '2'
        mov byte [date_buf+7], '0'
        movzx eax, byte [rtc_buf + 5]
        call byte_to_dec
        mov [date_buf+8], dl
        mov [date_buf+9], al
        mov byte [date_buf+10], 0
        mov esi, date_buf
        call draw_value
        add dword [line_y], LINE_H

        ; ---- Separator ----
        add dword [line_y], 4
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, [line_y]
        mov edx, WIN_W - 20
        mov esi, 1
        mov edi, 0x00506080
        call gui_fill_rect
        add dword [line_y], 8

        ; ---- Memory ----
        mov esi, lbl_mem
        call draw_label
        mov esi, val_mem
        call draw_value
        add dword [line_y], LINE_H

        mov esi, lbl_vidmem
        call draw_label
        mov esi, val_vidmem
        call draw_value
        add dword [line_y], LINE_H

        ; ---- Separator ----
        add dword [line_y], 4
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, [line_y]
        mov edx, WIN_W - 20
        mov esi, 1
        mov edi, 0x00506080
        call gui_fill_rect
        add dword [line_y], 8

        ; ---- Architecture ----
        mov esi, lbl_arch
        call draw_label
        mov esi, val_arch
        call draw_value
        add dword [line_y], LINE_H

        mov esi, lbl_disk
        call draw_label
        mov esi, val_disk
        call draw_value
        add dword [line_y], LINE_H

        mov esi, lbl_video
        call draw_label
        mov esi, val_video
        call draw_value
        add dword [line_y], LINE_H

        mov esi, lbl_desktop
        call draw_label
        mov esi, val_desktop
        call draw_value

        POPALL
        ret

;---------------------------------------
; draw_label - Draw label at current line
; ESI = label string
;---------------------------------------
draw_label:
        push rax
        push rbx
        push rcx
        push rdi
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, [line_y]
        mov edi, 0x00809CBA
        call gui_draw_text
        pop rdi
        pop rcx
        pop rbx
        pop rax
        ret

;---------------------------------------
; draw_value - Draw value at current line (right side)
; ESI = value string
;---------------------------------------
draw_value:
        push rax
        push rbx
        push rcx
        push rdi
        mov eax, [win_id]
        mov ebx, 150
        mov ecx, [line_y]
        mov edi, 0x00E0E8F0
        call gui_draw_text
        pop rdi
        pop rcx
        pop rbx
        pop rax
        ret

;---------------------------------------
; byte_to_dec - Convert BCD byte to two ASCII digits
; Input: AL = BCD byte
; Output: DL = tens digit, AL = ones digit
;---------------------------------------
byte_to_dec:
        push rcx
        mov dl, al
        shr dl, 4
        and dl, 0x0F
        add dl, '0'
        and al, 0x0F
        add al, '0'
        pop rcx
        ret

; ---- Data ----
title_str:      db "BSysMon", 0
hdr_str:        db "System Information", 0

lbl_os:         db "OS:", 0
val_os:         db "Mellivora OS v2.2", 0

lbl_time:       db "Time:", 0
lbl_date:       db "Date:", 0

lbl_mem:        db "Memory:", 0
val_mem:        db "128 MB mapped", 0

lbl_vidmem:     db "Video:", 0
val_vidmem:     db "640x480x32 VBE", 0

lbl_arch:       db "CPU:", 0
val_arch:       db "Core 2 Duo+ (64-bit)", 0

lbl_disk:       db "Disk:", 0
val_disk:       db "2 GB HBFS", 0

lbl_video:      db "Display:", 0
val_video:      db "Bochs BGA LFB", 0

lbl_desktop:    db "Desktop:", 0
val_desktop:    db "Burrows WM", 0

win_id:         dd 0
line_y:         dd 0
time_buf:       times 12 db 0
date_buf:       times 12 db 0
rtc_buf:        times 8 db 0
