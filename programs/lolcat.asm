; lolcat.asm - Rainbow-colored text output
; Usage: lolcat <filename>
;        lolcat              (reads from keyboard)

%include "syscalls.inc"

MAX_FILE equ 32768

; VGA text-mode colors to cycle through (rainbow-ish)
NUM_COLORS equ 6

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        mov ecx, 256
        int 0x80

        mov esi, arg_buf
        call skip_spaces

        cmp byte [esi], 0
        je interactive_mode

        ; File mode: copy filename
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

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        mov edx, MAX_FILE
        int 0x80
        cmp eax, 0
        jl file_error
        mov [file_len], eax

        ; Rainbow print the file
        mov esi, file_buf
        mov ecx, [file_len]
        xor ebp, ebp           ; color index
.print_loop:
        cmp ecx, 0
        jle .done
        movzx eax, byte [esi]

        cmp al, 10             ; newline
        je .newline

        ; Set color from rainbow table
        push ecx
        push esi
        movzx ebx, byte [colors + ebp]
        mov eax, SYS_SETCOLOR
        int 0x80
        pop esi
        pop ecx

        ; Print character
        push ecx
        push esi
        movzx ebx, byte [esi]
        mov eax, SYS_PUTCHAR
        int 0x80
        pop esi
        pop ecx

        ; Advance color
        inc ebp
        cmp ebp, NUM_COLORS
        jb .next_char
        xor ebp, ebp
.next_char:
        inc esi
        dec ecx
        jmp .print_loop

.newline:
        push ecx
        push esi
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop esi
        pop ecx
        ; Shift color offset on new line
        inc ebp
        cmp ebp, NUM_COLORS
        jb .nl_ok
        xor ebp, ebp
.nl_ok:
        inc esi
        dec ecx
        jmp .print_loop

.done:
        ; Reset color
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; Interactive: read keyboard, rainbow each char
interactive_mode:
        xor ebp, ebp
.loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 4              ; Ctrl+D
        je .quit
        cmp al, 0
        je .quit

        cmp al, 10
        je .i_newline
        cmp al, 13
        je .i_newline

        ; Set rainbow color
        push eax
        movzx ebx, byte [colors + ebp]
        mov eax, SYS_SETCOLOR
        int 0x80
        pop eax

        ; Echo char
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80

        inc ebp
        cmp ebp, NUM_COLORS
        jb .loop
        xor ebp, ebp
        jmp .loop

.i_newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc ebp
        cmp ebp, NUM_COLORS
        jb .inl_ok
        xor ebp, ebp
.inl_ok:
        jmp .loop

.quit:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

file_error:
        mov eax, SYS_PRINT
        mov ebx, err_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

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
err_str:    db "Error: cannot read file", 10, 0

; Rainbow color cycle (VGA text attributes):
; Red, Yellow, Green, Cyan, Blue, Magenta
colors:     db 0x0C, 0x0E, 0x0A, 0x0B, 0x09, 0x0D

filename:   times 64 db 0
arg_buf:    times 256 db 0
file_len:   dd 0
file_buf:   times MAX_FILE db 0
