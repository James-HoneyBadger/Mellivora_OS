; cron.asm - Simple cron job scheduler for Mellivora OS
;
; Reads "crontab" from the filesystem and executes commands at scheduled times.
; Run in the background: cron &
;
; Crontab format (one entry per line):
;   MM HH command [args]
;
; MM  = minute  (0-59 or * for every minute)
; HH  = hour    (0-23 or * for every hour)
;
; Examples:
;   0 * backup          - run backup at the top of every hour
;   30 12 hello         - run hello at 12:30
;   * * dmesg           - run dmesg every minute
;
; Lines starting with '#' are comments. Blank lines are ignored.

%include "syscalls.inc"

CRON_MAX_ENTRIES equ 32
CRON_ENTRY_SIZE  equ 128        ; bytes per entry: MM(1)+HH(1)+cmd(126)

; Cron entry offsets
CE_MIN          equ 0           ; byte: 0-59 or 0xFF for *
CE_HOUR         equ 1           ; byte: 0-23 or 0xFF for *
CE_CMD          equ 2           ; null-terminated command string (up to 126)

start:
        ; Name ourselves in the task list
        mov eax, SYS_TASKNAME
        mov ebx, task_name
        int 0x80

        ; Print startup banner
        mov eax, SYS_PRINT
        mov ebx, msg_start
        int 0x80

        ; Load crontab from disk
        call load_crontab
        cmp eax, 0
        je .no_crontab

        ; Print count of loaded entries
        mov eax, [entry_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_entries
        int 0x80

        jmp .main_loop

.no_crontab:
        mov eax, SYS_PRINT
        mov ebx, msg_no_crontab
        int 0x80

.main_loop:
        ; Get current time
        mov eax, SYS_GETTIME
        int 0x80
        ; AL = hours, AH = minutes (Mellivora SYS_GETTIME returns: AL=hours, AH=minutes)
        mov [cur_hour], al
        mov [cur_min], ah

        ; Check each cron entry
        mov ebp, 0              ; entry index
        mov esi, cron_table
.check_loop:
        cmp ebp, [entry_count]
        jge .check_done

        ; Load entry min/hour
        mov cl, [esi + CE_MIN]
        mov ch, [esi + CE_HOUR]

        ; Check minute match: 0xFF = wildcard
        cmp cl, 0xFF
        je .min_ok
        cmp cl, [cur_min]
        jne .next_entry
.min_ok:
        ; Check hour match: 0xFF = wildcard
        cmp ch, 0xFF
        je .hour_ok
        cmp ch, [cur_hour]
        jne .next_entry
.hour_ok:
        ; Time matches — check if already fired this minute
        ; Use last_min / last_hour tracking per entry
        push esi
        push ebp
        imul ebx, ebp, 2        ; 2 bytes per fired-flag entry
        mov al, [fired_table + ebx]         ; last min when fired
        mov ah, [fired_table + ebx + 1]     ; last hour when fired
        cmp al, [cur_min]
        jne .fire
        cmp ah, [cur_hour]
        je .skip_fire           ; Already fired this minute

.fire:
        ; Update fired timestamp
        mov al, [cur_min]
        mov ah, [cur_hour]
        mov [fired_table + ebx], al
        mov [fired_table + ebx + 1], ah

        ; Copy command to exec_buf and execute
        push esi
        mov esi, esi
        add esi, CE_CMD
        mov edi, exec_buf
        xor ecx, ecx
.copy_cmd:
        lodsb
        stosb
        test al, al
        jz .cmd_copied
        inc ecx
        cmp ecx, 127
        jb .copy_cmd
        mov byte [edi], 0
.cmd_copied:
        pop esi

        mov eax, SYS_EXEC
        mov ebx, exec_buf
        int 0x80

.skip_fire:
        pop ebp
        pop esi

.next_entry:
        add esi, CRON_ENTRY_SIZE
        inc ebp
        jmp .check_loop

.check_done:
        ; Sleep 30 seconds between checks
        mov eax, SYS_SLEEP
        mov ebx, 3000
        int 0x80
        jmp .main_loop

;=======================================================================
; load_crontab - Parse "crontab" file from disk
; Returns: EAX = 0 if file not found, 1 if loaded
;=======================================================================
load_crontab:
        pushad

        mov eax, SYS_FREAD
        mov ebx, crontab_name
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .not_found
        test eax, eax
        jz .not_found

        ; Parse lines
        mov dword [entry_count], 0
        mov esi, file_buf
        mov edi, cron_table

.parse_line:
        ; Skip blank lines and comments
        cmp byte [esi], 0
        je .parse_done
        cmp byte [esi], 10
        je .skip_line
        cmp byte [esi], 13
        je .skip_line
        cmp byte [esi], '#'
        je .skip_comment

        ; Check we haven't exceeded max entries
        cmp dword [entry_count], CRON_MAX_ENTRIES
        jge .parse_done

        ; Parse minute field
        call parse_field        ; EAX = value or -1 for *, ESI advanced
        cmp eax, -1
        je .field_wildcard_min
        cmp eax, 59
        jg .skip_comment        ; Out of range
        mov [edi + CE_MIN], al
        jmp .parse_hour
.field_wildcard_min:
        mov byte [edi + CE_MIN], 0xFF

.parse_hour:
        call skip_ws
        call parse_field
        cmp eax, -1
        je .field_wildcard_hour
        cmp eax, 23
        jg .skip_comment
        mov [edi + CE_HOUR], al
        jmp .parse_cmd
.field_wildcard_hour:
        mov byte [edi + CE_HOUR], 0xFF

.parse_cmd:
        call skip_ws
        ; Copy rest of line as command (strip newline)
        push edi
        add edi, CE_CMD
        xor ecx, ecx
.copy_line:
        mov al, [esi]
        cmp al, 0
        je .line_end
        cmp al, 10
        je .line_end
        cmp al, 13
        je .line_end
        cmp ecx, 125
        jge .line_end
        stosb
        inc ecx
        inc esi
        jmp .copy_line
.line_end:
        mov byte [edi], 0       ; null terminate
        pop edi

        ; Advance to next entry
        add edi, CRON_ENTRY_SIZE
        inc dword [entry_count]

.skip_line:
        ; Advance ESI past current line
        cmp byte [esi], 0
        je .parse_done
        mov al, [esi]
        inc esi
        cmp al, 10
        jne .skip_line
        jmp .parse_line

.skip_comment:
        ; Skip to end of line
        cmp byte [esi], 0
        je .parse_done
        mov al, [esi]
        inc esi
        cmp al, 10
        jne .skip_comment
        jmp .parse_line

.parse_done:
        ; Zero the fired table
        mov edi, fired_table
        xor eax, eax
        mov ecx, CRON_MAX_ENTRIES * 2 / 4 + 1
        rep stosd

        popad
        mov eax, 1
        ret

.not_found:
        popad
        xor eax, eax
        ret

;---------------------------------------
; parse_field - Parse a field (number or *)
; ESI = current position in input
; Returns: EAX = numeric value, or -1 for '*'
;          ESI advanced past token
;---------------------------------------
parse_field:
        cmp byte [esi], '*'
        jne .pf_num
        inc esi
        mov eax, -1
        ret
.pf_num:
        xor eax, eax
        xor ecx, ecx
.pf_digit:
        mov cl, [esi]
        cmp cl, '0'
        jb .pf_done
        cmp cl, '9'
        ja .pf_done
        imul eax, 10
        sub cl, '0'
        movzx ecx, cl
        add eax, ecx
        inc esi
        jmp .pf_digit
.pf_done:
        ret

;---------------------------------------
; skip_ws - Advance ESI past spaces/tabs
;---------------------------------------
skip_ws:
        mov al, [esi]
        cmp al, ' '
        je .sw_skip
        cmp al, 9
        je .sw_skip
        ret
.sw_skip:
        inc esi
        jmp skip_ws


;=======================================================================
; DATA
;=======================================================================
task_name:      db "cron", 0
crontab_name:   db "crontab", 0

msg_start:      db "cron: daemon started, reading crontab...", 10, 0
msg_entries:    db " job(s) loaded.", 10, 0
msg_no_crontab: db "cron: no crontab file found. Create 'crontab' to add jobs.", 10, 0

; Cron table: CRON_MAX_ENTRIES * CRON_ENTRY_SIZE bytes
cron_table:     times (CRON_MAX_ENTRIES * CRON_ENTRY_SIZE) db 0

; Per-entry last-fired tracking: [minute, hour] pairs
fired_table:    times (CRON_MAX_ENTRIES * 2) db 0xFF  ; 0xFF = never fired

; Variables
entry_count:    dd 0
cur_hour:       db 0
cur_min:        db 0

; Buffers
file_buf:       times 4096 db 0
exec_buf:       times 128 db 0
