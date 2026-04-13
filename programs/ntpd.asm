; ntpd.asm - NTP Time Synchronization Client
; Queries an NTP server and sets the system RTC.
;
; Usage: ntpd [server_ip]
;   Default server: pool.ntp.org (resolved via DNS)
;   If IP given as a.b.c.d, connects directly.
;
; NTP uses UDP port 123. We send a version 3 client request
; and parse the transmit timestamp from the 48-byte response.

%include "syscalls.inc"
%include "lib/net.inc"

NTP_PORT        equ 123
NTP_PACKET_SIZE equ 48
NTP_TIMEOUT     equ 500         ; 5 seconds at 100Hz

; NTP epoch: 1900-01-01, Unix epoch: 1970-01-01
; Difference: 70 years = 2208988800 seconds
NTP_UNIX_DELTA  equ 2208988800

start:
        ; Print banner
        mov eax, SYS_PRINT
        mov ebx, msg_banner
        int 0x80

        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 128
        int 0x80

        ; Check if argument provided
        cmp byte [arg_buf], 0
        je .use_dns

        ; Try to parse as IP address
        mov esi, arg_buf
        call parse_ip
        cmp eax, 0
        je .use_dns
        mov [server_ip], eax
        jmp .have_ip

.use_dns:
        ; Resolve pool.ntp.org
        mov eax, SYS_PRINT
        mov ebx, msg_resolving
        int 0x80

        mov eax, SYS_DNS
        mov ebx, ntp_hostname
        int 0x80
        cmp eax, 0
        je .dns_fail
        mov [server_ip], eax

.have_ip:
        ; Print server IP
        mov eax, SYS_PRINT
        mov ebx, msg_query
        int 0x80
        mov eax, [server_ip]
        call print_ip
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Create UDP socket
        mov eax, NET_UDP
        call net_socket
        cmp eax, -1
        je .sock_fail
        mov [sockfd], eax

        ; Connect to NTP server
        mov eax, [sockfd]
        mov ebx, [server_ip]
        mov ecx, NTP_PORT
        call net_connect
        cmp eax, -1
        je .connect_fail

        ; Build NTP request packet
        call build_ntp_request

        ; Send request
        mov eax, [sockfd]
        mov ebx, ntp_packet
        mov ecx, NTP_PACKET_SIZE
        call net_send
        cmp eax, -1
        je .send_fail

        ; Wait for response
        mov eax, SYS_PRINT
        mov ebx, msg_waiting
        int 0x80

        mov dword [retry_count], 0
.recv_loop:
        mov eax, [sockfd]
        mov ebx, recv_buf
        mov ecx, 256
        call net_recv
        cmp eax, 0
        jg .got_response

        ; Sleep and retry
        mov eax, SYS_SLEEP
        mov ebx, 10            ; 100ms
        int 0x80

        inc dword [retry_count]
        cmp dword [retry_count], NTP_TIMEOUT / 10
        jl .recv_loop

        ; Timeout
        mov eax, SYS_PRINT
        mov ebx, msg_timeout
        int 0x80
        jmp .cleanup

.got_response:
        cmp eax, NTP_PACKET_SIZE
        jl .bad_response

        ; Parse NTP response
        call parse_ntp_response
        cmp eax, 0
        je .bad_response

        ; Convert NTP timestamp to date/time
        mov eax, [ntp_seconds]
        call ntp_to_datetime

        ; Print the time
        mov eax, SYS_PRINT
        mov ebx, msg_time
        int 0x80
        call print_datetime

        ; Set the RTC
        mov eax, SYS_SETDATE
        mov ebx, date_buf
        movzx ecx, byte [century]
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_set_ok
        int 0x80
        jmp .cleanup

.bad_response:
        mov eax, SYS_PRINT
        mov ebx, msg_bad_resp
        int 0x80

.cleanup:
        mov eax, [sockfd]
        call net_close

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns_fail
        int 0x80
        jmp .exit

.sock_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_sock_fail
        int 0x80
        jmp .exit

.connect_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_conn_fail
        int 0x80
        jmp .cleanup

.send_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_send_fail
        int 0x80
        jmp .cleanup

;---------------------------------------
; build_ntp_request - Build a 48-byte NTP client request
; First byte: LI=0, VN=3, Mode=3 (client) = 0x1B
; Rest: zeros
;---------------------------------------
build_ntp_request:
        pushad
        mov edi, ntp_packet
        mov ecx, NTP_PACKET_SIZE / 4
        xor eax, eax
        rep stosd
        mov byte [ntp_packet], 0x1B     ; LI=0, VN=3, Mode=3
        popad
        ret

