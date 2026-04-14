; neofetch.asm - System information display with ASCII art logo
; Usage: neofetch
; Shows OS info alongside an ASCII art honey badger

%include "syscalls.inc"

; VGA color bytes for direct framebuffer rendering
C_LOGO   equ 0x0E              ; Yellow on black (logo)
C_LABEL  equ 0x0F              ; White on black (labels)
C_VALUE  equ 0x07              ; Light gray on black (values)
C_SEP    equ 0x08              ; Dark gray (separator)
C_BAR_ON equ 0x0A              ; Light green (used portion)
C_BAR_OFF equ 0x02             ; Dark green (free portion)
C_ACCENT equ 0x0B              ; Light cyan (accent)

LOGO_LINES equ 12
INFO_START_COL equ 34          ; Column where info text begins

start:
        ; Gather system info first
        call gather_info

        ; Print blank line
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; Print each line: logo on left, info on right
        xor esi, esi            ; Line counter

.print_line:
        cmp esi, LOGO_LINES
        jge .done

        ; Print logo portion
        push esi
        mov eax, SYS_SETCOLOR
        mov ebx, C_LOGO
        int 0x80

        ; Calculate logo line address
        mov eax, esi
        imul eax, 33            ; each logo line is 33 bytes (32 chars + null)
        lea ebx, [logo_art + eax]
        push ebx
        mov eax, SYS_PRINT
        pop ebx
        int 0x80

        ; Pad to INFO_START_COL
        ; Each logo line is 32 chars, pad with 2 spaces to reach col 34
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        int 0x80

        pop esi

        ; Print info line
        push esi
        call print_info_line
        pop esi

        ; Newline
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        inc esi
        jmp .print_line

.done:
        ; Print color bar
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        call print_color_bar

        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; gather_info - Collect system information
;---------------------------------------
gather_info:
        ; Get memory info
        mov eax, SYS_MEMINFO
        int 0x80
        mov [mem_free_pages], eax
        mov [mem_total_pages], ebx

        ; Get uptime
        mov eax, SYS_GETTIME
        int 0x80
        mov [uptime_ticks], eax

        ; Get date
        mov eax, SYS_DATE
        mov ebx, date_buf
        int 0x80

        ; Get CWD
        mov eax, SYS_GETCWD
        mov ebx, cwd_buf
        int 0x80

        ; Get PID
        mov eax, SYS_GETPID
        int 0x80
        mov [my_pid], eax

        ret

;---------------------------------------
; print_info_line - Print info for line ESI
;---------------------------------------
print_info_line:
        cmp esi, 0
        je .line_user
        cmp esi, 1
        je .line_sep
        cmp esi, 2
        je .line_os
        cmp esi, 3
        je .line_kernel
        cmp esi, 4
        je .line_shell
        cmp esi, 5
        je .line_uptime
        cmp esi, 6
        je .line_memory
        cmp esi, 7
        je .line_membar
        cmp esi, 8
        je .line_cpu
        cmp esi, 9
        je .line_disk
        cmp esi, 10
        je .line_term
        cmp esi, 11
        je .line_cwd
        ret

.line_user:
        mov eax, SYS_SETCOLOR
        mov ebx, C_ACCENT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_root
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LABEL
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_at
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_ACCENT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_hostname
        int 0x80
        ret

.line_sep:
        mov eax, SYS_SETCOLOR
        mov ebx, C_SEP
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_separator
        int 0x80
        ret

.line_os:
        call print_label
        db "OS", 0
        mov eax, SYS_PRINT
        mov ebx, str_os_val
        int 0x80
        ret

.line_kernel:
        call print_label
        db "Kernel", 0
        mov eax, SYS_PRINT
        mov ebx, str_kernel_val
        int 0x80
        ret

.line_shell:
        call print_label
        db "Shell", 0
        mov eax, SYS_PRINT
        mov ebx, str_shell_val
        int 0x80
        ret

