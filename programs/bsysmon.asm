; bsysmon.asm - BSysMon - Burrows System Monitor
; Displays system information: live memory, uptime, process count.
; v6.5: live memory bar, uptime, process count, periodic refresh.

%include "syscalls.inc"
%include "lib/gui.inc"

WIN_W   equ 360
WIN_H   equ 340
LINE_H  equ 18
MEM_TOTAL_PAGES equ (128 * 1024 * 1024) / 4096   ; 32768 pages = 128 MB
REFRESH_TICKS   equ 50          ; ~500 ms

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
        call sample_stats
        call gui_compose
        call render_info
        call gui_flip

.poll:
        call gui_poll_event
        cmp eax, EVT_NONE
        je .idle
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        jne .poll
        cmp bl, 27
        je .close
        cmp bl, 'q'
        je .close
        cmp bl, 'Q'
        je .close
        jmp .poll

.idle:
        mov eax, SYS_SLEEP
        mov ebx, REFRESH_TICKS
        int 0x80
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
        pushad

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

        ; ---- Uptime (HH:MM:SS) ----
        mov esi, lbl_uptime
        call draw_label
        mov esi, uptime_buf
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

        ; ---- Memory (live) ----
        mov esi, lbl_mem
        call draw_label
        mov esi, mem_buf
        call draw_value
        add dword [line_y], LINE_H

        ; Memory bar: 200px wide, shows used%
        mov eax, [win_id]
        mov ebx, 150
        mov ecx, [line_y]
        mov edx, 200
        mov esi, 8
        mov edi, 0x00102030
        call gui_fill_rect
        ; Filled portion (used)
        mov eax, [mem_used_pct]
        imul eax, 200
        mov ecx, 100
        xor edx, edx
        div ecx                 ; EAX = used pixels (0..200)
        test eax, eax
        jz .mb_skip
        mov edx, eax            ; width
        ; pick color: <70% green, 70-89 yellow, >=90 red
        mov edi, 0x0044CC44
        cmp dword [mem_used_pct], 70
        jl  .mb_col_done
        mov edi, 0x00CCCC44
        cmp dword [mem_used_pct], 90
        jl  .mb_col_done
        mov edi, 0x00CC4444
.mb_col_done:
        mov eax, [win_id]
        mov ebx, 150
        mov ecx, [line_y]
        mov esi, 8
        call gui_fill_rect
.mb_skip:
        add dword [line_y], 14

        ; ---- Processes count ----
        mov esi, lbl_procs
        call draw_label
        mov esi, procs_buf
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

        popad
        ret

;---------------------------------------
; draw_label - Draw label at current line
; ESI = label string
;---------------------------------------
draw_label:
        push eax
        push ebx
        push ecx
        push edi
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, [line_y]
        mov edi, 0x00809CBA
        call gui_draw_text
        pop edi
        pop ecx
        pop ebx
        pop eax
        ret

;---------------------------------------
; draw_value - Draw value at current line (right side)
; ESI = value string
;---------------------------------------
draw_value:
        push eax
        push ebx
        push ecx
        push edi
        mov eax, [win_id]
        mov ebx, 150
        mov ecx, [line_y]
        mov edi, 0x00E0E8F0
        call gui_draw_text
        pop edi
        pop ecx
        pop ebx
        pop eax
        ret

;---------------------------------------
; byte_to_dec - Convert BCD byte to two ASCII digits
; Input: AL = BCD byte
; Output: DL = tens digit, AL = ones digit
;---------------------------------------
byte_to_dec:
        push ecx
        mov dl, al
        shr dl, 4
        and dl, 0x0F
        add dl, '0'
        and al, 0x0F
        add al, '0'
        pop ecx
        ret

;---------------------------------------
; uint_to_dec - Convert unsigned int (EAX) to ASCII decimal
; EAX = value, EDI = destination buffer
; Writes null-terminated string. Returns EDI past terminator-1
;---------------------------------------
uint_to_dec:
        push eax
        push ebx
        push ecx
        push edx
        mov ebx, 10
        xor ecx, ecx                    ; digit count
        test eax, eax
        jnz .utd_loop
        mov byte [edi], '0'
        mov byte [edi+1], 0
        jmp .utd_done
.utd_loop:
        test eax, eax
        jz .utd_emit
        xor edx, edx
        div ebx
        add dl, '0'
        push edx
        inc ecx
        jmp .utd_loop
.utd_emit:
        test ecx, ecx
        jz .utd_term
        pop edx
        mov [edi], dl
        inc edi
        dec ecx
        jmp .utd_emit
.utd_term:
        mov byte [edi], 0
.utd_done:
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;---------------------------------------
; sample_stats - Refresh memory / uptime / process counters
;---------------------------------------
sample_stats:
        pushad
        ; ---- Memory ----
        mov eax, SYS_MEMINFO
        int 0x80
        ; EAX = free pages
        mov ecx, MEM_TOTAL_PAGES
        sub ecx, eax                    ; ECX = used pages
        ; used MB = used_pages * 4096 / (1024*1024) = used_pages / 256
        mov edx, ecx
        shr edx, 8                      ; used MB
        mov [mem_used_mb], edx
        ; total MB = MEM_TOTAL_PAGES / 256 = 128
        mov dword [mem_total_mb], 128
        ; pct = used_pages * 100 / total_pages
        mov eax, ecx
        mov edx, 0
        mov ebx, 100
        mul ebx                         ; EDX:EAX = used*100
        mov ebx, MEM_TOTAL_PAGES
        div ebx                         ; EAX = pct
        mov [mem_used_pct], eax
        ; Format "USED / TOTAL MB (PP%)"
        mov edi, mem_buf
        mov eax, [mem_used_mb]
        call uint_to_dec
        ; advance edi to terminator
