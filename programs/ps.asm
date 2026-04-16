; ps.asm - Process Status Listing
; Shows all active tasks from the Mellivora scheduler.
;
; Usage: ps

%include "syscalls.inc"

MAX_TASKS       equ 16

start:
        ; Print header
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; cyan
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, hdr_line
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07           ; grey
        int 0x80

        ; Iterate scheduler slots
        xor ebp, ebp            ; slot index
        xor edi, edi            ; active count

.loop:
        cmp ebp, MAX_TASKS
        jge .done

        mov eax, SYS_PROCLIST
        mov ebx, ebp
        mov ecx, task_info
        int 0x80

        cmp eax, -1
        je .next

        ; Check if slot is active (state != 0)
        cmp dword [task_info], 0        ; TASK_FREE
        je .next

        inc edi

        ; Print slot number
        mov eax, ebp
        call print_padded_num

        ; Print PID
        mov eax, [task_info + 4]
        call print_padded_num

        ; Print state
        mov eax, SYS_PRINT
        mov ebx, str_spaces
        int 0x80

        mov eax, [task_info]
        cmp eax, 1
        je .st_ready
        cmp eax, 2
        je .st_run
        mov ebx, st_other
        jmp .st_print
.st_ready:
        mov ebx, st_ready
        jmp .st_print
.st_run:
        mov ebx, st_running
.st_print:
        mov eax, SYS_PRINT
        int 0x80

        ; Print entry point address
        mov eax, SYS_PRINT
        mov ebx, str_spaces
        int 0x80

        mov eax, [task_info + 8]        ; entry point (low 32 bits of qword)
        call print_hex

        ; Print ESP
        mov eax, SYS_PRINT
        mov ebx, str_spaces
        int 0x80

        mov eax, [task_info + 16]       ; rsp (low 32 bits of qword)
        call print_hex

        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.next:
        inc ebp
        jmp .loop

.done:
        ; Summary
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, edi
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_tasks
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; print_padded_num - Print EAX as right-justified 4-wide decimal
;---------------------------------------
print_padded_num:
        PUSHALL
        ; Count digits
        mov ecx, 0
        push rax
        cmp eax, 0
        jne .ppn_count
        mov ecx, 1
        jmp .ppn_pad
.ppn_count:
        cmp eax, 0
        je .ppn_pad
        xor edx, edx
        mov ebx, 10
        div ebx
        inc ecx
        jmp .ppn_count
.ppn_pad:
        ; Pad to width 6
        mov edx, 6
        sub edx, ecx
        jle .ppn_print
.ppn_sp:
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rdx
        dec edx
        jg .ppn_sp
.ppn_print:
        pop rax
        call print_decimal
        POPALL
        ret

;---------------------------------------
; print_decimal
;---------------------------------------
print_decimal:
        PUSHALL
        cmp eax, 0
        jne .pd_nz
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        POPALL
        ret
.pd_nz:
        xor ecx, ecx
        mov ebx, 10
.pd_div:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        cmp eax, 0
        jne .pd_div
.pd_out:
        pop rbx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .pd_out
        POPALL
        ret

;---------------------------------------
; print_hex - Print EAX as 8-digit hex
;---------------------------------------
print_hex:
        PUSHALL
        mov ecx, 8
        mov edx, eax
.ph_loop:
        rol edx, 4
        mov eax, edx
        and eax, 0x0F
        cmp eax, 10
        jl .ph_digit
        add eax, 'A' - 10
        jmp .ph_out
.ph_digit:
        add eax, '0'
.ph_out:
        push rcx
        push rdx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rdx
        pop rcx
        dec ecx
        jnz .ph_loop
        POPALL
        ret

;=======================================
; Data
;=======================================
hdr_line:       db "  SLOT   PID  STATE      ENTRY     ESP", 10
                db "  ----   ---  -----      -----     ---", 10, 0
st_ready:       db "READY  ", 0
st_running:     db "RUNNING", 0
st_other:       db "OTHER  ", 0
str_spaces:     db "  ", 0
msg_tasks:      db " active task(s)", 10, 0

; BSS
task_info:      times 24 db 0
