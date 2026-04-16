; df.asm - Disk Free / Filesystem Status
; Shows disk usage statistics for the HBFS filesystem

%include "syscalls.inc"

MAX_ENTRIES     equ 300

start:
        mov eax, SYS_PRINT
        mov ebx, msg_hdr
        int 0x80

        ; Count files and total size
        xor ebp, ebp           ; entry index
        xor edi, edi           ; file count
        mov dword [total_bytes], 0
        mov dword [total_blocks], 0

.scan:
        mov eax, SYS_READDIR
        mov ebx, dirent_buf
        mov ecx, ebp
        int 0x80

        cmp eax, -1
        je .done

        cmp eax, 0             ; free slot
        je .next

        inc edi
        add [total_bytes], ecx

        ; Calculate blocks for this file (8KB blocks)
        mov eax, ecx
        add eax, 8191
        shr eax, 13
        cmp eax, 0
        jne .has_blocks
        mov eax, 1
.has_blocks:
        add [total_blocks], eax

.next:
        inc ebp
        cmp ebp, MAX_ENTRIES
        jl .scan

.done:
        ; Print filesystem line
        ; Name
        mov eax, SYS_PRINT
        mov ebx, fs_name
        int 0x80

        ; Total (4 GB disk image, ~524288 blocks of 8KB)
        mov eax, 4096
        call print_size_mb

        ; Used
        mov eax, [total_bytes]
        add eax, 524288         ; round to nearest MB
        shr eax, 20             ; bytes to MB
        cmp eax, 0
        jne .print_used
        mov eax, 1              ; at least 1 MB
.print_used:
        call print_size_mb

        ; Available (approx)
        mov eax, 4096
        mov ecx, [total_bytes]
        shr ecx, 20
        sub eax, ecx
        call print_size_mb

        ; Use%
        mov eax, [total_blocks]
        xor edx, edx
        imul eax, 100
        mov ecx, 524288
        div ecx
        call print_pct

        ; Files
        mov eax, edi
        call print_padded

        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Total line
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, edi
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_files
        int 0x80

        mov eax, [total_bytes]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_bytes
        int 0x80

        mov eax, [total_blocks]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, msg_blocks
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; print_size_mb - Print EAX as right-justified 8-wide MB value
;---------------------------------------
print_size_mb:
        PUSHALL
        mov [.val], eax
        ; Count digits
        xor ecx, ecx
        cmp eax, 0
        jne .psm_count
        mov ecx, 1
        jmp .psm_pad
.psm_count:
        cmp eax, 0
        je .psm_pad
        xor edx, edx
        mov ebx, 10
        div ebx
        inc ecx
        jmp .psm_count
.psm_pad:
        add ecx, 2             ; "MB"
        mov edx, 8
        sub edx, ecx
        jle .psm_val
.psm_sp:
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rdx
        dec edx
        jg .psm_sp
.psm_val:
        mov eax, [.val]
        call print_decimal
        mov eax, SYS_PRINT
        mov ebx, str_mb
        int 0x80
        POPALL
        ret
.val:   dd 0

;---------------------------------------
; print_pct - Print EAX as right-justified percentage
;---------------------------------------
print_pct:
        PUSHALL
        mov [.pval], eax
        xor ecx, ecx
        cmp eax, 0
        jne .pp_cnt
        mov ecx, 1
        jmp .pp_pad
.pp_cnt:
        cmp eax, 0
        je .pp_pad
        xor edx, edx
        mov ebx, 10
        div ebx
        inc ecx
        jmp .pp_cnt
.pp_pad:
        add ecx, 1             ; %
        mov edx, 6
        sub edx, ecx
        jle .pp_val
.pp_sp:
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rdx
        dec edx
        jg .pp_sp
.pp_val:
        mov eax, [.pval]
        call print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, '%'
        int 0x80
        POPALL
        ret
.pval:  dd 0

;---------------------------------------
; print_padded - Print EAX right-justified to 8 wide
;---------------------------------------
print_padded:
        PUSHALL
        push rax
        xor ecx, ecx
        cmp eax, 0
        jne .pp2_cnt
        mov ecx, 1
        jmp .pp2_pad
.pp2_cnt:
        cmp eax, 0
        je .pp2_pad
        xor edx, edx
        mov ebx, 10
        div ebx
        inc ecx
        jmp .pp2_cnt
.pp2_pad:
        mov edx, 8
        sub edx, ecx
        jle .pp2_val
.pp2_sp:
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rdx
        dec edx
        jg .pp2_sp
.pp2_val:
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

;=======================================
; Data
;=======================================
msg_hdr:
        db "Filesystem  Total    Used    Avail  Use%   Files", 10, 0
fs_name:
        db "hbfs    ", 0
str_mb:
        db "M", 0
msg_files:
        db " file(s), ", 0
msg_bytes:
        db " bytes, ", 0
msg_blocks:
        db " blocks used", 10, 0

; BSS
dirent_buf:     times 288 db 0
total_bytes:    dd 0
total_blocks:   dd 0