.ss_m1: cmp byte [edi], 0
        je  .ss_m1d
        inc edi
        jmp .ss_m1
.ss_m1d:
        mov byte [edi], ' '
        mov byte [edi+1], '/'
        mov byte [edi+2], ' '
        add edi, 3
        mov eax, [mem_total_mb]
        call uint_to_dec
.ss_m2: cmp byte [edi], 0
        je  .ss_m2d
        inc edi
        jmp .ss_m2
.ss_m2d:
        mov byte [edi], ' '
        mov byte [edi+1], 'M'
        mov byte [edi+2], 'B'
        mov byte [edi+3], ' '
        mov byte [edi+4], '('
        add edi, 5
        mov eax, [mem_used_pct]
        call uint_to_dec
.ss_m3: cmp byte [edi], 0
        je  .ss_m3d
        inc edi
        jmp .ss_m3
.ss_m3d:
        mov byte [edi], '%'
        mov byte [edi+1], ')'
        mov byte [edi+2], 0

        ; ---- Uptime: SYS_GETTIME returns ticks (10ms each) ----
        mov eax, SYS_GETTIME
        int 0x80
        ; seconds = ticks / 100
        xor edx, edx
        mov ebx, 100
        div ebx                         ; EAX = total seconds
        ; HH:MM:SS
        xor edx, edx
        mov ebx, 60
        div ebx                         ; EAX=minutes EDX=sec
        mov ecx, edx                    ; ECX = sec
        xor edx, edx
        div ebx                         ; EAX=hours EDX=min
        ; EAX=hours, EDX=min, ECX=sec
        push ecx
        push edx
        mov edi, uptime_buf
        call uint_to_dec
.ss_u1: cmp byte [edi], 0
        je  .ss_u1d
        inc edi
        jmp .ss_u1
.ss_u1d:
        mov byte [edi], 'h'
        mov byte [edi+1], ' '
        add edi, 2
        pop eax                         ; min
        call uint_to_dec
.ss_u2: cmp byte [edi], 0
        je  .ss_u2d
        inc edi
        jmp .ss_u2
.ss_u2d:
        mov byte [edi], 'm'
        mov byte [edi+1], ' '
        add edi, 2
        pop eax                         ; sec
        call uint_to_dec
.ss_u3: cmp byte [edi], 0
        je  .ss_u3d
        inc edi
        jmp .ss_u3
.ss_u3d:
        mov byte [edi], 's'
        mov byte [edi+1], 0

        ; ---- Process count ----
        xor ebx, ebx                    ; slot
        xor esi, esi                    ; count of non-FREE
.ss_p:
        cmp ebx, 128
        jge .ss_pd
        push ebx
        mov eax, 66                     ; SYS_PROCLIST
        mov ecx, proc_buf
        int 0x80
        pop ebx
        cmp eax, 0
        jne .ss_pn
        cmp dword [proc_buf], 0         ; state == TASK_FREE?
        je  .ss_pn
        inc esi
.ss_pn:
        inc ebx
        jmp .ss_p
.ss_pd:
        mov edi, procs_buf
        mov eax, esi
        call uint_to_dec
.ss_pe: cmp byte [edi], 0
        je  .ss_ped
        inc edi
        jmp .ss_pe
.ss_ped:
        mov byte [edi], ' '
        mov byte [edi+1], 'a'
        mov byte [edi+2], 'c'
        mov byte [edi+3], 't'
        mov byte [edi+4], 'i'
        mov byte [edi+5], 'v'
        mov byte [edi+6], 'e'
        mov byte [edi+7], 0

        popad
        ret

; ---- Data ----
title_str:      db "BSysMon", 0
hdr_str:        db "System Information", 0

lbl_os:         db "OS:", 0
val_os:         db "Mellivora OS v8.5.0", 0

lbl_time:       db "Time:", 0
lbl_date:       db "Date:", 0
lbl_uptime:     db "Uptime:", 0

lbl_mem:        db "Memory:", 0
lbl_procs:      db "Procs:", 0

lbl_vidmem:     db "Video:", 0
val_vidmem:     db "640x480x32 VBE", 0

lbl_arch:       db "CPU:", 0
val_arch:       db "i486+ (32-bit)", 0

lbl_disk:       db "Disk:", 0
val_disk:       db "2 GB HBFS", 0

lbl_video:      db "Display:", 0
val_video:      db "Bochs BGA LFB", 0

lbl_desktop:    db "Desktop:", 0
val_desktop:    db "Burrows WM", 0

win_id:         dd 0
line_y:         dd 0
mem_used_mb:    dd 0
mem_total_mb:   dd 128
mem_used_pct:   dd 0
time_buf:       times 12 db 0
date_buf:       times 12 db 0
uptime_buf:     times 24 db 0
mem_buf:        times 32 db 0
procs_buf:      times 24 db 0
proc_buf:       times 48 db 0
rtc_buf:        times 8 db 0