.line_uptime:
        call print_label
        db "Uptime", 0
        ; Convert ticks to seconds
        mov eax, [uptime_ticks]
        xor edx, edx
        mov ecx, 100            ; PIT_HZ
        div ecx                 ; EAX = total seconds
        ; Convert to minutes and seconds
        xor edx, edx
        mov ecx, 60
        div ecx                 ; EAX = minutes, EDX = seconds
        push edx
        mov eax, SYS_SETCOLOR
        mov ebx, C_VALUE
        int 0x80
        pop edx
        push edx
        ; Re-divide for minutes
        mov eax, [uptime_ticks]
        xor edx, edx
        mov ecx, 100
        div ecx
        xor edx, edx
        mov ecx, 60
        div ecx
        push edx                ; seconds
        ; EAX = minutes
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, str_min
        int 0x80
        pop eax                 ; seconds
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, str_sec
        int 0x80
        pop edx                 ; discard extra push
        ret

.line_memory:
        call print_label
        db "Memory", 0
        mov eax, SYS_SETCOLOR
        mov ebx, C_VALUE
        int 0x80
        ; Used = total - free
        mov eax, [mem_total_pages]
        sub eax, [mem_free_pages]
        ; Pages to KB: * 4
        shl eax, 2
        ; KB to MB: / 1024
        shr eax, 10
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, str_mib_slash
        int 0x80
        mov eax, [mem_total_pages]
        shl eax, 2
        shr eax, 10
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, str_mib
        int 0x80
        ret

.line_membar:
        ; Memory usage bar [████████░░░░░░░░] XX%
        mov eax, SYS_SETCOLOR
        mov ebx, C_SEP
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_bar_pad
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '['
        int 0x80

        ; Calculate percentage: (used * 100) / total
        mov eax, [mem_total_pages]
        sub eax, [mem_free_pages]
        imul eax, 100
        xor edx, edx
        mov ecx, [mem_total_pages]
        test ecx, ecx
        jz .membar_done
        div ecx                 ; EAX = percent used
        mov [mem_pct], eax

        ; Bar width: 20 chars, filled = pct * 20 / 100
        imul eax, 20
        xor edx, edx
        mov ecx, 100
        div ecx                 ; EAX = filled blocks
        mov ecx, eax            ; ECX = filled
        mov edx, 20
        sub edx, ecx            ; EDX = empty

        ; Print filled blocks
        mov eax, SYS_SETCOLOR
        mov ebx, C_BAR_ON
        int 0x80
.bar_fill:
        test ecx, ecx
        jz .bar_empty
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB           ; Full block char
        int 0x80
        dec ecx
        jmp .bar_fill

.bar_empty:
        mov eax, SYS_SETCOLOR
        mov ebx, C_BAR_OFF
        int 0x80
.bar_emp_loop:
        test edx, edx
        jz .bar_close
        mov eax, SYS_PUTCHAR
        mov ebx, 0xB0           ; Light shade char
        int 0x80
        dec edx
        jmp .bar_emp_loop

.bar_close:
        mov eax, SYS_SETCOLOR
        mov ebx, C_SEP
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ']'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_VALUE
        int 0x80
        mov eax, [mem_pct]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '%'
        int 0x80
.membar_done:
        ret

.line_cpu:
        call print_label
        db "CPU", 0
        mov eax, SYS_PRINT
        mov ebx, str_cpu_val
        int 0x80
        ret

.line_disk:
        call print_label
        db "Disk", 0
        mov eax, SYS_PRINT
        mov ebx, str_disk_val
        int 0x80
        ret

.line_term:
        call print_label
        db "Terminal", 0
        mov eax, SYS_PRINT
        mov ebx, str_term_val
        int 0x80
        ret

.line_cwd:
        call print_label
        db "CWD", 0
        mov eax, SYS_SETCOLOR
        mov ebx, C_VALUE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, cwd_buf
        int 0x80
        ret

