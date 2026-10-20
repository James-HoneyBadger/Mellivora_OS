; bfault.asm - diagnostics: proves the desktop contains a crashing application
;
; Run it from a Burrows terminal (or anywhere) and the kernel should kill only
; this task, report it as a notification, and leave the desktop running. It is
; deliberately not in the launcher catalog; see docs/app-manifest-exclusions.json.
%include "syscalls.inc"
%include "lib/gui.inc"

start:
        mov eax, 260
        mov ebx, 170
        mov ecx, 320
        mov edx, 200
        mov esi, title
        call gui_create_window
        mov [win], eax

        mov eax, [win]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, 320
        mov esi, 200
        mov edi, 0x00C03030
        call gui_fill_rect

        mov eax, [win]
        mov ebx, 20
        mov ecx, 20
        mov esi, msg
        mov edi, 0x00FFFFFF
        call gui_draw_text

        mov eax, [win]
        call gui_compose
        mov eax, [win]
        call gui_flip

        ; Stay visible briefly, then fault on purpose.
        mov dword [wait_ticks], 20
.wait:
        mov eax, SYS_SLEEP
        mov ebx, 3
        int 0x80
        dec dword [wait_ticks]
        jnz .wait

        mov eax, 0xD0000000     ; no task maps this: guaranteed page fault
        mov dword [eax], 1

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

title:  db "Fault Test", 0
msg:    db "about to fault", 0

section .bss
win:        resd 1
wait_ticks: resd 1
