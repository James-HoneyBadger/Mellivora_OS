; ==========================================================================
; syslog - System logging utility for Mellivora OS
; Usage: syslog                  Display the system log
;        syslog -w <message>     Write timestamped message to log
;        syslog -c               Clear the system log
;        syslog -f               Follow log (tail, press Q/ESC to quit)
; Log file: /var/log/syslog
; ==========================================================================

%include "syscalls.inc"

MAX_FILE    equ 60000

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        cmp byte [arg_buf], 0
        je .display

        cmp byte [arg_buf], '-'
        jne .display
        movzx eax, byte [arg_buf + 1]
        cmp al, 'w'
        je .write
        cmp al, 'c'
        je .clear
        cmp al, 'f'
        je .follow
        jmp .usage

; ---- Display log ----
.display:
        mov eax, SYS_FREAD
        mov ebx, log_path
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jle .empty_log

        mov byte [file_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, file_buf
        int 0x80
        jmp .exit

; ---- Write message ----
.write:
        ; Ensure /var and /var/log exist
        mov eax, SYS_MKDIR
        mov ebx, var_dir
        int 0x80
        mov eax, SYS_MKDIR
        mov ebx, log_dir
        int 0x80

        ; Find message text after "-w "
        mov esi, arg_buf
        add esi, 2
.skip_ws:
        cmp byte [esi], ' '
        jne .got_msg
        inc esi
        jmp .skip_ws
.got_msg:
        cmp byte [esi], 0
        je .usage

        ; Get current date/time via SYS_DATE
        mov eax, SYS_DATE
        mov ebx, date_buf
        int 0x80

        ; Format: [YYYY-MM-DD HH:MM:SS] message\n
        mov edi, line_buf
        mov byte [edi], '['
        inc edi

        ; Year: date_buf[0]=century BCD, date_buf[1]=year BCD
        movzx eax, byte [date_buf]
        call write_bcd
        movzx eax, byte [date_buf + 1]
        call write_bcd
        mov byte [edi], '-'
        inc edi
        ; Month
        movzx eax, byte [date_buf + 2]
        call write_bcd
        mov byte [edi], '-'
        inc edi
        ; Day
        movzx eax, byte [date_buf + 3]
        call write_bcd
        mov byte [edi], ' '
        inc edi
        ; Hour
        movzx eax, byte [date_buf + 5]
        call write_bcd
        mov byte [edi], ':'
        inc edi
        ; Minute
        movzx eax, byte [date_buf + 6]
        call write_bcd
        mov byte [edi], ':'
        inc edi
        ; Second
        movzx eax, byte [date_buf + 7]
        call write_bcd
        mov byte [edi], ']'
        inc edi
        mov byte [edi], ' '
        inc edi

        ; Copy message text
.copy_msg:
        lodsb
        test al, al
        jz .msg_done
        stosb
        jmp .copy_msg
.msg_done:
        mov byte [edi], 10      ; newline
        inc edi
        mov byte [edi], 0

        ; Calculate line length
        mov eax, edi
        sub eax, line_buf
        mov [line_len], eax

        ; Read existing log (if any)
        mov eax, SYS_FREAD
        mov ebx, log_path
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jg .append
        xor eax, eax
.append:
        mov [file_len], eax

        ; Append new line to file_buf
        mov esi, line_buf
        mov edi, file_buf
        add edi, [file_len]
        mov ecx, [line_len]
        rep movsb

        ; Write back
        mov eax, SYS_FWRITE
        mov ebx, log_path
        mov ecx, file_buf
        mov edx, [file_len]
        add edx, [line_len]
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_logged
        int 0x80
        jmp .exit

; ---- Clear log ----
.clear:
        mov eax, SYS_DELETE
        mov ebx, log_path
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_cleared
        int 0x80
        jmp .exit

; ---- Follow log ----
.follow:
        ; Read and display current log
        mov eax, SYS_FREAD
        mov ebx, log_path
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jle .follow_init_empty
        mov [file_len], eax
        mov byte [file_buf + eax], 0
        push rax
        mov eax, SYS_PRINT
        mov ebx, file_buf
        int 0x80
        pop rax
        jmp .follow_loop
.follow_init_empty:
        mov dword [file_len], 0

.follow_loop:
        mov eax, SYS_SLEEP
        mov ebx, 100            ; ~1 second
        int 0x80

        ; Check for quit key
        mov eax, SYS_READ_KEY
        int 0x80
        cmp al, 'q'
        je .exit
        cmp al, 'Q'
        je .exit
        cmp al, 27
        je .exit

        ; Re-read log and check for new content
        mov eax, SYS_FREAD
        mov ebx, log_path
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jle .follow_loop
        cmp eax, [file_len]
        jle .follow_loop

        ; Print only new bytes
        mov ebx, file_buf
        add ebx, [file_len]
        mov [file_len], eax
        mov byte [file_buf + eax], 0
        mov eax, SYS_PRINT
        int 0x80
        jmp .follow_loop

; ---- Empty log message ----
.empty_log:
        mov eax, SYS_PRINT
        mov ebx, msg_empty
        int 0x80
        jmp .exit

; ---- Usage ----
.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ---- Write BCD byte in AL as two ASCII digits to [EDI] ----
; BCD: high nibble = tens, low nibble = ones
write_bcd:
        push rax
        mov ecx, eax
        shr ecx, 4
        and ecx, 0x0F
        add cl, '0'
        mov [edi], cl
        inc edi
        and eax, 0x0F
        add al, '0'
        mov [edi], al
        inc edi
        pop rax
        ret

; ---- Data ----
var_dir:    db '/var', 0
log_dir:    db '/var/log', 0
log_path:   db '/var/log/syslog', 0
msg_logged: db 'Message logged.', 10, 0
msg_cleared:db 'System log cleared.', 10, 0
msg_empty:  db 'System log is empty.', 10, 0
msg_usage:  db 'Usage: syslog [-w message | -c | -f]', 10
            db '  (no args)  Display system log', 10
            db '  -w msg     Write timestamped entry', 10
            db '  -c         Clear log', 10
            db '  -f         Follow log (Q to quit)', 10, 0

; ---- BSS ----
date_buf:   times 8 db 0
arg_buf:    times 256 db 0
line_buf:   times 512 db 0
line_len:   dd 0
file_len:   dd 0
file_buf:   times 61440 db 0
