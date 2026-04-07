; cal.asm - Calendar display for Mellivora OS
; Shows a month in a traditional calendar grid.
; Uses SYS_DATE to read the RTC, then calculates day-of-week
; via Tomohiko Sakamoto's algorithm.
;
; Usage: cal              (show current month)
;        cal <month>      (show month of current year)
;        cal <month> <year>  (show specific month/year)
%include "syscalls.inc"

start:
        ; Read current date/time (always needed for "today" highlight)
        mov eax, SYS_DATE
        mov ebx, date_buf
        int 0x80
        ; EAX = full year (e.g. 2026)
        mov [cur_year], eax
        mov [real_year], eax
        movzx eax, byte [date_buf + 4]     ; month (1-12)
        mov [cur_month], eax
        mov [real_month], eax
        movzx eax, byte [date_buf + 3]     ; day (1-31)
        mov [cur_day], eax

        ; Check for command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .no_args

        ; Parse first argument: month number
        mov esi, arg_buf
        call skip_spaces
        call parse_number
        test ecx, ecx          ; ECX = digits parsed
        jz .no_args             ; no digits found, use defaults
        ; Validate month 1-12
        cmp eax, 1
        jl .bad_args
        cmp eax, 12
        jg .bad_args
        mov [cur_month], eax

        ; Parse optional second argument: year
        call skip_spaces
        cmp byte [esi], 0
        je .args_done           ; no year given, use current year
        call parse_number
        test ecx, ecx
        jz .args_done           ; no digits, ignore
        ; Validate year 1-9999
        cmp eax, 1
        jl .bad_args
        cmp eax, 9999
        jg .bad_args
        mov [cur_year], eax

.args_done:
        ; If displaying a different month/year than today, disable
        ; "today" highlight by setting cur_day to 0
        mov eax, [cur_month]
        cmp eax, [real_month]
        jne .no_today
        mov eax, [cur_year]
        cmp eax, [real_year]
        je .no_args             ; same month/year, keep highlight
.no_today:
        mov dword [cur_day], 0
.no_args:
        call draw_calendar

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.bad_args:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;=== Skip spaces at ESI ===
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

;=== Parse decimal number at ESI -> EAX, digits parsed in ECX ===
parse_number:
        xor eax, eax
        xor ecx, ecx           ; digit count
.pn_loop:
        movzx edx, byte [esi]
        sub edx, '0'
        cmp edx, 9
        ja .pn_done
        imul eax, 10
        add eax, edx
        inc esi
        inc ecx
        jmp .pn_loop
.pn_done:
        ret

;=== Draw the calendar ===
draw_calendar:
        pushad
        ; Print header: "  Month Year"
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F           ; white on blue
        int 0x80

        ; Print month name
        mov eax, SYS_PRINT
        mov ebx, msg_spaces4
        int 0x80

        mov eax, [cur_month]
        dec eax                 ; 0-indexed
        imul eax, 10            ; 10 chars per month name
        lea ebx, [month_names + eax]
        mov eax, SYS_PRINT
        int 0x80

        ; Print year
        mov eax, [cur_year]
        call print_dec

        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80

        ; Print day-of-week header
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; yellow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_dow_header
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07           ; default
        int 0x80

        ; Calculate day-of-week for 1st of current month
        ; Using Tomohiko Sakamoto's algorithm:
        ;   dow(y, m, d):
        ;     t = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4]
        ;     if m < 3: y -= 1
        ;     return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7
        ;   Result: 0=Sunday, 1=Monday, ..., 6=Saturday

        mov eax, [cur_year]
        mov ecx, [cur_month]
        cmp ecx, 3
        jge .no_adjust
        dec eax
