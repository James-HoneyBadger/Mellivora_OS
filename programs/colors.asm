; colors.asm - Display all 16 text colors for Mellivora OS
%include "syscalls.inc"

start:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_header
        int 0x80

        ; Display each of the 16 foreground colors
        xor ecx, ecx           ; Color counter

.color_loop:
        ; Set color: background black, foreground = ECX
        mov eax, SYS_SETCOLOR
        mov ebx, ecx
        int 0x80

        ; Print color block: "  XX  "
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        int 0x80

        ; Print color number as hex
        mov eax, ecx
        cmp al, 10
        jl .digit
        add al, 'A' - 10
        jmp .print_hex
.digit:
        add al, '0'
.print_hex:
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80

        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        int 0x80

        ; Print sample text
        mov eax, SYS_PRINT
        mov ebx, msg_sample
        int 0x80

        inc ecx
        cmp ecx, 16
        jl .color_loop

        ; Now show background colors
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_bgheader
        int 0x80

        xor ecx, ecx

.bg_loop:
        ; Set color: background = ECX, foreground = white (0x0F)
        mov eax, ecx
        shl eax, 4
        or eax, 0x0F
        mov ebx, eax
        mov eax, SYS_SETCOLOR
        int 0x80

        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Print bg number
        mov eax, ecx
        cmp al, 10
        jl .bg_digit
        add al, 'A' - 10
        jmp .bg_print
.bg_digit:
        add al, '0'
.bg_print:
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80

        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        inc ecx
        cmp ecx, 8
        jl .bg_loop

        ; Reset
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_done
        int 0x80

        mov eax, SYS_EXIT
        int 0x80

msg_header:     db "=== VGA Color Chart (16 Foreground Colors) ===", 0x0A, 0
msg_sample:     db "Sample Text", 0x0A, 0
msg_bgheader:   db 0x0A, "=== Background Colors ===", 0x0A, 0
msg_done:       db 0x0A, 0x0A, "Color chart complete.", 0x0A, 0
