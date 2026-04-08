; apitest.asm - Test program to verify all Mellivora API libraries assemble
;
; This program demonstrates basic usage of each library.

%include "syscalls.inc"
%include "lib/string.inc"
%include "lib/io.inc"
%include "lib/math.inc"
%include "lib/vga.inc"
%include "lib/mem.inc"
%include "lib/data.inc"

start:
        ; Seed random number generator from system time
        call io_get_time
        call math_seed_random

        ; Clear screen and set color
        call io_clear
        mov bl, 0x0A
        call vga_set_color

        ; Print banner
        mov esi, msg_banner
        call io_println

        ; Reset color
        mov bl, 0x07
        call vga_set_color

        ; --- Test string API ---
        mov esi, msg_str_test
        call io_println

        mov esi, test_str
        call str_len
        mov esi, msg_str_len
        call io_print
        call print_dec
        call io_newline

        ; --- Test math API ---
        mov esi, msg_math_test
        call io_println

        mov eax, 42
        mov ecx, 3
        call math_power
        mov esi, msg_power
        call io_print
        call print_dec
        call io_newline

        mov eax, 144
        call math_sqrt
        mov esi, msg_sqrt
        call io_print
        call print_dec
        call io_newline

        ; --- Test random ---
        mov eax, 1
        mov ebx, 100
        call math_random_range
        mov esi, msg_random
        call io_print
        call print_dec
        call io_newline

        ; --- Test VGA direct write ---
        mov esi, msg_vga_test
        mov ebx, 0
        mov ecx, 6
        mov dl, 0x1F
        call vga_write_color

        ; Draw a small box
        mov ah, 0x0E
        mov ebx, 30
        mov ecx, 8
        mov edx, 20
        mov esi, 5
        call vga_draw_box

        ; --- Done ---
        mov bl, 0x07
        call vga_set_color
        mov ebx, 0
        mov ecx, 15
        call vga_set_cursor

        mov esi, msg_done
        call io_println

        mov esi, msg_prompt
        call io_print

        ; Read a line to pause
        mov edi, input_buf
        mov ecx, 64
        call io_read_line

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; Data
msg_banner:     db "=== Mellivora API Test Suite ===", 0
msg_str_test:   db "--- String API ---", 0
msg_str_len:    db "  str_len('Hello') = ", 0
msg_math_test:  db "--- Math API ---", 0
msg_power:      db "  42^3 = ", 0
msg_sqrt:       db "  sqrt(144) = ", 0
msg_random:     db "  random(1..100) = ", 0
msg_vga_test:   db " VGA Direct Write Test ", 0
msg_done:       db "All API libraries loaded and tested successfully.", 0
msg_prompt:     db "Press Enter to exit: ", 0
test_str:       db "Hello", 0

section .bss
input_buf:      resb 64
