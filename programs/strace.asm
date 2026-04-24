; strace.asm - Syscall trace wrapper for Mellivora OS
; Usage: strace <program> [args]
; Records the dmesg log before running the program, then after it exits
; shows all new kernel log entries (which include syscall traces if the
; kernel was built with SYS_DMESG_WRITE instrumentation), effectively
; tracing activity logged during the program's run.
;
; strace itself writes a SYS_DMESG_WRITE "strace: start" marker so the
; starting position in the log is unambiguous.

%include "syscalls.inc"

DMESG_LINE  equ 128
ARG_BUF_MAX equ 512

start:
        ; Get command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        mov [arg_len], eax

        ; Require at least one argument (the program name)
        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; ----- Count current dmesg entries so we know where to start -----
        xor ebp, ebp                ; EBP = number of existing entries
.count_loop:
        mov eax, SYS_DMESG_READ
        mov ebx, ebp
        mov ecx, dmesg_line_buf
        int 0x80
        cmp eax, -1
        je .count_done
        inc ebp
        jmp .count_loop
.count_done:
        mov [pre_count], ebp        ; save log depth before program runs

        ; ----- Write start marker into dmesg -----
        mov eax, SYS_DMESG_WRITE
        mov ebx, msg_start_marker
        int 0x80

        ; ----- Print header -----
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B               ; cyan
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_hdr
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi                ; program name (still in ESI from skip_spaces)
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; ----- Build exec command string: program + remaining args -----
        ; ESI = pointer into arg_buf at the program name
        mov edi, exec_cmd_buf
.build_cmd:
        lodsb
        cmp al, 0
        je .cmd_done
        stosb
        jmp .build_cmd
.cmd_done:
        mov byte [edi], 0

        ; ----- Execute the target program -----
        mov eax, SYS_EXEC
        mov ebx, exec_cmd_buf
        int 0x80
        ; SYS_EXEC returns here after the child completes (synchronous in Mellivora)
        mov [exec_result], eax

        ; ----- Print exit status -----
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E               ; yellow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_exit
        int 0x80
        mov eax, [exec_result]
        mov ebx, num_buf
        call int_to_dec
        mov eax, SYS_PRINT
        mov ebx, num_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; ----- Separator -----
        mov eax, SYS_PRINT
        mov ebx, msg_sep
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; ----- Dump new dmesg entries added during the run -----
        ; Start from [pre_count] (the marker entry itself)
        mov ebp, [pre_count]
.dump_loop:
        mov eax, SYS_DMESG_READ
        mov ebx, ebp
        mov ecx, dmesg_line_buf
        int 0x80
        cmp eax, -1
        je .dump_done
        ; Print entry index
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08               ; dark grey
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_idx_open
        int 0x80
        mov eax, ebp
        sub eax, [pre_count]        ; relative index
        mov ebx, num_buf
        call int_to_dec
        mov eax, SYS_PRINT
        mov ebx, num_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_idx_close
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07               ; white
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, dmesg_line_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        inc ebp
        jmp .dump_loop

.dump_done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;---------------------------------------
; int_to_dec - convert EAX to null-terminated decimal string at [EBX]
; Clobbers: EAX, ECX, EDX, EDI
;---------------------------------------
int_to_dec:
        mov edi, ebx
        test eax, eax
        jns .pos
        mov byte [edi], '-'
        inc edi
        neg eax
.pos:
        ; Write digits in reverse, then reverse the string
        push edi                ; save start
        mov ecx, 0              ; digit count
.digit_loop:
        xor edx, edx
        mov ecx, 10
        div ecx
        add dl, '0'
        push edx
        mov ecx, [esp+4]        ; can't use push edi trick; use counter
        inc ecx
        mov [esp+4], ecx
        test eax, eax
        jnz .digit_loop
        ; Pop digits (they come out in correct order via stack)
        mov ecx, [esp+4]
.pop_digits:
        pop eax
        mov [edi], al
        inc edi
        dec ecx
        jnz .pop_digits
        pop ecx                 ; discard saved start
        mov byte [edi], 0
        ret

;---------------------------------------
; skip_spaces - advance ESI past space characters
;---------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

; -----------------------------------------------------------------------
; Data section
; -----------------------------------------------------------------------

msg_usage:      db "Usage: strace <program> [args]", 0x0A, 0
msg_hdr:        db "strace: tracing ", 0
msg_start_marker: db "strace: --- start ---", 0
msg_exit:       db "strace: exit status = ", 0
msg_sep:        db "--- dmesg trace ---", 0x0A, 0
msg_idx_open:   db "[", 0
msg_idx_close:  db "] ", 0

; -----------------------------------------------------------------------
; BSS / uninitialised variables
; -----------------------------------------------------------------------

pre_count:      resd 1          ; dmesg entry count before program ran
exec_result:    resd 1          ; return value from SYS_EXEC
arg_len:        resd 1

arg_buf:        resb ARG_BUF_MAX
exec_cmd_buf:   resb ARG_BUF_MAX
dmesg_line_buf: resb DMESG_LINE
num_buf:        resb 16
