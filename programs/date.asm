; date.asm - Display or set the current date and time
; Usage: date            - show current date/time
;        date -s HHMMSS  - set time

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov esi, arg_buf
        call skip_spaces

        cmp byte [esi], '-'
        jne show_date
        cmp byte [esi+1], 's'
        jne show_date

        ; Set time mode
        add esi, 2
        call skip_spaces
        cmp byte [esi], 0
        je usage

        ; Parse HHMMSS
        call parse_two
        mov [set_hour], al
        call parse_two
        mov [set_min], al
        call parse_two
        mov [set_sec], al

        mov eax, SYS_SETDATE
        movzx ebx, byte [set_hour]
        movzx ecx, byte [set_min]
        movzx edx, byte [set_sec]
        int 0x80
        ; Fall through to show updated time

show_date:
        ; Get date
        mov eax, SYS_DATE
        mov ebx, date_buf
        int 0x80

        ; date_buf format: [year_hi, year_lo, month, day, dow, hour, min, sec]
        ; Print day-of-week name
        movzx eax, byte [date_buf + 4]   ; dow (1=Sun..7=Sat or 0=Sun)
        and eax, 7
        mov rax, [dow_table + rax * 8]
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80

        ; Print month name
        movzx eax, byte [date_buf + 2]
        ; BCD to binary
        call bcd_to_bin
        cmp eax, 12
        ja .bad_month
        cmp eax, 0
        je .bad_month
        dec eax
        mov rax, [month_table + rax * 8]
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        jmp .print_day
.bad_month:
        mov eax, SYS_PRINT
        mov ebx, str_unk
        int 0x80

.print_day:
        ; Day
        movzx eax, byte [date_buf + 3]
        call bcd_to_bin
        call print_padded2
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Time HH:MM:SS
        movzx eax, byte [date_buf + 5]
        call bcd_to_bin
        call print_padded2
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
        movzx eax, byte [date_buf + 6]
        call bcd_to_bin
        call print_padded2
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
        movzx eax, byte [date_buf + 7]
        call bcd_to_bin
        call print_padded2
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Year (BCD century + BCD year)
        movzx eax, byte [date_buf]
        call bcd_to_bin
        imul eax, 100
        push rax
        movzx eax, byte [date_buf + 1]
        call bcd_to_bin
        pop rcx
        add eax, ecx
        call print_dec

        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;--------------------------------------
; bcd_to_bin: Convert BCD byte in AL to binary in EAX
;--------------------------------------
bcd_to_bin:
        push rbx
        movzx ebx, al
        shr al, 4
        movzx eax, al
        imul eax, 10
        and ebx, 0x0F
        add eax, ebx
        pop rbx
        ret

;--------------------------------------
; print_padded2: Print 2-digit number in EAX with leading zero
;--------------------------------------
print_padded2:
        push rax
        cmp eax, 10
        jge .two
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        pop rax
.two:
        call print_dec
        pop rax
        ret

;--------------------------------------
; parse_two: Parse 2 digit decimal from [ESI], advance ESI, return in AL
;--------------------------------------
parse_two:
        movzx eax, byte [esi]
        sub al, '0'
        imul eax, 10
        inc esi
        movzx ecx, byte [esi]
        sub cl, '0'
        add al, cl
        inc esi
        ret

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
usage_str:  db "Usage: date [-s HHMMSS]", 10, 0
str_unk:    db "??? ", 0

dow_table:
        dq dow_sun, dow_mon, dow_tue, dow_wed, dow_thu, dow_fri, dow_sat, dow_sun

dow_sun: db "Sun ", 0
dow_mon: db "Mon ", 0
dow_tue: db "Tue ", 0
dow_wed: db "Wed ", 0
dow_thu: db "Thu ", 0
dow_fri: db "Fri ", 0
dow_sat: db "Sat ", 0

month_table:
        dq m_jan, m_feb, m_mar, m_apr, m_may, m_jun
        dq m_jul, m_aug, m_sep, m_oct, m_nov, m_dec

m_jan: db "Jan ", 0
m_feb: db "Feb ", 0
m_mar: db "Mar ", 0
m_apr: db "Apr ", 0
m_may: db "May ", 0
m_jun: db "Jun ", 0
m_jul: db "Jul ", 0
m_aug: db "Aug ", 0
m_sep: db "Sep ", 0
m_oct: db "Oct ", 0
m_nov: db "Nov ", 0
m_dec: db "Dec ", 0

date_buf:   times 16 db 0
set_hour:   db 0
set_min:    db 0
set_sec:    db 0
arg_buf:    times 256 db 0
