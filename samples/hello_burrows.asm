; hello_burrows.asm - Minimal Burrows GUI application
; Build from the repository root:
;   nasm -f bin -Iprograms/ -o hello_burrows.bin samples/hello_burrows.asm
; Run from the Mellivora shell:
;   hello_burrows

%include "syscalls.inc"
%include "lib/gui.inc"

section .bss
win_id  resd 1

section .data
title       db "Hello Burrows", 0
message     db "Welcome to the Burrows desktop!", 0
hint        db "Press Escape or close the window to exit.", 0

section .text
start:
        mov eax, 180
        mov ebx, 140
        mov ecx, 520
        mov edx, 180
        mov esi, title
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

.loop:
        mov eax, [win_id]
        mov ebx, 16
        mov ecx, 24
        mov edx, 488
        mov esi, 116
        mov edi, 0x00E8F1F7
        call gui_fill_rect

        mov eax, [win_id]
        mov ebx, 32
        mov ecx, 48
        mov esi, message
        mov edi, 0x00153045
        call gui_draw_text

        mov eax, [win_id]
        mov ebx, 32
        mov ecx, 80
        mov esi, hint
        mov edi, 0x004A6475
        call gui_draw_text

        call gui_compose
        call gui_flip

        mov eax, SYS_SLEEP
        mov ebx, 10
        int 0x80

        mov eax, [win_id]
        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        jne .loop
        cmp ebx, 27
        jne .loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
