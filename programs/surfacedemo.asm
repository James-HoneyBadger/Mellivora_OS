; surfacedemo.asm  —  Compositor surface API demo for Mellivora OS v10
;
; Demonstrates the new SYS_SURFACE_* syscalls (v10 / Burrows Desktop 2.0):
;   Creates a 320x240 RGBA surface at (200, 100), paints animated
;   horizontal colour bands that scroll each frame, and commits the
;   dirty rect so the Burrows compositor blits it to the screen.
;   Press any key to exit and destroy the surface cleanly.

%include "syscalls.inc"

; Surface geometry
SURF_W      equ 320
SURF_H      equ 240
SURF_X      equ 200
SURF_Y      equ 100

; Animation speed: pixels to shift the band pattern per frame
SCROLL_STEP equ 2

start:
        ; ---- Create surface ----
        mov eax, SYS_SURFACE_CREATE
        mov ebx, SURF_W
        mov ecx, SURF_H
        mov edx, SURF_X
        mov esi, SURF_Y
        int 0x80
        cmp eax, -1
        je  .no_surface
        mov [sd_surf_id], eax

        ; ---- Map its pixel buffer via SYS_MMAP ----
        ; Surface pixel buffer address = SYS_MMAP(SYS_SURFACE_CREATE result)
        ; The kernel returns a kernel-space address in EAX; programs access it
        ; via the shared flat address space.  Use SYS_FRAMEBUF sub 3 to get
        ; the surface buffer address.
        ;
        ; In Mellivora's flat model, the PMM-allocated buffer is in kernel
        ; space but mapped accessible from ring-3 via the flat 4 GB segment.
        ; We retrieve the address with a dedicated sub-syscall:
        mov eax, SYS_FRAMEBUF
        mov ebx, 5              ; sub-function 5 = query surface buffer ptr
        mov ecx, [sd_surf_id]
        int 0x80
        cmp eax, -1
        je  .no_buf
        mov [sd_buf_ptr], eax

        xor dword [sd_scroll], 0

.frame:
        ; ---- Non-blocking key check ----
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .done

        ; ---- Paint scrolling colour bands into pixel buffer ----
        ; Each pixel row has a colour derived from (y + scroll_offset) % 8
        ; to produce 8 colour bands of 30 pixels each:
        ;   band 0 = red, 1 = orange, 2 = yellow, 3 = green,
        ;   4 = cyan, 5 = blue, 6 = magenta, 7 = white
        mov edi, [sd_buf_ptr]
        xor ebx, ebx            ; EBX = row counter

.row:
        cmp ebx, SURF_H
        jge .commit

        ; band index = (row + scroll) % 8
        mov eax, ebx
        add eax, [sd_scroll]
        and eax, 0x7F           ; mod 128
        shr eax, 4              ; divide by 16 -> 0..7

        ; Colour table lookup
        mov ecx, eax
        shl ecx, 2
        mov eax, [sd_colors + ecx]

        ; Fill SURF_W pixels with this colour
        push edi
        push ebx
        mov ecx, SURF_W
        cld
        rep stosd
        pop ebx
        pop edi
        add edi, SURF_W * 4
        inc ebx
        jmp .row

.commit:
        ; ---- Commit full dirty rect ----
        mov eax, SYS_SURFACE_COMMIT
        mov ebx, [sd_surf_id]
        xor ecx, ecx            ; dirty_x = 0
        xor edx, edx            ; dirty_y = 0
        mov esi, SURF_W
        mov edi, SURF_H
        int 0x80

        ; Advance scroll
        add dword [sd_scroll], SCROLL_STEP

        ; Brief yield to let compositor run
        mov eax, SYS_YIELD
        int 0x80

        jmp .frame

.done:
        ; ---- Move surface off-screen briefly to show move API ----
        mov eax, SYS_SURFACE_MOVE
        mov ebx, [sd_surf_id]
        mov ecx, -SURF_W        ; x = -SURF_W (hidden)
        xor edx, edx
        int 0x80

        ; ---- Destroy surface ----
        mov eax, SYS_SURFACE_DESTROY
        mov ebx, [sd_surf_id]
        int 0x80

        ; ---- Print exit message ----
        mov eax, SYS_PRINT
        mov ebx, sd_msg_done
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.no_surface:
        mov eax, SYS_PRINT
        mov ebx, sd_msg_nosurf
        int 0x80
        jmp .exit_fail

.no_buf:
        ; Still destroy the orphaned surface
        mov eax, SYS_SURFACE_DESTROY
        mov ebx, [sd_surf_id]
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, sd_msg_nobuf
        int 0x80

.exit_fail:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; ---------------------------------------------------------------
; Data
; ---------------------------------------------------------------
sd_surf_id  dd 0
sd_buf_ptr  dd 0
sd_scroll   dd 0

sd_colors:
        dd 0x00FF0000   ; red
        dd 0x00FF8000   ; orange
        dd 0x00FFFF00   ; yellow
        dd 0x0000CC00   ; green
        dd 0x0000FFFF   ; cyan
        dd 0x000066FF   ; blue
        dd 0x00CC00FF   ; magenta
        dd 0x00FFFFFF   ; white

sd_msg_done:   db "surfacedemo: exited cleanly.", 0x0A, 0
sd_msg_nosurf: db "surfacedemo: SYS_SURFACE_CREATE failed (Burrows not running?).", 0x0A, 0
sd_msg_nobuf:  db "surfacedemo: could not get surface buffer pointer.", 0x0A, 0