.no_adjust:
        mov [tmp_year], eax

        ; y + y/4 - y/100 + y/400
        mov ebx, eax            ; y
        xor edx, edx
        push eax
        mov ecx, 4
        div ecx                 ; y/4
        add ebx, eax
        pop eax
        push eax
        xor edx, edx
        mov ecx, 100
        div ecx                 ; y/100
        sub ebx, eax
        pop eax
        xor edx, edx
        mov ecx, 400
        div ecx                 ; y/400
        add ebx, eax

        ; + t[m-1]
        mov ecx, [cur_month]
        dec ecx
        movzx eax, byte [dow_table + ecx]
        add ebx, eax

        ; + d (day=1 for first of month)
        add ebx, 1

        ; % 7
        mov eax, ebx
        xor edx, edx
        mov ecx, 7
        div ecx
        ; EDX = day of week for the 1st (0=Sun, 1=Mon, ..., 6=Sat)
        mov [first_dow], edx

        ; Get days in current month
        mov ecx, [cur_month]
        dec ecx
        movzx eax, byte [days_table + ecx]
        ; Check for February leap year
        cmp ecx, 1              ; February (0-indexed)
        jne .no_leap
        call is_leap_year
        test eax, eax
        jz .no_leap
        mov eax, 29
        jmp .got_days
.no_leap:
        movzx eax, byte [days_table + ecx]
.got_days:
        mov [days_in_month], eax

        ; Print leading spaces for first week
        mov ecx, [first_dow]
        test ecx, ecx
        jz .print_days
.lead_space:
        mov eax, SYS_PRINT
        mov ebx, msg_cell_blank
        int 0x80
        dec ecx
        jnz .lead_space

.print_days:
        mov dword [print_day], 1
        mov ecx, [first_dow]    ; Current column (0=Sun..6=Sat)

.day_loop:
        mov eax, [print_day]
        cmp eax, [days_in_month]
        jg .done

        ; Highlight today
        mov ebx, [print_day]
        cmp ebx, [cur_day]
        jne .not_today
        mov eax, SYS_SETCOLOR
        mov ebx, 0x4F           ; white on red (today)
        int 0x80
        jmp .print_num
.not_today:
        ; Sunday in different color
        test ecx, ecx
        jnz .normal_day
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; light red for Sunday
        int 0x80
        jmp .print_num
.normal_day:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

.print_num:
        ; Print day number right-aligned in 4 chars
        mov eax, [print_day]
        cmp eax, 10
        jge .two_digits
        ; Single digit: print 2 leading spaces + digit + space
        push eax
        mov eax, SYS_PRINT
        mov ebx, msg_cell_2sp
        int 0x80
        pop eax
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        jmp .day_printed

.two_digits:
        ; Two digits: print 1 leading space + digits + space
        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop eax
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

.day_printed:
        ; Reset color after today highlight
        push ecx
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        pop ecx

        inc ecx                 ; next column
        cmp ecx, 7
        jl .no_newline
        ; End of week, print newline
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        xor ecx, ecx
.no_newline:
        inc dword [print_day]
        jmp .day_loop

.done:
        ; Final newline
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80

        popad
        ret

;=== Leap year check ===
; Uses [.tmp_year]. Returns EAX=1 if leap, 0 if not.
is_leap_year:
        mov eax, [tmp_year]
        ; Divisible by 400 -> leap
        xor edx, edx
        mov ecx, 400
        div ecx
        test edx, edx
        jz .is_leap
        ; Divisible by 100 -> not leap
        mov eax, [tmp_year]
        xor edx, edx
        mov ecx, 100
        div ecx
        test edx, edx
        jz .not_leap
        ; Divisible by 4 -> leap
        mov eax, [tmp_year]
        xor edx, edx
        mov ecx, 4
        div ecx
        test edx, edx
        jz .is_leap
.not_leap:
        xor eax, eax
        ret
.is_leap:
        mov eax, 1
        ret

;=== Data ===
dow_table:      db 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4
days_table:     db 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31

month_names:
        db "January   "          ; 10 chars each
        db "February  "
        db "March     "
        db "April     "
        db "May       "
        db "June      "
        db "July      "
        db "August    "
        db "September "
        db "October   "
        db "November  "
        db "December  "

msg_dow_header: db " Sun Mon Tue Wed Thu Fri Sat", 0x0A, 0
msg_newline:    db 0x0A, 0
msg_cell_blank: db "    ", 0          ; 4-char blank cell
msg_cell_2sp:   db "  ", 0
msg_spaces4:    db "    ", 0
msg_usage:      db "Usage: cal [month] [year]", 0x0A, 0

;=== BSS ===
date_buf:       resb 8
arg_buf:        resb 128
cur_year:       resd 1
cur_month:      resd 1
cur_day:        resd 1
real_year:      resd 1
real_month:     resd 1
first_dow:      resd 1
days_in_month:  resd 1
print_day:      resd 1
tmp_year:       resd 1