;---------------------------------------
; print_label - Print "Label: " with colors
; Inline string follows the CALL instruction
;---------------------------------------
print_label:
        pop esi                 ; Return address = string address
        mov eax, SYS_SETCOLOR
        mov ebx, C_LABEL
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        ; Skip past the string to find next instruction
.skip:
        lodsb
        test al, al
        jnz .skip
        push esi                ; Push new return address
        mov eax, SYS_PRINT
        mov ebx, str_colon
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_VALUE
        int 0x80
        ret

;---------------------------------------
; print_color_bar - Show 16-color palette
;---------------------------------------
print_color_bar:
        ; Print spaces with each background color
        mov ecx, 16
        xor edx, edx           ; color index
        ; First row: 8 dark colors
        mov eax, SYS_PRINT
        mov ebx, str_bar_prefix
        int 0x80
.dark_loop:
        cmp edx, 8
        jge .dark_done
        ; Background = edx << 4
        mov eax, SYS_SETCOLOR
        mov ebx, edx
        shl ebx, 4
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PUTCHAR
        int 0x80
        inc edx
        jmp .dark_loop
.dark_done:
        mov eax, SYS_SETCOLOR
        mov ebx, C_VALUE
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; Second row: 8 bright colors
        mov eax, SYS_PRINT
        mov ebx, str_bar_prefix
        int 0x80
.bright_loop:
        cmp edx, 16
        jge .bright_done
        mov eax, SYS_SETCOLOR
        mov ebx, edx
        shl ebx, 4
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PUTCHAR
        int 0x80
        inc edx
        jmp .bright_loop
.bright_done:
        mov eax, SYS_SETCOLOR
        mov ebx, C_VALUE
        int 0x80
        ret

;=======================================================================
; DATA
;=======================================================================

; ASCII art logo - each line is 32 printable chars + null terminator (33 bytes)
; Honey badger / system logo
logo_art:
        db "                                ", 0  ; 0
        db "    _______________________     ", 0  ; 1
        db "   / ~~~~~~~~~~~~~~~~~~~ \      ", 0  ; 2
        db "  / ~~~~~~~~~~~~~~~~~~~~~ \     ", 0  ; 3
        db " | ~~~~~~  ( )  ~~~~~~~~~~ |    ", 0  ; 4
        db " | ~~~~~~   v   ~~~~~~~~~~ |    ", 0  ; 5
        db " | ~~~~~~  ---  ~~~~~~~~~~ |    ", 0  ; 6
        db "  \ ~~~~~~~~~~~~~~~~~~~~~ /     ", 0  ; 7
        db "   \_______________________/    ", 0  ; 8
        db "      |   |        |   |        ", 0  ; 9
        db "     /|   |\      /|   |\       ", 0  ; 10
        db "                                ", 0  ; 11

str_root:       db "root", 0
str_at:         db "@", 0
str_hostname:   db "honeybadger", 0
str_separator:  db "------------------------", 0
str_colon:      db ": ", 0
str_os_val:     db "Mellivora OS v3.0.1", 0
str_kernel_val: db "Mellivora 3.0.1 (i486 32-bit)", 0
str_shell_val:  db "HB Lair v2.2 (Honey Badger Lair)", 0
str_cpu_val:    db "i486+ (Protected Mode, Ring 0/3)", 0
str_disk_val:   db "ATA PIO, HBFS (2 GB, 4 KB blocks)", 0
str_term_val:   db "VGA 80x25, 16 colors", 0
str_min:        db "m ", 0
str_sec:        db "s", 0
str_mib_slash:  db " / ", 0
str_mib:        db " MiB", 0
str_bar_pad:    db "          ", 0  ; Padding to align bar under Memory
str_bar_prefix: db "                                  ", 0  ; 34 spaces

; Runtime data
uptime_ticks:   dd 0
mem_free_pages: dd 0
mem_total_pages: dd 0
mem_pct:        dd 0
my_pid:         dd 0
date_buf:       times 8 db 0
cwd_buf:        times 256 db 0
