; du.asm - Disk Usage: show size of files
; Usage: du [filename]      - show size of specific file
;        du                 - show sizes of all files

%include "syscalls.inc"

MAX_ENTRIES equ 300

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov esi, arg_buf
        call skip_spaces

        cmp byte [esi], 0
        je list_all

        ; Single file mode
        mov edi, filename
.copy:
        lodsb
        cmp al, ' '
        je .cdone
        cmp al, 0
        je .cdone
        stosb
        jmp .copy
.cdone:
        mov byte [edi], 0

        ; Stat the file
        mov eax, SYS_STAT
        mov ebx, filename
        mov ecx, stat_buf
        int 0x80
        cmp eax, 0
        jl not_found

        ; Print size and filename
        mov eax, [stat_buf + 4]    ; file size (offset may vary)
        call print_size
        mov eax, SYS_PUTCHAR
        mov ebx, 9                 ; tab
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, filename
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp exit_ok

; List all files
list_all:
        mov eax, SYS_PRINT
        mov ebx, hdr_str
        int 0x80

        xor ebp, ebp               ; entry index
        mov dword [total_size], 0

.scan:
        mov eax, SYS_READDIR
        mov ebx, dirent_buf
        mov ecx, ebp
        int 0x80

        cmp eax, -1
        je .summary

        cmp eax, 0
        je .next

        ; EAX = type, EBX = name ptr, ECX = size
        push ebp
        push ecx

        ; Print size
        mov eax, ecx
        call print_size

        ; Tab
        mov eax, SYS_PUTCHAR
        mov ebx, 9
        int 0x80

        ; Get name from dirent buf
        mov eax, SYS_PRINT
        mov ebx, dirent_buf
        int 0x80

        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        pop ecx
        pop ebp
        add [total_size], ecx

.next:
        inc ebp
        cmp ebp, MAX_ENTRIES
        jl .scan

.summary:
        ; Print total
        mov eax, SYS_PRINT
        mov ebx, sep_str
        int 0x80
        mov eax, [total_size]
        call print_size
        mov eax, SYS_PRINT
        mov ebx, total_str
        int 0x80

exit_ok:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

not_found:
        mov eax, SYS_PRINT
        mov ebx, err_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;--------------------------------------
; print_size: Print human-readable size in EAX
;--------------------------------------
print_size:
        ; If >= 1MB, show MB
        cmp eax, 1048576
        jge .show_mb
        ; If >= 1KB, show KB
        cmp eax, 1024
        jge .show_kb
        ; Show bytes
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, suffix_b
        int 0x80
        ret
.show_kb:
        shr eax, 10
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, suffix_kb
        int 0x80
        ret
.show_mb:
        shr eax, 20
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, suffix_mb
        int 0x80
        ret

;--------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

;=======================================
; Data
;=======================================
hdr_str:    db "SIZE", 9, "FILE", 10
            db "----", 9, "----", 10, 0
sep_str:    db "----", 9, "----------", 10, 0
total_str:  db 9, "total", 10, 0
err_str:    db "File not found", 10, 0
suffix_b:   db "B", 0
suffix_kb:  db "K", 0
suffix_mb:  db "M", 0

filename:   times 64 db 0
arg_buf:    times 256 db 0
stat_buf:   times 64 db 0
total_size: dd 0
dirent_buf: times 300 db 0
