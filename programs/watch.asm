; ==========================================================================
; watch - Repeatedly display file contents at intervals
;
; Usage: watch <filename>          Refresh every 2 seconds (default)
;        watch -n <secs> <file>    Refresh every <secs> seconds
;
; Repeatedly reads and displays a file, clearing the screen between
; updates. Useful for monitoring log files or changing data.
; Press any key to stop.
; ==========================================================================
%include "syscalls.inc"

DEFAULT_INTERVAL equ 2

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        mov dword [interval], DEFAULT_INTERVAL

        ; Check for -n flag
        cmp word [esi], '-n'
        jne .no_flag
        cmp byte [esi + 2], ' '
        jne .no_flag
        add esi, 3
        call skip_sp
        ; Parse interval number
        call parse_num
        test eax, eax
        jz .usage
        mov [interval], eax
        call skip_sp
.no_flag:
        ; Copy filename
        mov edi, filename
        call copy_token
        cmp byte [filename], 0
        je .usage

.loop:
        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Print header: "Every Ns: <filename>     <date>"
        mov eax, SYS_PRINT
        mov ebx, msg_every
        int 0x80
        mov eax, [interval]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_sec
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, filename
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; Read and display the file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .read_err
        mov [file_len], eax

        ; Null-terminate
        mov ecx, eax
        mov byte [file_buf + ecx], 0

        ; Print file contents
        mov eax, SYS_PRINT
        mov ebx, file_buf
        int 0x80

        ; Sleep for interval seconds (SYS_SLEEP takes ticks: 100 ticks = 1 sec)
        mov eax, [interval]
        imul eax, 100
        mov [sleep_ticks], eax

        ; Check for keypress during sleep (poll in small intervals)
        mov dword [slept], 0
.sleep_loop:
        mov eax, SYS_SLEEP
        mov ebx, 10             ; 100ms
        int 0x80

        ; Check for keypress (non-blocking via SYS_READ_KEY with short peek)
        mov eax, SYS_STDIN_READ
        mov ebx, key_buf
        int 0x80
        cmp eax, 0
        jg .quit                ; Key pressed - exit

        add dword [slept], 10
        mov eax, [slept]
        cmp eax, [sleep_ticks]
        jl .sleep_loop

        jmp .loop

.read_err:
        mov eax, SYS_PRINT
        mov ebx, msg_read_err
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, filename
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        ; Fall through to quit

.quit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -------------------------------------------------------------------
; Helpers
; -------------------------------------------------------------------

; parse_num - Parse decimal number from ESI, return in EAX, advance ESI
parse_num:
        xor eax, eax
        xor ecx, ecx
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

; skip_sp - Skip spaces at ESI
skip_sp:
        cmp byte [esi], ' '
        jne .sp_done
        inc esi
        jmp skip_sp
.sp_done:
        ret

; copy_token - Copy from ESI to EDI until space or null
copy_token:
        lodsb
        cmp al, ' '
        je .ct_done
        test al, al
        jz .ct_done
        stosb
        jmp copy_token
.ct_done:
        mov byte [edi], 0
        ret

; -------------------------------------------------------------------
; Data
; -------------------------------------------------------------------
msg_usage:      db "Usage: watch [-n secs] <filename>", 0x0A
                db "Repeatedly display file contents. Press any key to stop.", 0x0A, 0
msg_every:      db "Every ", 0
msg_sec:        db "s: ", 0
msg_read_err:   db "Error reading: ", 0

; BSS
interval:       dd 0
file_len:       dd 0
sleep_ticks:    dd 0
slept:          dd 0
filename:       times 256 db 0
arg_buf:        times 256 db 0
key_buf:        times 4 db 0
file_buf:       times 8192 db 0
