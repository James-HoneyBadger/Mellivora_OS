; ==========================================================================
; cron - Simple task scheduler daemon for Mellivora OS
;
; Usage: cron &              Start cron daemon in background
;        cron -l             List current crontab
;        cron -e             Edit crontab (opens in edit)
;
; Crontab file: /etc/crontab
; Format: MM HH CMD
;   MM  = minute (0-59, or * for every minute)
;   HH  = hour   (0-23, or * for every hour)
;   CMD = notification message or command name
;
; Example crontab:
;   00 12 Lunch time!
;   30 *  Half-hour mark
;   *  *  Every minute ping
;
; When a job triggers, cron uses SYS_NOTIFY to display it.
; ==========================================================================
%include "syscalls.inc"

MAX_JOBS    equ 16
JOB_SIZE    equ 68              ; 2+2+64 (min, hour, message)
CRONTAB     equ crontab_path

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        ; Check flags
        mov esi, arg_buf
        cmp byte [esi], 0
        je daemon_mode          ; No args = run daemon

        cmp word [esi], '-l'
        je list_crontab
        cmp word [esi], '-e'
        je edit_crontab

        ; Unknown flag / no flag = run daemon
        jmp daemon_mode

; -------------------------------------------------------------------
; List crontab
; -------------------------------------------------------------------
list_crontab:
        mov eax, SYS_FREAD
        mov ebx, CRONTAB
        mov ecx, file_buf
        int 0x80
        test eax, eax
        jz .no_crontab

        mov eax, SYS_PRINT
        mov ebx, file_buf
        int 0x80
        jmp exit_ok

.no_crontab:
        mov eax, SYS_PRINT
        mov ebx, msg_no_crontab
        int 0x80
        jmp exit_ok

; -------------------------------------------------------------------
; Edit crontab (exec editor)
; -------------------------------------------------------------------
edit_crontab:
        mov eax, SYS_EXEC
        mov ebx, edit_cmd
        int 0x80
        ; If exec fails
        mov eax, SYS_PRINT
        mov ebx, msg_no_edit
        int 0x80
        jmp exit_err

; -------------------------------------------------------------------
; Daemon mode: loop forever, check crontab each minute
; -------------------------------------------------------------------
daemon_mode:
        mov eax, SYS_PRINT
        mov ebx, msg_started
        int 0x80

        ; Load and parse crontab
        call load_crontab
        cmp dword [job_count], 0
        je .no_jobs

        ; Get initial time
        call get_current_time
        mov al, [cur_min]
        mov [last_min], al

.main_loop:
        ; Sleep ~10 seconds (1000 ticks)
        mov eax, SYS_SLEEP
        mov ebx, 1000
        int 0x80

        ; Check time
        call get_current_time

        ; Did the minute change?
        mov al, [cur_min]
        cmp al, [last_min]
        je .main_loop           ; same minute, keep sleeping

        ; Minute changed — check jobs
        mov al, [cur_min]
        mov [last_min], al

        call check_jobs
        jmp .main_loop

.no_jobs:
        mov eax, SYS_PRINT
        mov ebx, msg_no_crontab
        int 0x80
        jmp exit_ok

; -------------------------------------------------------------------
; load_crontab - Read /etc/crontab and parse entries
; -------------------------------------------------------------------
load_crontab:
        PUSHALL
        mov dword [job_count], 0

        mov eax, SYS_FREAD
        mov ebx, CRONTAB
        mov ecx, file_buf
        int 0x80
        test eax, eax
        jz .lc_done

        ; Null-terminate
        mov byte [file_buf + eax], 0

        ; Parse line by line
        mov esi, file_buf

.lc_line:
        cmp byte [esi], 0
        je .lc_done
        cmp dword [job_count], MAX_JOBS
        jge .lc_done

        ; Skip blank lines and comments
        cmp byte [esi], '#'
        je .lc_skip_line
        cmp byte [esi], 0x0A
        je .lc_next_char
        cmp byte [esi], 0x0D
        je .lc_next_char

        ; Parse: MM HH CMD
        ; Get job entry pointer
        mov eax, [job_count]
        imul eax, JOB_SIZE
        lea edi, [job_table + eax]

        ; Parse minute field
        cmp byte [esi], '*'
        jne .lc_parse_min
        mov word [edi], -1       ; wildcard
        inc esi
        jmp .lc_skip_sp1
.lc_parse_min:
        call parse_number
        mov [edi], ax
