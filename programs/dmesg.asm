; dmesg.asm - Print kernel ring buffer messages
; Usage: dmesg [-c] [-n N]
;   dmesg           print all kernel log entries
;   dmesg -n N      print last N entries
;   dmesg -c        clear the kernel log (not implemented yet)

%include "syscalls.inc"

DMESG_LINE_SIZE equ 128

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        mov esi, arg_buf
        call skip_spaces

        mov dword [limit], 0    ; 0 = print all
        mov dword [start_idx], 0

        ; Parse optional -n N flag
        cmp byte [esi], '-'
        jne .count_entries

        inc esi
        cmp byte [esi], 'n'
        jne .unknown_flag

        inc esi
        call skip_spaces

        ; Parse N
        xor ecx, ecx
.parse_n:
        mov al, [esi]
        cmp al, '0'
        jb .n_done
        cmp al, '9'
        ja .n_done
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_n
.n_done:
        mov [limit], ecx
        jmp .count_entries

.unknown_flag:
        ; Unknown flag - just print all
        jmp .count_entries

.count_entries:
        ; Count how many entries are in the log
        ; We do this by binary searching: try index until we get -1
        xor ebp, ebp            ; entry counter
.count_loop:
        mov eax, SYS_DMESG_READ
        mov ebx, ebp
        mov ecx, line_buf
        int 0x80
        cmp eax, -1
        je .count_done
        inc ebp
        jmp .count_loop
.count_done:
        ; ebp = total entries available
        test ebp, ebp
        jz .nothing

        ; Determine starting index based on limit
        mov eax, [limit]
        test eax, eax
        jz .print_from_zero

        ; start from max(0, count - limit)
        cmp ebp, eax
        jle .print_from_zero
        mov ecx, ebp
        sub ecx, eax
        mov [start_idx], ecx
        jmp .print_entries

.print_from_zero:
        mov dword [start_idx], 0

.print_entries:
        mov esi, [start_idx]
.print_loop:
        cmp esi, ebp
        jge .done

        mov eax, SYS_DMESG_READ
        mov ebx, esi
        mov ecx, line_buf
        int 0x80
        cmp eax, -1
        je .done

        ; Print "[N] " prefix
        push esi
        mov eax, esi
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_sep
        int 0x80
        pop esi

        ; Print message
        mov eax, SYS_PRINT
        mov ebx, line_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        inc esi
        jmp .print_loop

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.nothing:
        mov eax, SYS_PRINT
        mov ebx, msg_empty
        int 0x80
        jmp .done

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces


msg_sep:        db "] ", 0
msg_empty:      db "(dmesg: log is empty)", 10, 0
arg_buf:        times 256 db 0
line_buf:       times DMESG_LINE_SIZE db 0
start_idx:      dd 0
limit:          dd 0