;---------------------------------------
; parse_ntp_response - Parse 48-byte NTP response
; Extracts transmit timestamp (bytes 40-43 = seconds since 1900)
; Returns: EAX=1 on success, 0 on failure
;---------------------------------------
parse_ntp_response:
        ; Check stratum (byte 1) - 0 = kiss-of-death, 16 = unsync
        movzx eax, byte [recv_buf + 1]
        cmp eax, 0
        je .pnr_fail
        cmp eax, 16
        jge .pnr_fail

        ; Transmit timestamp is at offset 40 (big-endian seconds)
        mov eax, [recv_buf + 40]
        bswap eax               ; big-endian to little-endian
        mov [ntp_seconds], eax

        ; Sanity check: timestamp should be > 2025 epoch
        ; 2025-01-01 = 3944678400 NTP seconds
        cmp eax, 3944678400
        jl .pnr_fail

        mov eax, 1
        ret
.pnr_fail:
        xor eax, eax
        ret

;---------------------------------------
; ntp_to_datetime - Convert NTP seconds to date/time
; Input: EAX = NTP seconds since 1900-01-01
; Fills date_buf[sec, min, hour, day, month, year2] and century
;---------------------------------------
ntp_to_datetime:
        pushad

        ; Convert to Unix epoch
        sub eax, NTP_UNIX_DELTA
        mov [.unix_ts], eax

        ; Extract time of day
        xor edx, edx
        mov ecx, 86400          ; seconds per day
        div ecx
        ; EAX = days since 1970-01-01, EDX = seconds in day
        mov [.days], eax
        mov [.secs], edx

        ; Parse seconds into H:M:S
        mov eax, edx
        xor edx, edx
        mov ecx, 3600
        div ecx
        mov [.hour], al

        mov eax, edx
        xor edx, edx
        mov ecx, 60
        div ecx
        mov [.min], al
        mov [.sec], dl

        ; Parse days into Y/M/D (simplified Gregorian)
        mov eax, [.days]
        ; Start from 1970-01-01
        mov dword [.year], 1970
        mov dword [.month], 1
        mov dword [.day], 1

.year_loop:
        ; Days in this year
        mov ecx, 365
        push eax
        mov eax, [.year]
        call .is_leap
        pop eax
        cmp edx, 1
        jne .not_leap_y
        inc ecx
.not_leap_y:
        cmp eax, ecx
        jl .month_loop
        sub eax, ecx
        inc dword [.year]
        jmp .year_loop

.month_loop:
        ; Days in current month
        push eax
        mov ecx, [.month]
        dec ecx
        movzx ecx, byte [.month_days + ecx]
        ; Feb special handling for leap
        cmp dword [.month], 2
        jne .not_feb
        push ecx
        mov eax, [.year]
        call .is_leap
        pop ecx
        cmp edx, 1
        jne .not_feb
        inc ecx
.not_feb:
        mov edx, ecx           ; days in this month
        pop eax
        cmp eax, edx
        jl .done_date
        sub eax, edx
        inc dword [.month]
        cmp dword [.month], 13
        jl .month_loop
        ; Should not happen
        jmp .done_date

.done_date:
        ; EAX = remaining days (0-based day of month)
        inc eax
        mov [.day], eax

        ; Fill date_buf
        mov al, [.sec]
        mov [date_buf], al
        mov al, [.min]
        mov [date_buf + 1], al
        mov al, [.hour]
        mov [date_buf + 2], al
        mov eax, [.day]
        mov [date_buf + 3], al
        mov eax, [.month]
        mov [date_buf + 4], al
        mov eax, [.year]
        xor edx, edx
        mov ecx, 100
        div ecx
        ; EAX = century, EDX = year within century
        mov [century], al
        mov [date_buf + 5], dl

        popad
        ret

; is_leap: EAX=year, returns EDX=1 if leap
.is_leap:
        push eax
        push ecx
        xor edx, edx
        mov ecx, eax
        push eax
        xor edx, edx
        mov eax, ecx
        push ecx
        mov ecx, 4
        div ecx
        pop ecx
        pop eax
        cmp edx, 0
        jne .not_leap
        ; Divisible by 4 - check 100
        push eax
        xor edx, edx
        mov eax, ecx
        push ecx
        mov ecx, 100
        div ecx
        pop ecx
        pop eax
        cmp edx, 0
        jne .is_leap_yes
        ; Divisible by 100 - check 400
        push eax
        xor edx, edx
        mov eax, ecx
        push ecx
        mov ecx, 400
        div ecx
        pop ecx
        pop eax
        cmp edx, 0
        jne .not_leap