.lc_skip_sp1:
        call skip_sp

        ; Parse hour field
        cmp byte [esi], '*'
        jne .lc_parse_hr
        mov word [edi + 2], -1   ; wildcard
        inc esi
        jmp .lc_skip_sp2
.lc_parse_hr:
        call parse_number
        mov [edi + 2], ax
.lc_skip_sp2:
        call skip_sp

        ; Rest of line is the command/message
        lea edi, [edi + 4]       ; point to message field
        mov ecx, 63
.lc_copy_msg:
        lodsb
        test al, al
        jz .lc_msg_end
        cmp al, 0x0A
        je .lc_msg_end
        cmp al, 0x0D
        je .lc_msg_end
        stosb
        dec ecx
        jnz .lc_copy_msg
.lc_msg_end:
        mov byte [edi], 0
        inc dword [job_count]
        ; If we stopped on \n, continue
        cmp al, 0x0A
        je .lc_line
        cmp al, 0x0D
        je .lc_line
        jmp .lc_done

.lc_skip_line:
        lodsb
        test al, al
        jz .lc_done
        cmp al, 0x0A
        jne .lc_skip_line
        jmp .lc_line

.lc_next_char:
        inc esi
        jmp .lc_line

.lc_done:
        POPALL
        ret

; -------------------------------------------------------------------
; check_jobs - Check all jobs against current time
; -------------------------------------------------------------------
check_jobs:
        PUSHALL
        mov ecx, [job_count]
        test ecx, ecx
        jz .cj_done
        mov esi, job_table

.cj_loop:
        ; Check minute
        movzx eax, word [esi]
        cmp ax, -1               ; wildcard?
        je .cj_min_ok
        cmp al, [cur_min]
        jne .cj_next
.cj_min_ok:
        ; Check hour
        movzx eax, word [esi + 2]
        cmp ax, -1
        je .cj_hr_ok
        cmp al, [cur_hour]
        jne .cj_next
.cj_hr_ok:
        ; Job matches! Send notification
        push rcx
        push rsi
        lea ebx, [esi + 4]      ; message text
        mov eax, SYS_NOTIFY
        mov edx, 0x0E           ; yellow
        int 0x80
        pop rsi
        pop rcx

.cj_next:
        add esi, JOB_SIZE
        dec ecx
        jnz .cj_loop
.cj_done:
        POPALL
        ret

; -------------------------------------------------------------------
; get_current_time - Read RTC into cur_min, cur_hour
; -------------------------------------------------------------------
get_current_time:
        push rax
        push rbx
        mov eax, SYS_DATE
        mov ebx, date_buf
        int 0x80
        ; date_buf: [sec, min, hour, day, month, year] (BCD)
        ; Convert BCD min and hour
        movzx eax, byte [date_buf + 1]
        call bcd_to_bin
        mov [cur_min], al
        movzx eax, byte [date_buf + 2]
        call bcd_to_bin
        mov [cur_hour], al
        pop rbx
        pop rax
        ret

; bcd_to_bin - Convert BCD byte in AL to binary
bcd_to_bin:
        push rbx
        mov bl, al
        shr al, 4
        mov ah, 10
        mul ah                   ; AL = high_nibble * 10
        and bl, 0x0F
        add al, bl
        pop rbx
        ret

; parse_number - Parse decimal number from ESI, result in AX
parse_number:
        xor eax, eax
.pn_loop:
        cmp byte [esi], '0'
        jb .pn_done
        cmp byte [esi], '9'
        ja .pn_done
        imul eax, 10
        movzx ebx, byte [esi]
        sub ebx, '0'
        add eax, ebx
        inc esi
        jmp .pn_loop
.pn_done:
        ret

; skip_sp - Skip spaces at ESI
skip_sp:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_sp
.ss_done:
        ret

exit_ok:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
exit_err:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; -------------------------------------------------------------------
; Strings
; -------------------------------------------------------------------
msg_started:    db "cron: daemon started", 0x0A, 0
msg_no_crontab: db "cron: no /etc/crontab found", 0x0A, 0
msg_no_edit:    db "cron: editor not found", 0x0A, 0
crontab_path:   db "/etc/crontab", 0
edit_cmd:       db "edit /etc/crontab", 0

; -------------------------------------------------------------------
; BSS
; -------------------------------------------------------------------
date_buf:       times 6 db 0
cur_min:        db 0
cur_hour:       db 0
last_min:       db 0
job_count:      dd 0
arg_buf:        times 256 db 0
file_buf:       times 4096 db 0
job_table:      times (MAX_JOBS * JOB_SIZE) db 0
