; free.asm - Display memory usage
; Uses SYS_MEMINFO to query the physical memory manager.

%include "syscalls.inc"

start:
        ; Get memory info
        mov eax, SYS_MEMINFO
        int 0x80
        ; EAX = current free pages, EBX = initial free pages at boot
        mov [cur_free], eax
        mov [boot_free], ebx

        ; Calculate used = boot_free - cur_free
        mov ecx, ebx
        sub ecx, eax
        mov [used], ecx

        ; Convert pages to KB (each page = 4KB)
        mov eax, [boot_free]
        shl eax, 2
        mov [total_kb], eax

        mov eax, [used]
        shl eax, 2
        mov [used_kb], eax

        mov eax, [cur_free]
        shl eax, 2
        mov [free_kb], eax

        ; Print header
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, hdr
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Print values
        mov eax, SYS_PRINT
        mov ebx, lbl_mem
        int 0x80

        mov eax, [total_kb]
        call print_rj8
        mov eax, [used_kb]
        call print_rj8
        mov eax, [free_kb]
        call print_rj8

        ; Calculate percentage used
        mov eax, [used]
        imul eax, 100
        xor edx, edx
        mov ecx, [boot_free]
        cmp ecx, 0
        je .skip_pct
        div ecx
.skip_pct:
        call print_rj6pct

        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Print page counts
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_pages
        int 0x80
        mov eax, [cur_free]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_of
        int 0x80
        mov eax, [boot_free]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_free
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; print_rj8 - Print EAX KB value right-justified in 10 chars
;---------------------------------------
print_rj8:
        PUSHALL
        push rax
        ; Convert KB to appropriate unit
        cmp eax, 1048576
        jge .pr_gb
        cmp eax, 1024
        jge .pr_mb
        ; KB
        pop rax
        call count_digits
        add ecx, 3             ; " KB"
        mov edx, 10
        sub edx, ecx
        call print_spaces
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, unit_kb
        int 0x80
        POPALL
        ret
.pr_mb:
        pop rax
        shr eax, 10
        push rax
        call count_digits
        add ecx, 3
        mov edx, 10
        sub edx, ecx
        call print_spaces
        pop rax
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, unit_mb
        int 0x80
        POPALL
        ret
.pr_gb:
        pop rax
        shr eax, 20
        push rax
        call count_digits
        add ecx, 3
        mov edx, 10
        sub edx, ecx
        call print_spaces
        pop rax
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, unit_gb
        int 0x80
        POPALL
        ret

;---------------------------------------
; print_rj6pct - Print EAX as 6-wide right-justified percentage
;---------------------------------------
print_rj6pct:
        PUSHALL
        push rax
        call count_digits
        inc ecx             ; %
        mov edx, 8
        sub edx, ecx
        call print_spaces
        pop rax
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, '%'
        int 0x80
        POPALL
        ret

;---------------------------------------
count_digits:
        ; EAX -> ECX = number of digits
        PUSHALL
        xor ecx, ecx
        cmp eax, 0
        jne .cd_lp
        mov ecx, 1
        mov [rsp + 96], ecx    ; ECX in PUSHALL frame
        POPALL
        ret
.cd_lp:
        cmp eax, 0
        je .cd_done
        xor edx, edx
        mov ebx, 10
        div ebx
        inc ecx
        jmp .cd_lp
.cd_done:
        mov [rsp + 96], ecx
        POPALL
        ret

print_spaces:
        ; Print EDX spaces
        cmp edx, 0
        jle .psp_done
.psp_loop:
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rdx
        dec edx
        jg .psp_loop
.psp_done:
        ret

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

;=======================================
; Data
;=======================================
hdr:        db "          Total      Used      Free    Use%", 10, 0
lbl_mem:    db "Mem: ", 0
unit_kb:    db " KB", 0
unit_mb:    db " MB", 0
unit_gb:    db " GB", 0
msg_pages:  db "Pages: ", 0
msg_of:     db " of ", 0
msg_free:   db " free (4 KB/page)", 10, 0

; BSS
cur_free:   dd 0
boot_free:  dd 0
used:       dd 0
total_kb:   dd 0
used_kb:    dd 0
free_kb:    dd 0