.is_leap_yes:
        pop ecx
        pop eax
        mov edx, 1
        ret
.not_leap:
        pop ecx
        pop eax
        xor edx, edx
        ret

.month_days: db 31,28,31,30,31,30,31,31,30,31,30,31
.unix_ts: dd 0
.days:   dd 0
.secs:   dd 0
.hour:   db 0
.min:    db 0
.sec:    db 0
.year:   dd 0
.month:  dd 0
.day:    dd 0

;---------------------------------------
; parse_ip - Parse dotted-decimal IP from ESI
; Returns: EAX = IP (network byte order), 0 on failure
;---------------------------------------
parse_ip:
        pushad
        xor edi, edi            ; result IP
        xor ecx, ecx           ; octet count
.pi_octet:
        xor eax, eax           ; current value
.pi_digit:
        movzx edx, byte [esi]
        cmp dl, '0'
        jl .pi_sep
        cmp dl, '9'
        jg .pi_sep
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pi_digit
.pi_sep:
        cmp eax, 255
        ja .pi_fail
        shl edi, 8
        or edi, eax
        inc ecx
        cmp dl, '.'
        jne .pi_check
        inc esi
        jmp .pi_octet
.pi_check:
        cmp ecx, 4
        jne .pi_fail
        ; Convert to little-endian for our stack
        bswap edi
        mov [esp + 28], edi     ; return in EAX
        popad
        ret
.pi_fail:
        mov dword [esp + 28], 0
        popad
        ret

;---------------------------------------
; print_ip - Print IP address in EAX
;---------------------------------------
print_ip:
        pushad
        mov edi, eax
        ; Byte 0 (lowest)
        movzx eax, byte [esp + 28]      ; byte 0 from original EAX
        ; Actually let's just pull from edi
        mov eax, edi
        and eax, 0xFF
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        mov eax, edi
        shr eax, 8
        and eax, 0xFF
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        mov eax, edi
        shr eax, 16
        and eax, 0xFF
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
        mov eax, edi
        shr eax, 24
        and eax, 0xFF
        call print_decimal
        popad
        ret

;---------------------------------------
; print_decimal - Print EAX as decimal
;---------------------------------------
print_decimal:
        pushad
        cmp eax, 0
        jne .pd_nonzero
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        popad
        ret
.pd_nonzero:
        mov ecx, 0             ; digit count
        mov ebx, 10
.pd_div:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        cmp eax, 0
        jne .pd_div
.pd_print:
        pop ebx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .pd_print
        popad
        ret

;---------------------------------------
; print_datetime - Print date and time
;---------------------------------------
print_datetime:
        pushad
        ; Year
        movzx eax, byte [century]
        imul eax, 100
        movzx edx, byte [date_buf + 5]
        add eax, edx
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        ; Month
        movzx eax, byte [date_buf + 4]
        call print_2digit
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        ; Day
        movzx eax, byte [date_buf + 3]
        call print_2digit
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        ; Hour
        movzx eax, byte [date_buf + 2]
        call print_2digit
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
        ; Minute
        movzx eax, byte [date_buf + 1]
        call print_2digit
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
        ; Second
        movzx eax, byte [date_buf]
        call print_2digit
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        popad
        ret

; Print EAX as 2-digit zero-padded
print_2digit:
        pushad
        cmp eax, 10
        jge .p2_two
        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        pop eax
        call print_decimal
        popad
        ret
.p2_two:
        call print_decimal
        popad
        ret

;=======================================
; Data
;=======================================
msg_banner:     db "Mellivora NTP Client v1.0", 10, 0
msg_resolving:  db "Resolving pool.ntp.org...", 10, 0
msg_query:      db "Querying NTP server: ", 0
msg_waiting:    db "Waiting for response...", 10, 0
msg_timeout:    db "Error: NTP server timeout", 10, 0
msg_bad_resp:   db "Error: Invalid NTP response", 10, 0
msg_time:       db "Server time: ", 0
msg_set_ok:     db "System clock updated successfully.", 10, 0
msg_dns_fail:   db "Error: DNS resolution failed", 10, 0
msg_sock_fail:  db "Error: Could not create socket", 10, 0
msg_conn_fail:  db "Error: Could not connect", 10, 0
msg_send_fail:  db "Error: Could not send packet", 10, 0

ntp_hostname:   db "pool.ntp.org", 0

; BSS
arg_buf:        times 128 db 0
server_ip:      dd 0
sockfd:         dd 0
retry_count:    dd 0
ntp_packet:     times 48 db 0
recv_buf:       times 256 db 0
ntp_seconds:    dd 0
date_buf:       times 6 db 0
century:        db 0
